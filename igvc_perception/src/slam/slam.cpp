#include <slam/slam.h>

// Using statements
using gtsam::symbol_shorthand::B;
using gtsam::symbol_shorthand::V;
using gtsam::symbol_shorthand::X;

/**
 * Initializes the factor graph and all the subscribers and publishers
 */
Slam::Slam() : pnh_{ "~" }
{
  //    odom_sub_ = pnh_.subscribe("/wheel_odometry", 10, &Slam::OdomCallback, this);
  imu_sub_ = pnh_.subscribe("/imu", 100, &Slam::imuCallback, this);
  gps_sub_ = pnh_.subscribe("/odometry/gps", 1, &Slam::gpsCallback, this);
  mag_sub_ = pnh_.subscribe("/magnetometer_mag", 1, &Slam::magCallback, this);
  location_pub_ = pnh_.advertise<nav_msgs::Odometry>("/slam/position", 1);

  curr_index_ = 0;
  imu_connected_ = false;
  imu_update_available_ = false;

  intializeDirectionOfLocalMagField();
  initializeNoiseMatrices();
  initializeImuParams();
  initializePriors();
}

/**
 * The gpsCallback adds GPS measurements to the factor graph
 * @param msg An odometery message derived from GPS measurements (published by navsat_transform_node)
 */
void Slam::gpsCallback(const nav_msgs::Odometry &msg)
{
  gtsam::Point3 currPoint = Conversion::getPoint3FromOdom(msg);
  gtsam::GPSFactor gpsFactor(X(curr_index_), currPoint, gps_noise_);
  graph_.add(gpsFactor);

  if (imu_update_available_)
  {
    integrateAndAddIMUFactor();
  }

  imu_update_available_ = false;
}

/**
 * The imuCallback adds IMU measurements to the accumulator when it receives an IMU measurement
 * @param msg An imu sensor message (published by yostlab_driver_node)
 */
void Slam::imuCallback(const sensor_msgs::Imu &msg)
{
  ros::Time currTime = ros::Time::now();
  Vec3 measuredAcc = Vec3(msg.linear_acceleration.x, msg.linear_acceleration.y, msg.linear_acceleration.z);
  Vec3 measuredOmega = Vec3(msg.angular_velocity.x, msg.angular_velocity.y, msg.angular_velocity.z);
  if (!imu_connected_)
  {
    imu_connected_ = true;
    accum_.integrateMeasurement(measuredAcc, measuredOmega, 0.005);
  }
  else
  {
    double deltaT = (currTime - lastImuMeasurement_).toSec();
    if (deltaT > 0)
      accum_.integrateMeasurement(measuredAcc, measuredOmega, deltaT);
  }
  imu_update_available_ = true;
  lastImuMeasurement_ = currTime;
}

/**
 * The magCallback updates the curr_mag_reading_ with its most recent value
 * @param msg An MagneticField sensor message (published by yostlab_driver_node)
 */
void Slam::magCallback(const sensor_msgs::MagneticField &msg)
{
  curr_mag_reading_ = gtsam::Point3(msg.magnetic_field.x, msg.magnetic_field.y, msg.magnetic_field.z);
}

/**
 * If there are IMU measurements in the acumulator, this adds them as a single factor to the factor graph.
 */
void Slam::integrateAndAddIMUFactor()
{
  if (accum_.preintMeasCov().trace() != 0)
  {
    // Add bias factor
    auto factor = boost::make_shared<gtsam::BetweenFactor<gtsam::imuBias::ConstantBias> >(
        B(curr_index_), B(curr_index_ + 1), gtsam::imuBias::ConstantBias(), bias_noise_);
    graph_.add(factor);
    initEstimate_.insert(B(curr_index_ + 1), gtsam::imuBias::ConstantBias());

    // Add imu factor
    gtsam::ImuFactor imufac(X(curr_index_), V(curr_index_), X(curr_index_ + 1), V(curr_index_ + 1), B(curr_index_ + 1),
                            accum_);

    graph_.add(imufac);
    auto newPoseEstimate = result_.at<gtsam::Pose3>(X(curr_index_));  // gtsam::Pose3 newPoseEstimate
    newPoseEstimate =
        gtsam::Pose3(accum_.deltaRij() * newPoseEstimate.rotation(), accum_.deltaPij() + newPoseEstimate.translation());
    initEstimate_.insert(X(curr_index_ + 1), newPoseEstimate);
    Vec3 lastVel = result_.at<gtsam::Vector3>(V(curr_index_));
    lastVel += accum_.deltaVij();
    initEstimate_.insert(V(curr_index_ + 1), lastVel);
    curr_index_++;
    optimize();
  }
  accum_.resetIntegration();
}

