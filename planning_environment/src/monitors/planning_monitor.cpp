/*********************************************************************
* Software License Agreement (BSD License)
* 
*  Copyright (c) 2008, Willow Garage, Inc.
*  All rights reserved.
* 
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
* 
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
* 
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/** \author Ioan Sucan, Sachin Chitta */

#include "planning_environment/monitors/planning_monitor.h"
#include "planning_environment/util/kinematic_state_constraint_evaluator.h"
#include <boost/scoped_ptr.hpp>
#include <motion_planning_msgs/DisplayTrajectory.h>

void planning_environment::PlanningMonitor::loadParams(void)
{
  nh_.param<double>("collision_map_safety_timeout", intervalCollisionMap_, 0.0);
  nh_.param<double>("joint_states_safety_timeout", intervalState_, 0.0);
  nh_.param<double>("tf_safety_timeout", intervalPose_, 0.0);
  nh_.param<int>("contacts_to_compute_for_allowable_contacts_test", num_contacts_allowable_contacts_test_, 10);
  nh_.param<int>("contacts_to_compute_for_display", num_contacts_for_display_, 1);

  display_collision_pose_publisher_ = nh_.advertise<motion_planning_msgs::DisplayTrajectory>("collision_pose", 1);
  display_state_validity_publisher_ = nh_.advertise<motion_planning_msgs::DisplayTrajectory>("state_validity", 1);
}

bool planning_environment::PlanningMonitor::prepareForValidityChecks(const std::vector<std::string>& joint_names,
                                                                     const motion_planning_msgs::OrderedCollisionOperations& ordered_collision_operations,
                                                                     const std::vector<motion_planning_msgs::AllowedContactSpecification>& allowed_contacts,
                                                                     const motion_planning_msgs::Constraints& path_constraints,
                                                                     const motion_planning_msgs::Constraints& goal_constraints,
                                                                     const std::vector<motion_planning_msgs::LinkPadding>& link_padding,
                                                                     motion_planning_msgs::ArmNavigationErrorCodes &error_code)
{
  getEnvironmentModel()->lock();

  //copying collision map filtered for static objects into the collision space
  setCollisionSpace();

  //changing any necessary link paddings, which destroys and recreates the objects
  applyLinkPaddingToCollisionSpace(link_padding);
 
  for(unsigned int i  = 0; i < joint_names.size(); i++) {
    ROS_DEBUG_STREAM("Parent joint " << joint_names[i]);
  }

  //turning off collisions except for a set of specified joints, after which any other requested ordered collision operations will be preformed
  motion_planning_msgs::OrderedCollisionOperations operations;
  std::vector<std::string> child_links;
  getChildLinks(joint_names, child_links);
  for(unsigned int i  = 0; i < child_links.size(); i++) {
    ROS_DEBUG_STREAM("Child link " << child_links[i]);
  }

  getOrderedCollisionOperationsForOnlyCollideLinks(child_links,ordered_collision_operations,operations);
  applyOrderedCollisionOperationsToCollisionSpace(operations);

  //setting any allowed contacts
  setAllowedContacts(allowed_contacts);
  
  //setting path and goal constraints, which may fail due to frame transform issues  
  if(!setPathConstraints(path_constraints, error_code)) {
    return false;
  }

  if(!setGoalConstraints(goal_constraints, error_code)) {
    return false;
  }
  return true;
}

void planning_environment::PlanningMonitor::revertToDefaultState() {
  revertAllowedCollisionToDefault();
  revertCollisionSpacePaddingToDefault();
  clearAllowedContacts();
  clearConstraints();
  getEnvironmentModel()->unlock();
}

bool planning_environment::PlanningMonitor::isEnvironmentSafe(motion_planning_msgs::ArmNavigationErrorCodes &error_code) const
{

  if (use_collision_map_ && (!haveMap_ || !isMapUpdated(intervalCollisionMap_)))
  {
    ROS_WARN("Environment is not safe for motion: Collision map not updated in the last %f seconds", intervalCollisionMap_);
    error_code.val = error_code.SENSOR_INFO_STALE;
    return false;
  }
  
  if (!isJointStateUpdated(intervalState_))
  {
    ROS_WARN("Environment is not safe for motion: Robot state not updated in the last %f seconds", intervalState_);
    error_code.val = error_code.ROBOT_STATE_STALE;
    return false;
  }  

  if (!isPoseUpdated(intervalPose_))
  {
    ROS_WARN("Environment is not safe for motion: Robot pose not updated in the last %f seconds", intervalPose_);
    error_code.val = error_code.FRAME_TRANSFORM_FAILURE;
    return false;
  }
  
  error_code.val = error_code.SUCCESS;
  return true;
}

