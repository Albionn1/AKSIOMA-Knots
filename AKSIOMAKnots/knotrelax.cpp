#include "knotrelax.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace KnotMath
{

namespace
{

constexpr double Pi = 3.1415926535897932384626433832795028841971693993751;

// number of points of the relaxed polygon (two tries if resampling to the
// first count would change the knot)
constexpr int pointCounts[] = {150, 300};

// Relax: springs, stiffness, time step and largest step. The curve counts
// as settled once, over `settledWindow` steps, no point has come further
// than `settledChange` from the curve as it was (the points keep sliding
// along the curve and jitter a little, which does not change its shape).
constexpr double relaxSpring = 1.0;
constexpr double relaxBending = 0.25;
constexpr double relaxTimeStep = 0.08;
constexpr double relaxMaxStep = 0.25;
constexpr double relaxContactStrength = 1.0;
constexpr double relaxContactRange = 0.8;
constexpr double settledChange = 0.04;
constexpr int settledWindow = 250;

// ... or after this many relaxing steps in any case
constexpr int longestRelax = 30000;

// edges (normally of length about 1) are kept at least this long
constexpr double shortestEdge = 0.3;

// Scramble: swirls, squeeze, short-range repulsion within this distance
// (at least; more for a thick tube). The pushes are smoothed along the
// curve so that neighbouring points move together, and the stiffness
// passes keep the bends round enough for the tube.
constexpr int swirlCount = 6;
constexpr double scrambleMaxStep = 0.3;
constexpr double scrambleSwirlStep = 0.25;
constexpr double scrambleSqueeze = 0.012;
constexpr double scrambleRepulsionRange = 1.4;
constexpr double scrambleLongestRange = 6.0;
constexpr int scrambleSmoothing = 3;
constexpr int scrambleStiffness = 16;
constexpr double scrambleRod = 0.08;
constexpr double scrambleSpring = 0.15;

// Step limits are spread along the curve over this many points on each
// side, so that a point held back by a nearby strand slows its neighbours
// down too instead of leaving a kink.
constexpr int limitSpread = 3;

// Smallest distance between the segments p0-p1 and q0-q1, reached at
// p0 + s (p1 - p0) and q0 + t (q1 - q0) (C. Ericson, Real-Time Collision
// Detection, 5.1.9).
double segmentDistance(
    const Vec3 &p0, const Vec3 &p1, const Vec3 &q0, const Vec3 &q1,
    double &s, double &t)
{
    const Vec3 d1 = p1 - p0;
    const Vec3 d2 = q1 - q0;
    const Vec3 r = p0 - q0;

    const double a = Vec3::dot(d1, d1);
    const double e = Vec3::dot(d2, d2);
    const double f = Vec3::dot(d2, r);

    constexpr double tiny = 1.0e-14;

    s = 0.0;
    t = 0.0;

    if (a <= tiny && e <= tiny)
        return r.length();

    if (a <= tiny)
    {
        t = std::clamp(f / e, 0.0, 1.0);
    }
    else
    {
        const double c = Vec3::dot(d1, r);

        if (e <= tiny)
        {
            s = std::clamp(-c / a, 0.0, 1.0);
        }
        else
        {
            const double b = Vec3::dot(d1, d2);
            const double denominator = a * e - b * b;

            s = denominator > tiny ? std::clamp((b * f - c * e) / denominator, 0.0, 1.0) : 0.0;
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
    }

    return ((p0 + d1 * s) - (q0 + d2 * t)).length();
}

// v turned by `angle` about the unit vector `axis` (Rodrigues)
Vec3 rotated(const Vec3 &v, const Vec3 &axis, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    return v * c + Vec3::cross(axis, v) * s + axis * (Vec3::dot(axis, v) * (1.0 - c));
}

// cyclic distance of two indices of a closed polygon with n points
int cyclicGap(int i, int j, int n)
{
    const int d = std::abs(i - j);
    return std::min(d, n - d);
}

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
    *this = KnotRelaxation();

    if (curve.size() < 4)
    {
        error_ = tr("The curve has too few points.");
        return false;
    }

    for (const int count : pointCounts)
    {
        const QVector<Vec3> resampled = resampleClosedCurve(curve, count, true);

        if (resampled.size() < 4 || !sameAlexander(curve, resampled))
            continue;

        points_ = resampled;
        normalise();
        measureGaps(0.0);

        if (gap_ <= 1.0e-9)
        {
            points_.clear();
            continue;
        }

        return true;
    }

    error_ = tr("Strands of this curve come too close to each other to "
                     "move it safely.");
    return false;
}

bool KnotRelaxation::isValid() const
{
    return !points_.isEmpty();
}

QString KnotRelaxation::error() const
{
    return error_;
}

void KnotRelaxation::beginScramble(quint32 seed)
{
    if (!isValid())
        return;

    random_.seed(seed);

    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    std::uniform_real_distribution<double> angle(0.0, 2.0 * Pi);

    auto randomDirection = [&]()
    {
        Vec3 v;

        do
        {
            v = Vec3(unit(random_), unit(random_), unit(random_));
        } while (v.lengthSquared() < 0.01 || v.lengthSquared() > 1.0);

        return v.normalized();
    };

    scrambleRadius_ = 0.0;

    for (const Vec3 &p : points_)
        scrambleRadius_ = std::max(scrambleRadius_, p.length());

    swirls_.clear();

    for (int i = 0; i < swirlCount; ++i)
    {
        Swirl swirl;

        // wavelengths about the size of the knot; the amplitude is
        // perpendicular to the wave, so every swirl only turns and shears
        const Vec3 direction = randomDirection();

        swirl.wave = direction * (2.0 * Pi / (scrambleRadius_ * (0.8 + 0.8 * std::abs(unit(random_)))));
        swirl.amplitude = Vec3::cross(direction, randomDirection()).normalized();
        swirl.phase = angle(random_);
        swirl.speed = 0.01 + 0.02 * std::abs(unit(random_));

        swirls_.push_back(swirl);
    }

    scrambleLeft_ = scrambleSteps();
    resetSettling();
}

bool KnotRelaxation::scrambling() const
{
    return scrambleLeft_ > 0;
}

double KnotRelaxation::scrambleProgress() const
{
    return 1.0 - static_cast<double>(scrambleLeft_) / scrambleSteps();
}

int KnotRelaxation::advance(double milliseconds, int scrambleLimit)
{
    if (!isValid())
        return 0;

    QElapsedTimer timer;
    timer.start();

    int done = 0;
    int scrambled = 0;

    do
    {
        if (scrambling())
        {
            if (scrambled >= scrambleLimit)
                break;

            scrambleStep();
            --scrambleLeft_;
            ++scrambled;
        }
        else if (!settled())
        {
            relaxStep();
        }
        else
        {
            break;
        }

        ++done;
        ++steps_;
    } while (timer.nsecsElapsed() < milliseconds * 1.0e6);

    return done;
}

bool KnotRelaxation::settled() const
{
    return !scrambling() && settled_;
}

void KnotRelaxation::resetSettling()
{
    settled_ = false;
    windowSteps_ = 0;
    windowStart_.clear();
    relaxSteps_ = 0;
}

bool KnotRelaxation::bestViewRotation(Vec3 &axis, double &angle) const
{
    if (!isValid())
        return false;

    const QVector<Vec3> current = curve();
    const Vec3 z(0.0, 0.0, 1.0);

    int fewest = detectProjectionCrossings(current, 2, 700).size();

    axis = Vec3(1.0, 0.0, 0.0);
    angle = 0.0;

    // directions spread evenly over the upper half sphere
    constexpr int count = 90;

    const double golden = Pi * (3.0 - std::sqrt(5.0));

    for (int i = 0; i < count; ++i)
    {
        const double h = (i + 0.5) / count;
        const double r = std::sqrt(std::max(0.0, 1.0 - h * h));
        const double phi = golden * i;

        const Vec3 direction(r * std::cos(phi), r * std::sin(phi), h);

        // rotation that takes `direction` to z
        const Vec3 a = Vec3::cross(direction, z);
        const double s = a.length();

        if (s < 1.0e-9)
            continue;

        const Vec3 candidateAxis = a / s;
        const double candidateAngle = std::atan2(s, Vec3::dot(direction, z));

        QVector<Vec3> turned = current;

        for (Vec3 &p : turned)
            p = rotated(p, candidateAxis, candidateAngle);

        const int crossings = detectProjectionCrossings(turned, 2, 700).size();

        // fewer crossings, or as many with a smaller turn
        if (crossings < fewest ||
            (crossings == fewest && angle > 0.0 && candidateAngle < angle))
        {
            fewest = crossings;
            axis = candidateAxis;
            angle = candidateAngle;
        }
    }

    return angle > 0.0;
}

void KnotRelaxation::rotate(const Vec3 &axis, double angle)
{
    // a rigid motion: strands keep their distances
    for (Vec3 &p : points_)
        p = rotated(p, axis, angle);
}

int KnotRelaxation::steps() const
{
    return steps_;
}

QVector<Vec3> KnotRelaxation::curve() const
{
    QVector<Vec3> result = points_;

    if (result.isEmpty())
        return result;

    Vec3 centre;

    for (const Vec3 &p : result)
        centre += p;

    centre = centre / static_cast<double>(result.size());

    double extent = 0.0;

    for (const Vec3 &p : result)
        extent = std::max(extent, (p - centre).length());

    const double scale = extent > 1.0e-12 ? 2.7 / extent : 1.0;

    for (Vec3 &p : result)
        p = (p - centre) * scale;

    return result;
}

void KnotRelaxation::setTubeRadius(double radius)
{
    tubeRadius_ = std::max(0.0, radius);
}

double KnotRelaxation::displayScale() const
{
    double extent = 0.0;

    Vec3 centre;

    for (const Vec3 &p : points_)
        centre += p;

    centre = centre / std::max(1, static_cast<int>(points_.size()));

    for (const Vec3 &p : points_)
        extent = std::max(extent, (p - centre).length());

    return extent > 1.0e-12 ? 2.7 / extent : 1.0;
}

double KnotRelaxation::tubeLimit(double requested) const
{
    const int n = points_.size();

    if (n < 4 || requested <= 0.0)
        return 0.0;

    const double scale = displayScale();

    // the requested radius in the coordinates of points_ (edges about 1)
    const double wanted = requested / scale;

    // A tube folds into itself where the curve bends tighter than its
    // radius. The bend is measured over two edges (a single sharp corner
    // of the polygon is rounded off by the spline that is drawn).
    double sharpest = std::numeric_limits<double>::max();

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &a = points_.at((i + n - 2) % n);
        const Vec3 &b = points_.at(i);
        const Vec3 &c = points_.at((i + 2) % n);

        const Vec3 u = b - a;
        const Vec3 v = c - b;

        const double lu = u.length();
        const double lv = v.length();

        if (lu < 1.0e-12 || lv < 1.0e-12)
            continue;

        const double turn = std::acos(std::clamp(Vec3::dot(u, v) / (lu * lv), -1.0, 1.0));

        if (turn > 1.0e-6)
            sharpest = std::min(sharpest, 0.5 * (lu + lv) / turn);
    }

    // Pieces closer than this along the curve belong to the same stretch
    // of tube and may overlap.
    const int skip = std::clamp(static_cast<int>(std::ceil(3.0 * wanted)), 3, std::max(3, n / 4));

    double gap = std::numeric_limits<double>::max();

    // a bound for the distance of the segments i and j, as in measureGaps()
    QVector<Vec3> middle(n);
    QVector<double> half(n);

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &a = points_.at(i);
        const Vec3 &b = points_.at((i + 1) % n);

        middle[i] = (a + b) * 0.5;
        half[i] = (b - a).length() * 0.5;
    }

    for (int i = 0; i < n; ++i)
    {
        for (int j = i + skip; j < n; ++j)
        {
            if (cyclicGap(i, j, n) < skip)
                continue;

            if ((middle.at(i) - middle.at(j)).length() - half.at(i) - half.at(j) >= gap)
                continue;

            double s = 0.0;
            double t = 0.0;

            gap = std::min(
                gap,
                segmentDistance(
                    points_.at(i), points_.at((i + 1) % n),
                    points_.at(j), points_.at((j + 1) % n),
                    s, t));
        }
    }

    // tubes of radius r around two segments touch at distance 2r; the
    // spline drawn through the points bulges a little, hence 0.45
    const double limit = std::min(0.45 * gap, 0.9 * sharpest);

    return std::min(requested, limit * scale);
}


