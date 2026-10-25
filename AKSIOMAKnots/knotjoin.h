#ifndef KNOTJOIN_H
#define KNOTJOIN_H

#include "knotmath.h"

#include <QString>
#include <QVector>

#include <map>

namespace KnotMath
{

/*
 * The connected sum K1 # K2, built in space.
 *
 * Each knot is turned so that one of its arcs sticks out on the side facing
 * the other knot: beyond a plane there is only that arc, and along it the
 * distance from the plane rises once and falls once. Both arcs are cut out
 * and the four ends are joined by two strands (a band) that cross the gap
 * between the two planes, always moving from the first knot towards the
 * second.
 *
 * A plane between the knots then meets the new knot in exactly two points.
 * On each side, the arc that was cut out has been replaced by an arc with a
 * single highest point, and both are unknotted arcs of the same half-space
 * with the same ends - so closing each side up gives back K1 and K2. That is
 * the definition of K1 # K2 (with the orientations of the two curves; for
 * the knots up to 8 crossings the orientation does not matter).
 */
struct ConnectedSum
{
    bool valid = false;
    QString error;

    // How a knot is placed: turned by `yaw` about the y axis, then by `tilt`
    // about the z axis (radians), then shifted.
    struct Pose
    {
        double yaw = 0.0;
        double tilt = 0.0;
        Vec3 shift;
    };

    // The knots evenly resampled and centred at the origin, as given (the
    // second one mirrored if asked), and their poses.
    QVector<Vec3> firstBase;
    QVector<Vec3> secondBase;

    Pose firstPose;
    Pose secondPose;

    // the knots in their places
    QVector<Vec3> first;
    QVector<Vec3> second;

    // The arcs that are cut out: indices cutStart..cutEnd (cyclic, in the
    // direction of the curve); peak: the point that sticks out most.
    int firstCutStart = 0;
    int firstCutEnd = 0;
    int firstPeak = 0;

    int secondCutStart = 0;
    int secondCutEnd = 0;
    int secondPeak = 0;

    // bridgeOut runs from first[firstCutStart - 1] to
    // second[secondCutEnd + 1], bridgeBack from second[secondCutStart - 1]
    // to first[firstCutEnd + 1] (both with their ends).
    QVector<Vec3> bridgeOut;
    QVector<Vec3> bridgeBack;

    // The new knot: the rest of the first knot, bridgeOut, the rest of the
    // second knot, bridgeBack. Per point the part it comes from: 0 first
    // knot, 1 second knot, 2 bridge.
    QVector<Vec3> joined;
    QVector<int> joinedPart;
};

// The connected sum of two closed curves (in the program's coordinates,
// about 2.7 across). The strands of the band keep `tubeRadius` tubes apart.
// `mirrorSecond`: use the mirror image of the second knot.
ConnectedSum buildConnectedSum(
    const QVector<Vec3> &first,
    const QVector<Vec3> &second,
    double tubeRadius,
    bool mirrorSecond = false,
    int samples = 320);

// `curve` turned by (yaw, tilt), scaled by `scale` about the origin and
// shifted (the placing of ConnectedSum::Pose, also part of the way).
QVector<Vec3> placeCurve(
    const QVector<Vec3> &curve,
    double yaw,
    double tilt,
    const Vec3 &shift,
    double scale = 1.0);

// The closed curve without the points from..to (cyclic), as an open curve
// that starts right after `to` and ends right before `from`.
QVector<Vec3> curveWithout(
    const QVector<Vec3> &closedCurve,
    int from,
    int to);

// Product of two Laurent polynomials given by their coefficients from the
// lowest power up (the invariants of a connected sum multiply).
std::map<int, long long> multiplyPolynomials(
    const QVector<long long> &a,
    int lowestA,
    const QVector<long long> &b,
    int lowestB);

} // namespace KnotMath

#endif // KNOTJOIN_H
