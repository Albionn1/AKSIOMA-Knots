#ifndef CONTROLHELP_H
#define CONTROLHELP_H

#include <QLabel>
#include <QString>

class QLineEdit;
class QSlider;

namespace ControlHelp
{

/*
 * A control that is greyed out says why: `reason` is its tooltip while it is
 * disabled, `tip` while it can be used.
 */
void explainDisabled(
    QWidget *control,
    const QString &tip,
    const QString &reason);

// a double click on the slider brings back `value`
void resetOnDoubleClick(
    QSlider *slider,
    int value);

/*
 * Parses what was typed for a slider of `unit` per step: a comma counts as a
 * decimal point, a percent sign and spaces are ignored. False if it is no
 * number; otherwise `value` is the nearest step within minimum..maximum.
 */
bool parseValue(
    const QString &text,
    double unit,
    int minimum,
    int maximum,
    int &value);

} // namespace ControlHelp

/*
 * The number next to a slider: click it to type an exact value (Enter takes
 * it, Esc or clicking away leaves things as they were).
 */
class SliderValue final
    : public QLabel
{
    Q_OBJECT

public:
    SliderValue(
        const QString &text,
        QSlider *slider,
        double unit,
        QWidget *parent);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void beginEditing();
    void finishEditing(bool take);

    QSlider *slider_ = nullptr;
    QLineEdit *edit_ = nullptr;
    double unit_ = 1.0;
};

#endif // CONTROLHELP_H
