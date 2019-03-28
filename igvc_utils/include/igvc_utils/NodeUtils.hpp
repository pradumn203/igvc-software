#ifndef NODEUTILS_HPP
#define NODEUTILS_HPP

#include <ros/ros.h>
#include <Eigen/Dense>
#include <cmath>
#include <tuple>

namespace igvc
{
template <class T>
void param(const ros::NodeHandle &pNh, const std::string &param_name, T &param_val, const T &default_val)
{
  if (!pNh.param(param_name, param_val, default_val))
  {
    ROS_ERROR_STREAM("Missing parameter " << param_name << " from " << pNh.getNamespace()
                                          << ". Continuing with default values.");
  }
}

template <class T>
void getParam(const ros::NodeHandle &pNh, const std::string &param_name, T &param_val)
{
  if (!pNh.getParam(param_name, param_val))
  {
    ROS_ERROR_STREAM("Missing parameter " << param_name << " from " << pNh.getNamespace() << "... exiting");
    ros::shutdown();
  }
}

/**
make_unique method for when using versions of c++ < 14
*/
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&... args)
{
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

/**
Calculates euclidian distance between two points

@tparam T the data type of the input points to calculate the euclidian distance for
@param[in] x1 x value of first point
@param[in] y1 y value of first point
@param[in] x2 x value of second point
@param[in] y2 y value of second point
@return the euclidian distance between both points
*/
template <typename T>
inline T get_distance(T x1, T y1, T x2, T y2)
{
  return std::hypot(x2 - x1, y2 - y1);
}

/**
Calculates euclidian distance between two points, taking tuples for each
(x,y) point as arguments

@tparam T the data type contained within each input tuple
@param[in] p1 the first point
@param[in] p2 the second point
@return the euclidian distance between both points
*/
template <typename T>
inline T get_distance(const std::tuple<T, T> &p1, const std::tuple<T, T> &p2)
{
  return igvc::get_distance(std::get<0>(p1), std::get<1>(p1), std::get<0>(p2), std::get<1>(p2));
}

/**
symmetric round up
Bias: away from zero

@tparam T the data type to round up
@param[in] the value to round up
@return the value rounded away from zero
*/
template <typename T>
T ceil0(const T &value)
{
  return (value < 0.0) ? std::floor(value) : std::ceil(value);
}

/**
Adjust angle to lie within the polar range [-PI, PI]
*/
inline void fit_to_polar(double &angle)
{
  while (angle > M_PI)
    angle -= 2 * M_PI;
  while (angle < -M_PI)
    angle += 2 * M_PI;
}

/**
Computes the egocentric polar angle of vec2 wrt vec1 in 2D, that is:
  - clockwise: negative
  - counter-clockwise: positive

source: https://stackoverflow.com/questions/14066933/direct-way-of-computing-clockwise-angle-between-2-vectors

@param[in] angle the double variable to assign the computed angle to
@param[in] vec2 the vector the angle is computed with respect to
@param[in] vec1 the reference vector
*/
inline void compute_angle(double &angle, Eigen::Vector3d vec2, Eigen::Vector3d vec1)
{
  double dot = vec2[0] * vec1[0] + vec2[1] * vec1[1];  // dot product - proportional to cos
  double det = vec2[0] * vec1[1] - vec2[1] * vec1[0];  // determinant - proportional to sin

  angle = atan2(det, dot);
}

}  // namespace igvc
#endif
