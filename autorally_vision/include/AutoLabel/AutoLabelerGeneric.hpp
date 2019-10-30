/*
* Software License Agreement (BSD License)
* Copyright (c) 2019, Georgia Institute of Technology
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
* list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright notice,
* this list of conditions and the following disclaimer in the documentation
* and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/**
 * @file
 * @author Jason Gibson <jgibson37@gatech.edu>
 * @date OCtober 28, 2019
 * @copyright 2019 Georgia Institute of Technology
 * @brief Auto Label Tool
 *
 * @details generic wrapper for image auto labeling
 **/

#ifndef AUTOLABELER_GENERIC_HPP
#define AUTOLABELER_GENERIC_HPP

#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <opencv2/core/mat.hpp>
#include <boost/foreach.hpp>
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <sensor_msgs/image_encodings.h>
#include <opencv2/imgcodecs.hpp>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/Image.h>
#include <tf/LinearMath/Quaternion.h>
#include <tf/LinearMath/Matrix3x3.h>

namespace autorally_vision {
  struct ImageResult {
    cv::Mat mat;
    ros::Time time;
    nav_msgs::Odometry interp_pose;
  };

  class AutoLabellerGeneric {
  public:
    nav_msgs::Odometry findNextInterpolatedPose(rosbag::View::iterator& cur, const rosbag::View::iterator& end, const ros::Time time) {
      // search for the two pose messages that straddle the given time, then interpolate
      nav_msgs::Odometry::Ptr last_pose;
      while(cur != end) {
        rosbag::MessageInstance msg = *cur;
        nav_msgs::Odometry::Ptr next_pose = msg.instantiate<nav_msgs::Odometry>();
        if(msg.getTopic() == "pose_estimate") {
          // if past image message interpolate
          if(next_pose->header.stamp.toSec() > time.toSec()) {
            return interpolatePoses(*last_pose, *next_pose, time);
          }
          last_pose = next_pose;
        }
        cur++;
      }
    }

    nav_msgs::Odometry interpolatePoses(const nav_msgs::Odometry prev_odom, const nav_msgs::Odometry next_odom,
            const ros::Time time) {
      // linearly interpolate between the previous two poses, based off of time
      double ratio = (time.toSec() - prev_odom.header.stamp.toSec()) /
              (next_odom.header.stamp.toSec() - prev_odom.header.stamp.toSec());
      nav_msgs::Odometry result;
      result.header.stamp = time;
      result = prev_odom;
      result.pose.pose.position.x += (next_odom.pose.pose.position.x - prev_odom.pose.pose.position.x) * ratio;
      result.pose.pose.position.y += (next_odom.pose.pose.position.y - prev_odom.pose.pose.position.y) * ratio;
      result.pose.pose.position.z += (next_odom.pose.pose.position.z - prev_odom.pose.pose.position.z) * ratio;

      tf::Quaternion quat_1(prev_odom.pose.pose.orientation.x, prev_odom.pose.pose.orientation.y,
                            prev_odom.pose.pose.orientation.z, prev_odom.pose.pose.orientation.w);
      tf::Quaternion quat_2(next_odom.pose.pose.orientation.x, next_odom.pose.pose.orientation.y,
                            next_odom.pose.pose.orientation.z, next_odom.pose.pose.orientation.w);
      tf::Quaternion quat_result = quat_1.slerp(quat_2, ratio);
      result.pose.pose.orientation.x = quat_result.x();
      result.pose.pose.orientation.y = quat_result.y();
      result.pose.pose.orientation.z = quat_result.z();
      result.pose.pose.orientation.w = quat_result.w();
      return result;

    }

    ImageResult findNextImage(rosbag::View::iterator& cur, const rosbag::View::iterator& end) {
      // search through images until you find the next image
      ImageResult result;
      result.time = ros::Time(-1);
      bool found_image = false;
      nav_msgs::Odometry::Ptr last_pose;
      while(cur != end) {
        rosbag::MessageInstance msg = *cur;
        if(msg.getTopic() == "/left_camera/image_color/compressed") {
          found_image = true;
          sensor_msgs::Image::ConstPtr img = msg.instantiate<sensor_msgs::Image>();
          cv_bridge::CvImagePtr cv_ptr;
          try
          {
            cv_ptr = cv_bridge::toCvCopy(img, sensor_msgs::image_encodings::BGR8);
          } catch (cv_bridge::Exception& e) {
            ROS_ERROR("cv_bridge exception: %s", e.what());
            continue;
          }
          result.time = img->header.stamp;
          result.mat = cv_ptr->image;
          return result;
        } else if(found_image && msg.getTopic() == "/pose_estimate") {
          nav_msgs::Odometry::Ptr forward_pose = msg.instantiate<nav_msgs::Odometry>();
          result.interp_pose = interpolatePoses(*last_pose, *forward_pose, result.time);
          return result;
        } else if(msg.getTopic() == "/pose_estimate") {
          last_pose = msg.instantiate<nav_msgs::Odometry>();
        }
        cur++;
      }
      return result;
    }
  };
}


#endif //AUTOLABELER_GENERIC_HPP