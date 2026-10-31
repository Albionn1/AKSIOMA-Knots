#include "ropephysics.h"

#include <QElapsedTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Photo
{

namespace
{

using KnotMath::Vec3;

constexpr double Pi = 3.14159265358979323846;

constexpr double Gravity = 9.81; // m/s^2

/*
 * The simulation works in units of the rope's radius (r = 1) and of the
 * time sqrt(r / g) (g = 1); the rope weighs 1 per unit of length.
 */

// points per radius of rope
constexpr double Spacing = 0.5;

// parts of the rope closer than this along it never collide (they are
// neighbours; tubes this close along the curve may overlap, as in
// KnotMath::safeTubeRadius)
constexpr double NeighbourArc = 4.0;

// time step, and the longest a rope may take to come to rest
constexpr double TimeStep = 0.04;
constexpr double LongestTime = 200.0;

// at rest: no point faster than this (radii per time unit) for RestSteps
constexpr double RestSpeed = 0.003;
constexpr int RestSteps = 150;

// constraint passes per step
constexpr int Passes = 3;

// internal damping of the fibres (per time unit)
constexpr double Damping = 0.8;

// no point moves more than this in one step (no tunnelling through the
// rope: two radii wide)
constexpr double MaxStep = 0.12;

// closest points of the segments a0-a1 and b0-b1: parameters s, t (0..1)
void closestPoints(
    const Vec3 &a0,
    const Vec3 &a1,
    const Vec3 &b0,
    const Vec3 &b1,
    double &s,
    double &t)
{
    const Vec3 d1 = a1 - a0;
    const Vec3 d2 = b1 - b0;
    const Vec3 r = a0 - b0;

    const double a = Vec3::dot(d1, d1);
    const double e = Vec3::dot(d2, d2);
    const double f = Vec3::dot(d2, r);
    const double c = Vec3::dot(d1, r);
    const double b = Vec3::dot(d1, d2);

    const double denominator = a * e - b * b;

    s = denominator > 1.0e-12 ? std::clamp((b * f - c * e) / denominator, 0.0, 1.0) : 0.0;
    t = (b * s + f) / e;

    if (t < 0.0)
    {
        t = 0.0;
        s = std::clamp(-c / a, 0.0, 1.0);
    }
    else if (t > 1.0)
    {
        t = 1.0;
        s = std::clamp((b - c) / a, 0.0, 1.0);
    }
}

// distance along the rope between points i and j (in points)
int arcGap(int i, int j, int n, bool closed)
{
    const int gap = std::abs(i - j);

    return closed ? std::min(gap, n - gap) : gap;
}

} // namespace

RestingRope layOnTable(
    const QVector<Vec3> &curve,
    bool closed,
    double radius,
    const RopeMaterial &material)
{
    RestingRope result;

    if (curve.size() < 4 || radius <= 0.0)
    {
        result.error = QStringLiteral("no curve");
        return result;
    }

    /*
     * Evenly spaced points along a smooth curve through the given ones,
     * half a radius apart.
     */
    const QVector<Vec3> smooth = KnotMath::refineCurve(curve, 4, closed);

    double length = 0.0;

    for (int i = 1; i < smooth.size(); ++i)
        length += (smooth.at(i) - smooth.at(i - 1)).length();

    if (closed)
        length += (smooth.first() - smooth.last()).length();

    const int n = std::clamp(
        static_cast<int>(std::lround(length / (Spacing * radius))),
        16,
        6000);

    const QVector<Vec3> start = KnotMath::resampleClosedCurve(smooth, n, closed);

    if (start.size() != n)
    {
        result.error = QStringLiteral("resampling failed");
        return result;
    }

    // laid down flat, the lowest point touching the table
    QVector<QVector3D> flat;

    flat.reserve(n);

    for (const Vec3 &p : start)
        flat.push_back(QVector3D(float(p.x), float(p.y), float(p.z)));

    QMatrix4x4 pose = flatPose(flat);

    double lowest = 1.0e30;

    for (const QVector3D &p : flat)
        lowest = std::min(lowest, double(pose.map(p).y()));

    QMatrix4x4 lift;

    lift.translate(0.0f, float(radius - lowest), 0.0f);

    pose = lift * pose;

    result.pose = pose;

    QVector<Vec3> x(n);

    for (int i = 0; i < n; ++i)
    {
        const QVector3D p = pose.map(flat.at(i));

        x[i] = Vec3(p.x(), p.y(), p.z()) / radius;
    }

    const Settling settling = settleRope(x, QVector<bool>(n, false), closed, material, Ground());

    result.steps = settling.steps;
    result.seconds = settling.seconds;
    result.finalSpeed = settling.finalSpeed;
    result.settled = settling.settled;

    // back to the knot's coordinates
    const QMatrix4x4 back = pose.inverted();

    result.points.reserve(n);

    for (const Vec3 &q : x)
    {
        const QVector3D k = back.map(QVector3D(
            float(q.x * radius),
            float(q.y * radius),
            float(q.z * radius)));

        result.points.push_back(Vec3(k.x(), k.y(), k.z()));
    }

    result.valid = true;

    return result;
}

Settling settleRope(
    QVector<Vec3> &x,
    const QVector<bool> &pinned,
    bool closed,
    const RopeMaterial &material,
    const Ground &ground)
{
    Settling result;

    RopeSimulation simulation(x, pinned, closed, material, ground);

    if (!simulation.isValid())
        return result;

    int step = 0;

    const int longest = static_cast<int>(LongestTime / TimeStep);

    for (step = 0; step < longest; ++step)
    {
        simulation.step();

        if (simulation.atRest())
            break;
    }

    x = simulation.points();

    const double radiusMm = 0.5 * material.diameterMm;
    const double timeUnit = std::sqrt(radiusMm * 0.001 / Gravity);

    result.steps = step;
    result.seconds = step * TimeStep * timeUnit;
    result.finalSpeed = simulation.fastest() * radiusMm / timeUnit;
    result.settled = simulation.atRest();

    return result;
}

RopeSimulation::RopeSimulation(
    const QVector<Vec3> &x,
    const QVector<bool> &pinned,
    bool closed,
    const RopeMaterial &material,
    const Ground &ground)
    : closed_(closed),
      x_(x),
      ground_(ground)
{
    const int n = x_.size();

    if (n < 4)
        return;

    // the true spacing along the rope (the resampling is close to even)
    segments_ = closed ? n : n - 1;

    rest_.resize(segments_);

    for (int i = 0; i < segments_; ++i)
        rest_[i] = (x_.at((i + 1) % n) - x_.at(i)).length();

    double h = 0.0;

    for (const double l : rest_)
        h += l;

    h_ = h / std::max(segments_, 1);

    /*
     * Each point moves in proportion to its inverse mass: 1 for a free
     * point (all free points weigh the same), 0 for a pinned one.
     */
    w_.resize(n);

    for (int i = 0; i < n; ++i)
        w_[i] = pinned.value(i, false) ? 0.0 : 1.0;

    /*
     * The material in these units. Bending stiffness B = w c^3 with w = 1:
     * B = c^3, c the bending length in radii. One point stands for a
     * length h of rope: mass h (inverse mass 1/h).
     */
    radiusMm_ = 0.5 * material.diameterMm;

    const double c = material.bendingLengthMm / radiusMm_;
    const double bending = c * c * c;

    const double dt = TimeStep;

    // XPBD: compliance of the bending constraint C = turning angle at a
    // point (curvature theta / h, energy B h kappa^2 / 2 = B theta^2 / 2h)
    compliance_ = h_ / bending / (dt * dt);

    muSurface_ = material.frictionSurface;
    muSelf_ = material.frictionSelf;

    neighbours_ = static_cast<int>(std::ceil(NeighbourArc / h_));

    // the part of the rope next to the hand (three radii either way)
    handArc_ = static_cast<int>(std::ceil(3.0 / h_));

    v_.resize(n);
    p_.resize(n);

    cellOf_.resize(segments_);
    order_.resize(segments_);

    valid_ = true;
}

bool RopeSimulation::atRest() const
{
    return quiet_ >= RestSteps;
}

double RopeSimulation::secondsPerStep() const
{
    return TimeStep * std::sqrt(radiusMm_ * 0.001 / Gravity);
}

void RopeSimulation::hold(int index, const Vec3 &target, bool under)
{
    if (!valid_ || index < 0 || index >= x_.size())
        return;

    letGo();

    held_ = index;
    heldWeight_ = w_.at(index);
    under_ = under;
    hand_ = target;

    // the hand moves it; nothing else does
    w_[index] = 0.0;

    quiet_ = 0;
}

void RopeSimulation::moveHand(const Vec3 &target)
{
    hand_ = target;
    quiet_ = 0;
}

void RopeSimulation::letGo()
{
    if (held_ >= 0 && held_ < w_.size())
        w_[held_] = heldWeight_;

    held_ = -1;
    under_ = false;
    quiet_ = 0;
}

void RopeSimulation::step()
{
    if (!valid_)
        return;

    const int n = x_.size();
    const int segments = segments_;
    const bool closed = closed_;
    const double h = h_;
    const double dt = TimeStep;

    QVector<Vec3> &x = x_;
    QVector<Vec3> &v = v_;
    QVector<Vec3> &p = p_;
    const QVector<double> &w = w_;
    const QVector<double> &rest = rest_;

    // the arrays through plain pointers (no checks on every access)
    Vec3 *X = x.data();
    Vec3 *V = v.data();
    Vec3 *P = p.data();
    const double *W = w.constData();
    const double *Rest = rest.constData();

    // segments sorted into columns of a grid on the table (x, z), each a
    // little wider than the reach of a contact
    constexpr double Cell = 2.6;

    // the hand carries its point towards it, a safe distance per step;
    // up and down it moves gently, as a hand lifts a rope (about half a
    // second to the full height), so the rope never jumps
    constexpr double HandStep = 0.1;
    constexpr double HandRise = 0.012;

    // gravity, damping, predicted positions
    const double keep = std::exp(-Damping * dt);

    for (int i = 0; i < n; ++i)
    {
        if (i == held_)
        {
            Vec3 move = hand_ - X[i];

            move.y = std::clamp(move.y, -HandRise, HandRise);

            const double distance = move.length();

            if (distance > HandStep)
                move *= HandStep / distance;

            P[i] = X[i] + move;
            continue;
        }

        if (W[i] == 0.0)
        {
            P[i] = X[i];
            continue;
        }

        V[i].y -= dt;
        V[i] *= keep;

        const double speed = V[i].length();

        if (speed * dt > MaxStep)
            V[i] *= MaxStep / (speed * dt);

        P[i] = X[i] + V[i] * dt;
    }

    for (int pass = 0; pass < Passes; ++pass)
    {
        // the rope does not stretch
        for (int i = 0; i < segments; ++i)
        {
            const int j = (i + 1) % n;

            const double weights = W[i] + W[j];

            if (weights == 0.0)
                continue;

            const Vec3 d = P[j] - P[i];
            const double l = d.length();

            if (l < 1.0e-9)
                continue;

            const Vec3 move = d * ((l - Rest[i]) / l / weights);

            P[i] += move * W[i];
            P[j] -= move * W[j];
        }

        /*
         * It resists bending: the turning angle theta between the two
         * pieces at a point, with the gradients
         *     d theta / d a = n1 / |e1|,  d theta / d b = -n2 / |e2|,
         *     d theta / d i = -(d theta / d a + d theta / d b),
         * e1 = i - a, e2 = b - i, n1 and n2 the unit directions at
         * right angles to them in their plane, towards each other.
         */
        for (int i = closed ? 0 : 1; i < (closed ? n : n - 1); ++i)
        {
            const int a = (i - 1 + n) % n;
            const int b = (i + 1) % n;

            if (W[a] + W[b] + W[i] == 0.0)
                continue;

            const Vec3 e1 = P[i] - P[a];
            const Vec3 e2 = P[b] - P[i];

            const double l1 = e1.length();
            const double l2 = e2.length();

            if (l1 < 1.0e-9 || l2 < 1.0e-9)
                continue;

            const Vec3 u1 = e1 / l1;
            const Vec3 u2 = e2 / l2;

            const double cosine = std::clamp(Vec3::dot(u1, u2), -1.0, 1.0);
            const double sine = Vec3::cross(u1, u2).length();
            const double theta = std::atan2(sine, cosine);

            if (sine < 1.0e-6)
                continue;

            const Vec3 n1 = (u2 - u1 * cosine) / sine;
            const Vec3 n2 = (u1 - u2 * cosine) / sine;

            const Vec3 ga = n1 / l1;
            const Vec3 gb = n2 * (-1.0 / l2);
            const Vec3 gi = (ga + gb) * -1.0;

            const double ma = W[a] / h;
            const double mb = W[b] / h;
            const double mi = W[i] / h;

            const double denominator =
                ma * ga.lengthSquared() + mb * gb.lengthSquared() + mi * gi.lengthSquared() +
                compliance_;

            const double dl = -theta / denominator;

            P[a] += ga * (ma * dl);
            P[b] += gb * (mb * dl);
            P[i] += gi * (mi * dl);
        }
    }

    /*
     * In contacts and on the ground the point in the hand gives way like
     * any other: the hand carries it, but cannot force it through another
     * strand or into the table (a strand trapped between the two would
     * otherwise be squeezed through).
     */
    QVector<double> solid = w;

    if (held_ >= 0)
        solid[held_] = 1.0;

    const double *Solid = solid.constData();

    /*
     * Contacts of the rope with itself: two segments far apart along
     * the rope stay two radii apart (centre lines). The push is shared
     * by their four points by where the closest points lie (a pinned
     * point takes none of it); friction takes away the sliding of the
     * two parts against each other.
     */
    double minX = 1.0e30;
    double minZ = 1.0e30;
    double maxX = -1.0e30;
    double maxZ = -1.0e30;

    for (const Vec3 &q : p)
    {
        minX = std::min(minX, q.x);
        minZ = std::min(minZ, q.z);
        maxX = std::max(maxX, q.x);
        maxZ = std::max(maxZ, q.z);
    }

    const int columns = std::max(1, static_cast<int>((maxX - minX) / Cell) + 1);
    const int rows = std::max(1, static_cast<int>((maxZ - minZ) / Cell) + 1);

    QVector<int> &cellOf = cellOf_;
    QVector<int> &order = order_;
    QVector<int> &cellStart = cellStart_;

    cellStart.fill(0, columns * rows + 1);

    for (int i = 0; i < segments; ++i)
    {
        const Vec3 middle = (P[i] + P[(i + 1) % n]) * 0.5;

        const int cx = std::clamp(static_cast<int>((middle.x - minX) / Cell), 0, columns - 1);
        const int cz = std::clamp(static_cast<int>((middle.z - minZ) / Cell), 0, rows - 1);

        cellOf[i] = cz * columns + cx;
        ++cellStart[cellOf.at(i) + 1];
    }

    for (int k = 0; k < columns * rows; ++k)
        cellStart[k + 1] += cellStart.at(k);

    {
        QVector<int> fill = cellStart;

        for (int i = 0; i < segments; ++i)
            order[fill[cellOf.at(i)]++] = i;
    }

    // a segment next to the hand, when it pushes the rope's end under
    auto nearHand = [this, n, closed](int segment)
    {
        return held_ >= 0 && under_ && arcGap(segment, held_, n, closed) <= handArc_;
    };

    /*
     * Parts that may not be lifted over the hand's part: those lying
     * under it anywhere (they would go through it), and the rope for a
     * few radii either side of them (a lifted point pulls its neighbours
     * up with it).
     */
    QVector<bool> &pinnedDown = pinnedDown_;

    pinnedDown.fill(false, n);

    if (held_ >= 0 && under_)
    {
        QVector<bool> under(n, false);

        for (int j = 0; j < n; ++j)
        {
            if (arcGap(j, held_, n, closed) <= handArc_ + neighbours_)
                continue;

            for (int z = -handArc_; z <= handArc_ + 1; ++z)
            {
                const int k = closed ? (held_ + z + n) % n : held_ + z;

                if (k < 0 || k >= n)
                    continue;

                const double dx = P[k].x - P[j].x;
                const double dz = P[k].z - P[j].z;

                if (dx * dx + dz * dz < 2.5 * 2.5 && P[k].y > P[j].y + 0.3)
                {
                    under[j] = true;
                    break;
                }
            }
        }

        const int margin = static_cast<int>(std::ceil(4.0 / h));

        for (int j = 0; j < n; ++j)
        {
            if (!under.at(j))
                continue;

            for (int k = -margin; k <= margin; ++k)
            {
                const int m = closed ? (j + k + n) % n : j + k;

                if (m >= 0 && m < n)
                    pinnedDown[m] = true;
            }
        }
    }

    for (int i = 0; i < segments; ++i)
    {
        const int cx = cellOf.at(i) % columns;
        const int cz = cellOf.at(i) / columns;

        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int ox = cx + dx;
                const int oz = cz + dz;

                if (ox < 0 || oz < 0 || ox >= columns || oz >= rows)
                    continue;

                const int cell = oz * columns + ox;

                for (int k = cellStart.at(cell); k < cellStart.at(cell + 1); ++k)
                {
                    const int j = order.at(k);

                    if (j <= i || arcGap(i, j, n, closed) <= neighbours_)
                        continue;

                    const int i1 = (i + 1) % n;
                    const int j1 = (j + 1) % n;

                    // two pinned parts never move
                    if (Solid[i] + Solid[i1] + Solid[j] + Solid[j1] == 0.0)
                        continue;

                    // far apart for sure (their middles, less the pieces'
                    // own half lengths, beyond the two radii of a contact)
                    {
                        const Vec3 gap = (P[i] + P[i1] - P[j] - P[j1]) * 0.5;
                        const double reach =
                            2.0 + 0.5 * ((P[i1] - P[i]).length() + (P[j1] - P[j]).length());

                        if (gap.lengthSquared() > reach * reach)
                            continue;
                    }

                    double s = 0.0;
                    double t = 0.0;

                    closestPoints(P[i], P[i1], P[j], P[j1], s, t);

                    const Vec3 pa = P[i] * (1.0 - s) + P[i1] * s;
                    const Vec3 pb = P[j] * (1.0 - t) + P[j1] * t;

                    const Vec3 d = pa - pb;
                    const double distance = d.length();

                    if (distance >= 2.0)
                        continue;

                    Vec3 normal =
                        distance > 1.0e-9 ? d / distance : Vec3(0.0, 1.0, 0.0);

                    double depth = 2.0 - distance;

                    /*
                     * The hand pushes its part under another: that one is
                     * lifted straight up over it (by as much as the two
                     * need to be apart, at their distance across). Only
                     * a part beside it or above it, and none near a part
                     * lying under it (pinnedDown): those would have to go
                     * through it, so their contacts are ordinary ones.
                     */
                    const bool handA = nearHand(i);
                    const bool handB = nearHand(j);

                    if (handA != handB)
                    {
                        const Vec3 up(0.0, handA ? -1.0 : 1.0, 0.0);

                        // how far the other part is above the hand's part
                        const double rise = Vec3::dot(d, up);
                        const double across2 = std::max(d.lengthSquared() - rise * rise, 0.0);

                        const int other = handA ? j : i;
                        const int otherEnd = handA ? j1 : i1;

                        if (rise >= -0.3 && !pinnedDown.at(other) && !pinnedDown.at(otherEnd))
                        {
                            normal = up;
                            depth = std::sqrt(std::max(4.0 - across2, 0.0)) - rise;

                            if (depth <= 0.0)
                                continue;
                        }
                    }

                    const double wa0 = 1.0 - s;
                    const double wa1 = s;
                    const double wb0 = 1.0 - t;
                    const double wb1 = t;

                    const double share =
                        Solid[i] * wa0 * wa0 + Solid[i1] * wa1 * wa1 +
                        Solid[j] * wb0 * wb0 + Solid[j1] * wb1 * wb1;

                    if (share < 1.0e-12)
                        continue;

                    const Vec3 push = normal * (depth / share);

                    P[i] += push * (wa0 * Solid[i]);
                    P[i1] += push * (wa1 * Solid[i1]);
                    P[j] -= push * (wb0 * Solid[j]);
                    P[j1] -= push * (wb1 * Solid[j1]);

                    // sliding of the two parts against each other in this step
                    const Vec3 moveA =
                        (P[i] - X[i]) * wa0 + (P[i1] - X[i1]) * wa1;
                    const Vec3 moveB =
                        (P[j] - X[j]) * wb0 + (P[j1] - X[j1]) * wb1;

                    Vec3 slide = moveA - moveB;
                    slide -= normal * Vec3::dot(slide, normal);

                    const double amount = slide.length();

                    if (amount < 1.0e-12)
                        continue;

                    // static friction holds, kinetic friction slows
                    const double stop =
                        amount < muSelf_ * depth ? 1.0 : std::min(muSelf_ * depth / amount, 1.0);

                    const Vec3 hold = slide * (stop / share);

                    P[i] -= hold * (wa0 * Solid[i]);
                    P[i1] -= hold * (wa1 * Solid[i1]);
                    P[j] += hold * (wb0 * Solid[j]);
                    P[j1] += hold * (wb1 * Solid[j1]);
                }
            }
        }
    }

    /*
     * The ground (with friction): a point below it goes back out along
     * the ground's normal, by its depth below the sloping surface; its
     * sliding along the surface in this step is held or slowed.
     */
    for (int i = 0; i < n; ++i)
    {
        if (Solid[i] == 0.0)
            continue;

        double slopeX = 0.0;
        double slopeZ = 0.0;

        const double top = ground_ ? ground_(P[i].x, P[i].z, slopeX, slopeZ) : 0.0;

        if (P[i].y >= top + 1.0)
            continue;

        const Vec3 normal = Vec3(-slopeX, 1.0, -slopeZ).normalized();
        const double depth = (top + 1.0 - P[i].y) * normal.y;

        P[i] += normal * depth;

        Vec3 slide = P[i] - X[i];
        slide -= normal * Vec3::dot(slide, normal);

        const double amount = slide.length();

        if (amount < muSurface_ * depth)
            P[i] -= slide;
        else if (amount > 1.0e-12)
            P[i] -= slide * std::min(muSurface_ * depth / amount, 1.0);
    }

    // velocities from the motion
    fastest_ = 0.0;

    for (int i = 0; i < n; ++i)
    {
        V[i] = (P[i] - X[i]) / dt;
        X[i] = P[i];

        if (W[i] != 0.0)
            fastest_ = std::max(fastest_, V[i].length());
    }

    // (a rope in the hand is never at rest)
    quiet_ = fastest_ < RestSpeed && held_ < 0 ? quiet_ + 1 : 0;
}

