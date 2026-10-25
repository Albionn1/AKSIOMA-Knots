#include "knotjoin.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace KnotMath
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

QString joinTr(const char *text)
{
    return QCoreApplication::translate("KnotMath", text);
}

int wrap(int index, int count)
{
    return ((index % count) + count) % count;
}

// `count` points evenly spaced along the closed polygon
QVector<Vec3> resampleClosed(const QVector<Vec3> &curve, int count)
{
    const int n = curve.size();

    QVector<double> cumulative(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
        cumulative[i + 1] = cumulative[i] + (curve[(i + 1) % n] - curve[i]).length();

    const double total = cumulative[n];

    QVector<Vec3> result;
    result.reserve(count);

    int segment = 0;

    for (int k = 0; k < count; ++k)
    {
        const double s = total * k / count;

        while (segment < n - 1 && cumulative[segment + 1] < s)
            ++segment;

        const double length = cumulative[segment + 1] - cumulative[segment];
        const double f = length > 0.0 ? (s - cumulative[segment]) / length : 0.0;

        result.push_back(curve[segment] + (curve[(segment + 1) % n] - curve[segment]) * f);
    }

    return result;
}

// centred at the middle of its bounding box
QVector<Vec3> centred(QVector<Vec3> curve)
{
    Vec3 low = curve.first();
    Vec3 high = curve.first();

    for (const Vec3 &p : curve)
    {
        low = Vec3(std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z));
        high = Vec3(std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z));
    }

    const Vec3 centre = (low + high) * 0.5;

    for (Vec3 &p : curve)
        p -= centre;

    return curve;
}

Vec3 turn(const Vec3 &p, double yaw, double tilt)
{
    // about y
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);

    const Vec3 a(p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy);

    // about z
    const double ct = std::cos(tilt);
    const double st = std::sin(tilt);

    return Vec3(a.x * ct - a.y * st, a.x * st + a.y * ct, a.z);
}

double outerRadius(const QVector<Vec3> &curve)
{
    double radius = 0.0;

    for (const Vec3 &p : curve)
        radius = std::max(radius, p.length());

    return radius;
}

/*
 * The arc that sticks out on side `side` (+1: towards +x, -1: towards -x).
 * Measured by h = side * x: beyond the level h > level there must be one
 * arc only, the arc with the highest point, and on it h must rise once and
 * fall once. Nothing else may come within `margin` of the level (only so
 * that the cut is not decided by rounding; how close the other strands
 * come to the band is checked in space later), and the
 * two ends of the cut must be `minimumChord` apart (room for the band).
 */
struct FacingArc
{
    bool found = false;

    // h of the cut, in the knot's own coordinates
    double level = 0.0;

    int peak = 0;
    int cutStart = 0;
    int cutEnd = 0;
};

