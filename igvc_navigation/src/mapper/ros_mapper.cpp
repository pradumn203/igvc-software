// Subscribes to Point Cloud Data, updates the occupancy grid, then publishes the data.
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <unordered_set>

#include <Eigen/Core>
#include <opencv2/core/eigen.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

#include <pcl/point_cloud.h>
#include <pcl_ros/point_cloud.h>
#include <pcl_ros/transforms.h>

#include <ros/ros.h>

#include <igvc_msgs/map.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/Image.h>

#include <tf/transform_datatypes.h>
#include <tf_conversions/tf_eigen.h>

#include <visualization_msgs/Marker.h>
#include <igvc_utils/NodeUtils.hpp>
#include <igvc_utils/RobotState.hpp>

#include <cv_bridge/cv_bridge.h>

#include "map_utils.h"
#include "ros_mapper.h"

ROSMapper::ROSMapper() : tf_listener_{ std::unique_ptr<tf::TransformListener>(new tf::TransformListener()) }
{
  ros::NodeHandle nh;
  ros::NodeHandle pNh("~");

  igvc::getParam(pNh, "octree/resolution", resolution_);
  igvc::getParam(pNh, "map/length", length_x_);
  igvc::getParam(pNh, "map/width", width_y_);
  igvc::getParam(pNh, "map/start_x", start_x_);
  igvc::getParam(pNh, "map/start_y", start_y_);

  igvc::getParam(pNh, "blur/kernel_size", kernel_size_);
  igvc::getParam(pNh, "blur/std_dev", blur_std_dev_);

  igvc::getParam(pNh, "topics/lidar", lidar_topic_);
  igvc::getParam(pNh, "topics/line_segmentation", line_topic_);
  igvc::getParam(pNh, "topics/projected_line_pc", projected_line_topic_);
  igvc::getParam(pNh, "topics/camera_info", camera_info_topic_);

  igvc::getParam(pNh, "cameras/resize_width", resize_width_);
  igvc::getParam(pNh, "cameras/resize_height", resize_height_);

  igvc::getParam(pNh, "node/debug", debug_);
  igvc::getParam(pNh, "node/use_lines", use_lines_);

  mapper_ = std::make_unique<Mapper>(pNh);

  ros::Subscriber pcl_sub = nh.subscribe<pcl::PointCloud<pcl::PointXYZ>>(lidar_topic_, 1, &ROSMapper::pcCallback, this);
  ros::Subscriber line_map_sub;
  ros::Subscriber projected_line_map_sub;
  ros::Subscriber camera_info_sub;

  if (use_lines_)
  {
    ROS_INFO_STREAM("Subscribing to " << line_topic_ << " for image and " << projected_line_topic_
                                      << " for projected pointclouds");
    line_map_sub = nh.subscribe<sensor_msgs::Image>(line_topic_, 1, &ROSMapper::segmentedImageCallback, this);
    projected_line_map_sub =
        nh.subscribe<pcl::PointCloud<pcl::PointXYZ>>(projected_line_topic_, 1, &ROSMapper::projctedLineCallback, this);
    camera_info_sub = nh.subscribe<sensor_msgs::CameraInfo>(camera_info_topic_, 1, &ROSMapper::cameraInfoCallback, this);
  }

  //  published_map_ = std::unique_ptr<cv::Mat>(new cv::Mat(length_x_, width_y_, CV_8UC1));

  map_pub_ = nh.advertise<igvc_msgs::map>("/map", 1);

  if (debug_)
  {
    debug_pcl_pub_ = nh.advertise<pcl::PointCloud<pcl::PointXYZRGB>>("/map_debug_pcl", 1);
    debug_blurred_pc_ = nh.advertise<pcl::PointCloud<pcl::PointXYZRGB>>("/map_debug_pcl/blurred", 1);
  }

  ros::spin();
}

/**
 * Updates <code>RobotState state</code> with the latest tf transform using the timestamp of the message passed in
 * @param[in] msg <code>pcl::PointCloud</code> message with the timestamp used for looking up the tf transform
 */
template <>
bool ROSMapper::getOdomTransform(const ros::Time message_timestamp)
// bool ROSMapper::getOdomTransform(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr &msg)
{
  tf::StampedTransform transform;
  static ros::Duration wait_time = ros::Duration(transform_max_wait_time_);
  try
  {
    if (tf_listener_->waitForTransform("/odom", "/base_link", message_timestamp, wait_time))
    {
      tf_listener_->lookupTransform("/odom", "/base_link", message_timestamp, transform);
      state_.setState(transform);
      return true;
    }
    else
    {
      ROS_DEBUG("Failed to get transform from /base_link to /odom in time, using newest transforms");
      tf_listener_->lookupTransform("/odom", "/base_link", ros::Time(0), transform);
      state_.setState(transform);
      tf_listener_->lookupTransform("/odom", "/lidar", ros::Time(0), transform);
      return true;
    }
  }
  catch (const tf::TransformException &ex)
  {
    ROS_ERROR("tf Transform error");
    ROS_ERROR("%s", ex.what());
    return false;
  }
  catch (std::runtime_error &ex)
  {
    ROS_ERROR("runtime error");
    ROS_ERROR("Runtime Exception at getOdomTransform: [%s]", ex.what());
    return false;
  }
}

