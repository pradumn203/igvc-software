#include <algorithm>
#include <chrono>
#include <thread>

#include <igvc_msgs/velocity_pair.h>
#include <ros/ros.h>
#include <sensor_msgs/JointState.h>
#include <std_msgs/Bool.h>
#include <std_msgs/Float64.h>

#include <igvc_utils/NodeUtils.hpp>
#include "gazebo_control.h"

GazeboControl::GazeboControl() : nh_{}, pNh_{ "~" }
{
  getParams();
  setupPublishers();
  setupShock();
  setupSubscribers();

  ros::Timer timer = nh_.createTimer(ros::Duration(1 / loop_rate_), &GazeboControl::controlLoop, this);
  ros::spin();
}

void GazeboControl::controlLoop(const ros::TimerEvent &timer_event)
{
  double dt = (timer_event.current_real - timer_event.last_real).toSec();
  auto [voltage_left, voltage_right] = getControls(dt);

  std_msgs::Float64 left_msg;
  std_msgs::Float64 right_msg;

  left_msg.data = voltage_left;
  right_msg.data = voltage_right;

  voltage_pub_left_.publish(left_msg);
  voltage_pub_right_.publish(right_msg);
}

std::pair<double, double> GazeboControl::getControls(double dt)
{
  if (dt == 0)
  {
    return std::make_pair(0.0, 0.0);
  }
  // 1. Calculate error
  double error_left = set_point_left_ - measured_left_;
  double error_right = set_point_right_ - measured_right_;

  ROS_INFO_STREAM("error: (" << error_left << " = " << set_point_left_ << " - " << measured_left_ << ", " << error_right
                             << " = " << set_point_right_ << " - " << measured_right_ << ")");

  // 2. Calculate derivative error
  lp_derivative_l =
      d_low_pass_alpha_ * (measured_left_prev_ - measured_left_) / dt + (1 - d_low_pass_alpha_) * lp_derivative_l;
  lp_derivative_r =
      d_low_pass_alpha_ * (measured_right_prev_ - measured_right_) / dt + (1 - d_low_pass_alpha_) * lp_derivative_r;

  //  ROS_INFO_STREAM("L: " << d_low_pass_alpha_ << " * (" << measured_left_prev_ << " - " << measured_left_ << ") / "
  //  << dt << " + (1 - " << d_low_pass_alpha_ << ") * " << lp_derivative_l); ROS_INFO_STREAM("R: " << d_low_pass_alpha_
  //  << " * (" << measured_right_prev_ << " - " << measured_right_ << ") / " << dt << " + (1 - " << d_low_pass_alpha_
  //  << ") * " << lp_derivative_r);

  // 3. Calculate integral error
  ROS_INFO_STREAM("accum_left: " << accum_left_ << " += " << error_left << " * " << dt << " = " << error_left * dt);
  ROS_INFO_STREAM("accum_right: " << accum_right_ << " += " << error_right << " * " << dt << " = " << error_right * dt
                                  << std::endl);
  accum_left_ += error_left * dt;
  accum_right_ += error_right * dt;

  // 4. Perform clamping on integral
  accum_left_ = std::clamp(accum_left_, -integral_clamp_, integral_clamp_);
  accum_right_ = std::clamp(accum_right_, -integral_clamp_, integral_clamp_);

  // 5. Sum P, I and D terms
  double feedback_left =
      left_coeffs_.Kp * error_left + left_coeffs_.Kd * lp_derivative_l + left_coeffs_.Ki * accum_left_;
  double feedback_right =
      right_coeffs_.Kp * error_right + right_coeffs_.Kd * lp_derivative_r + right_coeffs_.Ki * accum_right_;

  ROS_INFO_STREAM("L: " << left_coeffs_.Kp << " * " << error_left << " + " << left_coeffs_.Kd << " * "
                        << lp_derivative_l << " + " << left_coeffs_.Ki << " * " << accum_left_);
  ROS_INFO_STREAM("R: " << right_coeffs_.Kp << " * " << error_right << " + " << right_coeffs_.Kd << " * "
                        << lp_derivative_r << " + " << right_coeffs_.Ki << " * " << accum_right_ << std::endl);

  // 6. Calculate feedforward
  double feedforward_left = left_feedforward_coeffs_.Kv * set_point_left_;
  double feedforward_right = right_feedforward_coeffs_.Kv * set_point_right_;

  // 7. Sum feedback and feedforward
  double voltage_left = feedback_left + feedforward_left;
  double voltage_right = feedback_right + feedforward_right;

  //  ROS_INFO_STREAM("L: " << voltage_left << " = " << feedback_left << " + " << feedforward_left);
  //  ROS_INFO_STREAM("R: " << voltage_right << " = " << feedback_right << " + " << feedforward_right << std::endl);

  // Update previous terms
  measured_left_prev_ = measured_left_;
  measured_right_prev_ = measured_right_;

  return std::make_pair(voltage_left, voltage_right);
}