void KnotRelaxation::relaxStep()
{
    const int n = points_.size();

    measureGaps(relaxContactRange);

    QVector<Vec3> force(n);

    // Strands that nearly touch between two points are pushed apart too:
    // otherwise the springs and the smoothing can press one strand against
    // another, and the safe steps would only bring them ever closer.
    addContactRepulsion(force, relaxContactStrength, relaxContactRange);

    // repulsion between points that are not close along the curve
    for (int i = 0; i < n; ++i)
    {
        for (int j = i + 1; j < n; ++j)
        {
            if (cyclicGap(i, j, n) < 3)
                continue;

            const Vec3 d = points_.at(i) - points_.at(j);
            const double r2 = std::max(d.lengthSquared(), 1.0e-6);
            const Vec3 f = d / (r2 * r2);

            force[i] += f;
            force[j] -= f;
        }
    }

    addSpringsAndBending(force, relaxSpring, relaxBending);

    for (Vec3 &f : force)
        f *= relaxTimeStep;

    applyMoves(force, relaxMaxStep);

    normalise();

    ++relaxSteps_;

    if (windowStart_.isEmpty())
        windowStart_ = points_;

    if (++windowSteps_ >= settledWindow)
    {
        settled_ = shapeChange(windowStart_) < settledChange || relaxSteps_ >= longestRelax;

        windowStart_ = points_;
        windowSteps_ = 0;
    }
}

