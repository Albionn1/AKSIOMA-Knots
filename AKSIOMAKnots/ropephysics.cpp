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
constexpr double TimeStep = 0.08;
constexpr double LongestTime = 200.0;

// at rest: no point has come further than this speed (radii per time unit)
// would take it, over each of the windows of RestWindow steps in RestSteps.
// Over a window: what counts is where the rope is going, not the last
// trembling of a part held tight.
constexpr double RestSpeed = 0.003;
constexpr int RestSteps = 75;
constexpr int RestWindow = 25;

// the rod and the contacts in turn, per step
constexpr int Passes = 3;

// the rod's factors are used for this many steps (they change slowly)
constexpr int RodFactorSteps = 1;

// this far along the rope (radii) from where it is held fast, it has taken
// the shape it is held in
constexpr double HeldShapeArc = 6.0;

// the most the rod's correction may move a point or turn a piece, per pass
constexpr double TrustMove = 0.25;
constexpr double TrustTurn = 0.4;

// a rod all but unstretchable: its compliance in the solved system
constexpr double StretchCompliance = 1.0e-6;

// no point moves more than this in one step, counted from the mean motion
// of the rope (no tunnelling through the rope: two radii wide)
constexpr double MaxStep = 0.4;

// the rope as a whole moves no more than this in one step: what it lands on
// stops it only at the end of that step, and MaxFall + MaxStep stays under
// the two radii a strand would have to cross
constexpr double MaxFall = 1.5;

// a rope lying still takes no set: below this speed (radii per time unit)
// the creep fades away (see the end of step())
constexpr double SetSpeed = 0.1;

// A mouse moves in jumps, once a frame; a hand moves smoothly. The hand
// follows the mouse this late (seconds), or every jump would shake the rope.
constexpr double HandDelay = 0.04;

// pieces this much beyond contact are still kept in the list of pairs, and
// the list is renewed before anything can have moved that far
constexpr double PairMargin = 1.2;

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

Quaternion multiply(const Quaternion &a, const Quaternion &b)
{
    return Quaternion{
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

Quaternion conjugate(const Quaternion &q)
{
    return Quaternion{q.w, -q.x, -q.y, -q.z};
}

// the vector v of the piece's own frame, in the world
Vec3 rotated(const Quaternion &q, const Vec3 &v)
{
    const Vec3 u(q.x, q.y, q.z);
    const Vec3 t = Vec3::cross(u, v) * 2.0;

    return v + t * q.w + Vec3::cross(u, t);
}

// the direction of the piece (d3 of its frame)
Vec3 axisOf(const Quaternion &q)
{
    return Vec3(
        2.0 * (q.x * q.z + q.w * q.y),
        2.0 * (q.y * q.z - q.w * q.x),
        1.0 - 2.0 * (q.x * q.x + q.y * q.y));
}

// turned by the small rotation `by` (a vector in the world)
void turnBy(Quaternion &q, const Vec3 &by)
{
    const double w = q.w;
    const double x = q.x;
    const double y = q.y;
    const double z = q.z;

    q.w += 0.5 * (-by.x * x - by.y * y - by.z * z);
    q.x += 0.5 * (by.x * w + by.y * z - by.z * y);
    q.y += 0.5 * (-by.x * z + by.y * w + by.z * x);
    q.z += 0.5 * (by.x * y - by.y * x + by.z * w);

    const double scale = 1.0 / std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);

    q.w *= scale;
    q.x *= scale;
    q.y *= scale;
    q.z *= scale;
}

// the shortest turn taking the unit vector a to the unit vector b
Quaternion turnBetween(const Vec3 &a, const Vec3 &b)
{
    const Vec3 c = Vec3::cross(a, b);
    const double w = 1.0 + Vec3::dot(a, b);

    if (w < 1.0e-9)
    {
        const Vec3 side = std::abs(a.x) < 0.9 ? Vec3::cross(a, Vec3(1.0, 0.0, 0.0)).normalized()
                                              : Vec3::cross(a, Vec3(0.0, 1.0, 0.0)).normalized();

        return Quaternion{0.0, side.x, side.y, side.z};
    }

    const double scale = 1.0 / std::sqrt(w * w + c.x * c.x + c.y * c.y + c.z * c.z);

    return Quaternion{w * scale, c.x * scale, c.y * scale, c.z * scale};
}

// A = L L^T in place (6 x 6, row by row); only the lower part is used
void cholesky6(double *a)
{
    for (int j = 0; j < 6; ++j)
    {
        double sum = a[j * 6 + j];

        for (int k = 0; k < j; ++k)
            sum -= a[j * 6 + k] * a[j * 6 + k];

        const double pivot = std::sqrt(std::max(sum, 1.0e-14));

        a[j * 6 + j] = pivot;

        for (int i = j + 1; i < 6; ++i)
        {
            double value = a[i * 6 + j];

            for (int k = 0; k < j; ++k)
                value -= a[i * 6 + k] * a[j * 6 + k];

            a[i * 6 + j] = value / pivot;
        }
    }
}

// b = (L L^T)^-1 b
void solve6(const double *l, double *b)
{
    for (int i = 0; i < 6; ++i)
    {
        double value = b[i];

        for (int k = 0; k < i; ++k)
            value -= l[i * 6 + k] * b[k];

        b[i] = value / l[i * 6 + i];
    }

    for (int i = 5; i >= 0; --i)
    {
        double value = b[i];

        for (int k = i + 1; k < 6; ++k)
            value -= l[k * 6 + i] * b[k];

        b[i] = value / l[i * 6 + i];
    }
}

/*
 * b = (L L^T)^-1 b for all six columns of b at once: what solve6 does to each
 * of them, number for number, with the loop over the columns on the inside,
 * where the processor can do two of them at a time.
 */
void solve6Columns(const double *l, double *b)
{
    for (int i = 0; i < 6; ++i)
    {
        double row[6];

        std::copy(b + 6 * i, b + 6 * i + 6, row);

        for (int k = 0; k < i; ++k)
        {
            const double a = l[i * 6 + k];
            const double *other = b + 6 * k;

            for (int c = 0; c < 6; ++c)
                row[c] -= a * other[c];
        }

        const double pivot = l[i * 6 + i];

        for (int c = 0; c < 6; ++c)
            b[i * 6 + c] = row[c] / pivot;
    }

    for (int i = 5; i >= 0; --i)
    {
        double row[6];

        std::copy(b + 6 * i, b + 6 * i + 6, row);

        for (int k = i + 1; k < 6; ++k)
        {
            const double a = l[k * 6 + i];
            const double *other = b + 6 * k;

            for (int c = 0; c < 6; ++c)
                row[c] -= a * other[c];
        }

        const double pivot = l[i * 6 + i];

        for (int c = 0; c < 6; ++c)
            b[i * 6 + c] = row[c] / pivot;
    }
}

// x = M^-1 x for the tridiagonal M (the rows below, on and above the diagonal)
template<typename T>
void solveTridiagonal(const double *below, const double *on, const double *above, double *work, T *x, int n)
{
    double pivot = on[0];

    x[0] = x[0] * (1.0 / pivot);

    for (int i = 1; i < n; ++i)
    {
        work[i] = above[i - 1] / pivot;
        pivot = on[i] - below[i] * work[i];
        x[i] = (x[i] - x[i - 1] * below[i]) * (1.0 / pivot);
    }

    for (int i = n - 2; i >= 0; --i)
        x[i] = x[i] - x[i + 1] * work[i + 1];
}

/*
 * v = (1 - rub L)^-1 v, L the differences of the velocities of neighbours
 * along the rope: every point's velocity is drawn towards theirs, as far as
 * the rubbing of the fibres takes it in a step, however strong. Points with
 * no weight (held fast, `free` 0) keep theirs and hold their neighbours back.
 * On a closed rope the first and last are neighbours as well: the two
 * corners of the system are taken out by Sherman-Morrison.
 */
void rubAlong(Vec3 *v, int n, bool closed, double rub, const double *free)
{
    QVector<double> below(n);
    QVector<double> on(n);
    QVector<double> above(n);
    QVector<double> work(n);

    for (int i = 0; i < n; ++i)
    {
        const bool moves = free == nullptr || free[i] != 0.0;
        const bool first = !closed && i == 0;
        const bool last = !closed && i == n - 1;

        below[i] = moves && !first ? -rub : 0.0;
        above[i] = moves && !last ? -rub : 0.0;
        on[i] = 1.0 - below.at(i) - above.at(i);
    }

    if (!closed)
    {
        solveTridiagonal(below.constData(), on.constData(), above.constData(), work.data(), v, n);
        return;
    }

    // (row 0 reaches the last point, the last row reaches point 0)
    const double cornerLow = below.at(0);
    const double cornerLast = above.at(n - 1);
    const double scale = -on.at(0);

    on[0] -= scale;
    on[n - 1] -= cornerLast * cornerLow / scale;

    QVector<double> z(n, 0.0);

    z[0] = scale;
    z[n - 1] = cornerLast;

    solveTridiagonal(below.constData(), on.constData(), above.constData(), work.data(), v, n);
    solveTridiagonal(below.constData(), on.constData(), above.constData(), work.data(), z.data(), n);

    const Vec3 top = v[0] + v[n - 1] * (cornerLow / scale);
    const double share = 1.0 / (1.0 + z.at(0) + z.at(n - 1) * cornerLow / scale);

    for (int i = 0; i < n; ++i)
        v[i] = v[i] - top * (share * z.at(i));
}

/*
 * The motion of the rope as a solid body: the velocities drift + turn x
 * (X(i) - centre) closest to V(i) over all its points (of equal mass). It is
 * what is left when every part of the rope keeps its place on the others.
 */
void rigidMotion(const Vec3 *X, const Vec3 *V, int n, Vec3 &centre, Vec3 &drift, Vec3 &turn)
{
    centre = Vec3(0.0, 0.0, 0.0);
    drift = Vec3(0.0, 0.0, 0.0);

    for (int i = 0; i < n; ++i)
    {
        centre += X[i];
        drift += V[i];
    }

    centre = centre / static_cast<double>(n);
    drift = drift / static_cast<double>(n);

    // L = sum r x v, I = sum |r|^2 E - r r^T
    Vec3 momentum(0.0, 0.0, 0.0);

    double xx = 0.0;
    double yy = 0.0;
    double zz = 0.0;
    double xy = 0.0;
    double xz = 0.0;
    double yz = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const Vec3 r = X[i] - centre;

        momentum += Vec3::cross(r, V[i] - drift);

        xx += r.y * r.y + r.z * r.z;
        yy += r.x * r.x + r.z * r.z;
        zz += r.x * r.x + r.y * r.y;
        xy -= r.x * r.y;
        xz -= r.x * r.z;
        yz -= r.y * r.z;
    }

    // (a rope dead straight does not turn about itself)
    const double tiny = 1.0e-9 * (xx + yy + zz) + 1.0e-12;

    xx += tiny;
    yy += tiny;
    zz += tiny;

    const double c00 = yy * zz - yz * yz;
    const double c01 = xz * yz - xy * zz;
    const double c02 = xy * yz - xz * yy;
    const double c11 = xx * zz - xz * xz;
    const double c12 = xy * xz - xx * yz;
    const double c22 = xx * yy - xy * xy;

    const double det = xx * c00 + xy * c01 + xz * c02;

    turn = Vec3(
        (c00 * momentum.x + c01 * momentum.y + c02 * momentum.z) / det,
        (c01 * momentum.x + c11 * momentum.y + c12 * momentum.z) / det,
        (c02 * momentum.x + c12 * momentum.y + c22 * momentum.z) / det);
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
    const RopeMaterial &material,
    RopeProgress *progress)
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

    QVector<Vec3> frames;

    const Settling settling = settleRope(x, QVector<bool>(n, false), closed, material, Ground(), &frames, progress);

    result.cancelled = settling.cancelled;

    if (settling.cancelled)
        return result;

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

    for (const Vec3 &f : frames)
    {
        const QVector3D k = back.mapVector(QVector3D(float(f.x), float(f.y), float(f.z)));

        result.frames.push_back(Vec3(k.x(), k.y(), k.z()));
    }

    result.valid = true;

    return result;
}