FacingArc findFacingArc(
    const QVector<Vec3> &curve,
    int side,
    double margin,
    double minimumChord)
{
    const int n = curve.size();
    const double radius = outerRadius(curve);
    const double tolerance = 1e-9 * radius;

    QVector<double> h(n);

    for (int i = 0; i < n; ++i)
        h[i] = side * curve[i].x;

    const int peak =
        static_cast<int>(std::max_element(h.begin(), h.end()) - h.begin());

    const double top = h[peak];

    // a small cut first, deeper ones if the ends are too close together
    for (int step = 0; step < 10; ++step)
    {
        const double level = top - (0.03 + 0.03 * step) * radius;
        const double low = level - margin;

        // the run of points around the peak above `low`
        int back = 0;

        while (back < n && h[wrap(peak - back - 1, n)] > low)
            ++back;

        int ahead = 0;

        while (ahead < n && h[wrap(peak + ahead + 1, n)] > low)
            ++ahead;

        if (back + ahead + 1 >= n)
            continue;

        // nothing else up there
        bool alone = true;

        for (int i = 0; i < n && alone; ++i)
        {
            const int offset = wrap(i - peak, n);

            if (offset <= ahead || offset >= n - back)
                continue;

            if (h[i] > low)
                alone = false;
        }

        if (!alone)
            continue;

        // rising up to the peak, falling after it
        bool single = true;

        for (int k = -back; k < 0 && single; ++k)
        {
            if (h[wrap(peak + k + 1, n)] < h[wrap(peak + k, n)] - tolerance)
                single = false;
        }

        for (int k = 0; k < ahead && single; ++k)
        {
            if (h[wrap(peak + k + 1, n)] > h[wrap(peak + k, n)] + tolerance)
                single = false;
        }

        if (!single)
            continue;

        // the cut: the points above the level
        int cutBack = 0;

        while (h[wrap(peak - cutBack - 1, n)] > level)
            ++cutBack;

        int cutAhead = 0;

        while (h[wrap(peak + cutAhead + 1, n)] > level)
            ++cutAhead;

        // a quarter of the knot at most
        if (cutBack + cutAhead + 1 > n / 4)
            continue;

        FacingArc arc;

        arc.level = level;
        arc.peak = peak;
        arc.cutStart = wrap(peak - cutBack, n);
        arc.cutEnd = wrap(peak + cutAhead, n);

        const double chord =
            (curve[wrap(arc.cutStart - 1, n)] - curve[wrap(arc.cutEnd + 1, n)]).length();

        if (chord < minimumChord)
            continue;

        // The curve must climb towards the cut steeply (at 27 degrees or
        // more) at both ends, so that the band can leave straight outwards;
        // near the very tip it runs almost sideways.
        const int exit = wrap(arc.cutStart - 1, n);
        const int entry = wrap(arc.cutEnd + 1, n);

        const Vec3 towards = (curve[wrap(exit + 1, n)] - curve[wrap(exit - 1, n)]).normalized();
        const Vec3 away = (curve[wrap(entry + 1, n)] - curve[wrap(entry - 1, n)]).normalized();

        if (side * towards.x < 0.45 || side * away.x > -0.45)
            continue;

        arc.found = true;

        return arc;
    }

    return FacingArc();
}

// A knot turned into a pose in which an arc faces `side`.
struct Placement
{
    double yaw = 0.0;
    double tilt = 0.0;

    QVector<Vec3> curve;
    FacingArc arc;
};

// All poses with a facing arc, those that turn the knot least first. The
// knots are seen from the front (along z): turning them in that plane
// (tilt) keeps them face on, turning about y (yaw) only a little.
QVector<Placement> placements(
    const QVector<Vec3> &curve,
    int side,
    double margin,
    double minimumChord)
{
    QVector<Placement> result;

    const double yaws[] = {0.0, 0.35, -0.35, 0.7, -0.7};

    for (const double yaw : yaws)
    {
        for (int k = 0; k < 24; ++k)
        {
            // 0, 15, -15, 30, -30, ... degrees
            const int step = (k + 1) / 2;
            const double tilt = (k % 2 == 1 ? 1.0 : -1.0) * step * Pi / 12.0;

            Placement placement;

            placement.yaw = yaw;
            placement.tilt = tilt;

            placement.curve.reserve(curve.size());

            for (const Vec3 &p : curve)
                placement.curve.push_back(turn(p, yaw, tilt));

            placement.arc = findFacingArc(placement.curve, side, margin, minimumChord);

            if (placement.arc.found)
                result.push_back(placement);
        }
    }

    return result;
}

// Cubic Hermite basis functions.
double h00(double u) { return (1.0 + 2.0 * u) * (1.0 - u) * (1.0 - u); }
double h10(double u) { return u * (1.0 - u) * (1.0 - u); }
double h01(double u) { return u * u * (3.0 - 2.0 * u); }
double h11(double u) { return u * u * (u - 1.0); }