void planning_environment::PlanningMonitor::clearConstraints(void)
{
  path_constraints_.joint_constraints.clear();
  path_constraints_.position_constraints.clear();
  path_constraints_.orientation_constraints.clear();
  path_constraints_.visibility_constraints.clear();

  goal_constraints_.joint_constraints.clear();
  goal_constraints_.position_constraints.clear();
  goal_constraints_.orientation_constraints.clear();
  goal_constraints_.visibility_constraints.clear();
}

bool planning_environment::PlanningMonitor::setPathConstraints(const motion_planning_msgs::Constraints &constraints, 
                                                               motion_planning_msgs::ArmNavigationErrorCodes &error_code)
{
  path_constraints_ = constraints;
  return transformConstraintsToFrame(path_constraints_, getWorldFrameId(), error_code);
}

bool planning_environment::PlanningMonitor::setGoalConstraints(const motion_planning_msgs::Constraints &constraints,
                                                               motion_planning_msgs::ArmNavigationErrorCodes &error_code)
{
  goal_constraints_ = constraints;
  return transformConstraintsToFrame(goal_constraints_, getWorldFrameId(), error_code);
}

bool planning_environment::PlanningMonitor::transformConstraintsToFrame(motion_planning_msgs::Constraints &constraints, 
                                                                        const std::string &target, 
                                                                        motion_planning_msgs::ArmNavigationErrorCodes &error_code) const
{
  geometry_msgs::PointStamped pos;
  geometry_msgs::PoseStamped tmp_pose;
  for (unsigned int i = 0; i < constraints.position_constraints.size() ; ++i)
  {
    bool ok = false;	
    unsigned int steps = 0;
    while (!tf_->canTransform(target, constraints.position_constraints[i].header.frame_id,ros::Time()) && steps < 10)
    {
      ros::Duration(0.01).sleep();
      steps++;
    }	
    try
    {
      pos.point = constraints.position_constraints[i].position;
      pos.header = constraints.position_constraints[i].header;
      pos.header.stamp = ros::Time();
      tf_->transformPoint(target,pos,pos);
      constraints.position_constraints[i].position = pos.point;

      ROS_DEBUG_STREAM("Transforming position constraint from frame " << target << " from frame " << pos.header.frame_id);
      ROS_DEBUG_STREAM("Resulting position is " << pos.point.x << " " << pos.point.y << " " << pos.point.z);

      geometry_msgs::QuaternionStamped tmp_quaternion;
      tmp_quaternion.quaternion = constraints.position_constraints[i].constraint_region_orientation;
      tmp_quaternion.header = constraints.position_constraints[i].header;
      tmp_quaternion.header.stamp = ros::Time();
      tf_->transformQuaternion(target,tmp_quaternion,tmp_quaternion);
      constraints.position_constraints[i].header.frame_id = tmp_quaternion.header.frame_id;
      constraints.position_constraints[i].header.stamp = ros::Time::now();
      ok = true;
    }
    catch(...)
    {
    }
	
    if (!ok)
    {
      ROS_ERROR("Unable to transform pose constraint on link '%s' to frame '%s'", constraints.position_constraints[i].link_name.c_str(), target.c_str());
      error_code.val = error_code.FRAME_TRANSFORM_FAILURE;
      return false;
    }
  }

  for (unsigned int i = 0; i < constraints.orientation_constraints.size() ; ++i)
  {
    bool ok = false;	
    unsigned int steps = 0;
    while (!tf_->canTransform(target, constraints.orientation_constraints[i].header.frame_id, ros::Time()) && steps < 10)
    {
      ros::Duration(0.01).sleep();
      steps++;
    }
    try
    {
      geometry_msgs::QuaternionStamped orientation;
      orientation.header = constraints.orientation_constraints[i].header;
      orientation.header.stamp = ros::Time();
      orientation.quaternion = constraints.orientation_constraints[i].orientation;

      tf_->transformQuaternion(target, orientation, orientation);
      constraints.orientation_constraints[i].orientation = orientation.quaternion;
      constraints.orientation_constraints[i].header = orientation.header;
      ok = true;
    }
    catch(...)
    {
    }
	
    if (!ok)
    {
      ROS_ERROR("Unable to transform pose constraint on link '%s' to frame '%s'", constraints.orientation_constraints[i].link_name.c_str(), target.c_str());
      error_code.val = error_code.FRAME_TRANSFORM_FAILURE;
      return false;
    }
  }    
  // if there are any floating or planar joints, transform them
  //TODO - figure out what to do here
  /*
    if (getKinematicModel()->getPlanarJoints().size() > 0 || getKinematicModel()->getFloatingJoints().size() > 0)
    {
    for (unsigned int i = 0; i < constraints.joint_constraints.size() ; ++i)
    if (!transformJoint(constraints.joint_constraints[i].joint_name, 
    0, constraints.joint_constraints[i].position, 
    constraints.joint_constraints[i].header, target))
    {
    error_code.val = error_code.FRAME_TRANSFORM_FAILURE;        
    return false;
    }
  }
  */
  for (unsigned int i = 0; i < constraints.visibility_constraints.size() ; ++i)
  {
    bool ok = false;	
    geometry_msgs::PointStamped point = constraints.visibility_constraints[i].target;
    point.header.stamp = ros::Time();
    try
    {
      tf_->transformPoint(target, point, point);
      constraints.visibility_constraints[i].target = point;
      ok = true;
    }
    catch(...)
    {
    }
    if (!ok)
    {
      ROS_ERROR("Unable to transform visibility constraint on link '%s' to frame '%s'", constraints.visibility_constraints[i].target.header.frame_id.c_str(), target.c_str());
      error_code.val = error_code.FRAME_TRANSFORM_FAILURE;
      return false;
    }
  }    
  error_code.val = error_code.SUCCESS;
  return true;
}

