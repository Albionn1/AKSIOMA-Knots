#ifndef KNOTDIAGRAM_H
#define KNOTDIAGRAM_H

#include "knotmath.h"

namespace KnotMath
{

// One crossing of a hand-drawn knot diagram. The two passes are on the
// segments segA and segB of the closed polyline (parameters tA, tB in [0,1)).
struct DiagramCrossing
{
    int segA = 0;
    double tA = 0.0;

    int segB = 0;
    double tB = 0.0;

    Vec3 position;

    // true: the pass on segment segA lies over the pass on segB.
    bool aOver = true;
};

struct KnotDiagram
{
    // Closed polyline in the XY plane (z = 0), y pointing up.
    QVector<Vec3> points;

    QVector<DiagramCrossing> crossings;

    bool isValid() const
    {
        return points.size() >= 8;
    }
};

// Turns a freehand stroke (any number of points, y up, not necessarily
// closed) into a smooth closed diagram with `samples` points, centred at the
// origin and scaled to half-extent `halfExtent`. Crossings are found and set
// to alternating over/under. Returns an invalid diagram if the stroke is too
// short or degenerate.
KnotDiagram makeDiagramFromStroke(
    const QVector<Vec3> &stroke,
    int samples = 160,
    double halfExtent = 2.0);

// Finds all crossings of the polyline (keeps nothing from before) and sets
// them alternating along the curve.
void detectDiagramCrossings(
    KnotDiagram &diagram);

// Alternating over/under along the curve.
void alternateDiagram(
    KnotDiagram &diagram);

// Swaps over and under at every crossing (mirror image).
void mirrorDiagram(
    KnotDiagram &diagram);

// Index of the crossing closest to (x, y) within `maxDistance`, or -1.
int nearestDiagramCrossing(
    const KnotDiagram &diagram,
    double x,
    double y,
    double maxDistance);

// 3D curve above the diagram: over-passes are lifted by +height, under-passes
// lowered by -height, smoothly interpolated in between. Seen from +z it has
// exactly the drawn diagram.
QVector<Vec3> diagramToCurve(
    const KnotDiagram &diagram,
    double height = 0.3);

// "x, y, z" per line - the format of the Custom Curve input.
QString diagramToCoordinateText(
    const QVector<Vec3> &curve);

} // namespace KnotMath

#endif // KNOTDIAGRAM_H
