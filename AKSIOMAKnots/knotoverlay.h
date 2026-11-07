#ifndef KNOTOVERLAY_H
#define KNOTOVERLAY_H

#include "knotmath.h"
#include <aksioma/layer.h>
#include <aksioma/renderer.h>

#include <QColor>
#include <QCoreApplication>
#include <aksioma/clock.h>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;
class TrainLayer;

/*
 * What the knot app draws over the 3D picture, as a layer of the view: the
 * numbers of the crossings, the flashes where strands pass through each other,
 * the knot's diagram in a corner, a caption at the foot, and in the Toy train
 * scene the black bars of a film and the panel of the train's journey.
 */
class KnotOverlay final : public Aksioma::Layer
{
    // its texts are translated as the view's were (KnotTheory_sq_AL.ts)
    Q_DECLARE_TR_FUNCTIONS(KnotGLWidget)

public:
    KnotOverlay(Aksioma::Renderer &view, const TrainLayer &train);

    // 2D knot diagram shown in a corner during the unknotting animation.
    struct DiagramInset
    {
        // closed curve in walking order; index 0 is the base point
        QVector<QPointF> points;

        struct Crossing
        {
            QPointF position;

            // passes on top and below right now (0..1 along the curve)
            double overU = 0.0;
            double underU = 0.0;

            // crossing change number (1-based), 0 = not changed
            int change = 0;

            // 0 plain, 1 still to be changed, 2 changing now, 3 changed
            int state = 0;

            // the strand on top now is the one that started below (for the
            // colours of the crossing that is changing)
            bool swapped = false;
        };

        QVector<Crossing> crossings;

        QString caption;

        // The diagram of an unknotting animation after `changesDone`
        // crossing changes; crossing change `activeChange` (1-based, 0 for
        // none) is drawn as the one changing now.
        static DiagramInset fromUnknotting(
            const KnotMath::UnknottingAnimation::Diagram &diagram,
            int changesDone,
            int activeChange);
    };

    // KnotGLWidget's overlays (see there)
    void setCrossings(const QVector<KnotMath::CrossingInfo> &crossings);
    void setShowCrossingNumbers(bool enabled);
    void setFlashes(const QVector<KnotMath::Vec3> &positions, const QVector<float> &progress,
                    const QVector<int> &numbers);
    void clearFlashes();
    void setDiagramInset(const DiagramInset &inset);
    void clearDiagramInset();
    void setCaption(const QString &title, const QString &body);

    // a new journey: no bridge is next yet
    void resetJourney();

    static void paintDiagram(QPainter &painter, const QRectF &area, const DiagramInset &inset,
                             const QColor &background);

    // Aksioma::Layer
    bool hasOverlay(const Aksioma::Renderer &view) const override;
    void paintOverlay(Aksioma::Renderer &view, QPainter &painter) override;

private:
    void drawCrossingLabels(QPainter &painter);
    void drawFlashes(QPainter &painter);
    void drawDiagramInset(QPainter &painter);
    void drawCaption(QPainter &painter);
    void drawJourney(QPainter &painter);

    // a film is showing (Cinema), the journey panel is up, the crossings are
    // numbered (the journey speaks of the bridges by number)
    bool film() const;
    bool journeyShown() const;
    bool labelsShown() const;

    Aksioma::Renderer &view_;
    const TrainLayer &train_;

    QVector<KnotMath::CrossingInfo> crossings_;
    bool showCrossingNumbers_ = false;

    QVector<KnotMath::Vec3> flashPositions_;
    QVector<float> flashProgress_;
    QVector<int> flashNumbers_;

    DiagramInset inset_;
    bool showInset_ = false;

    QString captionTitle_;
    QString captionBody_;
    // (the renderer's clock: a caption fades in a recording too)
    Aksioma::Stopwatch captionClock_;

    // the bridge the train comes to next (drawJourney)
    int journeyBridge_ = 0;
};

#endif // KNOTOVERLAY_H
