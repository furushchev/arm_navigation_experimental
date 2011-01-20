/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2010, Willow Garage, Inc.
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

/** \author E. Gil Jones */

#ifndef COLLISION_PROXIMITY_SPACE_
#define COLLISION_PROXIMITY_SPACE_

#include <vector>
#include <string>
#include <algorithm>
#include <sstream>

#include <ros/ros.h>

#include <planning_models/kinematic_model.h>
#include <planning_models/kinematic_state.h>
#include <planning_environment/monitors/collision_space_monitor.h>

#include <collision_proximity/collision_proximity_types.h>

namespace collision_proximity
{
//A class for implementation of proximity queries and proximity-based
//collision queries

class CollisionProximitySpace
{

public:

  CollisionProximitySpace(planning_environment::CollisionSpaceMonitor* monitor);
  ~CollisionProximitySpace();

  //this function sets up the collision proximity space for making a series of 
  //proximity collision or gradient queries for the indicated group
  void setupForGroupQueries(const std::string& group_name,
                            const motion_planning_msgs::RobotState& state);

  //returns the updating objects lock and destroys the current kinematic state
  void revertAfterGroupQueries();

  // sets the current group given the kinematic state
  void setCurrentGroupState(const planning_models::KinematicState& state);

  // returns true if the current group is in collision in the indicated state.
  // This doesn't affect the distance field or other robot links not in the group
  bool isStateInCollision() const;

  // returns the full set of collision information for each group link
  bool getStateCollisions(std::vector<std::string>& link_names, 
                          std::vector<std::string>& attached_body_names,
                          bool& in_collision, 
                          std::vector<CollisionType>& collisions) const;
  
  // returns the full gradient information for each group_link
  bool getStateGradients(std::vector<std::string>& link_names,
                         std::vector<std::string>& attached_body_names,
                         std::vector<double>& link_closest_distances, 
                         std::vector<std::vector<double> >& closest_distances, 
                         std::vector<std::vector<btVector3> >& closest_gradients,
                         bool subtract_radii = false) const;

  // returns the single closest proximity for the group previously configured
  //bool getEnvironmentProximity(ProximityInfo& prox) const;
  
  // returns true or false for environment collisions for the group that's been configured
  //bool getEnvironmentCollision() const;

  //
  //visualization functions
  //

  void visualizeProximityGradients(const std::vector<std::string>& link_names, 
                                   const std::vector<std::string>& attached_body_names,
                                   const std::vector<double>& link_closest_distances, 
                                   const std::vector<std::vector<double> >& closest_distances, 
                                   const std::vector<std::vector<btVector3> >& closest_gradients) const;
  

  void visualizeDistanceField() const;

  //void visualizeClosestCollisionSpheres(const std::vector<std::string>& link_names) const;

  void visualizeCollisions(const std::vector<std::string>& link_names, 
                           const std::vector<std::string>& attached_body_names, 
                           const std::vector<CollisionType> collisions) const;

  void visualizeObjectVoxels(const std::vector<std::string>& object_names) const;

  void visualizeObjectSpheres(const std::vector<std::string>& object_names) const;

  void visualizePaddedTrimeshes(const planning_models::KinematicState& state, const std::vector<std::string>& link_names) const;

  void visualizeConvexMeshes(const std::vector<std::string>& link_names) const;

  void visualizeBoundingCylinders(const std::vector<std::string>& object_names) const;

  
private:

  // returns true if current setup is in environment collision
  bool isEnvironmentCollision() const;

  // returns true if current setup is in intra-group collision
  bool isIntraGroupCollision() const;

  // sets the poses of the body to those held in the kinematic state
  void setBodyPosesToCurrent();

  // sets the body poses given the indicated kinematic state
  void setBodyPosesGivenKinematicState(const planning_models::KinematicState& state);

  void setDistanceFieldForGroupQueries(const std::string& group_name,
                                       const planning_models::KinematicState& state);

  bool getIntraGroupCollisions(std::vector<bool>& collisions,
                               bool stop_at_first = false) const;
    