double KnotRelaxation::shapeChange(const QVector<Vec3> &before) const
{
    const int n = before.size();

    if (n != points_.size())
        return std::numeric_limits<double>::max();

    double change = 0.0;

    for (const Vec3 &p : points_)
    {
        double nearest = std::numeric_limits<double>::max();

        for (int i = 0; i < n; ++i)
        {
            double s = 0.0;
            double t = 0.0;

            nearest = std::min(
                nearest,
                segmentDistance(p, p, before.at(i), before.at((i + 1) % n), s, t));
        }

        change = std::max(change, nearest);
    }

    return change;
}

void KnotRelaxation::scrambleStep()
{
    const int n = points_.size();
    const int step = scrambleSteps() - scrambleLeft_;

    // Strands keep a little more than a tube diameter apart (tubes of
    // radius r touch at distance 2r, and tubeLimit() allows 0.45 x gap).
    const double thickness = 2.4 * tubeRadius_ / displayScale();

    const double range =
        std::clamp(thickness, scrambleRepulsionRange, scrambleLongestRange);

    measureGaps(range);

    QVector<Vec3> force(n);

    // swirls that slowly change
    double strongest = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &p = points_.at(i);

        for (const Swirl &swirl : swirls_)
        {
            const double phase = Vec3::dot(swirl.wave, p) + swirl.phase + swirl.speed * step;
            force[i] += swirl.amplitude * std::sin(phase);
        }

        strongest = std::max(strongest, force.at(i).length());
    }

    // The whole field is scaled at once so that it stays below the largest
    // step. (Cutting every point down to the largest step on its own would
    // keep only the directions, and where the field changes sign along the
    // curve two neighbours would be pushed apart at full speed: a corner.)
    const double swirlScale =
        strongest > 1.0e-12 ? scrambleSwirlStep / strongest : 0.0;

    for (int i = 0; i < n; ++i)
    {
        force[i] *= swirlScale;

        // squeeze towards the centre
        force[i] -= points_.at(i) * scrambleSqueeze;
    }

    // Short-range repulsion keeps room for the tube between strands. The
    // strength grows with the range, so that it is about as strong at the
    // same fraction of the range for any thickness. Segments closer than
    // about the range along the curve are only neighbours on the same
    // stretch of rope.
    QVector<Vec3> push(n);

    addContactRepulsion(
        push,
        0.6 * range * range,
        range,
        static_cast<int>(std::ceil(1.5 * range)) + 1);

    // capped, so that one close contact cannot yank single points out of
    // the rope (the smoothing below spreads the push over its neighbours)
    for (int i = 0; i < n; ++i)
    {
        const double length = push.at(i).length();

        force[i] += length > 1.0 ? push.at(i) / length : push.at(i);
    }

    // neighbouring points move together (no jitter, no kinks)
    for (int pass = 0; pass < scrambleSmoothing; ++pass)
    {
        const QVector<Vec3> rough = force;

        for (int i = 0; i < n; ++i)
        {
            force[i] =
                rough.at(i) * 0.5 +
                (rough.at((i + n - 1) % n) + rough.at((i + 1) % n)) * 0.25;
        }
    }

    // Stiffness: the swirls shear neighbouring points apart faster than a
    // single bending force could undo, and the curve would fold into
    // corners far tighter than the tube. So the place every point is
    // pushed to is straightened a few times like an elastic rod (the
    // fourth difference x[i-2] - 4 x[i-1] + 6 x[i] - 4 x[i+1] + x[i+2]):
    // it takes out corners a few points long almost at once but hardly
    // touches loops, which smoothing with the second difference would pull
    // tight around the other strands. Weak springs keep the spacing even.
    // (Gains 16 x rod + 4 x spring below 2: a zigzag dies out instead of
    // growing.)
    QVector<Vec3> target(n);

    for (int i = 0; i < n; ++i)
        target[i] = points_.at(i) + force.at(i);

    for (int pass = 0; pass < scrambleStiffness; ++pass)
    {
        const QVector<Vec3> rough = target;

        for (int i = 0; i < n; ++i)
        {
            const Vec3 &a = rough.at((i + n - 2) % n);
            const Vec3 &b = rough.at((i + n - 1) % n);
            const Vec3 &c = rough.at(i);
            const Vec3 &d = rough.at((i + 1) % n);
            const Vec3 &e = rough.at((i + 2) % n);

            target[i] -= (a - b * 4.0 + c * 6.0 - d * 4.0 + e) * scrambleRod;

            // springs of the two edges at this point, towards length 1
            for (const Vec3 *neighbour : {&b, &d})
            {
                const Vec3 edge = *neighbour - c;
                const double length = edge.length();

                if (length > 1.0e-12)
                    target[i] += edge * (scrambleSpring * (length - 1.0) / length);
            }
        }
    }

    for (int i = 0; i < n; ++i)
        force[i] = target.at(i) - points_.at(i);

    applyMoves(force, scrambleMaxStep);

    // keep the centre and the length (the squeeze must crumple the curve,
    // not shrink it)
    normalise();
}

