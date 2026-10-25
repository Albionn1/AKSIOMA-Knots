#ifndef KNOTPRACTICE_H
#define KNOTPRACTICE_H

#include "knotdiagram.h"

#include <QVector>

namespace KnotMath
{

/*
 * The structure of a knot diagram that the Practice exercises work with.
 *
 * Walking along the curve (in the direction of increasing sample index),
 * the strand is cut at every under-pass. The pieces in between are the
 * ARCS of the diagram: a diagram with c > 0 crossings has exactly c arcs,
 * a diagram without crossings has one (the whole loop).
 *
 * At every crossing three arc ends meet: the over arc, the arc that comes
 * in underneath and the arc that leaves underneath.
 */
struct DiagramArcs
{
    // Number of points of the diagram polyline.
    int pointCount = 0;

    // Positions (sample index + parameter) of the under-passes along the
    // curve, sorted. Arc k runs from underS[k] to underS[k + 1] (the last
    // one wraps around to underS[0]).
    QVector<double> underS;

    int arcCount = 1;

    // Per crossing, in the order of diagram.crossings.
    QVector<int> overArc;
    QVector<int> inArc;
    QVector<int> outArc;

    // Position of the over-pass and of the under-pass along the curve.
    QVector<double> overS;
    QVector<double> underPassS;

    // +1 (right-handed) or -1 (left-handed) for the orientation of
    // increasing sample index: +1 when the under strand comes from the
    // right as seen along the over strand.
    QVector<int> sign;
};

DiagramArcs diagramArcs(
    const KnotDiagram &diagram);

// The arc that contains the curve position s (sample index + parameter).
int arcAt(
    const DiagramArcs &arcs,
    double s);

// Tricolouring rule at one crossing: the three arcs have the same colour or
// three different colours. Colours are 0, 1, 2; -1 (not coloured yet)
// counts as fine.
bool crossingColoringOk(
    const DiagramArcs &arcs,
    int crossing,
    const QVector<int> &colors);

// Dimension of the space of all colourings of the arcs with the colours
// 0, 1, 2 (arithmetic mod 3) that keep the rule at every crossing. The
// three one-colour colourings always work, so this is at least 1; the
// knot is tricolourable exactly when it is 2 or more. It does not depend
// on the diagram (it is a knot invariant).
int coloringDimension(
    const DiagramArcs &arcs);

// A colouring with at least two colours that keeps the rule everywhere and
// agrees with `fixed` (colour per arc, -1 = free). Empty if there is none.
QVector<int> completeTricoloring(
    const DiagramArcs &arcs,
    const QVector<int> &fixed);

// Determinant of the knot: |det| of the colouring matrix (row per
// crossing: 2 for the over arc, -1 for each under arc) with one row and one
// column removed. Equals |Alexander polynomial(-1)|; the knot is
// tricolourable exactly when 3 divides it. -1 for more than 24 crossings.
long long diagramDeterminant(
    const DiagramArcs &arcs);

} // namespace KnotMath

#endif // KNOTPRACTICE_H
