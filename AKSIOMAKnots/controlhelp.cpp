#include "controlhelp.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QSlider>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace
{

// swaps a control's tooltip as it is enabled and disabled
class DisabledTip final
    : public QObject
{
public:
    DisabledTip(QWidget *control, const QString &tip, const QString &reason)
        : QObject(control),
          control_(control),
          tip_(tip),
          reason_(reason)
    {
        control->installEventFilter(this);

        apply();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == control_ && event->type() == QEvent::EnabledChange)
            apply();

        return QObject::eventFilter(object, event);
    }

private:
    void apply()
    {
        control_->setToolTip(control_->isEnabled() ? tip_ : reason_);
    }

    QWidget *control_;
    QString tip_;
    QString reason_;
};

class DoubleClickReset final
    : public QObject
{
public:
    DoubleClickReset(QSlider *slider, int value)
        : QObject(slider),
          slider_(slider),
          value_(value)
    {
        slider->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == slider_ && event->type() == QEvent::MouseButtonDblClick)
        {
            slider_->setValue(value_);

            return true;
        }

        return QObject::eventFilter(object, event);
    }

private:
    QSlider *slider_;
    int value_;
};

} // namespace

namespace ControlHelp
{

void explainDisabled(
    QWidget *control,
    const QString &tip,
    const QString &reason)
{
    new DisabledTip(control, tip, reason);
}

void resetOnDoubleClick(
    QSlider *slider,
    int value)
{
    new DoubleClickReset(slider, value);
}

bool parseValue(
    const QString &text,
    double unit,
    int minimum,
    int maximum,
    int &value)
{
    QString cleaned = text;

    cleaned.remove(QLatin1Char('%'));
    cleaned.remove(QLatin1Char(' '));
    cleaned.replace(QLatin1Char(','), QLatin1Char('.'));

    bool ok = false;

    const double number = cleaned.toDouble(&ok);

    if (!ok || !std::isfinite(number) || unit <= 0.0)
        return false;

    value = static_cast<int>(
        std::lround(std::clamp(number / unit, static_cast<double>(minimum), static_cast<double>(maximum))));

    return true;
}

} // namespace ControlHelp

SliderValue::SliderValue(
    const QString &text,
    QSlider *slider,
    double unit,
    QWidget *parent)
    : QLabel(text, parent),
      slider_(slider),
      unit_(unit)
{
    setCursor(Qt::IBeamCursor);
    setToolTip(tr("Click to type a value. Double-click the slider to reset it."));
}

void SliderValue::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        beginEditing();

    QLabel::mousePressEvent(event);
}

void SliderValue::beginEditing()
{
    if (edit_ != nullptr)
        return;

    edit_ = new QLineEdit(parentWidget());

    edit_->setAlignment(Qt::AlignRight);
    edit_->setText(
        unit_ < 1.0
            ? QString::number(slider_->value() * unit_, 'f', 2)
            : QString::number(slider_->value()));

    const int width = 74;

    edit_->setGeometry(this->x() + this->width() - width, this->y() - 5, width, this->height() + 10);
    edit_->installEventFilter(this);
    edit_->show();
    edit_->raise();
    edit_->selectAll();
    edit_->setFocus();
}

void SliderValue::finishEditing(bool take)
{
    if (edit_ == nullptr)
        return;

    QLineEdit *edit = edit_;

    edit_ = nullptr;

    if (take)
    {
        int value = 0;

        if (ControlHelp::parseValue(edit->text(), unit_, slider_->minimum(), slider_->maximum(), value))
            slider_->setValue(value);
    }

    edit->hide();
    edit->deleteLater();
}

bool SliderValue::eventFilter(QObject *object, QEvent *event)
{
    if (object == edit_)
    {
        if (event->type() == QEvent::KeyPress)
        {
            const int key = static_cast<QKeyEvent *>(event)->key();

            if (key == Qt::Key_Return || key == Qt::Key_Enter)
            {
                finishEditing(true);

                return true;
            }

            if (key == Qt::Key_Escape)
            {
                finishEditing(false);

                return true;
            }
        }

        if (event->type() == QEvent::FocusOut)
        {
            // (hidden from inside its own event: after it has been handled)
            QTimer::singleShot(0, this, [this]()
            {
                finishEditing(false);
            });
        }
    }

    return QLabel::eventFilter(object, event);
}
