#ifndef BARE_PANEL_H
#define BARE_PANEL_H

#include <ros/ros.h>
#include <QLabel>
#include <std_msgs/UInt8.h>

#include <rviz/panel.h>

#include <ctime>

class QLineEdit;

class TimePanel: public rviz::Panel
{
  Q_OBJECT
  public:
    TimePanel( QWidget* parent = 0 );

  private:
    void timeCallback(const std_msgs::UInt8 &msg);

  public Q_SLOTS:

  protected Q_SLOTS:

  protected:
    ros::Subscriber sub;
    ros::NodeHandle nh_;
    QLabel* output_topic_editor_;
    time_t start;

};

#endif // BARE_PANEL_H