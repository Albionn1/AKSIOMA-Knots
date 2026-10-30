#ifndef ROPEPHYSICS_H
#define ROPEPHYSICS_H

#include "knotmath.h"
#include "photoscene.h"

#include <QMatrix4x4>
#include <QVector>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

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
 * What the rope lies on, in units of its radius (y up): the height of the
 * surface over (x, z) and its slope there (dy/dx, dy/dz). Empty: a flat
 * table at y = 0.
 */
using Ground = std::function<double(double x, double z, double &slopeX, double &slopeZ)>;

struct Settling
{
    // simulated time (seconds), steps, the fastest point at the end
    // (millimetres per second), and whether it came to rest
    double seconds = 0.0;
    int steps = 0;
    double finalSpeed = 0.0;
    bool settled = false;
};

/*
 * The simulation of layOnTable on its own: the rope `x` (points about half
 * a radius apart, in units of the radius, y up) falls onto `ground` and
 * comes to rest. Pinned points never move (a part of the rope held fast,
 * such as a lace through the eyelets of a shoe); the free parts rest on
 * them as on any other part of the rope.
 */
Settling settleRope(
    QVector<KnotMath::Vec3> &x,
    const QVector<bool> &pinned,
    bool closed,
    const RopeMaterial &material,
    const Ground &ground);

/*
 * The same rope, one time step at a time, and a hand that can take hold
 * of it (Photo mode: the rope on the table picked up with the mouse).
 *
 * The hand pinches one point of the rope and carries it towards where the
 * hand is, no faster than the rope may move in a step (so it never
 * tunnels through another strand); the rest follows by its own physics.
 * Held high above the table, the part in the hand passes over the other
 * strands. Pressed down onto the table (`under`), the part in the hand
 * slides beneath them: the strands it meets near the hand are lifted over
 * it, as a rope end pushed under a loose loop lifts the loop. Either way
 * nothing passes through anything: the rope's knot changes only where it
 * has ends to go round.
 */
class RopeSimulation
{
public:
    RopeSimulation() = default;

    RopeSimulation(
        const QVector<KnotMath::Vec3> &x,
        const QVector<bool> &pinned,
        bool closed,
        const RopeMaterial &material,
        const Ground &ground = Ground());

    bool isValid() const { return valid_; }

    // the rope, in units of its radius (y up)
    const QVector<KnotMath::Vec3> &points() const { return x_; }
    bool closed() const { return closed_; }

    void step();

    // no point faster than the rest speed for a while
    bool atRest() const;

    // the fastest point after the last step (radii per time unit)
    double fastest() const { return fastest_; }

    // seconds of real time per step, for the material's size
    double secondsPerStep() const;

    // the hand (target in the rope's units)
    void hold(int index, const KnotMath::Vec3 &target, bool under);
    void moveHand(const KnotMath::Vec3 &target);
    void letGo();
    int heldPoint() const { return held_; }
    bool holdsUnder() const { return under_; }

private:
    bool valid_ = false;
    bool closed_ = true;

    QVector<KnotMath::Vec3> x_;
    QVector<KnotMath::Vec3> v_;
    QVector<KnotMath::Vec3> p_;
    QVector<double> w_;
    QVector<double> rest_;

    Ground ground_;

    int segments_ = 0;
    double h_ = 0.5;
    double radiusMm_ = 1.0;
    double compliance_ = 0.0;
    double muSurface_ = 0.5;
    double muSelf_ = 0.5;
    int neighbours_ = 8;

    // the grid of the contacts
    QVector<int> cellOf_;
    QVector<int> order_;
    QVector<int> cellStart_;
    QVector<bool> pinnedDown_;

    int quiet_ = 0;
    double fastest_ = 0.0;

    int held_ = -1;
    double heldWeight_ = 1.0;
    bool under_ = false;
    int handArc_ = 6;
    KnotMath::Vec3 hand_;
};

/*
 * The rope in the hand, alive: a RopeSimulation stepping in real time on a
 * thread of its own, so the rope moves at its own pace whatever the
 * frames cost (they draw the newest copy of it). Every call may come from
 * any thread. It rests (the thread sleeps) once the rope lies still, or
 * when told to, until it is taken hold of again.
 */
class RopeInHand
{
public:
    explicit RopeInHand(const RopeSimulation &rope);
    ~RopeInHand();

    RopeInHand(const RopeInHand &) = delete;
    RopeInHand &operator=(const RopeInHand &) = delete;

    // a copy of the rope as it is now (units of its radius), and a number
    // that changes with every step
    QVector<KnotMath::Vec3> points(quint64 *version = nullptr) const;

    bool closed() const;
    bool resting() const;

    void hold(int index, const KnotMath::Vec3 &target, bool under);
    void moveHand(const KnotMath::Vec3 &target);
    void letGo();
    int heldPoint() const;
    bool holdsUnder() const;

    // stop here (a rope that still creeps after a long while is taken as
    // it lies)
    void rest();

private:
    void loop();

    RopeSimulation rope_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;

    bool quit_ = false;
    bool stopped_ = false;
    quint64 version_ = 0;
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
 */
KnotMath::Mesh buildRopeMesh(
    const QVector<KnotMath::Vec3> &curve,
    bool closed,
    double radius,
    const RopeMaterial &material,
    bool quick = false);

} // namespace Photo

#endif // ROPEPHYSICS_H