  bool getIntraGroupProximityGradients(std::vector<double>& link_closest_distances,  
                                       std::vector<std::vector<double> >& closest_distances, 
                                       std::vector<std::vector<btVector3> >& closest_gradients,
                                       bool subtract_radii = false) const;

  bool getEnvironmentCollisions(std::vector<bool>& collisions,
                                bool stop_at_first = false) const;
  
  bool getEnvironmentProximityGradients(std::vector<double>& link_closest_distances, 
                                        std::vector<std::vector<double> >& closest_distances, 
                                        std::vector<std::vector<btVector3> >& closest_gradients,
                                        bool subtract_radii = false) const;

  bool getGroupLinkAndAttachedBodyNames(const std::string& group_name,
                                        std::vector<std::string>& link_names,
                                        std::vector<unsigned int>& link_indices,
                                        std::vector<std::string>& attached_body_names,
                                        std::vector<unsigned int>& attached_body_link_indices) const; 
  
  bool setupGradientStructures(const std::vector<std::string>& link_names,
                               const std::vector<std::string>& attached_body_names, 
                               std::vector<double>& link_closest_distance, 
                               std::vector<std::vector<double> >& closest_distances, 
                               std::vector<std::vector<btVector3> >& closest_gradients) const;

  void prepareDistanceField(const std::vector<std::string>& link_names, 
                            const planning_models::KinematicState& state);

  //double getCollisionSphereProximity(const std::vector<CollisionSphere>& sphere_list, 
  //                                  unsigned int& closest, btVector3& grad) const;

  btTransform getInverseWorldTransform(const planning_models::KinematicState& state) const;

  void staticObjectUpdateEvent(const mapping_msgs::CollisionObjectConstPtr &collisionObject);
  void attachedObjectUpdateEvent(const mapping_msgs::AttachedCollisionObjectConstPtr &attachedObject);

  //configuration convenience functions
  void loadRobotBodyDecompositions();
  void loadDefaultCollisionOperations();

  mutable std::vector<std::vector<double> > colors_;

  distance_field::PropagationDistanceField* distance_field_;

  planning_environment::CollisionSpaceMonitor* monitor_;

  ros::NodeHandle root_handle_, priv_handle_;

  ros::Publisher vis_marker_publisher_;
  ros::Publisher vis_marker_array_publisher_;

  mutable boost::recursive_mutex group_queries_lock_;

  std::map<std::string, BodyDecomposition*> body_decomposition_map_;
  std::map<std::string, BodyDecompositionVector*> static_object_map_;
  std::map<std::string, BodyDecompositionVector*> attached_object_map_;

  std::map<std::string, std::map<std::string, bool> > enabled_self_collision_links_;
  std::map<std::string, std::map<std::string, bool> > intra_group_collision_links_;
  std::map<std::string, std::map<std::string, bool> > attached_object_collision_links_;
  std::map<std::string, bool> environment_excludes_;

  //current entries to avoid map lookups during collision checks
  std::string current_group_name_;
  std::vector<std::string> current_link_names_;
  std::vector<std::string> current_attached_body_names_;
  std::vector<unsigned int> current_link_indices_;
  std::vector<unsigned int> current_attached_body_indices_;
  std::vector<BodyDecomposition*> current_link_body_decompositions_;
  std::vector<BodyDecompositionVector*> current_attached_body_decompositions_;
  std::vector<std::vector<bool> > current_intra_group_collision_links_;
  std::vector<bool> current_environment_excludes_;

  //just for initializing input
  std::vector<double> current_link_distances_;
  std::vector<std::vector<double> > current_closest_distances_;
  std::vector<std::vector<btVector3> > current_closest_gradients_;
  
  std::map<std::string, std::map<std::string, bool> > link_attached_objects_;

  //distance field configuration
  double size_x_, size_y_, size_z_;
  double origin_x_, origin_y_, origin_z_;
  double resolution_, tolerance_;

  double max_environment_distance_;

};

}
#endif