bool planning_environment::PlanningMonitor::transformTrajectoryToFrame(trajectory_msgs::JointTrajectory &kp, 
                                                                       motion_planning_msgs::RobotState &robot_state, 
                                                                       const std::string &target, 
                                                                       motion_planning_msgs::ArmNavigationErrorCodes &error_code) const
{    
  //TODO - here too
  // if there are no planar of floating transforms, there is nothing to do
  //  if (getKinematicModel()->getPlanarJoints().empty() && getKinematicModel()->getFloatingJoints().empty())
  //   {
  //     kp.header.frame_id = target;
  //     return true;
  //   }    
  //  roslib::Header updatedHeader = kp.header;
  std::string updated_frame_id = target;
    
  // transform start state
  for (unsigned int i = 0 ; i < robot_state.joint_state.position.size() ; ++i)
    if (!transformJointToFrame(robot_state.joint_state.position[i], robot_state.joint_state.name[i], kp.header.frame_id, target, error_code))
      return false;
    
    
  // transform the rest of the states

  // get the joints this path is for
  std::vector<const planning_models::KinematicModel::JointModel*> joints;
  joints.resize(kp.joint_names.size());
  for (unsigned int j = 0 ; j < joints.size() ; ++j)
  {
    joints[j] = getKinematicModel()->getJointModel(kp.joint_names[j]);
    if (joints[j] == NULL)
    {
      ROS_ERROR("Unknown joint '%s' found on path", kp.joint_names[j].c_str());
      error_code.val = error_code.INVALID_TRAJECTORY;
      return false;
    }
  }
    
  //TODO - here too
  // iterate through the states
  //  for (unsigned int i = 0 ; i < kp.points.size() ; ++i)
  //   {
  //     unsigned int u = 0;
  //     for (unsigned int j = 0 ; j < joints.size() ; ++j)
  //     {
  //       roslib::Header header = kp.header;
  //       if (!transformJoint(joints[j]->name, u, kp.points[i].positions[j], header, target, error_code))
  //       {
  //         error_code.val = error_code.FRAME_TRANSFORM_FAILURE;
  //         return false;
  //       }
  //       updatedHeader = header;
  //     }
  //   }
    
  kp.header.frame_id = updated_frame_id;
  return true;
}

bool planning_environment::PlanningMonitor::transformJointToFrame(double &value, 
                                                                  const std::string &joint_name, 
                                                                  std::string &frame_id, 
                                                                  const std::string &target,
                                                                  motion_planning_msgs::ArmNavigationErrorCodes &error_code) const
{
  return transformJoint(joint_name, 0, value, frame_id, target, error_code);
}

bool planning_environment::PlanningMonitor::transformJoint(const std::string &name, 
                                                           unsigned int index, 
                                                           double &param, 
                                                           std::string &frame_id, 
                                                           const std::string& target, 
                                                           motion_planning_msgs::ArmNavigationErrorCodes &error_code) const
{
  // TODO
  // planar joints and floating joints may need to be transformed 
  const planning_models::KinematicModel::JointModel *joint = getKinematicModel()->getJointModel(name);
  if (joint == NULL)
  {
    ROS_ERROR("Unknown joint '%s'", name.c_str());
    error_code.val = error_code.INVALID_TRAJECTORY;
    return false;
  }    
  frame_id = target;
  return true;
}

