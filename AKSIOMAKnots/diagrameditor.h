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

    // the diagram changed: remember the old one for Undo, find the moves
    void setDiagram(
        const KnotMath::KnotDiagram &diagram,
        bool remember);

    void startDrawing(const QPointF &pos);
    void startMove(int index);
    void updateHover(const QPointF &pos);

private:
    KnotMath::KnotDiagram diagram_;
    QVector<KnotMath::ReidemeisterMove> moves_;

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
    explicit DiagramEditorDialog(
        QWidget *parent = nullptr);

    const KnotMath::KnotDiagram &diagram() const;

private:
    void updateStatus();

private:
    DiagramCanvas *canvas_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *moveLabel_ = nullptr;
    QPushButton *okButton_ = nullptr;
    QPushButton *undoButton_ = nullptr;
    QPushButton *alternateButton_ = nullptr;
    QPushButton *mirrorButton_ = nullptr;

    // what the last move did (shown until the mouse points at another one)
    QString lastMove_;
};

#endif // DIAGRAMEDITOR_H
