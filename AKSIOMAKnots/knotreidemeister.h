#ifndef KNOTREIDEMEISTER_H
#define KNOTREIDEMEISTER_H

#include "knotdiagram.h"

namespace KnotMath
{

// A Reidemeister move that can be made on a drawn knot diagram, ready to be
// animated. The moves that tidy the picture up are offered:
//   R1: a twist (a loop with a single crossing) is untwisted: one crossing less
//   R2: a strand lying over another one twice is pulled off it: two less
//   R3: a strand going over (or under) both strands of a crossing slides
//       across that crossing: the same number, but a different picture
// Each move empties one face of the diagram: the loop, the two-sided or the
// three-sided region.
struct ReidemeisterMove
{
    // 1, 2 or 3
    int type = 1;

    // the crossings of the diagram the move acts on (1, 2 or 3 of them)
    QVector<int> crossings;

    // outline of the face the move works on
    QVector<Vec3> region;

    // a point well inside the region (for its label)
    Vec3 centre;

    // The curve before and after the move, point by point. z is the height
    // over the drawing plane, which decides over and under; the moving strand
    // is lifted above (or pushed below) everything it slides across, so every
    // frame in between is the same knot.
    QVector<Vec3> start;
    QVector<Vec3> end;

    // the diagram after the move, evenly resampled
    KnotDiagram result;
};

// All moves that can be made on the diagram. A move is only offered if its
// face is empty (no other strand inside) and the finished picture has
// exactly the crossings it should have.
QVector<ReidemeisterMove> findReidemeisterMoves(
    const KnotDiagram &diagram);

// Index of the move whose region contains (x, y), or -1.
int reidemeisterMoveAt(
    const QVector<ReidemeisterMove> &moves,
    double x,
    double y);

// The diagram part way through the move (t from 0 to 1).
KnotDiagram reidemeisterFrame(
    const ReidemeisterMove &move,
    double t);

// Diagram of a curve seen from above (+z): the crossings of its shadow,
// with over and under taken from the heights.
KnotDiagram diagramFromHeights(
    const QVector<Vec3> &curve);

// A last safety check: the Alexander polynomial before and after the move
// is the same (true as well if it cannot be computed).
bool reidemeisterMoveKeepsKnot(
    const KnotDiagram &before,
    const ReidemeisterMove &move);

} // namespace KnotMath

#endif // KNOTREIDEMEISTER_H