bool planning_environment::PlanningMonitor::isStateValid(const motion_planning_msgs::RobotState &robot_state, 
                                                         const int test, 
                                                         bool verbose, 
							 motion_planning_msgs::ArmNavigationErrorCodes &error_code)
{   
  planning_models::KinematicState state(getKinematicModel());

  //setting the robot's configuration
  setRobotStateAndComputeTransforms(robot_state, state);
  getEnvironmentModel()->updateRobotModel(&state);

  bool valid = true;
    
  bool vlevel = getEnvironmentModel()->getVerbose();
  getEnvironmentModel()->setVerbose(verbose);

  motion_planning_msgs::DisplayTrajectory d_path;
  convertKinematicStateToRobotState(state, d_path.robot_state);
  d_path.trajectory.joint_trajectory.header = d_path.robot_state.joint_state.header;
  d_path.trajectory.joint_trajectory.joint_names = d_path.robot_state.joint_state.name;
  d_path.trajectory.joint_trajectory.points.resize(1);
  d_path.trajectory.joint_trajectory.points[0].positions = d_path.robot_state.joint_state.position;
  display_state_validity_publisher_.publish(d_path);

  std::vector<collision_space::EnvironmentModel::Contact> contacts;
  if (test & COLLISION_TEST) {
    unsigned int numContacts = num_contacts_for_display_;
    if(!allowedContacts_.empty()) {
      numContacts = num_contacts_allowable_contacts_test_;
    }
    valid = !getEnvironmentModel()->getCollisionContacts(allowedContacts_, contacts, numContacts);    
    // If a callback exists for contact determination, call the callback for each of the found contacts                                                     
    if (onCollisionContact_) {
      for (unsigned int i = 0 ; i < contacts.size() ; ++i)
        onCollisionContact_(contacts[i]);
    }
    
    if(!valid) 
    {
      error_code.val = error_code.COLLISION_CONSTRAINTS_VIOLATED;
      if(verbose)
        ROS_ERROR("State is in collision.\n");
      return false;
    }
  }

  //
  // Check the joint limits
  //
  if (test & JOINT_LIMITS_TEST)
  {
    bool within_bounds = state.areJointsWithinBounds(robot_state.joint_state.name);      
    if(!within_bounds)
    {
      error_code.val = error_code.JOINT_LIMITS_VIOLATED;
      if(verbose)
        ROS_WARN("Joint limits violated.\n");
      return false;
    }
  }


  //
  // Check the path constraints
  //
  if (test & PATH_CONSTRAINTS_TEST)
  {	    
    valid = checkPathConstraints(&state,true);
    if(!valid) 
    {
      error_code.val = error_code.PATH_CONSTRAINTS_VIOLATED;
      if(verbose)
        ROS_WARN("State violates path constraints.\n");
      return false;
    }
  }


  //
  // Check the goal constraints
  //
  if (test & GOAL_CONSTRAINTS_TEST)
  {	    
    ROS_DEBUG("Evaluating goal constraints: joint: %u, position: %u, orientation: %u",
              (unsigned int)goal_constraints_.joint_constraints.size(),
              (unsigned int)goal_constraints_.position_constraints.size(),
              (unsigned int)goal_constraints_.orientation_constraints.size());

    valid = checkGoalConstraints(&state,true);
    if(!valid) 
    {
      error_code.val = error_code.GOAL_CONSTRAINTS_VIOLATED;
      if(verbose)
        ROS_WARN("State violates goal constraints.\n");
      return false;
    }
  }    
  getEnvironmentModel()->setVerbose(vlevel);
  return valid;    
}

int planning_environment::PlanningMonitor::closestStateOnTrajectory(const trajectory_msgs::JointTrajectory &trajectory, 
                                                                    motion_planning_msgs::RobotState &robot_state, 
                                                                    motion_planning_msgs::ArmNavigationErrorCodes &error_code) const
{
  return closestStateOnTrajectory(trajectory, robot_state, 0, trajectory.points.size() - 1, error_code);
}

int planning_environment::PlanningMonitor::closestStateOnTrajectory(const trajectory_msgs::JointTrajectory &trajectory, 
                                                                    motion_planning_msgs::RobotState &robot_state, 
                                                                    unsigned int start, 
                                                                    unsigned int end, 
                                                                    motion_planning_msgs::ArmNavigationErrorCodes &error_code) const
{
  if (end >= trajectory.points.size())
    end = trajectory.points.size() - 1;
  if (start > end)
  {
    ROS_ERROR("Invalid start %d and end %d specification",start,end);
    error_code.val = error_code.INVALID_INDEX;
    return -1;
  }

  if (trajectory.header.frame_id != getWorldFrameId())
  {
    trajectory_msgs::JointTrajectory pathT = trajectory;
    if (transformTrajectoryToFrame(pathT, robot_state, getWorldFrameId(), error_code))
      return closestStateOnTrajectoryAux(pathT, start, end, error_code);
    else
    {
      ROS_ERROR("Could not transform trajectory from %s to %s",trajectory.header.frame_id.c_str(),getWorldFrameId().c_str());
      error_code.val = error_code.FRAME_TRANSFORM_FAILURE;
      return -1;
    }
  }
  else
    return closestStateOnTrajectoryAux(trajectory, start, end, error_code);  
}

