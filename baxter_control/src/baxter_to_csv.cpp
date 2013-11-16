/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2013, University of Colorado, Boulder
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
 *   * Neither the name of the Univ of CO, Boulder nor the names of its
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

/* Author: Dave Coleman
   Desc:   Records baxter data to CSV for Matlab/etc analysis
*/

#include <baxter_control/baxter_to_csv.h>

namespace baxter_control
{

BaxterToCSV::BaxterToCSV(bool position_cmd_mode)
  : arm_name_("left"),
    joint_name_("w1"),
    current_state_count_(0),
    command_index_(0),
    first_update_(false),
    position_cmd_mode_(position_cmd_mode) // if we are sending commands to baxter via position or velcoity
{
  // Start subscribers
  sub_joint_state_ = nh_.subscribe<sensor_msgs::JointState>("/robot/limb/" + arm_name_ +
                     "/joint_states", 1, &BaxterToCSV::stateCallback, this);
  if (position_cmd_mode_)
  {
    sub_command_ = nh_.subscribe<baxter_msgs::JointPositions>("/robot/limb/" + arm_name_ +
                   "/command_joint_angles", 1, &BaxterToCSV::cmdPositionCallback, this);
  }
  else
  {
    sub_command_ = nh_.subscribe<baxter_msgs::JointVelocities>("/robot/limb/" + arm_name_ +
                   "/command_joint_velocities", 1, &BaxterToCSV::cmdVelocityCallback, this);
  }

  // Wait for first state message to be recieved
  while(ros::ok() && state_msg_timestamp_.toSec() == 0)
  {
    ROS_INFO_STREAM_NAMED(arm_name_,"Waiting for first state message to be recieved");
    ros::spinOnce();
    ros::Duration(0.25).sleep();
  }
}

// Start the data collection
void BaxterToCSV::startRecording(const std::string& file_name)
{
  file_name_ = file_name;

  // Reset data collections
  joint_states_.clear();
  joint_commands_.clear();
  cmd_position_msgs_.clear();
  cmd_velocity_msgs_.clear();

  // Start sampling loop
  ros::Duration update_freq = ros::Duration(1.0/RECORD_RATE_HZ);
  non_realtime_loop_ = nh_.createTimer(update_freq, &BaxterToCSV::update, this);
}

void BaxterToCSV::stopRecording()
{
  non_realtime_loop_.stop();
  writeToFile(file_name_);
}

BaxterToCSV::~BaxterToCSV()
{
}

void BaxterToCSV::update(const ros::TimerEvent& e)
{
  if (first_update_)
    first_update_ = false;
  else
    ROS_INFO_STREAM_THROTTLE_NAMED(2, "update","Updating with period: "
      << ((e.current_real - e.last_real)*100) << " hz" );

  // Check if we are still connected to Baxter
  if ( stateExpired() )
  {
    ROS_ERROR_STREAM_NAMED("update","Aborting early");
    stopRecording();
  }

  joint_states_.push_back(state_msg_);
  //TODO joint_commands_.push_back(output_command_msg_.data); // record current command
  if (position_cmd_mode_)
    cmd_position_msgs_.push_back(cmd_position_msg_);
  else
    cmd_velocity_msgs_.push_back(cmd_velocity_msg_);

}

void BaxterToCSV::stateCallback(const sensor_msgs::JointStateConstPtr& msg)
{
  // Copy the latest message into a buffer
  state_msg_ = *msg;
  state_msg_timestamp_ = ros::Time::now();
}

void BaxterToCSV::cmdPositionCallback(const baxter_msgs::JointPositionsConstPtr& msg)
{
  // Copy the latest message into a buffer
  cmd_position_msg_ = *msg;
}

void BaxterToCSV::cmdVelocityCallback(const baxter_msgs::JointVelocitiesConstPtr& msg)
{
  // Copy the latest message into a buffer
  cmd_velocity_msg_ = *msg;
}

bool BaxterToCSV::stateExpired()
{
  // Check that we have a non-expired state message
  // \todo lower the expiration duration
  if( ros::Time::now() > state_msg_timestamp_ + ros::Duration(STATE_EXPIRED_TIMEOUT)) // check that the message timestamp is no older than 1 second
  {
    ROS_WARN_STREAM_THROTTLE_NAMED(1,arm_name_,"State expired. Last recieved state " << (ros::Time::now() - state_msg_timestamp_).toSec() << " seconds ago." );
    return true;
  }
  return false;
}

bool BaxterToCSV::writeToFile(const std::string& file_name)
{
  if (!joint_states_.size())
  {
    ROS_ERROR_STREAM_NAMED("baxter_to_csv","No joint states populated");
    return false;
  }

  std::ofstream output_file;
  output_file.open (file_name.c_str());

  // Output header -------------------------------------------------------
  output_file << "timestamp,";
  for (std::size_t j = 0; j < joint_states_[0].position.size(); ++j)
  {
    output_file << joint_states_[0].name[j] << "_pos,"
                << joint_states_[0].name[j] << "_vel,"
                << joint_states_[0].name[j] << "_eff,";
    if (position_cmd_mode_)
      output_file << joint_states_[0].name[j] << "_pos_cmd,";
    else
      output_file << joint_states_[0].name[j] << "_vel_cmd,";
  }
  output_file << std::endl;

  // Output data ------------------------------------------------------

  // Subtract starting time
  double start_time = joint_states_[0].header.stamp.toSec();

  for (std::size_t i = 0; i < joint_states_.size(); ++i)
  {
    // Timestamp
    output_file << joint_states_[i].header.stamp.toSec() - start_time << ",";

    // Output entire state of robot to single line
    for (std::size_t j = 0; j < joint_states_[i].position.size(); ++j)
    {
      // Output State
      output_file << joint_states_[i].position[j] << ","
                  << joint_states_[i].velocity[j] << ","
                  << joint_states_[i].effort[j] << ",";

      // Output Command
      if (position_cmd_mode_)
        output_file << cmd_position_msgs_[i].angles[j] << ",";
      else
        output_file << cmd_velocity_msgs_[i].velocities[j] << ",";
    }

    output_file << std::endl;
  }
  output_file.close();
  ROS_INFO_STREAM_NAMED("baxter_to_csv","Wrote to file " << file_name);
  return true;
}


} // namespace