RopeInHand::RopeInHand(const RopeSimulation &rope)
    : rope_(rope)
{
    thread_ = std::thread([this]() { loop(); });
}

RopeInHand::~RopeInHand()
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }

    wake_.notify_all();

    thread_.join();
}

QVector<Vec3> RopeInHand::points(quint64 *version) const
{
    const std::lock_guard<std::mutex> lock(mutex_);

    if (version != nullptr)
        *version = version_;

    return rope_.points();
}

bool RopeInHand::closed() const
{
    const std::lock_guard<std::mutex> lock(mutex_);

    return rope_.closed();
}

bool RopeInHand::resting() const
{
    const std::lock_guard<std::mutex> lock(mutex_);

    return stopped_ || (rope_.atRest() && rope_.heldPoint() < 0);
}

void RopeInHand::hold(int index, const Vec3 &target, bool under)
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);

        rope_.hold(index, target, under);
        stopped_ = false;
    }

    wake_.notify_all();
}

void RopeInHand::moveHand(const Vec3 &target)
{
    const std::lock_guard<std::mutex> lock(mutex_);

    rope_.moveHand(target);
}

void RopeInHand::letGo()
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);

        rope_.letGo();
        stopped_ = false;
    }

    wake_.notify_all();
}

int RopeInHand::heldPoint() const
{
    const std::lock_guard<std::mutex> lock(mutex_);

    return rope_.heldPoint();
}