int planning_environment::PlanningMonitor::closestStateOnTrajectoryAux(const trajectory_msgs::JointTrajectory &trajectory, 
                                                                       unsigned int start, 
                                                                       unsigned int end, 
                                                                       motion_planning_msgs::ArmNavigationErrorCodes &error_code) const
{
  double dist = 0.0;
  int    pos  = -1;

  std::map<std::string, double> current_joint_vals = getCurrentJointStateValues();

  for(unsigned int i = 0; i < trajectory.joint_names.size(); i++) {
    if(current_joint_vals.find(trajectory.joint_names[i]) == current_joint_vals.end()) {
      ROS_ERROR("Unknown joint '%s' found on path", trajectory.joint_names[i].c_str());
      error_code.val = error_code.INVALID_TRAJECTORY;
      return -1;
    }
  }

  for (unsigned int i = start ; i <= end ; ++i)
  {
    double d = 0.0;
    for (unsigned int j = 0 ; j < trajectory.joint_names.size(); ++j)
    {
      double current_joint_position = current_joint_vals.find(trajectory.joint_names[j])->second;
      double diff = fabs(trajectory.points[i].positions[j] - current_joint_position);
      d += diff * diff;
    }
	
    if (pos < 0 || d < dist)
    {
      pos = i;
      dist = d;
    }
  }    
  return pos;
}



bool planning_environment::PlanningMonitor::isTrajectoryValid(const trajectory_msgs::JointTrajectory &trajectory, 
                                                              motion_planning_msgs::RobotState &robot_state, 
                                                              const int test, 
                                                              bool verbose, 
                                                              motion_planning_msgs::ArmNavigationErrorCodes &error_code, 
                                                              std::vector<motion_planning_msgs::ArmNavigationErrorCodes> &trajectory_error_codes)
{
  return isTrajectoryValid(trajectory, 
                           robot_state, 
                           0, 
                           trajectory.points.size() - 1, 
                           test, 
                           verbose,
                           error_code,trajectory_error_codes);
}

bool planning_environment::PlanningMonitor::isTrajectoryValid(const trajectory_msgs::JointTrajectory &trajectory, 
                                                              motion_planning_msgs::RobotState &robot_state, 
                                                              unsigned int start, 
                                                              unsigned int end, 
                                                              const int test, 
                                                              bool verbose,
                                                              motion_planning_msgs::ArmNavigationErrorCodes &error_code,
                                                              std::vector<motion_planning_msgs::ArmNavigationErrorCodes> &trajectory_error_codes)
{
  if (end >= trajectory.points.size())
    end = trajectory.points.size() - 1;
  if (start > end)
  {
    ROS_ERROR("Invalid trajectory: start: %d, end: %d",start,end);
    error_code.val = error_code.INVALID_INDEX;
    return true;
  }
  if (trajectory.header.frame_id != getWorldFrameId())
  {
    trajectory_msgs::JointTrajectory pathT = trajectory;

    if (transformTrajectoryToFrame(pathT, robot_state, getWorldFrameId(), error_code))
      return isTrajectoryValidAux(pathT, robot_state, start, end, test, verbose, error_code, trajectory_error_codes);
    else
    { 
      ROS_WARN("Could not transform trajectory from frame: %s to frame: %s",pathT.header.frame_id.c_str(),getWorldFrameId().c_str());
      error_code.val = error_code.FRAME_TRANSFORM_FAILURE;
      return false;
    }
  }
  else
    return isTrajectoryValidAux(trajectory, robot_state, start, end, test, verbose, error_code, trajectory_error_codes); 
}

