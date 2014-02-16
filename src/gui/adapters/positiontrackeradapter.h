#ifndef POSITIONTRACKERADAPTER_H
#define POSITIONTRACKERADAPTER_H

#include <QWidget>
#include <intelligence/posetracking/basicpositiontracker.h>
#include <vector>

namespace Ui {
class PositionTrackerAdapter;
}

class PositionTrackerAdapter : public QWidget
{
    Q_OBJECT

public:
    explicit PositionTrackerAdapter(BasicPositionTracker *src, QWidget *parent = 0);
    ~PositionTrackerAdapter();

protected:
    void paintEvent(QPaintEvent*);

Q_SIGNALS:
    void updateBecauseOfData();

private slots:
    void on_pushButton_clicked();

private:
    Ui::PositionTrackerAdapter *ui;

    BasicPositionTracker *posTracker;

    std::vector<RobotPosition> positions;

    double minx, maxx, miny, maxy;

    void onNewPosition(RobotPosition pos);
    LISTENER(PositionTrackerAdapter, onNewPosition, RobotPosition)

    void onOriginPercentage(int percent);
    LISTENER(PositionTrackerAdapter, onOriginPercentage, int)
};

#endif // POSITIONTRACKERADAPTER_H