Settling settleRope(
    QVector<Vec3> &x,
    const QVector<bool> &pinned,
    bool closed,
    const RopeMaterial &material,
    const Ground &ground,
    QVector<Vec3> *frames,
    RopeProgress *progress)
{
    Settling result;

    RopeSimulation simulation(x, pinned, closed, material, ground);

    if (!simulation.isValid())
        return result;

    // it has just been tied
    simulation.takeSet(material.tiedSet);

    int step = 0;

    const int longest = static_cast<int>(LongestTime / TimeStep);

    for (step = 0; step < longest; ++step)
    {
        simulation.step();

        if (simulation.atRest())
            break;

        if (progress != nullptr)
        {
            if (progress->cancel.load())
            {
                result.cancelled = true;

                return result;
            }

            progress->percent.store(100 * step / longest);
        }
    }

    x = simulation.points();

    if (frames != nullptr)
        *frames = simulation.frames();

    const double radiusMm = 0.5 * material.diameterMm;
    const double timeUnit = std::sqrt(radiusMm * 0.001 / Gravity);

    result.steps = step;
    result.seconds = step * TimeStep * timeUnit;
    result.finalSpeed = simulation.drift() * radiusMm / timeUnit;
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
    const double twisting = bending * material.twistStiffness;

    const double dt = TimeStep;

    // a piece's turn: its mass h, as a short thick cylinder
    wq_ = 1.0 / (h_ * (0.25 + h_ * h_ / 12.0));

    // compliance of a joint against turning by an angle (curvature
    // theta / h, energy B h kappa^2 / 2 = B theta^2 / 2h)
    bendSoftness_ = h_ / bending / (dt * dt);
    twistSoftness_ = h_ / twisting / (dt * dt);

    /*
     * A bend of wavenumber k swings at w = sqrt(B) k^2 (mass 1 per length),
     * and the rubbing takes its velocity away at the rate rub (k h)^2 / dt:
     * also in k^2, so the swings of every length lose the same share per
     * cycle, as those of a real rope do. Damped by the share z of what stops
     * it swinging back, that rate is 2 z w.
     */
    fibreRub_ = 2.0 * material.swingDamping * std::sqrt(bending) * dt / (h_ * h_);

    handEase_ = std::min(secondsPerStep() / HandDelay, 1.0);

    // what permanent bending starts at, as an angle per joint, and how
    // fast the rope gives way beyond it
    yieldAngle_ = h_ * radiusMm_ / std::max(material.setRadiusMm, 1.0);
    setRate_ = material.setRate * std::sqrt(radiusMm_ * 0.001 / Gravity);

    /*
     * The frame of every piece: its direction, carried along the rope with
     * no twist; on a closed rope the small turn left at the end is spread
     * evenly over its length.
     */
    q_.resize(segments_);

    {
        const Vec3 first = (x_.at(1 % n) - x_.at(0)).normalized();

        Quaternion q = turnBetween(Vec3(0.0, 0.0, 1.0), first);

        q_[0] = q;

        Vec3 axis = first;

        for (int i = 1; i < segments_; ++i)
        {
            const Vec3 next = (x_.at((i + 1) % n) - x_.at(i)).normalized();

            q = multiply(turnBetween(axis, next), q);

            q_[i] = q;
            axis = next;
        }

        if (closed_)
        {
            const Quaternion back = multiply(turnBetween(axis, first), q);

            Quaternion left = multiply(conjugate(q_.at(0)), back);

            if (left.w < 0.0)
                left = Quaternion{-left.w, -left.x, -left.y, -left.z};

            const double total = 2.0 * std::atan2(left.z, left.w);

            for (int i = 0; i < segments_; ++i)
            {
                const double a = -total * (double(i) + 0.5) / segments_ * 0.5;

                q_[i] = multiply(q_.at(i), Quaternion{std::cos(a), 0.0, 0.0, std::sin(a)});
            }
        }
    }

    qOld_ = q_;
    windowStart_ = x_;
    spin_.fill(Vec3(0.0, 0.0, 0.0), segments_);
    lambda_.fill(Vec3(0.0, 0.0, 0.0), segments_);
    groundDepth_.fill(0.0, n);

    /*
     * No rope is straight when it lies relaxed. It has been wound on a
     * reel and coiled in a hand: a slow, uneven curl stays in it, different
     * along its length, the same for the same rope.
     */
    shape_.resize(segments_);

    {
        const double curl = h_ * radiusMm_ / std::max(material.curlRadiusMm, 1.0);

        for (int i = 0; i < segments_; ++i)
        {
            const double s = i * h_;

            const double a = 0.6 * std::sin(s * 0.083 + 0.7) + 0.5 * std::sin(s * 0.211 + 2.1) +
                             0.4 * std::sin(s * 0.431 + 4.4);
            const double b = 0.6 * std::sin(s * 0.107 + 3.3) + 0.5 * std::sin(s * 0.269 + 0.4) +
                             0.4 * std::sin(s * 0.373 + 1.9);

            shape_[i] = Vec3(curl * a, curl * b, 0.0);
        }
    }

    rodAge_ = -1;

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

QVector<Vec3> RopeSimulation::frames() const
{
    const int n = x_.size();

    QVector<Vec3> result(n);

    for (int i = 0; i < n; ++i)
    {
        const int piece = std::min(i, segments_ - 1);
        const int before = closed_ ? (piece - 1 + segments_) % segments_ : std::max(piece - 1, 0);

        result[i] = (rotated(q_.at(piece), Vec3(1.0, 0.0, 0.0)) +
                     rotated(q_.at(before), Vec3(1.0, 0.0, 0.0))).normalized();
    }

    return result;
}

void RopeSimulation::useFrames(const QVector<Vec3> &frames)
{
    const int n = x_.size();

    if (!valid_ || frames.size() != n)
        return;

    for (int i = 0; i < segments_; ++i)
    {
        const Vec3 axis = (x_.at((i + 1) % n) - x_.at(i)).normalized();

        Vec3 wanted = frames.at(i) + frames.at((i + 1) % n);

        wanted = (wanted - axis * Vec3::dot(wanted, axis)).normalized();

        const Quaternion aligned = turnBetween(Vec3(0.0, 0.0, 1.0), axis);

        const Vec3 now = rotated(aligned, Vec3(1.0, 0.0, 0.0));

        const double half =
            0.5 * std::atan2(Vec3::dot(Vec3::cross(now, wanted), axis), Vec3::dot(now, wanted));

        q_[i] = multiply(aligned, Quaternion{std::cos(half), 0.0, 0.0, std::sin(half)});
    }

    qOld_ = q_;
}

void RopeSimulation::takeSet(double share)
{
    if (!valid_)
        return;

    const int joints = closed_ ? segments_ : segments_ - 1;
    const int n = x_.size();

    /*
     * Where the rope is held fast (a lace through its eyelets) it has long
     * taken the shape it is held in, whole: there and for a little way
     * either side, it has no wish to be anything else.
     */
    QVector<bool> held(n, false);

    const int reach = static_cast<int>(std::ceil(HeldShapeArc / h_));

    for (int i = 0; i < n; ++i)
    {
        if (w_.at(i) != 0.0)
            continue;

        for (int k = -reach; k <= reach; ++k)
        {
            const int m = closed_ ? (i + k + n) % n : i + k;

            if (m >= 0 && m < n)
                held[m] = true;
        }
    }

    for (int i = 0; i < joints; ++i)
    {
        Quaternion turn = multiply(conjugate(q_.at(i)), q_.at((i + 1) % segments_));

        if (turn.w < 0.0)
            turn = Quaternion{-turn.w, -turn.x, -turn.y, -turn.z};

        const double taken = held.at(i) ? 1.0 : share;

        shape_[i].x += taken * (2.0 * turn.x - shape_.at(i).x);
        shape_[i].y += taken * (2.0 * turn.y - shape_.at(i).y);
    }
}

void RopeSimulation::cut(int first, int count)
{
    const int n = x_.size();

    if (!valid_ || !closed_ || held_ >= 0 || count < 4 || count > n)
        return;

    // what is left of it, starting at `first`
    const auto keep = [first, n](auto &values, int size)
    {
        const auto all = values;

        values.resize(size);

        for (int k = 0; k < size; ++k)
            values[k] = all.at((first + k) % n);
    };

    keep(x_, count);
    keep(v_, count);
    keep(w_, count);

    keep(rest_, count - 1);
    keep(q_, count - 1);
    keep(spin_, count - 1);
    keep(shape_, count - 1);

    closed_ = false;
    segments_ = count - 1;

    p_.resize(count);
    groundDepth_.resize(count);
    lambda_.resize(segments_);
    cellOf_.resize(segments_);
    order_.resize(segments_);

    qOld_ = q_;
    windowStart_ = x_;
    windowSteps_ = 0;
    quiet_ = 0;
    rodAge_ = -1;

    pairs_.clear();
    travelled_ = 0.0;
}

void RopeSimulation::stayWithin(double left, double right, double back, double front)
{
    fenced_ = true;

    fenceLow_ = Vec3(left, 0.0, back);
    fenceHigh_ = Vec3(right, 0.0, front);

    // (a very tight knot can lie over the edge from the start)
    for (const Vec3 &p : x_)
    {
        fenceLow_.x = std::min(fenceLow_.x, p.x);
        fenceLow_.z = std::min(fenceLow_.z, p.z);
        fenceHigh_.x = std::max(fenceHigh_.x, p.x);
        fenceHigh_.z = std::max(fenceHigh_.z, p.z);
    }
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
    handEased_ = x_.at(index);

    // the hand moves it; nothing else does
    w_[index] = 0.0;

    rodAge_ = -1;
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

    rodAge_ = -1;
    held_ = -1;
    under_ = false;
    quiet_ = 0;
}

/*
 * The rod, solved whole. The rope is a Cosserat rod: points p with a mass,
 * and between every two of them a piece with a turn q (its material frame,
 * d3 along it). The constraints, at every joint i,
 *
 *     s(i) = p(i+1) - p(i) - l(i) d3(q(i)) = 0      no stretch, no shear
 *     b(i) = turn from q(i) to q(i+1) - rest shape  bending and twisting
 *
 * are coupled along the rope only from one joint to the next, so the system
 * of their corrections (A dl = -C, A = J M^-1 J^T + compliance) is block
 * tridiagonal with blocks of 6 and is solved exactly, end to end, in time
 * proportional to the length (Deul, Kugelstadt, Weiler, Bender 2018, Direct
 * Position-Based Solver for Stiff Rods). Stiffness is therefore the
 * material's own at any length, unlike constraints solved one after the
 * other, where a long rope comes out soft and stretchy.
 *
 * A closed rope has one more joint, tying the last piece to the first: the
 * last block is eliminated by its Schur complement. The factors change
 * slowly and are kept for a few steps.
 */
void RopeSimulation::factorRod()
{
    const int segments = segments_;
    const int n = x_.size();
    const int chain = closed_ ? segments - 1 : segments;

    rodD_.resize(36 * segments);
    rodE_.resize(36 * segments);
    rodL_.resize(36 * segments);
    rodF_.resize(36 * segments);

    double *D = rodD_.data();
    double *E = rodE_.data();
    double *L = rodL_.data();
    double *F = rodF_.data();

    const Quaternion *Q = q_.constData();
    const double *W = w_.constData();
    const double *Rest = rest_.constData();
    const double wq = wq_;
    const double wp = 1.0 / h_;

    std::fill(D, D + 36 * segments, 0.0);
    std::fill(E, E + 36 * segments, 0.0);

    const auto skew = [](double *m, int row, int column, const Vec3 &d, double scale)
    {
        // m[row + r][column + c] += scale * skew(d)
        m[row * 6 + column + 1] += -scale * d.z;
        m[row * 6 + column + 2] += scale * d.y;
        m[(row + 1) * 6 + column] += scale * d.z;
        m[(row + 1) * 6 + column + 2] += -scale * d.x;
        m[(row + 2) * 6 + column] += -scale * d.y;
        m[(row + 2) * 6 + column + 1] += scale * d.x;
    };

    const auto diagonal = [](double *m, int at, double value)
    {
        m[at * 6 + at] += value;
        m[(at + 1) * 6 + at + 1] += value;
        m[(at + 2) * 6 + at + 2] += value;
    };

    for (int i = 0; i < segments; ++i)
    {
        const int next = (i + 1) % n;
        const int after = (i + 1) % segments;

        const Vec3 d = axisOf(Q[i]);
        const double l = Rest[i];

        double *block = D + 36 * i;

        // s with s: the two points and the turn of the piece
        const double points = wp * (W[i] + W[next]) + StretchCompliance;
        const double turn = wq * l * l;

        diagonal(block, 0, points + turn);

        const double along[3] = {d.x, d.y, d.z};

        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
                block[r * 6 + c] -= turn * along[r] * along[c];
        }

        const bool bends = closed_ || i < segments - 1;

        if (bends)
        {
            // s with b, through the turn of this piece
            skew(block, 0, 3, d, -wq * l);
            skew(block, 3, 0, d, wq * l);

            // b with b: the two pieces, and how soft the joint is
            diagonal(block, 3, 2.0 * wq + bendSoftness_);

            const double softer = twistSoftness_ - bendSoftness_;

            for (int r = 0; r < 3; ++r)
            {
                for (int c = 0; c < 3; ++c)
                    block[(3 + r) * 6 + 3 + c] += softer * along[r] * along[c];
            }

            // to the next joint
            const Vec3 e = axisOf(Q[after]);
            const double m = Rest[after];

            double *link = E + 36 * i;

            diagonal(link, 0, -wp * W[next]);

            skew(link, 3, 0, e, -wq * m);

            if (closed_ || after < segments - 1)
                diagonal(link, 3, -wq);
        }
        else
        {
            // an open rope's last piece has no joint after it
            diagonal(block, 3, 1.0);
        }
    }

    // Eliminating along the chain: C(i+1) = D(i+1) - E(i)^T C(i)^-1 E(i)
    double working[36];

    std::copy(D, D + 36, working);

    for (int i = 0; i < chain; ++i)
    {
        double *C = L + 36 * i;

        std::copy(working, working + 36, C);

        cholesky6(C);

        if (i + 1 >= chain)
            break;

        const double *link = E + 36 * i;
        double *f = F + 36 * i;

        // F(i) = C(i)^-1 E(i)
        std::copy(link, link + 36, f);

        solve6Columns(C, f);

        std::copy(D + 36 * (i + 1), D + 36 * (i + 2), working);

        // (E is mostly zeros: only its other numbers cost anything)
        double sum[36] = {};

        for (int k = 0; k < 6; ++k)
        {
            for (int r = 0; r < 6; ++r)
            {
                const double a = link[k * 6 + r];

                if (a == 0.0)
                    continue;

                for (int c = 0; c < 6; ++c)
                    sum[r * 6 + c] += a * f[k * 6 + c];
            }
        }

        for (int t = 0; t < 36; ++t)
            working[t] -= sum[t];
    }

    if (!closed_)
    {
        rodAge_ = 0;
        return;
    }

    /*
     * The joint that closes the rope couples the last block to the first
     * and the last of the chain: Z = A^-1 G column by column, and its
     * Schur complement S = D(last) - G^T Z.
     */
    rodZ_.resize(36 * chain);
    rodS_.resize(36);

    double *Z = rodZ_.data();

    const double *toLast = E + 36 * (chain - 1);
    const double *wrap = E + 36 * (segments - 1);

    std::fill(Z, Z + 36 * chain, 0.0);

    // G(0) = wrap^T (the first block against the last), G(end) = E
    for (int r = 0; r < 6; ++r)
    {
        for (int c = 0; c < 6; ++c)
        {
            Z[r * 6 + c] = wrap[c * 6 + r];
            Z[36 * (chain - 1) + r * 6 + c] += toLast[r * 6 + c];
        }
    }

    // as solveChain, for all six columns of Z at once, in one sweep of the rope
    for (int i = 0; i + 1 < chain; ++i)
    {
        const double *f = F + 36 * i;
        const double *y = Z + 36 * i;
        double *next = Z + 36 * (i + 1);

        double sum[36] = {};

        for (int k = 0; k < 6; ++k)
        {
            for (int r = 0; r < 6; ++r)
            {
                const double a = f[k * 6 + r];

                for (int c = 0; c < 6; ++c)
                    sum[r * 6 + c] += a * y[k * 6 + c];
            }
        }

        for (int t = 0; t < 36; ++t)
            next[t] -= sum[t];
    }

    for (int i = chain - 1; i >= 0; --i)
    {
        double *x = Z + 36 * i;

        solve6Columns(L + 36 * i, x);

        if (i + 1 >= chain)
            continue;

        const double *f = F + 36 * i;
        const double *after = Z + 36 * (i + 1);

        double sum[36] = {};

        for (int r = 0; r < 6; ++r)
        {
            for (int k = 0; k < 6; ++k)
            {
                const double a = f[r * 6 + k];

                for (int c = 0; c < 6; ++c)
                    sum[r * 6 + c] += a * after[k * 6 + c];
            }
        }

        for (int t = 0; t < 36; ++t)
            x[t] -= sum[t];
    }

    double *S = rodS_.data();

    std::copy(D + 36 * chain, D + 36 * (chain + 1), S);

    for (int r = 0; r < 6; ++r)
    {
        for (int c = 0; c < 6; ++c)
        {
            double sum = 0.0;

            for (int k = 0; k < 6; ++k)
            {
                sum += wrap[r * 6 + k] * Z[k * 6 + c];
                sum += toLast[k * 6 + r] * Z[36 * (chain - 1) + k * 6 + c];
            }

            S[r * 6 + c] -= sum;
        }
    }

    cholesky6(S);

    rodAge_ = 0;
}