void KnotRelaxation::applyMoves(QVector<Vec3> &move, double maxStep)
{
    const int n = points_.size();

    removeRigidMotion(move);

    // Every point moves at most 0.4 times the distance from its two
    // segments to any segment that does not share a point with them (see
    // the class comment); measureGaps() has filled segmentGap_ for the
    // current points.
    QVector<double> factor(n, 1.0);

    for (int i = 0; i < n; ++i)
    {
        const double room = std::min(segmentGap_.at((i + n - 1) % n), segmentGap_.at(i));
        const double allowed = std::min(maxStep, 0.4 * room);
        const double length = move.at(i).length();

        if (length > allowed)
            factor[i] = allowed / length;
    }

    // Spread the limits along the curve: the smallest factor within
    // 2 x limitSpread points, then the average over limitSpread points on
    // each side. Every factor in that average is the smallest one of a
    // window that contains point i, so it is at most factor i: the limit
    // above still holds, but a point held back no longer leaves a kink.
    if (n > 4 * limitSpread + 2)
    {
        QVector<double> lowest(n);

        for (int i = 0; i < n; ++i)
        {
            double value = factor.at(i);

            for (int k = -2 * limitSpread; k <= 2 * limitSpread; ++k)
                value = std::min(value, factor.at(((i + k) % n + n) % n));

            lowest[i] = value;
        }

        for (int i = 0; i < n; ++i)
        {
            double sum = 0.0;

            for (int k = -limitSpread; k <= limitSpread; ++k)
                sum += lowest.at(((i + k) % n + n) % n);

            factor[i] = sum / (2 * limitSpread + 1);
        }
    }

    for (int i = 0; i < n; ++i)
        move[i] *= factor.at(i);

    // No edge may shrink below `shortestEdge` (or by more than 10 % if it
    // is already that short): an edge of length 0 would make the distance
    // between its two neighbours 0 and freeze the curve.
    auto edgesKeepLength = [&](double s)
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = (i + 1) % n;

            const double before = (points_.at(j) - points_.at(i)).length();
            const double after =
                ((points_.at(j) + move.at(j) * s) - (points_.at(i) + move.at(i) * s)).length();

            if (after < before && after < std::min(shortestEdge, 0.9 * before))
                return false;
        }

        return true;
    };

    double scale = 1.0;
    int halvings = 0;

    while (!edgesKeepLength(scale))
    {
        if (++halvings > 12)
            return;

        scale *= 0.5;
    }

    for (int i = 0; i < n; ++i)
        points_[i] += move.at(i) * scale;
}

