#include "timelineslider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cmath>

TimelineSlider::TimelineSlider(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
{
}

void TimelineSlider::setMarks(const QVector<Mark> &marks)
{
    marks_ = marks;
    update();
}

void TimelineSlider::clearMarks()
{
    marks_.clear();
    update();
}

double TimelineSlider::xForPosition(double position) const
{
    QStyleOptionSlider option;
    initStyleOption(&option);

    const QRect groove =
        style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);

    const QRect handle =
        style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);

    const double half = 0.5 * handle.width();
    const double span = std::max(1.0, groove.width() - 2.0 * half);

    return groove.left() + half + std::clamp(position, 0.0, 1.0) * span;
}

double TimelineSlider::positionForX(double x) const
{
    const double left = xForPosition(0.0);
    const double right = xForPosition(1.0);

    return std::clamp((x - left) / std::max(1.0, right - left), 0.0, 1.0);
}

void TimelineSlider::paintEvent(QPaintEvent *event)
{
    QSlider::paintEvent(event);

    if (marks_.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const double y = 0.5 * height();
    const QColor tick = isEnabled() ? QColor(138, 135, 128) : QColor(70, 69, 66);
    const QColor dot = isEnabled() ? QColor(217, 155, 66) : QColor(110, 86, 48);

    for (const Mark &mark : marks_)
    {
        const double x = xForPosition(mark.position);

        if (mark.change)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(dot);
            painter.drawEllipse(QPointF(x, y), 3.2, 3.2);
        }
        else
        {
            painter.setPen(QPen(tick, 1.4));
            painter.drawLine(QPointF(x, y - 6.0), QPointF(x, y + 6.0));
        }
    }
}

void TimelineSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !isEnabled())
    {
        QSlider::mousePressEvent(event);
        return;
    }

    // dragging the handle works as usual
    QStyleOptionSlider option;
    initStyleOption(&option);

    const QStyle::SubControl hit =
        style()->hitTestComplexControl(
            QStyle::CC_Slider, &option, event->position().toPoint(), this);

    if (hit == QStyle::SC_SliderHandle)
    {
        QSlider::mousePressEvent(event);
        return;
    }

    const double x = event->position().x();

    double position = positionForX(x);

    // snap to a mark that is close by
    constexpr double snapPixels = 7.0;

    double nearest = snapPixels;

    for (const Mark &mark : marks_)
    {
        const double distance = std::abs(xForPosition(mark.position) - x);

        if (distance <= nearest)
        {
            nearest = distance;
            position = mark.position;
        }
    }

    setValue(minimum() +
             static_cast<int>(std::lround(position * (maximum() - minimum()))));

    event->accept();
}