void RopeSimulation::solveChain(double *b) const
{
    const int chain = closed_ ? segments_ - 1 : segments_;

    const double *L = rodL_.constData();
    const double *F = rodF_.constData();

    // forward: y(i+1) = b(i+1) - F(i)^T y(i)
    for (int i = 0; i + 1 < chain; ++i)
    {
        const double *f = F + 36 * i;
        const double *y = b + 6 * i;
        double *next = b + 6 * (i + 1);

        double sum[6] = {};

        for (int k = 0; k < 6; ++k)
        {
            const double a = y[k];

            for (int r = 0; r < 6; ++r)
                sum[r] += f[k * 6 + r] * a;
        }

        for (int r = 0; r < 6; ++r)
            next[r] -= sum[r];
    }

    // back: x(i) = C(i)^-1 y(i) - F(i) x(i+1)
    for (int i = chain - 1; i >= 0; --i)
    {
        double *x = b + 6 * i;

        solve6(L + 36 * i, x);

        if (i + 1 >= chain)
            continue;

        const double *f = F + 36 * i;
        const double *after = b + 6 * (i + 1);

        for (int r = 0; r < 6; ++r)
        {
            double sum = 0.0;

            for (int k = 0; k < 6; ++k)
                sum += f[r * 6 + k] * after[k];

            x[r] -= sum;
        }
    }
}

