#ifndef KNOTRELAX_H
#define KNOTRELAX_H

#include "knotmath.h"

#include <aksioma/relaxation.h>

#include <QCoreApplication>
#include <QString>
#include <QVector>

namespace KnotMath
{

/*
 * A knot moved without ever letting a strand pass through another, so the
 * knot type never changes - only the shape does: the engine's curve
 * relaxation (aksioma/relaxation.h: relax, scramble, pull), with the knot's
 * own checks. Resampling the curve at the start must keep its Alexander
 * polynomial; the best view is the one with the fewest crossings of its
 * diagram; and what went wrong is said in the app's words.
 */
class KnotRelaxation : public Aksioma::Curves::Relaxation
{
    Q_DECLARE_TR_FUNCTIONS(KnotMath::KnotRelaxation)

public:
    // False if the curve cannot be used; the reason is in error().
    bool start(const QVector<Vec3> &curve);

    QString error() const;

    // Rotation (unit axis, angle) after which looking along z shows the
    // fewest crossings of the knot diagram; false if the current view is
    // already best.
    bool bestViewRotation(Vec3 &axis, double &angle) const;
};

} // namespace KnotMath

#endif // KNOTRELAX_H