bool planning_environment::PlanningMonitor::isTrajectoryValidAux(const trajectory_msgs::JointTrajectory &trajectory, 
                                                                 motion_planning_msgs::RobotState &robot_state, 
                                                                 unsigned int start, 
                                                                 unsigned int end, 
                                                                 const int test, 
                                                                 bool verbose,
                                                                 motion_planning_msgs::ArmNavigationErrorCodes &error_code,
                                                                 std::vector<motion_planning_msgs::ArmNavigationErrorCodes> &trajectory_error_codes)
{  
  
  planning_models::KinematicState state(getKinematicModel());
  
  //setting the robot's configuration
  setRobotStateAndComputeTransforms(robot_state, state);
  getEnvironmentModel()->updateRobotModel(&state);

  bool vlevel = getEnvironmentModel()->getVerbose();
  getEnvironmentModel()->setVerbose(verbose);
  
  bool valid = true;
    
  //joint map for setting trajectories
  std::map<std::string, double> joint_value_map;

  // get the joints this trajectory is for
  std::vector<planning_models::KinematicState::JointState*> joints(trajectory.joint_names.size());
  for (unsigned int j = 0 ; j < joints.size() ; ++j)
  {
    joints[j] = state.getJointState(trajectory.joint_names[j]);
    if (joints[j] == NULL)
    {
      ROS_ERROR("Unknown joint '%s' found on path", trajectory.joint_names[j].c_str());
      error_code.val = error_code.INVALID_TRAJECTORY;
      return false;
    } else {
      joint_value_map[joints[j]->getName()] = 0.0;
    }
  }

  //unsigned int remainingContacts = maxCollisionContacts_;    
  // check every state
  trajectory_error_codes.resize(trajectory.points.size());
  bool check_all = (test & CHECK_FULL_TRAJECTORY);
  bool valid_all = true;
  for (unsigned int i = start ; i <= end ; ++i)
  {
    if (trajectory.points[i].positions.size() != joints.size())
    {
      ROS_ERROR("Incorrect state specification on trajectory");
      error_code.val = error_code.INVALID_TRAJECTORY;      
      trajectory_error_codes[i].val = error_code.INVALID_TRAJECTORY;
      valid = false;
      valid_all = valid && valid_all;
      if(check_all)
        continue;
      else
        break;
    }
    for (unsigned int j = 0 ; j < trajectory.points[i].positions.size(); j++)
    {
      joint_value_map[trajectory.joint_names[j]] = trajectory.points[i].positions[j];
    }
    state.setKinematicState(joint_value_map);
    getEnvironmentModel()->updateRobotModel(&state);

    //
    // check the joint limits
    //
    if (test & JOINT_LIMITS_TEST)
    {
      valid = state.areJointsWithinBounds(trajectory.joint_names);
      valid_all = valid && valid_all;
      if(!valid)
      {
        error_code.val = error_code.JOINT_LIMITS_VIOLATED;
        trajectory_error_codes[i].val = error_code.JOINT_LIMITS_VIOLATED;
        ROS_ERROR("Joint limits violated");
        if(check_all)
          continue;
        else
          break;
      }
    }

    //
    // check for collisions
    //
    std::vector<collision_space::EnvironmentModel::Contact> contacts;
    if (test & COLLISION_TEST) 
    {
      unsigned int numContacts = num_contacts_for_display_;
      if(!allowedContacts_.empty()) {
        numContacts = num_contacts_allowable_contacts_test_;
      }
      valid = !getEnvironmentModel()->getCollisionContacts(allowedContacts_, contacts, numContacts);    
      
      if (onCollisionContact_) {
        for (unsigned int i = 0 ; i < contacts.size() ; ++i)
          onCollisionContact_(contacts[i]);
      }
      
      valid_all = valid && valid_all;
    }
    if(!valid)
    {
      //        if(!this->broadcastCollisions())
      //          ROS_WARN("Could not broadcast solutions");
      ROS_DEBUG("Found a collision for trajectory index: %d",i);

      motion_planning_msgs::DisplayTrajectory d_path;
      d_path.trajectory.joint_trajectory.header = robot_state.joint_state.header;
      d_path.trajectory.joint_trajectory.joint_names = trajectory.joint_names;
      d_path.trajectory.joint_trajectory.points.resize(1);
      d_path.trajectory.joint_trajectory.points[0] = trajectory.points[i];
      d_path.robot_state = robot_state;
      display_collision_pose_publisher_.publish(d_path);

      error_code.val = error_code.COLLISION_CONSTRAINTS_VIOLATED;
      trajectory_error_codes[i].val = error_code.COLLISION_CONSTRAINTS_VIOLATED;
      if(check_all)
        continue;
      else
        break;
    }
    //
    // check against the path constraints
    //
    if (test & PATH_CONSTRAINTS_TEST)
    {
      valid = checkPathConstraints(&state, verbose);
      valid_all = valid && valid_all;
      if (!valid)
      {
        if(verbose)
          ROS_INFO("isTrajectoryValid: State %d does not satisfy path constraints", i);
        error_code.val = error_code.PATH_CONSTRAINTS_VIOLATED;
        trajectory_error_codes[i].val = error_code.COLLISION_CONSTRAINTS_VIOLATED;
        if(check_all)
          continue;
        else
          break;
      }
    }	    
  }

  //
  // check against the goal constraints
  //
  if (valid_all && (test & GOAL_CONSTRAINTS_TEST))
  {
    valid = checkGoalConstraints(&state, verbose);
    valid_all = valid && valid_all;
    if (!valid)
    {
      ROS_WARN("isTrajectoryValid: Goal state does not satisfy goal constraints");
      error_code.val = error_code.GOAL_CONSTRAINTS_VIOLATED;
    }
  }

  getEnvironmentModel()->setVerbose(vlevel);
  return valid_all;
}

