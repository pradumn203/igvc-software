#ifndef PROJECT_MAPPER_H
#define PROJECT_MAPPER_H

#include <ros/publisher.h>
#include <pcl/point_cloud.h>
#include <pcl_ros/point_cloud.h>

class Mapper
{
using radians = double;
public:
  Mapper();

private:
  // Callbacks
  void pc_callback(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr &pc);

  void line_map_callback(const sensor_msgs::ImageConstPtr &segmented);

  void projected_line_callback(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr &pc);

  void publish(const cv::Mat &map, uint64_t stamp);

  void setMessageMetadata(igvc_msgs::map &message, sensor_msgs::Image &image, uint64_t pcl_stamp);

  bool checkExistsStaticTransform(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr &msg, const std::string &topic);

  bool getOdomTransform(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr &msg);

  int discretize(radians angle) const;

  void get_empty_points(const pcl::PointCloud<pcl::PointXYZ> &pc, pcl::PointCloud<pcl::PointXYZ> &empty_pc);

  void filter_points_behind(const pcl::PointCloud<pcl::PointXYZ> &pc, pcl::PointCloud<pcl::PointXYZ> &filtered_pc);

  void blur(cv::Mat &blurred_map);

  void publish_as_pcl(const ros::Publisher &pub, const cv::Mat &mat, std::string frame_id, uint64_t stamp);

  cv_bridge::CvImage m_img_bridge;

  ros::Publisher m_map_pub;                                  // Publishes map
  ros::Publisher m_blurred_pub;                              // Publishes blurred map
  ros::Publisher m_debug_pub;                                // Debug version of above
  ros::Publisher m_debug_pcl_pub;                            // Publishes map as individual PCL points
  ros::Publisher m_debug_blurred_pc;                         // Publishes blurred map as individual PCL points
  ros::Publisher m_ground_pub;                               // Publishes ground points
  ros::Publisher m_nonground_pub;                            // Publishes non ground points
  ros::Publisher m_sensor_pub;                               // Publishes lidar position
  std::unique_ptr<cv::Mat> m_published_map;                  // Matrix will be publishing
  std::map<std::string, tf::StampedTransform> m_transforms;  // Map of static transforms TODO: Refactor this
  std::unique_ptr<tf::TransformListener> m_tf_listener;      // TF Listener

  bool m_use_lines;
  double m_resolution;
  double m_transform_max_wait_time;
  int m_start_x;   // start x (m)
  int m_start_y;   // start y (m)
  int m_length_x;  // length (m)
  int m_width_y;   // width (m)
  int m_kernel_size;
  int m_segmented_kernel;
  int m_segmented_sigma;
  int m_segmented_threshold;

  bool m_debug;
  double m_radius;  // Radius to filter lidar points // TODO: Refactor to a new node
  double m_lidar_miss_cast_distance;
  double m_filter_distance;
  double m_blur_std_dev;
  radians m_filter_angle;
  radians m_lidar_start_angle;
  radians m_lidar_end_angle;
  radians m_angular_resolution;
  std::string m_lidar_topic;
  std::string m_line_topic;
  std::string m_projected_line_topic;
  RobotState m_state;                      // Odom -> Base_link
  RobotState m_odom_to_lidar;              // Odom -> Lidar
  RobotState m_odom_to_camera_projection;  // Odom -> Camera Projection

  sensor_msgs::CameraInfo camera_info;

  std::unique_ptr<Octomapper> m_octomapper;
  pc_map_pair m_pc_map_pair;      // Struct storing both the octomap for the lidar and the cv::Mat map
  pc_map_pair m_camera_map_pair;  // Struct storing both the octomap for the camera projections and the cv::Mat map
};
#endif //PROJECT_MAPPER_H