bool RopeInHand::holdsUnder() const
{
    const std::lock_guard<std::mutex> lock(mutex_);

    return rope_.holdsUnder();
}

void RopeInHand::rest()
{
    const std::lock_guard<std::mutex> lock(mutex_);

    stopped_ = true;
}

void RopeInHand::loop()
{
    // as many steps as real time has passed (a slow processor lets the
    // rope move more slowly rather than in jumps)
    constexpr int MostSteps = 40;

    const double each = rope_.secondsPerStep();

    QElapsedTimer clock;
    clock.start();

    double lag = 0.0;

    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            if (quit_)
                return;

            // nothing moves: sleep until the rope is taken hold of
            if (stopped_ || (rope_.atRest() && rope_.heldPoint() < 0))
            {
                wake_.wait(lock, [this]()
                {
                    return quit_ || (!stopped_ && !(rope_.atRest() && rope_.heldPoint() < 0));
                });

                clock.restart();
                lag = 0.0;

                continue;
            }
        }

        lag += static_cast<double>(clock.nsecsElapsed()) / 1.0e9;
        clock.restart();

        int steps = static_cast<int>(lag / each);

        if (steps > MostSteps)
        {
            steps = MostSteps;
            lag = 0.0;
        }
        else
        {
            lag -= steps * each;
        }

        // one step at a time, so the hand and the frames get in between
        for (int k = 0; k < steps; ++k)
        {
            const std::lock_guard<std::mutex> lock(mutex_);

            if (quit_ || stopped_)
                break;

            rope_.step();
            ++version_;
        }

        if (steps == 0)
            std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