template <>
bool ROSMapper::getOdomTransform(const uint64 timestamp)
{
  ros::Time message_timestamp;
  pcl_conversions::fromPCL(timestamp, message_timestamp);
  return getOdomTransform(message_timestamp);
}

// Populates igvc_msgs::map message with information from sensor_msgs::Image and the timestamp from pcl_stamp
/**
 * Populates <code>igvc_msgs::map message</code> with information from <code>sensor_msgs::Image</code> and the
 * timestamp from <code>pcl_stamp</code>
 * @param[out] message message to be filled out
 * @param[in] image image containing map data to be put into <code>message</code>
 * @param[in] pcl_stamp time stamp from the pcl to be used for <code>message</code>
 */
void ROSMapper::setMessageMetadata(igvc_msgs::map &message, sensor_msgs::Image &image, uint64_t pcl_stamp)
{
  pcl_conversions::fromPCL(pcl_stamp, image.header.stamp);
  pcl_conversions::fromPCL(pcl_stamp, message.header.stamp);
  message.header.frame_id = "/odom";
  message.image = image;
  message.length = static_cast<unsigned int>(length_x_ / resolution_);
  message.width = static_cast<unsigned int>(width_y_ / resolution_);
  message.resolution = static_cast<float>(resolution_);
  message.orientation = static_cast<float>(state_.yaw());
  message.x = static_cast<unsigned int>(std::round(state_.x() / resolution_) + start_x_ / resolution_);
  message.y = static_cast<unsigned int>(std::round(state_.y() / resolution_) + start_y_ / resolution_);
  message.x_initial = static_cast<unsigned int>(start_x_ / resolution_);
  message.y_initial = static_cast<unsigned int>(start_y_ / resolution_);
}

/**
 * Checks if transform from base_footprint to msg.header.frame_id exists
 * @param[in] msg
 * @param[in] topic Topic to check for
 */

template <>
bool ROSMapper::checkExistsStaticTransform(const std::string &frame_id, ros::Time message_timestamp,
                                        const std::string &topic)
{
  if (transforms_.find(topic) == transforms_.end())
  {
    // Wait for transform between frame_id (ex. /scan/pointcloud) and base_footprint.
    ROS_INFO_STREAM("Getting transform for " << topic << " from " << frame_id << " to /base_footprint \n");
    if (tf_listener_->waitForTransform("/base_footprint", frame_id, message_timestamp, ros::Duration(3.0)))
    {
      tf::StampedTransform transform;
      tf_listener_->lookupTransform("/base_footprint", frame_id, message_timestamp, transform);
      transforms_.insert(std::pair<std::string, tf::StampedTransform>(topic, transform));
      ROS_INFO_STREAM("Found static transform from " << frame_id << " to /base_footprint");
    }
    else
    {
      ROS_ERROR_STREAM("Failed to find transform for " << topic << " from " << frame_id << " to /base_footprint using empty transform");
      return false;
    }
  }
  return true;
}

template <>
bool ROSMapper::checkExistsStaticTransform(const std::string &frame_id, uint64 timestamp, const std::string &topic)
{
  if (transforms_.find(topic) == transforms_.end())
  {
    // Wait for transform between frame_id (ex. /scan/pointcloud) and base_footprint.
    ros::Time message_timestamp;
    pcl_conversions::fromPCL(timestamp, message_timestamp);
    return checkExistsStaticTransform(frame_id, message_timestamp, topic);
  }
  return true;
}

/**
 * Publishes the given map at the given stamp
 * @param[in] map map to be published
 * @param[in] stamp pcl stamp of the timestamp to be used
 */
void ROSMapper::publish(uint64_t stamp)
{
  // Combine line and lidar maps, then blur them
  cv::Mat blurred_map;
  if (pc_map_pair_.map)
  {
    blurred_map = pc_map_pair_.map->clone();
  }
  else
  {
    blurred_map = cv::Mat(camera_map_pair_.map->size().height, camera_map_pair_.map->size().width, CV_8UC1, 127);
  }
  if (use_lines_)
  {
    cv::max(blurred_map, *camera_map_pair_.map, blurred_map);
  }
  MapUtils::blur(blurred_map, kernel_size_);

  igvc_msgs::map message;
  sensor_msgs::Image image;

  img_bridge_ = cv_bridge::CvImage(message.header, sensor_msgs::image_encodings::MONO8, blurred_map);
  img_bridge_.toImageMsg(image);
  setMessageMetadata(message, image, stamp);
  map_pub_.publish(message);

  if (debug_)
  {
    if (pc_map_pair_.map)
    {
      publishAsPCL(debug_pcl_pub_, *pc_map_pair_.map, "/odom", stamp);
    }
    publishAsPCL(debug_blurred_pc_, blurred_map, "/odom", stamp);
  }
}

