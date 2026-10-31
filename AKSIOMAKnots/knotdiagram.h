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

// Diagram of a closed 3D curve: its cleanest projection (fewest crossings,
// spread out best; see diagramsFromCurve()), centred at the origin and scaled to
// half-extent `halfExtent`, with over/under taken from the curve. Returns
// an invalid diagram (with `error`) if there is no clean projection.
KnotDiagram diagramFromCurve(
    const QVector<Vec3> &curve,
    double halfExtent,
    QString &error);

// Several diagrams of the curve, best first: fewest crossings, then the
// crossings most evenly spread (far apart in the picture and along the
// curve). At most `maxCount`.
QVector<KnotDiagram> diagramsFromCurve(
    const QVector<Vec3> &curve,
    double halfExtent,
    QString &error,
    int maxCount);

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
// exactly the drawn diagram. Each pass gets two extra points close to it
// (at exactly its height), so the curve has 2 points more per pass than the
// diagram and over and under come out right however close passes are.
QVector<Vec3> diagramToCurve(
    const KnotDiagram &diagram,
    double height = 0.3);

// A place where two strands of the drawing lie too close for a tube of the
// wanted thickness: the whole tube would have to be thinned to fit it.
struct TightSpot
{
    Vec3 position;

    // the thickest tube that fits here
    double radius = 0.0;

    // the crossing whose smallest loop holds this place (see removeDiagramLoop),
    // or -1 if it lies on no small loop
    int crossing = -1;
};

// the tube thickness the drawing allows, at most `tubeRadius`
double diagramTubeRadius(
    const KnotDiagram &diagram,
    double tubeRadius,
    double height = 0.3);

// The tight spots, the tightest first.
QVector<TightSpot> findTightSpots(
    const KnotDiagram &diagram,
    double tubeRadius,
    double height = 0.3);

// Moves the strands apart until a tube of `tubeRadius` fits, as far as there
// is room in the picture. Only the shape changes: every step keeps the signed
// Gauss code of the diagram (the same crossings in the same order, with the
// same over and under), so it is the same knot. False if nothing got better.
bool spreadDiagram(
    KnotDiagram &diagram,
    double tubeRadius,
    double height = 0.3,
    double halfExtent = 2.0);

// Takes out the smaller of the two loops at the crossing: the curve goes
// straight on through it, and the crossings of that loop are gone. Unless
// nothing else crosses the loop this can change the knot. False if the rest
// could not be kept as it was.
bool removeDiagramLoop(
    KnotDiagram &diagram,
    int crossing,
    double halfExtent = 2.0);

// "x, y, z" per line - the format of the Custom Curve input.
QString diagramToCoordinateText(
    const QVector<Vec3> &curve);

} // namespace KnotMath

#endif // KNOTDIAGRAM_H