void GazeboControl::motorCallback(const igvc_msgs::velocity_pair::ConstPtr &msg)
{
  set_point_left_ = msg->left_velocity;
  set_point_right_ = msg->right_velocity;
}

void GazeboControl::encoderCallback(const igvc_msgs::velocity_pair::ConstPtr &msg)
{
  measured_left_ = msg->left_velocity;
  measured_right_ = msg->right_velocity;
}

void GazeboControl::getParams()
{
  igvc::param(pNh_, "topics/motors", motor_topic_, "/motors");
  igvc::param(pNh_, "topics/encoders", encoder_topic_, "/encoders");

  igvc::param(pNh_, "topics/voltage/left", voltage_left_topic_, "/voltage/left");
  igvc::param(pNh_, "topics/voltage/right", voltage_right_topic_, "/voltage/right");

  igvc::param(pNh_, "feedforward/left_motor/Kv", left_feedforward_coeffs_.Kv, 31.5);
  igvc::param(pNh_, "feedforward/right_motor/Kv", right_feedforward_coeffs_.Kv, 31.5);

  igvc::param(pNh_, "pid/left_motor/Kp", left_coeffs_.Kp, 5.0);
  igvc::param(pNh_, "pid/left_motor/Kd", left_coeffs_.Kd, 5.0);
  igvc::param(pNh_, "pid/left_motor/Ki", left_coeffs_.Ki, 5.0);

  igvc::param(pNh_, "pid/right_motor/Kp", right_coeffs_.Kp, 5.0);
  igvc::param(pNh_, "pid/right_motor/Kd", right_coeffs_.Kd, 5.0);
  igvc::param(pNh_, "pid/right_motor/Ki", right_coeffs_.Ki, 5.0);

  igvc::param(pNh_, "pid/low_pass_alpha", d_low_pass_alpha_, 0.75);
  igvc::param(pNh_, "pid/integral_clamp", integral_clamp_, 255.0);

  igvc::param(pNh_, "max_voltage", max_voltage_, 63.0);  // 1 - 127, with 64: stop => max is 63
  igvc::param(pNh_, "rate", loop_rate_, 60.0);
}

void GazeboControl::setupPublishers()
{
  voltage_pub_left_ = nh_.advertise<std_msgs::Float64>(voltage_left_topic_, 1);
  voltage_pub_right_ = nh_.advertise<std_msgs::Float64>(voltage_right_topic_, 1);
}

void GazeboControl::setupSubscribers()
{
  motor_sub_ = nh_.subscribe(motor_topic_, 1, &GazeboControl::motorCallback, this);
  encoder_sub_ = nh_.subscribe(encoder_topic_, 1, &GazeboControl::encoderCallback, this);
}

// TODO(Oswin): Move this somewhere or something? This doesn't really belong here, but I don't know where this should go
void GazeboControl::setupShock()
{
  ros::Publisher right_wheel_shock_publisher =
      nh_.advertise<std_msgs::Float64>("right_wheel_shock_controller/command", 1, true);
  ros::Publisher left_wheel_shock_publisher =
      nh_.advertise<std_msgs::Float64>("left_wheel_shock_controller/command", 1, true);

  std_msgs::Float64 shock_set_point;
  shock_set_point.data = 0.0;
  right_wheel_shock_publisher.publish(shock_set_point);
  left_wheel_shock_publisher.publish(shock_set_point);
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "jessii_controller");
  GazeboControl gazebo_control;
  return 0;
}
