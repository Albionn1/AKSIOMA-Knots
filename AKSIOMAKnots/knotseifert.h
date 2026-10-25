#ifndef KNOTSEIFERT_H
#define KNOTSEIFERT_H

#include "knotdiagram.h"

#include <QColor>
#include <QString>
#include <QVector>

#include <map>

namespace KnotMath
{

/*
 * =============================================================
 * SEIFERT SURFACES
 * =============================================================
 *
 * Every knot is the boundary of an orientable surface. Seifert's
 * algorithm builds one from a knot diagram:
 *
 *   1. Orient the knot and smooth every crossing the way the orientation
 *      allows: the diagram falls apart into disjoint loops, the SEIFERT
 *      CIRCLES.
 *   2. Fill every circle with a disk. Circles inside other circles get
 *      their disk higher up, so the disks do not meet.
 *   3. Join the two disks at every crossing with a HALF-TWISTED BAND
 *      (twisted to the right or to the left, as the crossing).
 *
 * The boundary of the result is the knot again. A diagram with c
 * crossings and s Seifert circles gives a surface of genus
 * g = (c - s + 1) / 2.
 *
 * Here the diagram is the cleanest projection of the curve. The surface is
 * built in "Seifert form": the disks are flat and the knot runs along
 * their edges and the edges of the bands.
 *
 * The SEIFERT MATRIX V (2g x 2g) is computed from the surface itself: for
 * a basis a_1 ... a_2g of loops on the surface (one loop through the
 * bands for every crossing that is not needed to connect the circles),
 * V_ij = lk(a_i, a_j+), the linking number of a_i with a_j pushed off to
 * the positive side of the surface. From V:
 *
 *   Alexander polynomial  det(V - t V^T)
 *   determinant           |det(V + V^T)|
 *   signature             (positive - negative eigenvalues of V + V^T)
 *
 * det(V - V^T) = +-1 for every knot; this is checked.
 */
struct SeifertSurface
{
    bool valid = false;
    QString error;

    int crossings = 0;
    int circles = 0;
    // genus of this surface, (crossings - circles + 1) / 2
    int genus = 0;

    // Size of the model (half the width of the diagram).
    double extent = 1.0;

    // Seifert circles: closed polygons at their heights (not repeating the
    // first point), whether they run counter-clockwise seen from +z (then
    // the positive side of their disk is +z), and how many other circles
    // they lie inside.
    QVector<QVector<Vec3>> circlePolygons;
    QVector<bool> circleCounterClockwise;
    QVector<int> circleDepth;

    // The knot as the boundary of the surface, and the same curve with all
    // circles at height 0 (the knot diagram).
    QVector<Vec3> boundary;
    QVector<Vec3> flatBoundary;

    // The band at every crossing: its two edges run along the knot.
    struct Band
    {
        int circleA = 0;
        int circleB = 0;
        // corners: a0 -> a1 on circle A, b0 -> b1 on circle B (b0 lies
        // across from a0)
        Vec3 a0, a1, b0, b1;
        double zA = 0.0;
        double zB = 0.0;
        // half the height difference of the two edges at the crossing;
        // positive: the a0 -> b0 edge is the upper one
        double twist = 0.0;
        // +1 right-handed, -1 left-handed crossing
        int sign = 1;
        // polygon index of the corner on each circle: the corner runs
        // from vertex cornerA to cornerA + 1
        int cornerA = 0;
        int cornerB = 0;
    };

    QVector<Band> bands;

    // Seifert matrix and what follows from it (only if matrixValid).
    bool matrixValid = false;
    QString matrixError;
    QVector<QVector<int>> seifertMatrix;
    int signature = 0;
    long long determinant = 0;
    // normalised like the program's other Alexander polynomials:
    // Delta(t) = Delta(1/t), Delta(1) = 1
    std::map<int, long long> alexander;
    QString alexanderText;

    // The loops a_1 ... a_2g behind the matrix (only if matrixValid): on
    // the surface, the positive normal of the surface at each of their
    // points, and pushed off the positive side along it, so that
    // V_ij = lk(loops[i], pushedLoops[j]).
    QVector<QVector<Vec3>> loops;
    QVector<QVector<Vec3>> loopNormals;
    QVector<QVector<Vec3>> pushedLoops;
};

// Two loops of the Seifert matrix shown on the finished surface (step 3):
// loop `loop` on the surface and loop `lifted` lifted off the positive
// side, so that the entry V[loop][lifted] can be seen; -1 shows none.
struct SeifertHighlight
{
    int loop = -1;
    int lifted = -1;
    QColor loopColor;
    QColor liftedColor;
};

// Point of a band: u from 0 (circle A) to 1 (circle B), v from 0 (the
// a0 -> b0 edge) to 1 (the a1 -> b1 edge).
Vec3 seifertBandPoint(
    const SeifertSurface::Band &band,
    double u,
    double v);

// The Seifert surface of a closed curve (from its cleanest projection).
// `computeMatrix`: also compute the Seifert matrix and its invariants.
SeifertSurface buildSeifertSurface(
    const QVector<Vec3> &curve,
    bool computeMatrix = true);

// Same, from a knot diagram of half-width `extent`.
SeifertSurface buildSeifertSurface(
    const KnotDiagram &diagram,
    double extent,
    bool computeMatrix = true);

/*
 * The 3D model, built up in steps:
 *
 *   0  the knot diagram (the knot lying flat)
 *   1  the Seifert circles (every crossing smoothed), stacked
 *   2  the circles filled with disks
 *   3  the bands added: the Seifert surface, with the knot as its edge
 *
 * The two sides of the surface have different colours (`front` is the
 * positive side). The model lies flat, like a map on the floor: the
 * diagram in the x-z plane, the levels of the circles going up (+y).
 *
 * At step 3, `highlight` adds two loops of the Seifert matrix as thin
 * tubes over the surface.
 */
Mesh seifertMesh(
    const SeifertSurface &surface,
    int step,
    double tubeRadius,
    const QColor &knotColor,
    const QColor &front,
    const QColor &back,
    const SeifertHighlight &highlight = SeifertHighlight());

// Loop `index` of the Seifert matrix lifted off the positive side as it is
// shown (farther than pushedLoops, so that it can be seen; in the model's
// own coordinates, before the model is laid flat).
QVector<Vec3> seifertLiftedLoop(
    const SeifertSurface &surface,
    int index);

// Tube radius that fits the bands of the surface (never more than
// `requestedRadius`).
double seifertTubeRadius(
    const SeifertSurface &surface,
    double requestedRadius);

} // namespace KnotMath

#endif // KNOTSEIFERT_H