bool planning_environment::PlanningMonitor::broadcastCollisions() {
  
  unsigned int numContacts = num_contacts_for_display_;
  if(!allowedContacts_.empty()) {
    numContacts = num_contacts_allowable_contacts_test_;
  }
  //this just broadcasts collisions for the most recent collision space calls
  if(onCollisionContact_) {
    std::vector<collision_space::EnvironmentModel::Contact> contacts;
    getEnvironmentModel()->getCollisionContacts(allowedContacts_, contacts, numContacts); 
    ROS_DEBUG("Callback defined with %u contacts", (unsigned int) contacts.size());
    for (unsigned int i = 0 ; i < contacts.size() ; ++i) {
      onCollisionContact_(contacts[i]);
    } 
  } 
  else 
  {
    return false;
  }
  return true;
}

//TODO - write this function
// bool planning_environment::PlanningMonitor::isRobotTrajectoryValid(const trajectory_msgs::JointTrajectory &trajectory, 
//                                                                    motion_planning_msgs::RobotState &robot_state, 
//                                                                    unsigned int start, 
//                                                                    unsigned int end, 
//                                                                    const int test, 
//                                                                    bool verbose,
//                                                                    motion_planning_msgs::ArmNavigationErrorCodes &error_code,
//                                                                    std::vector<motion_planning_msgs::ArmNavigationErrorCodes> &trajectory_error_codes) const {
  
// }


void planning_environment::PlanningMonitor::setAllowedContacts(const std::vector<collision_space::EnvironmentModel::AllowedContact> &allowedContacts)
{
  allowedContacts_ = allowedContacts;
}

void planning_environment::PlanningMonitor::setAllowedContacts(const std::vector<motion_planning_msgs::AllowedContactSpecification> &allowed_contacts)
{  
  allowedContacts_.clear();
  for (unsigned int i = 0 ; i < allowed_contacts.size() ; ++i)
  {
    collision_space::EnvironmentModel::AllowedContact ac;
    if (computeAllowedContact(allowed_contacts[i], ac))
      allowedContacts_.push_back(ac);
  }
}

const std::vector<collision_space::EnvironmentModel::AllowedContact>& planning_environment::PlanningMonitor::getAllowedContacts(void) const
{
  return allowedContacts_;
}

void planning_environment::PlanningMonitor::printAllowedContacts(std::ostream &out)
{
  out << allowedContacts_.size() << " allowed contacts" << std::endl;
  for (unsigned int i = 0 ; i < allowedContacts_.size() ; ++i)
  {
    out << "  - allowing contacts up to depth " << allowedContacts_[i].depth << " between links: [";
    for (unsigned int j = 0 ; j < allowedContacts_[i].links.size() ; ++j)
      out << allowedContacts_[i].links[j];
    out << "] and bound " << allowedContacts_[i].bound.get() << std::endl;
  }
}

void planning_environment::PlanningMonitor::printConstraints(std::ostream &out)
{
  out << "Path constraints:" << std::endl;

  KinematicConstraintEvaluatorSet constraint_evaluator;
  constraint_evaluator.add(path_constraints_.joint_constraints);
  constraint_evaluator.add(path_constraints_.position_constraints);
  constraint_evaluator.add(path_constraints_.orientation_constraints);
  constraint_evaluator.add(path_constraints_.visibility_constraints);
  constraint_evaluator.print(out);

  out << "Goal constraints:" << std::endl;
  constraint_evaluator.clear();
  constraint_evaluator.add(goal_constraints_.joint_constraints);
  constraint_evaluator.add(goal_constraints_.position_constraints);
  constraint_evaluator.add(goal_constraints_.orientation_constraints);
  constraint_evaluator.add(goal_constraints_.visibility_constraints);
  constraint_evaluator.print(out);
}

void planning_environment::PlanningMonitor::clearAllowedContacts(void)
{
  allowedContacts_.clear();
}

void planning_environment::PlanningMonitor::setCollisionCheck(const std::string link_name, bool state)
{
  getEnvironmentModel()->setCollisionCheck(link_name,state);
}

void planning_environment::PlanningMonitor::setCollisionCheckAll(bool state)
{
  getEnvironmentModel()->setCollisionCheckAll(state);
}

void planning_environment::PlanningMonitor::setCollisionCheckLinks(const std::vector<std::string> &link_names, bool state)
{
  getEnvironmentModel()->setCollisionCheckLinks(link_names,state);
}

void planning_environment::PlanningMonitor::setCollisionCheckOnlyLinks(const std::vector<std::string> &link_names, bool state)
{
  getEnvironmentModel()->setCollisionCheckOnlyLinks(link_names,state);
}

