#include "knotrelax.h"

namespace KnotMath
{

namespace
{

bool sameAlexander(const QVector<Vec3> &a, const QVector<Vec3> &b)
{
    const AlexanderPolynomial pa = computeAlexanderPolynomial(a);
    const AlexanderPolynomial pb = computeAlexanderPolynomial(b);

    // too many crossings to compute: nothing to compare
    if (!pa.valid)
        return true;

    return pb.valid && pa.coefficients == pb.coefficients;
}

} // namespace

bool KnotRelaxation::start(const QVector<Vec3> &curve)
{
    return Relaxation::start(curve, sameAlexander);
}

QString KnotRelaxation::error() const
{
    switch (problem())
    {
    case Problem::TooFewPoints:
        return tr("The curve has too few points.");
    case Problem::StrandsTooClose:
        return tr("Strands of this curve come too close to each other to "
                  "move it safely.");
    default:
        return QString();
    }
}

bool KnotRelaxation::bestViewRotation(Vec3 &axis, double &angle) const
{
    return Relaxation::bestViewRotation(axis, angle, [](const QVector<Vec3> &curve)
                                        { return static_cast<int>(detectProjectionCrossings(curve, 2, 700).size()); });
}

} // namespace KnotMath
