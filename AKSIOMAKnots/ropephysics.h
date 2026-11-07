#ifndef ROPEPHYSICS_H
#define ROPEPHYSICS_H

#include "knotmath.h"
#include "photoscene.h"

#include <aksioma/live.h>
#include <aksioma/rod.h>

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

// How far laying a rope down has come (0..100), and a way to stop it.
using RopeProgress = Aksioma::Physics::Progress;

struct RestingRope
{
    bool valid = false;
    QString error;

    // The centre line at rest, in the knot's own coordinates (the same
    // units, the same number of points for every call with this curve).
    QVector<KnotMath::Vec3> points;

    // at every point, a direction across the rope fixed in its fibres (see
    // RopeSimulation::frames), in the same coordinates
    QVector<KnotMath::Vec3> frames;

    // The knot's coordinates -> the table: y up, the table top at y = 0.
    QMatrix4x4 pose;

    // simulated time (seconds), steps, and the fastest point at the end
    // (millimetres per second)
    double seconds = 0.0;
    int steps = 0;
    double finalSpeed = 0.0;

    // the rope came to rest (and was not stopped at the time limit)
    bool settled = false;

    // stopped on request (RopeProgress): there is no rope
    bool cancelled = false;
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
    const RopeMaterial &material,
    RopeProgress *progress = nullptr);

// What the rope lies on, in units of its radius (y up); empty: a flat table.
using Ground = Aksioma::Physics::Ground;

using Settling = Aksioma::Physics::Settling;

/*
 * The simulation of layOnTable on its own: the rope `x` (points about half
 * a radius apart, in units of the radius, y up) falls onto `ground` and
 * comes to rest. Pinned points never move (a part of the rope held fast);
 * the free parts rest on them as on any other part of the rope.
 */
Settling settleRope(
    QVector<KnotMath::Vec3> &x,
    const QVector<bool> &pinned,
    bool closed,
    const RopeMaterial &material,
    const Ground &ground,
    QVector<KnotMath::Vec3> *frames = nullptr,
    RopeProgress *progress = nullptr);

/*
 * The same rope, one time step at a time, and a hand that can take hold of
 * it (Photo mode: the rope on the table picked up with the mouse): the
 * engine's rod (aksioma/rod.h), its grab the hand. Held high above the table,
 * the part in the hand passes over the other strands; pressed down onto the
 * table (grabbed beneath), it slides under them.
 */
using RopeSimulation = Aksioma::Physics::Rod;

/*
 * The rope in the hand, alive: a RopeSimulation stepping in real time on a
 * thread of its own (the engine's Aksioma::Live), so the rope moves at its
 * own pace whatever the frames cost (they draw the newest copy of it). Every
 * call may come from any thread. It rests once the rope lies still, or when
 * told to, until it is taken hold of again.
 */
class RopeInHand
{
public:
    explicit RopeInHand(const RopeSimulation &rope);

    // a copy of the rope as it is now (units of its radius), and a number
    // that changes with every step
    QVector<KnotMath::Vec3> points(
        quint64 *version = nullptr,
        QVector<KnotMath::Vec3> *frames = nullptr) const;

    bool closed() const;
    bool resting() const;

    // (see RopeSimulation::cut)
    void cut(int first, int count);

    void hold(int index, const KnotMath::Vec3 &target, bool under);
    void moveHand(const KnotMath::Vec3 &target);
    void letGo();
    int heldPoint() const;
    bool holdsUnder() const;

    // stop here (a rope that still creeps after a long while is taken as
    // it lies)
    void rest();

private:
    Aksioma::Live<RopeSimulation> live_;
};

/*
 * The surface of the rope around a centre line, as it really is: three
 * strands twisted around each other with deep grooves between them (laid
 * rope), or yarns braided over and under each other (a lace). Texture
 * coordinates per vertex: across the strand or yarn on top (0..1), along
 * it (in radii), which strand or yarn, how deep in a groove (0..1).
 * Occlusion per vertex: how much of the room the point sees past the other
 * parts of the rope and the grooves (view factors). `quick`: the grooves
 * only, for a rope that moves (in the hand) and is built for every frame.
 * `frames` (one per point of `curve`): the direction the rope's material
 * has turned to there, so that the strands roll and twist with the rope
 * instead of being laid over it anew for every shape.
 */
KnotMath::Mesh buildRopeMesh(
    const QVector<KnotMath::Vec3> &curve,
    bool closed,
    double radius,
    const RopeMaterial &material,
    bool quick = false,
    const QVector<KnotMath::Vec3> &frames = {});

} // namespace Photo

#endif // ROPEPHYSICS_H