/*
 * =========================================================
 * THE ROPE'S SURFACE
 * =========================================================
 */

namespace
{

// three-strand rope: a strand is a circle of radius StrandA (in rope
// radii) whose middle is StrandC from the rope's axis; neighbours press
// into each other, so the grooves are narrow and about a third deep
constexpr double StrandA = 0.6;
constexpr double StrandC = 0.4;

// one turn of a strand around the rope every 3.2 diameters (lay length)
constexpr double LayLength = 6.4;

// braided lace: 8 yarns each way, at 50 degrees to the lace; the yarns
// stand out this much (of the radius)
constexpr int BraidYarns = 8;
constexpr double BraidAngle = 50.0 * Pi / 180.0;
constexpr double YarnHeight = 0.18;

double wrapAngle(double a)
{
    a = std::fmod(a + Pi, 2.0 * Pi);

    if (a < 0.0)
        a += 2.0 * Pi;

    return a - Pi;
}

// distance from the axis of the three-strand cross section at the angle
// delta from a strand's middle
double strandProfile(double delta)
{
    const double sine = StrandC * std::sin(delta);

    return StrandC * std::cos(delta) + std::sqrt(std::max(StrandA * StrandA - sine * sine, 0.0));
}

/*
 * How much of the room a point in a groove sees (cosine weighted): in the
 * cross section the neighbouring strand rises above the point's tangent;
 * with the angles hL, hR of the highest edge on both sides the seen part
 * is (cos hL + cos hR) / 2. Tabulated over the angle across a strand.
 */
double grooveVisibility(double delta)
{
    constexpr int Samples = 720;

    auto point = [](double angle)
    {
        double best = 0.0;

        for (int k = 0; k < 3; ++k)
            best = std::max(best, strandProfile(wrapAngle(angle - 2.0 * Pi * k / 3.0)));

        return std::pair<double, double>(best * std::cos(angle), best * std::sin(angle));
    };

    const auto [px, py] = point(delta);

    const auto [ax, ay] = point(delta - 1.0e-3);
    const auto [bx, by] = point(delta + 1.0e-3);

    // outward normal of the profile at the point
    double tx = bx - ax;
    double ty = by - ay;
    const double tl = std::hypot(tx, ty);

    tx /= tl;
    ty /= tl;

    const double nx = ty;
    const double ny = -tx;

    double highestLeft = 0.0;
    double highestRight = 0.0;

    for (int k = 1; k < Samples; ++k)
    {
        const double angle = delta + 2.0 * Pi * k / Samples;
        const auto [qx, qy] = point(angle);

        const double dx = qx - px;
        const double dy = qy - py;
        const double dl = std::hypot(dx, dy);

        if (dl < 1.0e-6)
            continue;

        // elevation above the tangent
        const double up = (dx * nx + dy * ny) / dl;

        if (up <= 0.0)
            continue;

        const double elevation = std::asin(std::min(up, 1.0));

        // which side of the point it lies on
        if (dx * tx + dy * ty >= 0.0)
            highestRight = std::max(highestRight, elevation);
        else
            highestLeft = std::max(highestLeft, elevation);
    }

    return 0.5 * (std::cos(highestLeft) + std::cos(highestRight));
}

/*
 * Helpers on every core of the processor, started once and kept waiting:
 * the rope's surface is built for every frame while it is in the hand,
 * and starting threads anew each time would cost more than the work.
 */
class Workers
{
public:
    static Workers &instance()
    {
        static Workers workers;
        return workers;
    }