/*
 * The band between the two cuts: a ribbon whose centre line runs straight
 * across the gap and whose two edges (the strands) turn about it from the
 * way the ends lie at the first knot to the way they lie at the second.
 * The strands therefore never meet (they stay on opposite sides of the
 * centre line). Their ends are bent to leave and enter the knots along the
 * curves, so the joins are smooth.
 *
 * strand 0 runs from a0 to b0, strand 1 from a1 to b1; ta0 ... are the
 * directions the strands must have at their ends (from a towards b).
 */
QVector<Vec3> bandStrand(
    int which,
    const Vec3 &a0,
    const Vec3 &a1,
    const Vec3 &b0,
    const Vec3 &b1,
    const Vec3 &ta,
    const Vec3 &tb,
    int count)
{
    const Vec3 m0 = (a0 + a1) * 0.5;
    const Vec3 m1 = (b0 + b1) * 0.5;

    const Vec3 hA = (a0 - a1) * 0.5;
    const Vec3 hB = (b0 - b1) * 0.5;

    const double length = m1.x - m0.x;
    const Vec3 along(length, 0.0, 0.0);

    const double angleA = std::atan2(hA.z, hA.y);
    const double angleB = std::atan2(hB.z, hB.y);

    double angleTurn = angleB - angleA;

    while (angleTurn > Pi)
        angleTurn -= 2.0 * Pi;

    while (angleTurn < -Pi)
        angleTurn += 2.0 * Pi;

    const double radiusA = std::hypot(hA.y, hA.z);
    const double radiusB = std::hypot(hB.y, hB.z);

    const double sign = which == 0 ? 1.0 : -1.0;

    auto ribbon = [&](double u)
    {
        const Vec3 centre =
            m0 * h00(u) + along * h10(u) + m1 * h01(u) + along * h11(u);

        const double s = u * u * (3.0 - 2.0 * u);
        const double angle = angleA + angleTurn * s;
        const double radius = radiusA + (radiusB - radiusA) * s;

        const Vec3 offset(
            hA.x + (hB.x - hA.x) * u,
            radius * std::cos(angle),
            radius * std::sin(angle));

        return centre + offset * sign;
    };

    // directions at the ends: along the knots, at the ribbon's speed
    constexpr double e = 1e-4;

    const Vec3 d0 = (ribbon(e) - ribbon(0.0)) / e;
    const Vec3 d1 = (ribbon(1.0) - ribbon(1.0 - e)) / e;

    const Vec3 want0 = ta.normalized() * d0.length();
    const Vec3 want1 = tb.normalized() * d1.length();

    QVector<Vec3> strand;
    strand.reserve(count + 1);

    for (int k = 0; k <= count; ++k)
    {
        const double u = static_cast<double>(k) / count;

        strand.push_back(
            ribbon(u) + (want0 - d0) * h10(u) + (want1 - d1) * h11(u));
    }

    return strand;
}

double distanceToPoints(const Vec3 &p, const QVector<Vec3> &points)
{
    double best = 1e30;

    for (const Vec3 &q : points)
        best = std::min(best, (p - q).lengthSquared());

    return std::sqrt(best);
}

} // namespace

QVector<Vec3> placeCurve(
    const QVector<Vec3> &curve,
    double yaw,
    double tilt,
    const Vec3 &shift,
    double scale)
{
    QVector<Vec3> result;
    result.reserve(curve.size());

    for (const Vec3 &p : curve)
        result.push_back(turn(p, yaw, tilt) * scale + shift);

    return result;
}

QVector<Vec3> curveWithout(
    const QVector<Vec3> &closedCurve,
    int from,
    int to)
{
    const int n = closedCurve.size();
    const int removed = wrap(to - from, n) + 1;

    QVector<Vec3> result;
    result.reserve(n - removed);

    for (int k = 1; k <= n - removed; ++k)
        result.push_back(closedCurve[wrap(to + k, n)]);

    return result;
}