void KnotRelaxation::removeRigidMotion(QVector<Vec3> &move) const
{
    const int n = points_.size();

    if (n == 0)
        return;

    Vec3 centre;
    Vec3 shift;

    for (int i = 0; i < n; ++i)
    {
        centre += points_.at(i);
        shift += move.at(i);
    }

    centre = centre / static_cast<double>(n);
    shift = shift / static_cast<double>(n);

    // angular momentum L = sum r x m and inertia tensor I = sum |r|^2 E - r r^T
    Vec3 momentum;
    double inertia[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};

    for (int i = 0; i < n; ++i)
    {
        const Vec3 r = points_.at(i) - centre;
        const double c[3] = {r.x, r.y, r.z};
        const double r2 = r.lengthSquared();

        momentum += Vec3::cross(r, move.at(i) - shift);

        for (int a = 0; a < 3; ++a)
        {
            for (int b = 0; b < 3; ++b)
                inertia[a][b] += (a == b ? r2 : 0.0) - c[a] * c[b];
        }
    }

    // turn rate w = I^-1 L (Cramer's rule)
    auto determinant = [](const double m[3][3])
    {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
               m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    };

    const double det = determinant(inertia);

    Vec3 turn;

    if (std::abs(det) > 1.0e-12)
    {
        const double l[3] = {momentum.x, momentum.y, momentum.z};
        double w[3];

        for (int column = 0; column < 3; ++column)
        {
            double m[3][3];

            for (int a = 0; a < 3; ++a)
            {
                for (int b = 0; b < 3; ++b)
                    m[a][b] = b == column ? l[a] : inertia[a][b];
            }

            w[column] = determinant(m) / det;
        }

        turn = Vec3(w[0], w[1], w[2]);
    }

    for (int i = 0; i < n; ++i)
        move[i] -= shift + Vec3::cross(turn, points_.at(i) - centre);
}