    // work(first, last) over 0 .. count - 1, in one piece per thread; the
    // calling thread does the first piece
    void run(int count, const std::function<void(int, int)> &work)
    {
        const std::lock_guard<std::mutex> one(oneJob_);

        const int threads = static_cast<int>(threads_.size()) + 1;

        if (threads == 1 || count < 64)
        {
            work(0, count);
            return;
        }

        {
            const std::lock_guard<std::mutex> lock(mutex_);

            job_ = &work;
            count_ = count;
            piece_ = (count + threads - 1) / threads;
            pending_ = static_cast<int>(threads_.size());
            ++generation_;
        }

        start_.notify_all();

        work(0, std::min(count, piece_));

        std::unique_lock<std::mutex> lock(mutex_);

        done_.wait(lock, [this]() { return pending_ == 0; });

        job_ = nullptr;
    }

private:
    Workers()
    {
        const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);

        for (int t = 1; t < threads; ++t)
            threads_.emplace_back([this, t]() { loop(t); });
    }

    ~Workers()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }

        start_.notify_all();

        for (std::thread &thread : threads_)
            thread.join();
    }

    void loop(int index)
    {
        int seen = 0;

        for (;;)
        {
            const std::function<void(int, int)> *job = nullptr;
            int first = 0;
            int last = 0;

            {
                std::unique_lock<std::mutex> lock(mutex_);

                start_.wait(lock, [this, seen]() { return quit_ || generation_ != seen; });

                if (quit_)
                    return;

                seen = generation_;
                job = job_;
                first = index * piece_;
                last = std::min(count_, first + piece_);
            }

            if (first < last)
                (*job)(first, last);

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                --pending_;
            }

            done_.notify_one();
        }
    }

    std::vector<std::thread> threads_;

    std::mutex oneJob_;
    std::mutex mutex_;
    std::condition_variable start_;
    std::condition_variable done_;

    const std::function<void(int, int)> *job_ = nullptr;
    int count_ = 0;
    int piece_ = 0;
    int pending_ = 0;
    int generation_ = 0;
    bool quit_ = false;
};

template <typename Work>
void inParallel(int count, Work &&work)
{
    const std::function<void(int, int)> job = work;

    Workers::instance().run(count, job);
}

/*
 * The cross section of a three-strand rope at every angle around it,
 * measured from the middle of strand 0 (it turns along the rope with the
 * lay): the distance from the axis, the place across the strand on top,
 * which strand, how deep in a groove, and how much of the room the point
 * sees past the neighbouring strand. Worked out once.
 */
struct StrandSample
{
    float rho;
    float across;
    float which;
    float groove;
    float open;
};

constexpr int StrandSamples = 4096;

