#ifndef ROPEPHYSICS_H
#define ROPEPHYSICS_H

#include "knotmath.h"
#include "photoscene.h"

#include <QMatrix4x4>
#include <QVector>

/*
 * Photo mode: a real rope does not float. It lies on the table, pulled down
 * by its own weight: every part of it rests on the table or on another part
 * of the rope, it bends only as much as its stiffness lets gravity bend it,
 * and friction holds it where it came to rest.
 */
namespace Photo
{

struct RestingRope
{
    bool valid = false;
    QString error;

    // The centre line at rest, in the knot's own coordinates (the same
    // units, the same number of points for every call with this curve).
    QVector<KnotMath::Vec3> points;

    // The knot's coordinates -> the table: y up, the table top at y = 0.
    QMatrix4x4 pose;

    // simulated time (seconds), steps, and the fastest point at the end
    // (millimetres per second)
    double seconds = 0.0;
    int steps = 0;
    double finalSpeed = 0.0;

    // the rope came to rest (and was not stopped at the time limit)
    bool settled = false;
};

/*
 * Lets the knot (a closed or open centre line with tubes of `radius`) fall
 * onto a table and come to rest, as a rope of `material`. The knot is first
 * laid down flat (flatPose) just above the table, then simulated: an
 * inextensible elastic rod (bending stiffness from the material's bending
 * length) under gravity, with the table and every other part of the rope
 * as solid obstacles and Coulomb friction on both. The rope never passes
 * through itself, so the knot stays the same knot. Position based dynamics
 * with small steps (XPBD; Macklin et al. 2016, 2019).
 */
RestingRope layOnTable(
    const QVector<KnotMath::Vec3> &curve,
    bool closed,
    double radius,
    const RopeMaterial &material);

/*
 * The surface of the rope around a centre line, as it really is: three
 * strands twisted around each other with deep grooves between them (laid
 * rope), or yarns braided over and under each other (a lace). Texture
 * coordinates per vertex: across the strand or yarn on top (0..1), along
 * it (in radii), which strand or yarn, how deep in a groove (0..1).
 * Occlusion per vertex: how much of the room the point sees past the other
 * parts of the rope and the grooves (view factors).
 */
KnotMath::Mesh buildRopeMesh(
    const QVector<KnotMath::Vec3> &curve,
    bool closed,
    double radius,
    const RopeMaterial &material);

} // namespace Photo

#endif // ROPEPHYSICS_H