void KnotRelaxation::measureGaps(double contactRange)
{
    const int n = points_.size();

    segmentGap_.fill(std::numeric_limits<double>::max(), n);
    contacts_.clear();

    QVector<Vec3> middle(n);
    QVector<double> half(n);

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &a = points_.at(i);
        const Vec3 &b = points_.at((i + 1) % n);

        middle[i] = (a + b) * 0.5;
        half[i] = (b - a).length() * 0.5;
    }

    for (int i = 0; i < n; ++i)
    {
        for (int j = i + 2; j < n; ++j)
        {
            // segments i and j share a point
            if (i == 0 && j == n - 1)
                continue;

            // a lower bound for their distance; skip the pair if it can
            // neither lower a gap nor be a contact
            const double bound =
                (middle.at(i) - middle.at(j)).length() - half.at(i) - half.at(j);

            if (bound >= segmentGap_.at(i) &&
                bound >= segmentGap_.at(j) &&
                bound >= contactRange)
            {
                continue;
            }

            Contact contact;

            contact.i = i;
            contact.j = j;
            contact.distance = segmentDistance(
                points_.at(i), points_.at((i + 1) % n),
                points_.at(j), points_.at((j + 1) % n),
                contact.s, contact.t);

            segmentGap_[i] = std::min(segmentGap_.at(i), contact.distance);
            segmentGap_[j] = std::min(segmentGap_.at(j), contact.distance);

            // segments with one segment between them meet at every bend;
            // springs and smoothing take care of those
            if (contact.distance < contactRange && cyclicGap(i, j, n) >= 3)
                contacts_.push_back(contact);
        }
    }

    gap_ = *std::min_element(segmentGap_.cbegin(), segmentGap_.cend());
}