/**
 * Triggers ISAM2 to optimize the current graph and publish the current estimated pose
 */
void Slam::optimize()
{
  static int iteration = 0;
  ROS_WARN_STREAM("SLAM: Iteration:" << iteration++ << " Imu_updated: " << imu_update_available_
                                     << " curr_index: " << curr_index_);
  // graph.print();
  isam_.update(graph_, initEstimate_);
  result_ = isam_.calculateEstimate();
  graph_.resize(0);
  initEstimate_.clear();

  auto currPose = result_.at<gtsam::Pose3>(X(curr_index_));  // gtsam::Pose2 currPose
  location_pub_.publish(Conversion::getOdomFromPose3(currPose));
}

/**
 * Sets the IMU Params
 */
void Slam::initializeImuParams()
{
  // Should be replaced with actual imu measurements. Values should come from the launch file.
  auto params = gtsam::PreintegrationParams::MakeSharedU(KGRAVITY);
  params->setAccelerometerCovariance(gtsam::I_3x3 * 0.1);
  params->setGyroscopeCovariance(gtsam::I_3x3 * 0.1);
  params->setIntegrationCovariance(gtsam::I_3x3 * 0.1);
  params->setUse2ndOrderCoriolis(false);
  params->setOmegaCoriolis(Vec3(0, 0, 0));
  accum_ = gtsam::PreintegratedImuMeasurements(params);
}

/**
 * Adds initial values of variables in the factor graph.
 */
void Slam::initializePriors()
{
  // Adding Initial Position (Pose + Covariance Matrix)
  gtsam::Pose3 priorPose;
  noiseDiagonal::shared_ptr poseNoise =
      noiseDiagonal::Sigmas((gtsam::Vector(6) << Vec3::Constant(0.1), Vec3::Constant(0.3)).finished());
  graph_.push_back(gtsam::PriorFactor<gtsam::Pose3>(X(0), priorPose, poseNoise));
  initEstimate_.insert(X(0), priorPose);

  // Adding Initial Velocity (Pose + Covariance Matrix)
  Vec3 priorVel(0.0, 0.0, 0.0);
  noiseDiagonal::shared_ptr velNoise = noiseDiagonal::Sigmas(Vec3::Constant(0.1));
  graph_.push_back(gtsam::PriorFactor<Vec3>(V(0), priorVel, velNoise));
  initEstimate_.insert(V(0), priorVel);

  // Adding Bias Prior
  noiseDiagonal::shared_ptr biasNoise = noiseDiagonal::Sigmas(gtsam::Vector6::Constant(0.1));
  gtsam::PriorFactor<gtsam::imuBias::ConstantBias> biasprior(B(0), gtsam::imuBias::ConstantBias(), biasNoise);
  graph_.push_back(biasprior);
  initEstimate_.insert(B(0), gtsam::imuBias::ConstantBias());

  optimize();
  ROS_INFO_STREAM("Priors Initialized.");
}

/**
 * Initializes the shared noise matrices from parameters in the launch file
 */
void Slam::initializeNoiseMatrices()
{
  double bias_noise = pnh_.param("biasNoiseConst", 0.03);
  double gps_xy_noise = pnh_.param("gpsXYNoiseConstant", 0.15);
  double gps_z_noise = pnh_.param("gpsZNoiseConstant", 0.15);
  double mag_noise = pnh_.param("magNoiseConstant", 0.00000005);
  gps_noise_ = noiseDiagonal::Sigmas(Vec3(gps_xy_noise, gps_xy_noise, gps_z_noise));
  mag_noise_ = noiseDiagonal::Sigmas(Vec3::Constant(mag_noise));
  bias_noise_ = noiseDiagonal::Sigmas(gtsam::Vector6::Constant(bias_noise));
}

void Slam::intializeDirectionOfLocalMagField()
{
  std::vector<double> lmg =
      pnh_.param("localMagneticField", std::vector<double>{ 0.0000227095, -0.0000020783, 0.0000432753 });
  local_mag_field_ = gtsam::Unit3(lmg[0], lmg[1], lmg[2]);
}