void RopeSimulation::solveRod()
{
    const int segments = segments_;
    const int n = x_.size();

    Vec3 *P = p_.data();
    Quaternion *Q = q_.data();
    Vec3 *Sum = lambda_.data();

    const double *W = w_.constData();
    const double *Rest = rest_.constData();
    const Vec3 *Shape = shape_.constData();

    rodRhs_.resize(6 * segments);

    double *B = rodRhs_.data();

    // what the constraints are off by
    for (int i = 0; i < segments; ++i)
    {
        const int next = (i + 1) % n;
        const int after = (i + 1) % segments;

        const Vec3 d = axisOf(Q[i]);
        const Vec3 s = P[next] - P[i] - d * Rest[i];

        double *row = B + 6 * i;

        row[0] = -s.x;
        row[1] = -s.y;
        row[2] = -s.z;

        if (!closed_ && i == segments - 1)
        {
            row[3] = row[4] = row[5] = 0.0;
            continue;
        }

        Quaternion turn = multiply(conjugate(Q[i]), Q[after]);

        if (turn.w < 0.0)
            turn = Quaternion{-turn.w, -turn.x, -turn.y, -turn.z};

        const Vec3 off = rotated(Q[i], Vec3(2.0 * turn.x, 2.0 * turn.y, 2.0 * turn.z) - Shape[i]);

        // and how far the compliance has given way already in this step
        const double axial = Vec3::dot(d, Sum[i]);
        const Vec3 given = Sum[i] * bendSoftness_ + d * ((twistSoftness_ - bendSoftness_) * axial);

        row[3] = -off.x - given.x;
        row[4] = -off.y - given.y;
        row[5] = -off.z - given.z;
    }

    solveChain(B);

    if (closed_)
    {
        const int chain = segments - 1;

        const double *E = rodE_.constData();
        const double *toLast = E + 36 * (chain - 1);
        const double *wrap = E + 36 * (segments - 1);
        const double *Z = rodZ_.constData();

        double last[6];

        for (int r = 0; r < 6; ++r)
        {
            double sum = 0.0;

            for (int k = 0; k < 6; ++k)
            {
                sum += wrap[r * 6 + k] * B[k];
                sum += toLast[k * 6 + r] * B[6 * (chain - 1) + k];
            }

            last[r] = B[6 * chain + r] - sum;
        }

        solve6(rodS_.constData(), last);

        for (int i = 0; i < chain; ++i)
        {
            for (int r = 0; r < 6; ++r)
            {
                double sum = 0.0;

                for (int k = 0; k < 6; ++k)
                    sum += Z[36 * i + r * 6 + k] * last[k];

                B[6 * i + r] -= sum;
            }
        }

        for (int r = 0; r < 6; ++r)
            B[6 * chain + r] = last[r];
    }

    // the corrections, through the masses
    const double wp = 1.0 / h_;
    const double wq = wq_;

    rodMove_.resize(n);
    rodTurn_.resize(segments);

    Vec3 *Move = rodMove_.data();
    Vec3 *Turn = rodTurn_.data();

    /*
     * The system is the rod straightened out to first order about where it
     * is now. Far from there that is no guide: where a contact has just
     * pushed a part hard, and anywhere on a long rope in motion, where a
     * small error at one place turns a long arm of rope. So no point or piece
     * takes more than first order can be trusted for, and the next pass goes
     * on from there. The whole correction shrinks together, for all the rope:
     * only so does it stay the solution of the system. Limited point by point
     * it is not, it makes the rope worse, and all that moving becomes
     * velocity at the end of the step: a rope of a thousand points then boils,
     * never coming to rest, with the limit biting on every fourth point.
     */
    double share = 1.0;

    const auto trust = [&share](const Vec3 &step, double most)
    {
        const double far = step.lengthSquared();

        if (far > most * most)
            share = std::min(share, most / std::sqrt(far));
    };

    for (int i = 0; i < segments; ++i)
    {
        const double *row = B + 6 * i;
        const double *other = B + 6 * ((i - 1 + segments) % segments);

        const bool first = !closed_ && i == 0;

        const Vec3 stretch(row[0], row[1], row[2]);
        const Vec3 bend(row[3], row[4], row[5]);

        // What the system asks of point i: what it really moves, the
        // difference of the pulls on its two sides (a hanging rope is under
        // a great tension that all but cancels at every point).
        Vec3 pull = stretch;

        if (!first)
            pull -= Vec3(other[0], other[1], other[2]);

        Move[i] = pull * (-wp * W[i]);

        // and of piece i: -l S(d)^T... on its own turn, l (d x lambda) with
        // the sign of the stretch term, and the bend terms of its two joints
        const Vec3 own = Vec3::cross(axisOf(Q[i]), stretch) * (-wq * Rest[i]);

        const Vec3 before = first ? Vec3(0.0, 0.0, 0.0) : Vec3(other[3], other[4], other[5]);

        Turn[i] = own + (before - bend) * wq;

        trust(Move[i], TrustMove);
        trust(Turn[i], TrustTurn);
    }

    // the last point of an open rope has the one piece before it
    if (!closed_)
    {
        const double *row = B + 6 * (segments - 1);

        Move[n - 1] = Vec3(row[0], row[1], row[2]) * (wp * W[n - 1]);

        trust(Move[n - 1], TrustMove);
    }

    for (int i = 0; i < n; ++i)
        P[i] += Move[i] * share;

    for (int i = 0; i < segments; ++i)
    {
        Sum[i] += Vec3(B[6 * i + 3], B[6 * i + 4], B[6 * i + 5]) * share;

        turnBy(Q[i], Turn[i] * share);
    }
}

