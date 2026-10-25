#ifndef TIMELINESLIDER_H
#define TIMELINESLIDER_H

#include <QSlider>
#include <QVector>

class QMouseEvent;
class QPaintEvent;

// Scrub bar of the unknotting animation. Thin ticks mark where a stage
// starts, amber dots the moments of the crossing changes. A click near a
// mark jumps exactly there, a click anywhere else jumps to that point.
class TimelineSlider final
    : public QSlider
{
public:
    struct Mark
    {
        // 0..1 along the bar
        double position = 0.0;

        // true: crossing change (dot), false: stage start (tick)
        bool change = false;
    };

    explicit TimelineSlider(QWidget *parent = nullptr);

    void setMarks(const QVector<Mark> &marks);
    void clearMarks();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    // x of a position 0..1 (the centre of the handle at that value)
    double xForPosition(double position) const;
    double positionForX(double x) const;

    QVector<Mark> marks_;
};

#endif // TIMELINESLIDER_H