const std::vector<StrandSample> &strandTable()
{
    static const std::vector<StrandSample> table = []()
    {
        constexpr int GrooveTable = 64;

        double grooveTable[GrooveTable + 1];

        for (int k = 0; k <= GrooveTable; ++k)
            grooveTable[k] = grooveVisibility((k / double(GrooveTable) - 0.5) * 2.0 * Pi / 3.0);

        const double deepest = strandProfile(Pi / 3.0);

        std::vector<StrandSample> samples(StrandSamples);

        for (int k = 0; k < StrandSamples; ++k)
        {
            const double x = 2.0 * Pi * k / StrandSamples;

            // the strand standing out most at this angle
            int strand = 0;
            double best = -1.0;
            double delta = 0.0;

            for (int t = 0; t < 3; ++t)
            {
                const double d = wrapAngle(x - 2.0 * Pi * t / 3.0);
                const double r = strandProfile(d);

                if (r > best)
                {
                    best = r;
                    strand = t;
                    delta = d;
                }
            }

            const double u = std::clamp(0.5 + delta / (2.0 * Pi / 3.0), 0.0, 1.0) * GrooveTable;
            const int g = std::min(static_cast<int>(u), GrooveTable - 1);

            samples[k].rho = float(best);
            samples[k].across = float(0.5 + delta / (2.0 * Pi / 3.0));
            samples[k].which = float(strand);
            samples[k].groove = float((1.0 - best) / (1.0 - deepest));
            samples[k].open = float(grooveTable[g] + (grooveTable[g + 1] - grooveTable[g]) * (u - g));
        }

        return samples;
    }();

    return table;
}

/*
 * Loose fibres. A natural rope is hairy: the ends of short fibres stick
 * out of the yarns, most of them lying close along the strand and lifting
 * off a little, some curling away. They are thin tubes (three sides,
 * tapering) rooted on the tops of the strands; texture coordinates mark
 * them (w = -1, x = from root to tip). A braided cotton lace has far fewer
 * and shorter ones. The same rope always gets the same fibres.
 */
void addLooseFibres(KnotMath::Mesh &mesh, bool closed, double radius, bool braided, bool quick)
{
    const int n = mesh.ringCount;
    const int segments = mesh.ringSegments;

    if (n < 4 || segments < 3)
        return;

    const int vertices = mesh.positions.size() / 3;

    const bool colours = mesh.colors.size() == 3 * vertices;
    const bool samples = mesh.vertexSample.size() == vertices;

    const double length = mesh.centerlineArc.isEmpty()
        ? 0.0
        : mesh.strandLength.value(0, mesh.centerlineArc.last());

    // fibres per radius of rope, their length and thickness (in radii)
    // (a moving rope: half of them, the same ones)
    const double density = (braided ? 9.0 : 16.0) * (quick ? 0.4 : 1.0);
    const double shortest = braided ? 0.1 : 0.16;
    const double longest = braided ? 0.5 : 1.3;
    const double thickness = braided ? 0.009 : 0.012;

    const int count = std::clamp(static_cast<int>(density * length / radius), 0, 40000);

    constexpr int Pieces = 5;
    constexpr int Sides = 3;

    constexpr int FibreVertices = (Pieces + 1) * Sides;
    constexpr int FibreIndices = Pieces * Sides * 6;

    // room for all of them at once; each fibre fills its own part
    const int firstIndex = mesh.indices.size();

    mesh.positions.resize(3 * (vertices + count * FibreVertices));
    mesh.normals.resize(3 * (vertices + count * FibreVertices));
    mesh.texcoords.resize(4 * (vertices + count * FibreVertices));
    mesh.occlusion.resize(vertices + count * FibreVertices);
    mesh.indices.resize(firstIndex + count * FibreIndices);

    if (colours)
        mesh.colors.resize(3 * (vertices + count * FibreVertices), 1.0f);

    if (samples)
        mesh.vertexSample.resize(vertices + count * FibreVertices);

    float *positions = mesh.positions.data();
    float *normals = mesh.normals.data();
    float *texcoords = mesh.texcoords.data();
    float *occlusionData = mesh.occlusion.data();
    unsigned int *indices = mesh.indices.data();
    int *sampleData = samples ? mesh.vertexSample.data() : nullptr;

    const float *centerline = mesh.centerline.constData();

    auto centreAt = [centerline](int ring)
    {
        return Vec3(centerline[4 * ring], centerline[4 * ring + 1], centerline[4 * ring + 2]);
    };

    inParallel(count, [&](int firstFibre, int lastFibre)
    {
        for (int f = firstFibre; f < lastFibre; ++f)
        {
            // random numbers of this fibre's own (xorshift from its number):
            // the same rope always gets the same fibres
            quint32 state = 0x9e3779b9u ^ (static_cast<quint32>(f) * 0x85ebca6bu);

            auto random = [&state]()
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;

                return (state & 0xffffff) / double(0x1000000);
            };

            random();
            random();

            const int ring = std::min(static_cast<int>(random() * n), n - 1);

            // on top of a strand or yarn, never down in a groove
            int segment = std::min(static_cast<int>(random() * segments), segments - 1);

            for (int attempt = 0; attempt < 6; ++attempt)
            {
                if (texcoords[4 * (ring * segments + segment) + 3] < 0.35f)
                    break;

                segment = std::min(static_cast<int>(random() * segments), segments - 1);
            }

            const int root = ring * segments + segment;

            const Vec3 base(positions[3 * root], positions[3 * root + 1], positions[3 * root + 2]);
            const Vec3 normal = Vec3(normals[3 * root], normals[3 * root + 1], normals[3 * root + 2]).normalized();

            const int before = closed ? (ring - 1 + n) % n : std::max(ring - 1, 0);
            const int after = closed ? (ring + 1) % n : std::min(ring + 1, n - 1);

            Vec3 along = centreAt(after) - centreAt(before);

            along = (along - normal * Vec3::dot(along, normal)).normalized();

            const Vec3 side = Vec3::cross(normal, along);

            // either way along the rope, turned a little off it, lifting off
            const double turn = (random() - 0.5) * (braided ? 1.6 : 1.0);
            const double lift = 0.12 + 0.55 * random() * random();
            const double way = random() < 0.5 ? -1.0 : 1.0;

            Vec3 direction =
                (along * (way * std::cos(turn)) + side * std::sin(turn)) * std::cos(lift) +
                normal * std::sin(lift);

            // most fibres short, a few long (the longest fibre ends of hemp)
            const double u = random();
            const double fibre = (shortest + (longest - shortest) * u * u * u) * radius;

            // it curls: turns a little at every piece, about the normal and away
            const double curl = (random() - 0.5) * 0.9;
            const double droop = (random() - 0.3) * 0.5;

            const int first = vertices + f * FibreVertices;
            const float occlusion = occlusionData[root];
            const float id = float(random());

            Vec3 point = base - normal * (0.05 * radius);

            for (int p = 0; p <= Pieces; ++p)
            {
                const double t = double(p) / Pieces;

                // a frame around the fibre
                const Vec3 helper = std::abs(Vec3::dot(direction, normal)) < 0.9 ? normal : side;
                const Vec3 a = Vec3::cross(direction, helper).normalized();
                const Vec3 b = Vec3::cross(direction, a);

                const double width = thickness * radius * (1.0 - 0.75 * t);

                for (int s = 0; s < Sides; ++s)
                {
                    const double angle = 2.0 * Pi * s / Sides;
                    const Vec3 out = a * std::cos(angle) + b * std::sin(angle);
                    const Vec3 position = point + out * width;

                    const int v = first + p * Sides + s;

                    positions[3 * v] = float(position.x);
                    positions[3 * v + 1] = float(position.y);
                    positions[3 * v + 2] = float(position.z);

                    normals[3 * v] = float(out.x);
                    normals[3 * v + 1] = float(out.y);
                    normals[3 * v + 2] = float(out.z);

                    texcoords[4 * v] = float(t);
                    texcoords[4 * v + 1] = 0.0f;
                    texcoords[4 * v + 2] = id;
                    texcoords[4 * v + 3] = -1.0f;

                    // the root sits among the yarns, the tip out in the open
                    occlusionData[v] = float(occlusion + (1.0 - occlusion) * t);

                    if (sampleData != nullptr)
                        sampleData[v] = ring;
                }

                point += direction * (fibre / Pieces);

                direction = (direction + side * (curl / Pieces) + normal * (droop / Pieces)).normalized();
            }

            unsigned int *out = indices + firstIndex + f * FibreIndices;

            for (int p = 0; p < Pieces; ++p)
            {
                for (int s = 0; s < Sides; ++s)
                {
                    const unsigned a0 = unsigned(first + p * Sides + s);
                    const unsigned a1 = unsigned(first + p * Sides + (s + 1) % Sides);
                    const unsigned b0 = a0 + Sides;
                    const unsigned b1 = a1 + Sides;

                    *out++ = a0;
                    *out++ = a1;
                    *out++ = b0;
                    *out++ = a1;
                    *out++ = b1;
                    *out++ = b0;
                }
            }
        }
    });
}

} // namespace