/*
 * The contacts: of the rope with itself and with the ground. The pairs of
 * pieces near each other are found on a grid only when the rope has moved
 * far enough for a new pair to come into reach.
 */
void RopeSimulation::findPairs()
{
    const int n = x_.size();
    const int segments = segments_;

    const Vec3 *P = p_.constData();

    // two radii apart, and what they may still move before the next search
    constexpr double Cell = 3.8;

    double minX = 1.0e30;
    double minZ = 1.0e30;
    double maxX = -1.0e30;
    double maxZ = -1.0e30;

    for (int i = 0; i < n; ++i)
    {
        minX = std::min(minX, P[i].x);
        minZ = std::min(minZ, P[i].z);
        maxX = std::max(maxX, P[i].x);
        maxZ = std::max(maxZ, P[i].z);
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

    pairs_.clear();

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

                    if (j <= i || arcGap(i, j, n, closed_) <= neighbours_)
                        continue;

                    const Vec3 gap =
                        (P[i] + P[(i + 1) % n] - P[j] - P[(j + 1) % n]) * 0.5;

                    const double reach = 2.0 + PairMargin + h_;

                    if (gap.lengthSquared() < reach * reach)
                    {
                        pairs_.push_back(i);
                        pairs_.push_back(j);
                    }
                }
            }
        }
    }

    travelled_ = 0.0;
}

