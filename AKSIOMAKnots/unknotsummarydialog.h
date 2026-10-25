#ifndef UNKNOTSUMMARYDIALOG_H
#define UNKNOTSUMMARYDIALOG_H

#include "knotglwidget.h"
#include "knotmath.h"

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QListWidget;
class QPushButton;

/*
 * Shown when an unknotting animation has finished: every crossing change
 * it made, one step at a time, with the knot diagram before and after the
 * change and why that crossing had to be changed. "Show in 3D" takes the
 * animation to that moment.
 */
class UnknotSummaryDialog final
    : public QDialog
{
    Q_OBJECT

public:
    struct Summary
    {
        KnotMath::UnknottingAnimation::Diagram diagram;

        // animation times (0..1): the diagram before the first change, every
        // crossing change (in order) and the finished descending diagram
        double startTime = 0.0;
        QVector<double> changeTimes;
        double resultTime = 1.0;

        // known unknotting number as text ("1", "[1,2]"), empty if unknown
        QString known;

        // name of the knot, empty if it has none
        QString knotName;
    };

    explicit UnknotSummaryDialog(
        const Summary &summary,
        QWidget *parent = nullptr);

signals:
    // shows the animation at time t (0..1)
    void showMoment(double t);

private:
    void showStep(int step);

    int changeCount() const;

    Summary summary_;

    QListWidget *steps_ = nullptr;

    QWidget *beforeView_ = nullptr;
    QWidget *afterView_ = nullptr;

    QLabel *stepTitle_ = nullptr;
    QLabel *stepText_ = nullptr;

    QPushButton *previousButton_ = nullptr;
    QPushButton *nextButton_ = nullptr;
};

#endif // UNKNOTSUMMARYDIALOG_H