KnotMath::Mesh buildRopeMesh(
    const QVector<Vec3> &curve,
    bool closed,
    double radius,
    const RopeMaterial &material,
    bool quick)
{
    const bool braided = material.construction == 2;

    /*
     * Rings close enough to follow the strands or yarns, and around. A
     * rope that moves (quick, built for every frame) is drawn lighter: in
     * motion the grooves need fewer points, and a long rope (a knot of a
     * dozen crossings) keeps to a budget of vertices.
     */
    double ringSpacing = (braided ? 0.12 : 0.2) * radius * (quick ? 1.4 : 1.0);
    const int segments = braided ? (quick ? 64 : 96) : (quick ? 48 : 72);

    constexpr double QuickVertices = 60000.0;

    const QVector<Vec3> smooth = KnotMath::refineCurve(curve, 4, closed);

    double length = 0.0;

    for (int i = 1; i < smooth.size(); ++i)
        length += (smooth.at(i) - smooth.at(i - 1)).length();

    if (closed)
        length += (smooth.first() - smooth.last()).length();

    if (quick)
        ringSpacing = std::max(ringSpacing, length * segments / QuickVertices);

    const int rings = std::clamp(
        static_cast<int>(std::lround(length / ringSpacing)),
        16,
        60000);

    const QVector<Vec3> line = KnotMath::resampleClosedCurve(smooth, rings, closed);

    KnotMath::Mesh mesh = KnotMath::buildTubeMeshWithCrossings(
        line,
        radius,
        segments,
        closed,
        KnotMath::ColorMode::SingleColor,
        {});

    if (!mesh.isValid() || mesh.ringSegments != segments)
        return mesh;

    const int n = mesh.ringCount;
    const int ringVertices = n * segments;

    const double total = mesh.strandLength.isEmpty() ? length : mesh.strandLength.first();

    /*
     * The pattern makes a whole number of turns on a closed rope, so it
     * closes up: strands once round per lay length, yarns once round per
     * 2 pi r / tan(braid angle).
     */
    const double period =
        braided ? 2.0 * Pi * radius / std::tan(BraidAngle) : LayLength * radius;

    const double turns =
        closed ? std::max(1.0, std::round(total / period)) : total / period;

    const double phaseScale = 2.0 * Pi * turns / std::max(total, 1.0e-9);

    QVector<float> openness(ringVertices, 1.0f);

    mesh.occlusion.resize(mesh.positions.size() / 3);

    // written through pointers from several threads at once (each ring by
    // one of them)
    float *positions = mesh.positions.data();
    float *normals = mesh.normals.data();
    float *texcoords = mesh.texcoords.data();
    float *occlusion = mesh.occlusion.data();
    float *open = openness.data();

    const float *centerline = mesh.centerline.constData();
    const float *arcs = mesh.centerlineArc.constData();

    const StrandSample *strands = strandTable().data();

    inParallel(n, [&](int first, int last)
    {
        for (int i = first; i < last; ++i)
        {
            const Vec3 centre(centerline[4 * i], centerline[4 * i + 1], centerline[4 * i + 2]);

            const double s = arcs[i];
            const double phase = s * phaseScale;

            for (int j = 0; j < segments; ++j)
            {
                const int index = i * segments + j;

                const double theta = 2.0 * Pi * j / segments;

                const Vec3 radial(normals[3 * index], normals[3 * index + 1], normals[3 * index + 2]);

                double rho = 1.0;
                float across = 0.0f;
                float along = float(s / radius);
                float which = 0.0f;
                float groove = 0.0f;

                if (!braided)
                {
                    // the strand this point is on and where across it
                    // (the cross section turns with the lay: tabulated)
                    double x = std::fmod(theta - phase, 2.0 * Pi);

                    if (x < 0.0)
                        x += 2.0 * Pi;

                    const int k = static_cast<int>(x / (2.0 * Pi) * StrandSamples + 0.5) % StrandSamples;
                    const StrandSample &sample = strands[k];

                    rho = sample.rho;
                    across = sample.across;
                    which = sample.which;
                    groove = sample.groove;

                    open[index] = sample.open;
                }
                else
                {
                    /*
                     * Plain weave: a yarn of one direction is on top in
                     * every other cell. The yarn on top bulges; where it
                     * dives under a crossing yarn it sinks.
                     */
                    const double one = BraidYarns * (theta - phase) / (2.0 * Pi);
                    const double other = BraidYarns * (theta + phase) / (2.0 * Pi);

                    const double fOne = one - std::floor(one);
                    const double fOther = other - std::floor(other);

                    const double profileOne = std::sqrt(std::max(std::sin(Pi * fOne), 0.0));
                    const double profileOther = std::sqrt(std::max(std::sin(Pi * fOther), 0.0));

                    const double liftOne =
                        0.775 + 0.225 * std::cos(Pi * (other - 0.5 + std::floor(one)));
                    const double liftOther =
                        0.775 - 0.225 * std::cos(Pi * (one - 0.5 + std::floor(other)));

                    const double heightOne = profileOne * liftOne;
                    const double heightOther = profileOther * liftOther;

                    const bool oneOnTop = heightOne >= heightOther;
                    const double height = std::max(heightOne, heightOther);

                    rho = 1.0 - YarnHeight * (1.0 - height);
                    across = float(oneOnTop ? fOne : fOther);
                    along = float(oneOnTop ? other : one);
                    which = float((oneOnTop ? 0 : 1) + 2 * ((oneOnTop ? int(std::floor(one)) : int(std::floor(other))) & 7));
                    groove = float(1.0 - height);

                    open[index] = float(0.55 + 0.45 * std::sqrt(height));
                }

                const Vec3 position = centre + radial * (rho * radius);

                positions[3 * index] = float(position.x);
                positions[3 * index + 1] = float(position.y);
                positions[3 * index + 2] = float(position.z);

                texcoords[4 * index] = across;
                texcoords[4 * index + 1] = along;
                texcoords[4 * index + 2] = which;
                texcoords[4 * index + 3] = groove;
            }
        }
    });

    /*
     * Normals of the real surface, from its neighbours on the grid of
     * rings and segments.
     */
    inParallel(n, [&](int first, int last)
    {
        auto at = [positions, segments](int ring, int segment)
        {
            const int k = 3 * (ring * segments + segment);

            return Vec3(positions[k], positions[k + 1], positions[k + 2]);
        };

        for (int i = first; i < last; ++i)
        {
            const int previous = closed ? (i - 1 + n) % n : std::max(i - 1, 0);
            const int next = closed ? (i + 1) % n : std::min(i + 1, n - 1);

            for (int j = 0; j < segments; ++j)
            {
                const int left = (j - 1 + segments) % segments;
                const int right = (j + 1) % segments;

                const Vec3 around = at(i, right) - at(i, left);
                const Vec3 along = at(next, j) - at(previous, j);

                Vec3 normal = Vec3::cross(around, along).normalized();

                const int index = i * segments + j;

                const Vec3 radial(normals[3 * index], normals[3 * index + 1], normals[3 * index + 2]);

                if (Vec3::dot(normal, radial) < 0.0)
                    normal = normal * -1.0;

                normals[3 * index] = float(normal.x);
                normals[3 * index + 1] = float(normal.y);
                normals[3 * index + 2] = float(normal.z);
            }
        }
    });

    /*
     * What the rest of the rope hides from each point: the view factor of
     * the nearby parts of the rope (cylinders of radius r), summed over
     * the segments of its centre line,
     *     dF = 2 r |s x d| max(n . d, 0) ds / (pi D^2),
     * s the direction of the segment, d the unit direction to it, D the
     * distance (for an endless straight rope this adds up to exactly the
     * view factor of a cylinder, r/D facing it, r^2/(x^2 + r^2) from a
     * table it lies on). Parts within a few radii along the rope are the
     * point's own neighbourhood (the grooves are counted separately).
     * Worked out for twelve directions around every ring and interpolated.
     */
    const int stride = std::max(1, static_cast<int>(std::lround(0.5 * radius / ringSpacing)));

    QVector<Vec3> axis;

    for (int i = 0; i < n; i += stride)
        axis.push_back(Vec3(centerline[4 * i], centerline[4 * i + 1], centerline[4 * i + 2]));

    const int m = axis.size();
    const double reach = 12.0 * radius;
    const int ownArc = static_cast<int>(std::ceil(3.0 * radius / (stride * ringSpacing)));

    constexpr int Directions = 12;

    QVector<float> hidden(n * Directions, 0.0f);

    float *hiddenData = hidden.data();

    inParallel(quick ? 0 : n, [&](int first, int last)
    {
        for (int i = first; i < last; ++i)
        {
            const Vec3 centre(centerline[4 * i], centerline[4 * i + 1], centerline[4 * i + 2]);
            const int own = i / stride;

            for (int dir = 0; dir < Directions; ++dir)
            {
                const int j = dir * segments / Directions;
                const int index = i * segments + j;

                const Vec3 normal(normals[3 * index], normals[3 * index + 1], normals[3 * index + 2]);

                // the outside of the rope at this angle
                const Vec3 radial(
                    positions[3 * index] - centre.x,
                    positions[3 * index + 1] - centre.y,
                    positions[3 * index + 2] - centre.z);

                const Vec3 point = centre + radial.normalized() * radius;

                double sum = 0.0;

                for (int k = 0; k < m; ++k)
                {
                    if (arcGap(k, own, m, closed) <= ownArc)
                        continue;

                    const int k1 = closed ? (k + 1) % m : k + 1;

                    if (k1 >= m)
                        continue;

                    const Vec3 middle = (axis.at(k) + axis.at(k1)) * 0.5;
                    Vec3 d = middle - point;

                    const double distance = d.length();

                    if (distance > reach || distance < 1.0e-9)
                        continue;

                    d = d / distance;

                    const double facing = Vec3::dot(normal, d);

                    if (facing <= 0.0)
                        continue;

                    const Vec3 piece = axis.at(k1) - axis.at(k);
                    const double ds = piece.length();

                    if (ds < 1.0e-12)
                        continue;

                    const double sine = Vec3::cross(piece / ds, d).length();
                    const double clamped = std::max(distance, 1.2 * radius);

                    sum += 2.0 * radius * sine * facing * ds / (Pi * clamped * clamped);
                }

                hiddenData[i * Directions + dir] = float(std::min(sum, 0.9));
            }
        }
    });

    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < segments; ++j)
        {
            const double u = double(j) * Directions / segments;
            const int a = static_cast<int>(u) % Directions;
            const int b = (a + 1) % Directions;
            const double f = u - std::floor(u);

            const double rope =
                hiddenData[i * Directions + a] * (1.0 - f) + hiddenData[i * Directions + b] * f;

            const int index = i * segments + j;

            occlusion[index] = float((1.0 - rope) * open[index]);
        }
    }

    // the end caps of an open rope: as open as their ring
    for (int k = ringVertices; k < mesh.occlusion.size(); ++k)
        mesh.occlusion[k] = 1.0f;

    addLooseFibres(mesh, closed, radius, braided, quick);

    return mesh;
}

} // namespace Photo
