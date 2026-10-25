#ifndef PRACTICEDIALOG_H
#define PRACTICEDIALOG_H

#include "knotpractice.h"

#include <QDialog>
#include <QPointF>
#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QPainterPath;
class QPushButton;
class QWidget;

/*
 * PRACTICE: exercises on a knot diagram, checked by the program.
 *
 *   Tricolouring    - colour the arcs with three colours so that at every
 *                     crossing the three arcs are all the same or all
 *                     different, using at least two colours. Possible
 *                     exactly when 3 divides the knot's determinant; if it
 *                     is possible the knot is not the unknot.
 *   Crossing signs  - mark every crossing of the oriented diagram +1 or -1;
 *                     their sum is the writhe of the diagram.
 */
class PracticeCanvas final
    : public QWidget
{
    Q_OBJECT

public:
    enum class Mode
    {
        Coloring = 0,
        Signs
    };

    explicit PracticeCanvas(
        QWidget *parent = nullptr);

    void setDiagram(
        const KnotMath::KnotDiagram &diagram);

    const KnotMath::KnotDiagram &diagram() const;
    const KnotMath::DiagramArcs &arcs() const;

    void setMode(Mode mode);

    // Colour the next click gives an arc (0, 1, 2; -1 erases).
    void setBrush(int color);

    // Colour per arc (-1 = not coloured).
    const QVector<int> &colors() const;
    void setColors(const QVector<int> &colors);

    // Answer per crossing: +1, -1, 0 = not answered.
    const QVector<int> &signAnswers() const;
    void setSignAnswers(const QVector<int> &answers);

    // Marks right and wrong sign answers (until the next change).
    void setSignsChecked(bool checked);

    // An arc drawn with a glow (hints), -1 for none.
    void setHighlightArc(int arc);

    static QColor tricolor(int color);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    // The user coloured an arc or answered a crossing.
    void changed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    double widgetScale() const;
    QPointF toWidget(double x, double y) const;
    QPointF toDiagram(const QPointF &widgetPoint) const;

    // Curve position (sample index + parameter) <-> length along the curve.
    double lengthAt(double s) const;
    double sAtLength(double length) const;
    QPointF pointAt(double s) const;
    // Unit tangent (widget coordinates) at s.
    QPointF tangentAt(double s) const;

    // Half the gap left in an under strand, in curve length.
    double gapLength() const;

    // The visible part of arc k as a path (from the end of one gap to the
    // start of the next).
    QPainterPath arcPath(int arc) const;

    // Arc under the widget point, -1 if none is near.
    int arcAtWidget(const QPointF &point) const;
    int crossingAtWidget(const QPointF &point) const;

    void drawArrow(QPainter &painter, double s, const QColor &color) const;
    void drawSignLegend(QPainter &painter) const;

private:
    KnotMath::KnotDiagram diagram_;
    KnotMath::DiagramArcs arcs_;
    // cumulative length of the polyline, cum_[i] at point i (n + 1 values)
    QVector<double> cum_;

    Mode mode_ = Mode::Coloring;
    int brush_ = 0;
    QVector<int> colors_;
    QVector<int> signAnswers_;
    bool signsChecked_ = false;
    int highlightArc_ = -1;
    int hoverArc_ = -1;
    int hoverCrossing_ = -1;
};

class PracticeDialog final
    : public QDialog
{
    Q_OBJECT

public:
    // `currentCurve`: the knot of the 3D view (empty if it is not a single
    // closed curve), `currentName` its name.
    PracticeDialog(
        const QVector<KnotMath::Vec3> &currentCurve,
        const QString &currentName,
        QWidget *parent = nullptr);

private:
    void exerciseChanged();
    void useCurrentKnot();
    void useRandomKnot();
    void drawOwnKnot();

    void setKnot(
        const KnotMath::KnotDiagram &diagram,
        const QString &name,
        bool mystery,
        const QString &error = QString());

    void brushChosen(int color);

    void coloringChanged();
    void coloringHint();
    void coloringImpossible();
    void coloringSolution();
    void coloringClear();

    void signsChanged();
    void signsCheck();
    void signsShow();
    void signsClear();

    // Marks the exercise as done and shows the name of a mystery knot;
    // returns a sentence naming it (empty if it was not hidden).
    QString revealName();
    void updateKnotLabel();
    void updateInstructions();
    void setFeedback(const QString &html);

    // Sentence about the determinant and tricolourability.
    QString determinantText() const;

private:
    QVector<KnotMath::Vec3> currentCurve_;
    QString currentName_;

    QString knotName_;
    bool mystery_ = false;
    bool solved_ = false;

    QComboBox *exerciseCombo_ = nullptr;
    QLabel *knotLabel_ = nullptr;
    QPushButton *currentButton_ = nullptr;
    QLabel *instructionsLabel_ = nullptr;
    PracticeCanvas *canvas_ = nullptr;
    QWidget *coloringTools_ = nullptr;
    QWidget *signTools_ = nullptr;
    QVector<QPushButton *> brushButtons_;
    QLabel *feedbackLabel_ = nullptr;
};

#endif // PRACTICEDIALOG_H
