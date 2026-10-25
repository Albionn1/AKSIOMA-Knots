#ifndef DIAGRAMEDITOR_H
#define DIAGRAMEDITOR_H

#include "knotdiagram.h"

#include <QDialog>
#include <QPointF>
#include <QVector>
#include <QWidget>

class QLabel;
class QMouseEvent;
class QPaintEvent;
class QPainterPath;
class QPushButton;

// Drawing area: the user draws a closed loop with the mouse; crossings are
// found automatically and can be clicked to swap which strand is on top.
class DiagramCanvas final
    : public QWidget
{
    Q_OBJECT

public:
    explicit DiagramCanvas(
        QWidget *parent = nullptr);

    const KnotMath::KnotDiagram &diagram() const;

    void clearDrawing();
    void alternate();
    void mirror();

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    void diagramChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPointF toWidget(double x, double y) const;
    double widgetScale() const;
    void strandRange(
        QPainterPath &path,
        double s,
        double halfSamples) const;

private:
    KnotMath::KnotDiagram diagram_;
    QVector<QPointF> stroke_;
    bool drawing_ = false;
    bool tooShort_ = false;
    int hover_ = -1;
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
    QPushButton *okButton_ = nullptr;
    QPushButton *alternateButton_ = nullptr;
    QPushButton *mirrorButton_ = nullptr;
};

#endif // DIAGRAMEDITOR_H