void planning_environment::PlanningMonitor::getChildLinks(const std::vector<std::string> &joints, 
							  std::vector<std::string> &link_names)
{
  std::set<std::string> links_set;

  for(unsigned int i=0; i < joints.size(); i++)
  {
    const planning_models::KinematicModel::JointModel *joint = getKinematicModel()->getJointModel(joints[i]);
    if(joint)
    {
      if(joint->getChildLinkModel()) {
        std::vector<const planning_models::KinematicModel::LinkModel*> child_links;
        getKinematicModel()->getChildLinkModels(joint->getChildLinkModel(), child_links);
        for(std::vector<const planning_models::KinematicModel::LinkModel*>::iterator it = child_links.begin();
            it != child_links.end();
            it++) {
          links_set.insert((*it)->getName());
        }
      }
    }
  }
  for(std::set<std::string>::iterator set_iterator = links_set.begin(); set_iterator!= links_set.end(); set_iterator++)
  {
    link_names.push_back(*set_iterator);
  }
}

void planning_environment::PlanningMonitor::getOrderedCollisionOperationsForOnlyCollideLinks(const std::vector<std::string> &collision_check_links, 
                                                                                             const motion_planning_msgs::OrderedCollisionOperations &requested_collision_operations,
                                                                                             motion_planning_msgs::OrderedCollisionOperations &result_collision_operations)
{
  motion_planning_msgs::OrderedCollisionOperations result;
  motion_planning_msgs::CollisionOperation op;
  std::vector<motion_planning_msgs::CollisionOperation> self_collisions;

  //this disables everything and everything
  op.object1 = op.COLLISION_SET_ALL;
  op.object2 = op.COLLISION_SET_ALL;
  op.operation = op.operation = motion_planning_msgs::CollisionOperation::DISABLE;;
  result.collision_operations.push_back(op);
  
  //now we need to add bodies attached to these links
  std::vector<std::string> all_collision_links = collision_check_links;
  const std::vector<const planning_models::KinematicModel::AttachedBodyModel*> att_vec = getEnvironmentModel()->getAttachedBodies();
  for(unsigned int i = 0; i < att_vec.size(); i++) 
  {
    for(std::vector<std::string>::const_iterator it = collision_check_links.begin();
        it != collision_check_links.end();
        it++) 
    {
      if(att_vec[i]->getAttachedLinkModel()->getName() == (*it)) {
        all_collision_links.push_back(att_vec[i]->getName());
      }
    }
  }

  //this enables collision_check_links with everything
  for(std::vector<std::string>::const_iterator it = all_collision_links.begin();
      it != all_collision_links.end();
      it++) 
  {
    op.object1 = (*it);
    op.object2 = op.COLLISION_SET_ALL;
    op.operation = motion_planning_msgs::CollisionOperation::ENABLE;
    result.collision_operations.push_back(op);
  } 

  std::vector<std::vector<bool> > current_allowed;
  std::map<std::string, unsigned int> vec_indices;
  //this disables collision_check_links with things they are allowed to collide with
  getEnvironmentModel()->getDefaultAllowedCollisionMatrix(current_allowed, vec_indices);
  for(std::vector<std::string>::const_iterator it = all_collision_links.begin();
      it != all_collision_links.end();it++)
  {
    std::map<std::string, unsigned int>::iterator map_it = vec_indices.find(*it);
    if(map_it == vec_indices.end())
      continue;
    unsigned int index = map_it->second;
    for(std::map<std::string, unsigned int>::iterator index_it = vec_indices.begin(); index_it != vec_indices.end(); index_it++)
    {
      if(current_allowed[index][index_it->second])
      {	
        op.object1 = (*it);
        op.object2 = index_it->first;
        op.operation = motion_planning_msgs::CollisionOperation::DISABLE;
        result.collision_operations.push_back(op);    
      }
    }
  }  

  //this adds extra requested collision operations
  for(std::vector<motion_planning_msgs::CollisionOperation>::const_iterator it = requested_collision_operations.collision_operations.begin();
      it != requested_collision_operations.collision_operations.end();it++)
  {
    result.collision_operations.push_back(*it);
  }  

  result_collision_operations.collision_operations = result.collision_operations;

}

bool planning_environment::PlanningMonitor::checkPathConstraints(const planning_models::KinematicState* state, bool verbose) const {
  KinematicConstraintEvaluatorSet constraint_evaluator;

  constraint_evaluator.add(path_constraints_.joint_constraints);
  constraint_evaluator.add(path_constraints_.position_constraints);
  constraint_evaluator.add(path_constraints_.orientation_constraints);
  constraint_evaluator.add(path_constraints_.visibility_constraints);
  return(constraint_evaluator.decide(state, verbose));
}

bool planning_environment::PlanningMonitor::checkGoalConstraints(const planning_models::KinematicState* state, bool verbose) const {
  KinematicConstraintEvaluatorSet constraint_evaluator;

  constraint_evaluator.add(goal_constraints_.joint_constraints);
  constraint_evaluator.add(goal_constraints_.position_constraints);
  constraint_evaluator.add(goal_constraints_.orientation_constraints);
  constraint_evaluator.add(goal_constraints_.visibility_constraints);
  return(constraint_evaluator.decide(state, verbose));
}