void RopeSimulation::solveContacts()
{
    const int n = x_.size();
    const bool closed = closed_;
    const double h = h_;

    Vec3 *X = x_.data();
    Vec3 *P = p_.data();

    /*
     * In contacts and on the ground the point in the hand gives way like
     * any other: the hand carries it, but cannot force it through another
     * strand or into the table (a strand trapped between the two would
     * otherwise be squeezed through).
     */
    QVector<double> solid = w_;

    if (held_ >= 0)
        solid[held_] = 1.0;

    const double *Solid = solid.constData();

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

    const int *pairs = pairs_.constData();
    const int pairCount = pairs_.size() / 2;

    for (int pair = 0; pair < pairCount; ++pair)
    {
        const int i = pairs[2 * pair];
        const int j = pairs[2 * pair + 1];

        const int i1 = (i + 1) % n;
        const int j1 = (j + 1) % n;

        // two pinned parts never move
        if (Solid[i] + Solid[i1] + Solid[j] + Solid[j1] == 0.0)
            continue;

        // far apart for sure (their middles, less the pieces' own half
        // lengths, beyond the two radii of a contact)
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

        Vec3 normal = distance > 1.0e-9 ? d / distance : Vec3(0.0, 1.0, 0.0);

        double depth = 2.0 - distance;

        /*
         * The hand pushes its part under another: that one is lifted
         * straight up over it (by as much as the two need to be apart, at
         * their distance across). Only a part beside it or above it, and
         * none near a part lying under it (pinnedDown): those would have to
         * go through it, so their contacts are ordinary ones.
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
        const Vec3 moveA = (P[i] - X[i]) * wa0 + (P[i1] - X[i1]) * wa1;
        const Vec3 moveB = (P[j] - X[j]) * wb0 + (P[j1] - X[j1]) * wb1;

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

    /*
     * The ground (with friction): a point below it goes back out along
     * the ground's normal, by its depth below the sloping surface; its
     * sliding along the surface in this step is held or slowed.
     */
    for (int i = 0; i < n; ++i)
    {
        if (Solid[i] == 0.0)
            continue;

        /*
         * The edge of the table. A rope has momentum and a loop swings past
         * the hand, so holding the hand back does not keep it on the table.
         * Nor can the edge be a drop in the ground: the part beyond it would
         * hang through the table's side, and that changed the knot when tried.
         * So the rope goes no further, as against a low rail.
         */
        if (fenced_)
        {
            P[i].x = std::clamp(P[i].x, fenceLow_.x, fenceHigh_.x);
            P[i].z = std::clamp(P[i].z, fenceLow_.z, fenceHigh_.z);
        }

        double slopeX = 0.0;
        double slopeZ = 0.0;

        const double top = ground_ ? ground_(P[i].x, P[i].z, slopeX, slopeZ) : 0.0;

        if (P[i].y >= top + 1.0)
            continue;

        const Vec3 normal = Vec3(-slopeX, 1.0, -slopeZ).normalized();
        const double depth = (top + 1.0 - P[i].y) * normal.y;

        P[i] += normal * depth;

        // Friction may take away only what the pressure on the ground in
        // this step allows, over all the passes together.
        groundDepth_[i] += muSurface_ * depth;

        Vec3 slide = P[i] - X[i];
        slide -= normal * Vec3::dot(slide, normal);

        const double amount = slide.length();

        if (amount < 1.0e-12)
            continue;

        const double take = std::min(amount, groundDepth_[i]);

        groundDepth_[i] -= take;

        P[i] -= slide * (take / amount);
    }
}

void RopeSimulation::step()
{
    if (!valid_)
        return;

    const int n = x_.size();
    const int segments = segments_;
    const double dt = TimeStep;

    Vec3 *X = x_.data();
    Vec3 *V = v_.data();
    Vec3 *P = p_.data();
    Quaternion *Q = q_.data();
    Quaternion *Old = qOld_.data();
    Vec3 *Spin = spin_.data();
    const double *W = w_.constData();

    // the hand carries its point towards it, a safe distance per step;
    // up and down it moves gently, as a hand lifts a rope (about half a
    // second to the full height), so the rope never jumps
    constexpr double HandStep = 0.35;
    constexpr double HandRise = 0.024;

    // no piece turns further than this in a step
    constexpr double MaxTurn = 0.3;

    if (held_ >= 0)
        handEased_ += (hand_ - handEased_) * handEase_;

    /*
     * Moving as a whole carries no strand through another, so the limit on a
     * point's speed counts from the mean motion of the rope, and a rope falls
     * as fast as gravity takes it. That holds only while it touches nothing:
     * what lies on the table or is held has stopped, and the rest may then
     * come down on it no faster than two strands can meet anyway.
     */
    Vec3 together(0.0, 0.0, 0.0);

    bool touching = held_ >= 0;

    for (int i = 0; i < n; ++i)
    {
        together += V[i];

        if (!touching)
        {
            double slopeX = 0.0;
            double slopeZ = 0.0;

            const double top = ground_ ? ground_(X[i].x, X[i].z, slopeX, slopeZ) : 0.0;

            touching = W[i] == 0.0 || X[i].y <= top + 1.001;
        }
    }

    together = together / static_cast<double>(n);

    together.y -= dt;

    {
        const double limit = (touching ? MaxStep : MaxFall) / dt;
        const double mean = together.length();

        if (mean > limit)
            together *= limit / mean;
    }

    for (int i = 0; i < n; ++i)
    {
        if (i == held_)
        {
            Vec3 move = handEased_ - X[i];

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

        // Falling is free: the only thing that slows the rope in the air is
        // the air, and for a rope that is too little to see. What takes the
        // energy out is inside the rope and in its contacts.
        V[i].y -= dt;

        Vec3 own = V[i] - together;

        const double speed = own.length();

        if (speed * dt > MaxStep)
            own *= MaxStep / (speed * dt);

        V[i] = together + own;

        P[i] = X[i] + V[i] * dt;
    }

    for (int i = 0; i < segments; ++i)
    {
        Old[i] = Q[i];

        Vec3 turn = Spin[i] * dt;

        const double angle = turn.length();

        if (angle > MaxTurn)
            turn *= MaxTurn / angle;

        turnBy(Q[i], turn);
    }

    if (rodAge_ < 0 || ++rodAge_ >= RodFactorSteps)
        factorRod();

    std::fill(lambda_.begin(), lambda_.end(), Vec3(0.0, 0.0, 0.0));
    std::fill(groundDepth_.begin(), groundDepth_.end(), 0.0);

    if (travelled_ > PairMargin || pairs_.isEmpty())
        findPairs();

    /*
     * The rod and the contacts in turn. They ask for different things; the
     * last word is the contacts', so that no strand ever passes through
     * another.
     */
    for (int pass = 0; pass < Passes; ++pass)
    {
        solveRod();
        solveContacts();
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

    for (int i = 0; i < segments; ++i)
    {
        Quaternion change = multiply(Q[i], conjugate(Old[i]));

        if (change.w < 0.0)
            change = Quaternion{-change.w, -change.x, -change.y, -change.z};

        Spin[i] = Vec3(change.x, change.y, change.z) * (2.0 / dt);
    }

    /*
     * The rope's own damping: the fibres rub against each other as it
     * bends, so motion of one part against the next dies away. Only that:
     * a curved rope turning as a solid body has neighbours moving apart in
     * space but no fibre sliding on another, so the rope's motion as a solid
     * is taken out first and given back afterwards, whole. The rubbing has
     * moved a little of the rest into that motion, and that is taken out as
     * well: what is left is smaller than before and at right angles to the
     * solid motion. On the velocities once the step is over, so it takes
     * energy out and, but for a whisper at a point held still, none in.
     */
    {
        Vec3 centre;
        Vec3 drift;
        Vec3 turn;

        rigidMotion(X, V, n, centre, drift, turn);

        for (int i = 0; i < n; ++i)
            V[i] -= drift + Vec3::cross(turn, X[i] - centre);

        rubAlong(V, n, closed_, fibreRub_, W);
        rubAlong(Spin, segments, closed_, fibreRub_, nullptr);

        Vec3 moved;
        Vec3 leaked;
        Vec3 twist;

        rigidMotion(X, V, n, moved, leaked, twist);

        for (int i = 0; i < n; ++i)
            V[i] += drift + Vec3::cross(turn, X[i] - centre) - leaked - Vec3::cross(twist, X[i] - moved);
    }

    /*
     * Natural fibre takes a set: bent further than its yield, the rope
     * creeps towards the new shape and keeps part of it when let go. The
     * rest shape follows the bending (not the twist), in the frame of the
     * piece, so it travels with the rope. Creep is slow: over the seconds
     * this watches it only shows while the rope is being moved. A rope
     * lying still has stopped taking it, or it would go on crawling for
     * seconds as its shape gave way under it.
     */
    if (setRate_ > 0.0)
    {
        Vec3 *Shape = shape_.data();

        for (int i = 0; i < (closed_ ? segments : segments - 1); ++i)
        {
            const int after = (i + 1) % segments;

            Quaternion turn = multiply(conjugate(Q[i]), Q[after]);

            if (turn.w < 0.0)
                turn = Quaternion{-turn.w, -turn.x, -turn.y, -turn.z};

            const double bx = 2.0 * turn.x - Shape[i].x;
            const double by = 2.0 * turn.y - Shape[i].y;

            const double amount = std::sqrt(bx * bx + by * by);

            if (amount <= yieldAngle_)
                continue;

            const double moving = std::min(V[(i + 1) % n].length() / SetSpeed, 1.0);
            const double creep = moving * setRate_ * dt * (amount - yieldAngle_) / amount;

            Shape[i].x += bx * creep;
            Shape[i].y += by * creep;
        }
    }

    travelled_ += 2.0 * fastest_ * dt;

    if (++windowSteps_ >= RestWindow)
    {
        double furthest = 0.0;

        for (int i = 0; i < n; ++i)
        {
            if (W[i] != 0.0)
                furthest = std::max(furthest, (X[i] - windowStart_.at(i)).lengthSquared());
        }

        windowStart_ = x_;
        windowSteps_ = 0;

        drift_ = std::sqrt(furthest) / (RestWindow * dt);

        // (a rope in the hand is never at rest)
        quiet_ = drift_ < RestSpeed && held_ < 0 ? quiet_ + RestWindow : 0;
    }
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

QVector<Vec3> RopeInHand::points(quint64 *version, QVector<Vec3> *frames) const
{
    const std::lock_guard<std::mutex> lock(mutex_);

    if (version != nullptr)
        *version = version_;

    if (frames != nullptr)
        *frames = rope_.frames();

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

void RopeInHand::cut(int first, int count)
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);

        rope_.cut(first, count);

        ++version_;
        stopped_ = false;
    }

    wake_.notify_all();
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

// a cut end: nothing holds the strands together over the last of the rope
// (radii), so they open this much there
constexpr double EndFlareArc = 1.5;
constexpr double EndFlare = 0.14;

// the face of a cut end, in the texture coordinate w: the shader draws its
// fibres seen end on
constexpr float EndGrain = -0.25f;

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

    // a cut end has a tuft of fibre ends standing out of it, shared by the two
    const int tufts = closed ? 0 : (quick ? 300 : 900);
    const double shortest = braided ? 0.1 : 0.16;
    const double longest = braided ? 0.5 : 1.3;
    const double thickness = braided ? 0.009 : 0.012;

    const int along = std::clamp(static_cast<int>(density * length / radius), 0, 40000);
    const int count = along + tufts;

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

            const bool tuft = f >= along;

            const int ring = tuft ? ((f - along) % 2 == 0 ? 0 : n - 1) : std::min(static_cast<int>(random() * n), n - 1);

            // on top of a strand or yarn, never down in a groove
            int segment = std::min(static_cast<int>(random() * segments), segments - 1);

            for (int attempt = 0; attempt < 6; ++attempt)
            {
                if (texcoords[4 * (ring * segments + segment) + 3] < 0.35f)
                    break;

                segment = std::min(static_cast<int>(random() * segments), segments - 1);
            }

            const int root = ring * segments + segment;

            Vec3 base(positions[3 * root], positions[3 * root + 1], positions[3 * root + 2]);
            Vec3 normal = Vec3(normals[3 * root], normals[3 * root + 1], normals[3 * root + 2]).normalized();

            const int before = closed ? (ring - 1 + n) % n : std::max(ring - 1, 0);
            const int after = closed ? (ring + 1) % n : std::min(ring + 1, n - 1);

            Vec3 onward = centreAt(after) - centreAt(before);

            onward = (onward - normal * Vec3::dot(onward, normal)).normalized();

            Vec3 side = Vec3::cross(normal, onward);

            // either way along the rope, turned a little off it, lifting off
            const double turn = (random() - 0.5) * (braided ? 1.6 : 1.0);
            const double lift = 0.12 + 0.55 * random() * random();
            const double way = random() < 0.5 ? -1.0 : 1.0;

            Vec3 direction =
                (onward * (way * std::cos(turn)) + side * std::sin(turn)) * std::cos(lift) +
                normal * std::sin(lift);

            // most fibres short, a few long (the longest fibre ends of hemp)
            const double u = random();

            double fibre = (shortest + (longest - shortest) * u * u * u) * radius;

            // out of the face of a cut end, spreading from its middle: the
            // nearer the rim, the more
            if (tuft)
            {
                const Vec3 middle = centreAt(ring);
                const Vec3 out = (ring == 0 ? centreAt(0) - centreAt(1) : centreAt(n - 1) - centreAt(n - 2)).normalized();

                const Vec3 rim = base - middle;
                const double place = std::sqrt(random());

                base = middle + rim * place;
                normal = out;
                side = Vec3::cross(out, rim.normalized());

                direction = (out + rim.normalized() * (0.1 + 0.8 * place * random()) + side * (random() - 0.5)).normalized();

                fibre = (0.35 + 0.65 * random()) * fibre;
            }

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
    bool quick,
    const QVector<Vec3> &frames)
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

    /*
     * Where the material has turned to, along the rope: the given directions
     * by the fraction of the length, for every ring.
     */
    QVector<Vec3> ringFrames;

    if (frames.size() == curve.size() && curve.size() >= 4)
    {
        QVector<double> arc(curve.size(), 0.0);

        for (int i = 1; i < curve.size(); ++i)
            arc[i] = arc.at(i - 1) + (curve.at(i) - curve.at(i - 1)).length();

        const double whole =
            closed ? arc.last() + (curve.first() - curve.last()).length() : arc.last();

        ringFrames.resize(n);

        int at = 0;

        for (int i = 0; i < n; ++i)
        {
            const double s = whole * mesh.centerlineArc.at(i) / std::max(double(total), 1.0e-9);

            while (at + 1 < curve.size() && arc.at(at + 1) <= s)
                ++at;

            const int next = closed ? (at + 1) % curve.size() : std::min(at + 1, int(curve.size()) - 1);
            const double end = at + 1 < curve.size() ? arc.at(at + 1) : whole;
            const double f = std::clamp((s - arc.at(at)) / std::max(end - arc.at(at), 1.0e-12), 0.0, 1.0);

            ringFrames[i] = frames.at(at) * (1.0 - f) + frames.at(next) * f;
        }
    }

    /*
     * No rope is even. Its strands are a little thicker and thinner along
     * their length and the lay tightens and slackens, each strand on its
     * own: waves of whole numbers along a closed rope, so that it still
     * closes up.
     */
    const auto wave = [phaseScale, turns](double s, double perTurn, double offset)
    {
        return std::sin(std::round(turns * perTurn) / turns * s * phaseScale + offset);
    };

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

            double phase = s * phaseScale + 0.09 * wave(s, 0.37, 0.4) + 0.05 * wave(s, 1.13, 2.2);

            // how far the strands open here (a cut end)
            double flare = 1.0;

            if (!closed)
            {
                const double open = 1.0 - std::min(s, total - s) / (EndFlareArc * radius);

                if (open > 0.0)
                    flare = 1.0 + EndFlare * open * open;
            }

            // how far the material has turned here
            if (!ringFrames.isEmpty())
            {
                const Vec3 first(normals[3 * i * segments], normals[3 * i * segments + 1],
                                 normals[3 * i * segments + 2]);

                const int quarter = i * segments + segments / 4;

                const Vec3 second(normals[3 * quarter], normals[3 * quarter + 1], normals[3 * quarter + 2]);

                phase += std::atan2(Vec3::dot(ringFrames.at(i), second), Vec3::dot(ringFrames.at(i), first));
            }

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

                    across = sample.across;
                    which = sample.which;
                    groove = sample.groove;

                    // this strand, thicker or thinner here
                    rho = sample.rho * (1.0 + 0.016 * wave(s, 0.31, 1.7 + 2.1 * which) +
                                        0.009 * wave(s, 0.83, 4.1 * which));

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

                const Vec3 position = centre + radial * (rho * flare * radius);

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
     * The face of a cut end was made for a round tube, with a tube's texture
     * coordinates: it is laid over the end of the strands instead, and marked
     * for the shader.
     */
    if (!closed)
    {
        for (int cap = 0; cap < 2; ++cap)
        {
            const int ring = cap == 0 ? 0 : n - 1;
            const int middle = ringVertices + cap * (segments + 1);

            const auto vertex = [positions](int v)
            {
                return Vec3(positions[3 * v], positions[3 * v + 1], positions[3 * v + 2]);
            };

            // places on the face, in radii
            const Vec3 centre = vertex(middle);
            const Vec3 across = (vertex(ring * segments) - centre).normalized();
            const Vec3 out(normals[3 * middle], normals[3 * middle + 1], normals[3 * middle + 2]);
            const Vec3 up = Vec3::cross(out, across);

            for (int j = -1; j < segments; ++j)
            {
                const int v = middle + 1 + j;
                const int rim = ring * segments + std::max(j, 0);

                if (j >= 0)
                {
                    positions[3 * v] = positions[3 * rim];
                    positions[3 * v + 1] = positions[3 * rim + 1];
                    positions[3 * v + 2] = positions[3 * rim + 2];
                }

                const Vec3 place = vertex(v) - centre;

                texcoords[4 * v] = float(Vec3::dot(place, across) / radius);
                texcoords[4 * v + 1] = float(Vec3::dot(place, up) / radius);
                texcoords[4 * v + 2] = texcoords[4 * rim + 2];
                texcoords[4 * v + 3] = EndGrain;
            }
        }
    }

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