std::map<int, long long> multiplyPolynomials(
    const QVector<long long> &a,
    int lowestA,
    const QVector<long long> &b,
    int lowestB)
{
    std::map<int, long long> product;

    for (int i = 0; i < a.size(); ++i)
    {
        for (int j = 0; j < b.size(); ++j)
            product[lowestA + i + lowestB + j] += a[i] * b[j];
    }

    for (auto it = product.begin(); it != product.end();)
    {
        if (it->second == 0)
            it = product.erase(it);
        else
            ++it;
    }

    return product;
}

ConnectedSum buildConnectedSum(
    const QVector<Vec3> &first,
    const QVector<Vec3> &second,
    double tubeRadius,
    bool mirrorSecond,
    int samples)
{
    ConnectedSum sum;

    if (first.size() < 4 || second.size() < 4)
    {
        sum.error = joinTr("Both knots must be closed curves.");
        return sum;
    }

    sum.firstBase = centred(resampleClosed(first, samples));
    sum.secondBase = centred(resampleClosed(second, samples));

    if (mirrorSecond)
    {
        for (Vec3 &p : sum.secondBase)
            p.z = -p.z;
    }

    const double radiusA = outerRadius(sum.firstBase);
    const double radiusB = outerRadius(sum.secondBase);

    const double margin = 0.3 * tubeRadius;
    const double minimumChord = 3.5 * tubeRadius;
    const double clearance = 2.2 * tubeRadius;

    const QVector<Placement> left =
        placements(sum.firstBase, +1, margin, minimumChord);

    const QVector<Placement> right =
        placements(sum.secondBase, -1, margin, minimumChord);

    // the gap between the two cut planes
    const double gap = 0.45 * (radiusA + radiusB) + 4.0 * tubeRadius;

    // the poses that turn the knots least first; a few hundred tries at most
    int tries = 0;

    for (const Placement &a : left)
    {
        for (const Placement &b : right)
        {
            if (++tries > 600)
                break;

            const int n = a.curve.size();
            const int m = b.curve.size();

            const int exitA = wrap(a.arc.cutStart - 1, n);
            const int entryA = wrap(a.arc.cutEnd + 1, n);
            const int exitB = wrap(b.arc.cutStart - 1, m);
            const int entryB = wrap(b.arc.cutEnd + 1, m);

            // the cut planes at -gap/2 and +gap/2, the cuts facing each
            // other across the x axis
            const Vec3 middleA = (a.curve[exitA] + a.curve[entryA]) * 0.5;
            const Vec3 middleB = (b.curve[exitB] + b.curve[entryB]) * 0.5;

            const Vec3 shiftA(-0.5 * gap - a.arc.level, -middleA.y, -middleA.z);
            const Vec3 shiftB(0.5 * gap + b.arc.level, -middleB.y, -middleB.z);

            QVector<Vec3> placedA = a.curve;
            QVector<Vec3> placedB = b.curve;

            for (Vec3 &p : placedA)
                p += shiftA;

            for (Vec3 &p : placedB)
                p += shiftB;

            // directions of travel along the curves
            auto direction = [](const QVector<Vec3> &c, int i)
            {
                const int k = c.size();
                return (c[(i + 1) % k] - c[wrap(i - 1, k)]).normalized();
            };

            const double spacing =
                (placedA[1] - placedA[0]).length();

            const int count =
                std::max(8, static_cast<int>(std::ceil(1.3 * gap / spacing)));

            // out: leaves the first knot at its exit, enters the second at
            // its entry; back (built from the first knot's side): from the
            // first knot's entry to the second knot's exit, against the
            // direction of travel
            const QVector<Vec3> out =
                bandStrand(0,
                           placedA[exitA], placedA[entryA],
                           placedB[entryB], placedB[exitB],
                           direction(placedA, exitA), direction(placedB, entryB),
                           count);

            const QVector<Vec3> backward =
                bandStrand(1,
                           placedA[exitA], placedA[entryA],
                           placedB[entryB], placedB[exitB],
                           direction(placedA, entryA) * -1.0,
                           direction(placedB, exitB) * -1.0,
                           count);

            // each strand moves steadily from the first knot to the second
            bool steady = true;

            for (int k = 0; k < count && steady; ++k)
            {
                if (out[k + 1].x <= out[k].x || backward[k + 1].x <= backward[k].x)
                    steady = false;
            }

            if (!steady)
                continue;

            // The strands keep a tube apart, from each other and from the
            // rest of the knots - except from the few points of each knot
            // right where the strands continue it.
            const QVector<Vec3> restA = curveWithout(placedA, a.arc.cutStart, a.arc.cutEnd);
            const QVector<Vec3> restB = curveWithout(placedB, b.arc.cutStart, b.arc.cutEnd);

            const int leg = static_cast<int>(std::ceil(4.0 * tubeRadius / spacing)) + 1;

            auto trimmed = [leg](const QVector<Vec3> &rest)
            {
                return rest.mid(leg, std::max(0, static_cast<int>(rest.size()) - 2 * leg));
            };

            const QVector<Vec3> otherA = trimmed(restA);
            const QVector<Vec3> otherB = trimmed(restB);

            bool clear = true;

            for (int k = 0; k <= count && clear; ++k)
            {
                if ((out[k] - backward[k]).length() < clearance)
                    clear = false;

                for (const QVector<Vec3> *strand : {&out, &backward})
                {
                    const Vec3 &p = strand->at(k);

                    if (distanceToPoints(p, otherA) < clearance ||
                        distanceToPoints(p, otherB) < clearance)
                    {
                        clear = false;
                    }
                }
            }

            if (!clear)
                continue;

            sum.firstPose.yaw = a.yaw;
            sum.firstPose.tilt = a.tilt;
            sum.firstPose.shift = shiftA;

            sum.secondPose.yaw = b.yaw;
            sum.secondPose.tilt = b.tilt;
            sum.secondPose.shift = shiftB;

            sum.first = placedA;
            sum.second = placedB;

            sum.firstCutStart = a.arc.cutStart;
            sum.firstCutEnd = a.arc.cutEnd;
            sum.firstPeak = a.arc.peak;

            sum.secondCutStart = b.arc.cutStart;
            sum.secondCutEnd = b.arc.cutEnd;
            sum.secondPeak = b.arc.peak;

            sum.bridgeOut = out;

            sum.bridgeBack = backward;
            std::reverse(sum.bridgeBack.begin(), sum.bridgeBack.end());

            // the new knot, in the direction of the first one
            auto add = [&sum](const QVector<Vec3> &points, int part, int skipFirst, int skipLast)
            {
                for (int k = skipFirst; k < points.size() - skipLast; ++k)
                {
                    sum.joined.push_back(points[k]);
                    sum.joinedPart.push_back(part);
                }
            };

            add(restA, 0, 0, 0);
            add(sum.bridgeOut, 2, 1, 1);
            add(restB, 1, 0, 0);
            add(sum.bridgeBack, 2, 1, 1);

            // everything centred on the new knot
            Vec3 low = sum.joined.first();
            Vec3 high = low;

            for (const Vec3 &p : sum.joined)
            {
                low = Vec3(std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z));
                high = Vec3(std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z));
            }

            const Vec3 centre = (low + high) * 0.5;

            for (QVector<Vec3> *points :
                 {&sum.first, &sum.second, &sum.bridgeOut, &sum.bridgeBack, &sum.joined})
            {
                for (Vec3 &p : *points)
                    p -= centre;
            }

            sum.firstPose.shift -= centre;
            sum.secondPose.shift -= centre;

            sum.valid = true;

            return sum;
        }

        if (tries > 600)
            break;
    }

    sum.error = joinTr("No way was found to place the two knots side by side "
                       "with an arc of each facing the other. Try a thinner "
                       "tube.");

    return sum;
}

} // namespace KnotMath