void ROSMapper::publishAsPCL(const ros::Publisher &pub, const cv::Mat &mat, const std::string &frame_id, uint64_t stamp)
{
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr pointcloud = boost::make_shared<pcl::PointCloud<pcl::PointXYZRGB>>();
  for (int i = 0; i < width_y_ / resolution_; i++)
  {
    for (int j = 0; j < length_x_ / resolution_; j++)
    {
      pcl::PointXYZRGB p;
      uchar prob = mat.at<uchar>(i, j);
      if (prob > 127)
      {
        p = pcl::PointXYZRGB();
        p.x = static_cast<float>((i * resolution_) - (width_y_ / 2.0));
        p.y = static_cast<float>((j * resolution_) - (length_x_ / 2.0));
        p.r = 0;
        p.g = static_cast<uint8_t>((prob - 127) * 2);
        p.b = 0;
        pointcloud->points.push_back(p);
      }
      else if (prob < 127)
      {
        p = pcl::PointXYZRGB();
        p.x = static_cast<float>((i * resolution_) - (width_y_ / 2.0));
        p.y = static_cast<float>((j * resolution_) - (length_x_ / 2.0));
        p.r = 0;
        p.g = 0;
        p.b = static_cast<uint8_t>((127 - prob) * 2);
        pointcloud->points.push_back(p);
      }
    }
  }
  pointcloud->header.frame_id = frame_id;
  pointcloud->header.stamp = stamp;
  pub.publish(pointcloud);
}

/**
 * Callback for pointcloud. Filters the lidar scan, then inserts it into the octree.
 * @param[in] pc Lidar scan
 */
void ROSMapper::pcCallback(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr &pc)
{
  // Check if static transform already exists for this topic.
  if (!checkExistsStaticTransform(pc->header.frame_id, pc->header.stamp, lidar_topic_))
  {
    ROS_ERROR("Couldn't find static transform for pointcloud. Sleeping 2 seconds then trying again...");
    ros::Duration(2).sleep();
    return;
  }

  // Lookup transform form Ros Localization for position
  if (!getOdomTransform(pc->header.stamp))
  {
    ROS_ERROR("Couldn't find odometry transform in pcCallback. Sleeping 2 seconds then trying again...");
    ros::Duration(2).sleep();
    return;
  }

  mapper_->insertLidarScan(pc, state_.transform * transforms_.at(lidar_topic_));

  publish(pc->header.stamp);
}

/**
 * Callback for the neural network segmented image.
 * @param camera_info
 */
void ROSMapper::segmentedImageCallback(const sensor_msgs::ImageConstPtr &segmented)
{
  if (!camera_model_initialized_)
  {
    return;
  }
  // Check if static transform already exists for this topic.
  if (!checkExistsStaticTransform("optical_cacenter_", segmented->header.stamp, projected_line_topic_))
  {
    ROS_ERROR("Finding static transform failed. Sleeping 2 seconds then trying again...");
    ros::Duration(2).sleep();
    return;
  }

  // Lookup transform form Ros Localization for position
  if (!getOdomTransform(segmented->header.stamp))
  {
    ROS_ERROR("Finding odometry transform failed. Sleeping 2 seconds then trying again...");
    ros::Duration(2).sleep();
    return;
  }
  // Convert to OpenCV
  cv_bridge::CvImagePtr segmented_ptr = cv_bridge::toCvCopy(segmented, "mono8");

  cv::Mat image = segmented_ptr->image;

  mapper_->insertSegmentedImage(image, state_.transform * transforms_.at(projected_line_topic_));

  publish(pcl_conversions::toPCL(segmented->header.stamp));
}

/**
 * Callback for the line projetced onto lidar point cloud. Directly insert it into the line octomap
 * @param pc
 */
void ROSMapper::projctedLineCallback(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr &pc)
{
  // Lookup transform form Ros Localization for position
  if (!getOdomTransform(pc->header.stamp))
  {
    return;
  }
  mapper_->insertCameraProjection(pc, state_.transform);
  publish(pc->header.stamp);
}

void ROSMapper::cameraInfoCallback(const sensor_msgs::CameraInfoConstPtr &camera_info)
{
  if (!camera_model_initialized_)
  {
    image_geometry::PinholeCameraModel camera_model;
    sensor_msgs::CameraInfoConstPtr changed_camera_info = MapUtils::scaleCameraInfo(camera_info, resize_width_, resize_height_);
    camera_model.fromCameraInfo(changed_camera_info);
    mapper_->setProjectionModel(std::move(camera_model));

    camera_model_initialized_ = true;
  }
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "mapper");
  ROSMapper mapper;
}
