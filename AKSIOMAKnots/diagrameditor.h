#ifndef DIAGRAMEDITOR_H
#define DIAGRAMEDITOR_H

#include "knotdiagram.h"
#include "knotreidemeister.h"

#include <QDialog>
#include <QPointF>
#include <QVector>
#include <QWidget>

class QLabel;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QPainterPath;
class QPushButton;
class QVariantAnimation;

// Drawing area: the user draws a closed loop with the mouse; crossings are
// found automatically and can be clicked to swap which strand is on top.
// Faces of the diagram where a Reidemeister move can be made are shaded;
// clicking one animates the move.
class DiagramCanvas final
    : public QWidget
{
    Q_OBJECT

public:
    explicit DiagramCanvas(
        QWidget *parent = nullptr);

    const KnotMath::KnotDiagram &diagram() const;

    // the Reidemeister moves the diagram offers now
    const QVector<KnotMath::ReidemeisterMove> &moves() const;

    // type (1, 2, 3) of the move under the mouse, or 0
    int hoveredMoveType() const;

    // type of the move being animated, or 0
    int activeMoveType() const;

    bool isAnimating() const;
    bool canUndo() const;

    void clearDrawing();
    void alternate();
    void mirror();
    void undo();

    // The thickness the drawing will be made with: places too tight for it
    // are ringed. 0: nobody will make a tube of it, nothing is ringed.
    void setTubeRadius(double radius);
    const QVector<KnotMath::TightSpot> &tightSpots() const;

    // The review of a finished drawing: nothing is drawn any more, a click
    // picks a crossing (or a ringed place, with its crossing).
    void setReviewing(bool reviewing);
    int pickedCrossing() const;

    // moves the strands apart (the same knot); false if there was no room
    bool spreadApart();

    // of the picked crossing
    void flipPicked();

    // what the removal did: 0 nothing, 1 the same knot (a twist undone), 2
    // other crossings went with the loop (it may be another knot)
    int removePickedLoop();

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    void diagramChanged();

    // the mouse went onto or off a move's region
    void hoverChanged();

    // a Reidemeister move (type 1, 2, 3) was animated and is done
    void moveFinished(int type);

    // the move was not made: the safety check found the knot would change
    void moveRefused();

    // another crossing was picked in the review
    void pickChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPointF toWidget(double x, double y) const;
    KnotMath::Vec3 toDiagram(const QPointF &pos) const;
    double widgetScale() const;

    void drawDiagram(
        QPainter &painter,
        const KnotMath::KnotDiagram &diagram) const;

    void drawMoves(QPainter &painter) const;
    void drawTightSpots(QPainter &painter) const;

    // the diagram changed: remember the old one for Undo, find the moves
    void setDiagram(
        const KnotMath::KnotDiagram &diagram,
        bool remember);

    void startDrawing(const QPointF &pos);
    void startMove(int index);
    void updateHover(const QPointF &pos);
    void pickAt(const QPointF &pos);
    int tightSpotAt(const QPointF &pos) const;

private:
    KnotMath::KnotDiagram diagram_;
    QVector<KnotMath::ReidemeisterMove> moves_;

    double tubeRadius_ = 0.0;
    QVector<KnotMath::TightSpot> tightSpots_;
    bool reviewing_ = false;
    int picked_ = -1;

    QVector<QPointF> stroke_;
    bool drawing_ = false;
    bool tooShort_ = false;

    // the diagram before the drawing started (for Undo)
    KnotMath::KnotDiagram beforeDrawing_;

    int hover_ = -1;
    int hoverMove_ = -1;

    // pressed on a move's region: a click makes the move, a drag draws
    int pressedMove_ = -1;
    QPointF pressPos_;

    QVector<KnotMath::KnotDiagram> history_;

    QVariantAnimation *animation_ = nullptr;
    KnotMath::ReidemeisterMove activeMove_;
    KnotMath::KnotDiagram frame_;
};

class DiagramEditorDialog final
    : public QDialog
{
    Q_OBJECT

public:
    // `tubeRadius`: the thickness the knot will be made with. A drawing too
    // tight for it is reviewed before it is used. 0: no tube, no review.
    explicit DiagramEditorDialog(
        QWidget *parent = nullptr,
        double tubeRadius = 0.0);

    const KnotMath::KnotDiagram &diagram() const;

private:
    void updateStatus();
    void updateReviewStatus();
    void setReviewing(bool reviewing);
    void useKnot();

private:
    DiagramCanvas *canvas_ = nullptr;
    QLabel *helpLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *moveLabel_ = nullptr;
    QPushButton *okButton_ = nullptr;
    QPushButton *undoButton_ = nullptr;
    QPushButton *alternateButton_ = nullptr;
    QPushButton *mirrorButton_ = nullptr;

    // the review: its own row of buttons replaces the drawing's
    QWidget *drawButtons_ = nullptr;
    QWidget *reviewButtons_ = nullptr;
    QPushButton *flipButton_ = nullptr;
    QPushButton *removeButton_ = nullptr;
    QPushButton *reviewUndoButton_ = nullptr;
    QPushButton *reviewUseButton_ = nullptr;
    double tubeRadius_ = 0.0;
    bool reviewing_ = false;

    // what the last step of the review did
    QString reviewNote_;

    // what the last move did (shown until the mouse points at another one)
    QString lastMove_;
};

#endif // DIAGRAMEDITOR_H