void KnotRelaxation::addContactRepulsion(
    QVector<Vec3> &force,
    double strength,
    double range,
    int minimumGap) const
{
    const int n = points_.size();
    const double range2 = range * range;

    for (const Contact &contact : contacts_)
    {
        if (cyclicGap(contact.i, contact.j, n) < minimumGap)
            continue;

        const int i1 = (contact.i + 1) % n;
        const int j1 = (contact.j + 1) % n;

        const Vec3 p = points_.at(contact.i) * (1.0 - contact.s) + points_.at(i1) * contact.s;
        const Vec3 q = points_.at(contact.j) * (1.0 - contact.t) + points_.at(j1) * contact.t;

        const double r = std::max(contact.distance, 1.0e-6);
        const Vec3 f = (p - q) * (strength * (1.0 / (r * r) - 1.0 / range2) / r);

        // shared by the ends of each segment like the closest point
        force[contact.i] += f * (1.0 - contact.s);
        force[i1] += f * contact.s;
        force[contact.j] -= f * (1.0 - contact.t);
        force[j1] -= f * contact.t;
    }
}

void KnotRelaxation::addSpringsAndBending(
    QVector<Vec3> &force,
    double spring,
    double bending) const
{
    const int n = points_.size();

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &previous = points_.at((i + n - 1) % n);
        const Vec3 &current = points_.at(i);
        const Vec3 &next = points_.at((i + 1) % n);

        // spring of the edge to the next point, towards length 1
        const Vec3 edge = next - current;
        const double length = edge.length();

        if (length > 1.0e-12)
        {
            const Vec3 f = edge * (spring * (length - 1.0) / length);

            force[i] += f;
            force[(i + 1) % n] -= f;
        }

        force[i] += (previous + next - current * 2.0) * bending;
    }
}

void KnotRelaxation::normalise()
{
    const int n = points_.size();

    if (n == 0)
        return;

    Vec3 centre;
    double length = 0.0;

    for (int i = 0; i < n; ++i)
    {
        centre += points_.at(i);
        length += (points_.at((i + 1) % n) - points_.at(i)).length();
    }

    centre = centre / static_cast<double>(n);

    const double scale = length > 1.0e-12 ? n / length : 1.0;

    for (Vec3 &p : points_)
        p = (p - centre) * scale;

    gap_ *= scale;
}

} // namespace KnotMath
