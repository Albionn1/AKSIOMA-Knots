#include "knotdiagram.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace KnotMath
{

namespace
{

double dist2D(const Vec3 &a, const Vec3 &b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

QVector<Vec3> resampleClosed2D(const QVector<Vec3> &in, int count)
{
    const int n = in.size();

    QVector<double> arc(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
        arc[i + 1] = arc[i] + dist2D(in[i], in[(i + 1) % n]);

    const double total = arc[n];

    QVector<Vec3> out;

    if (total <= 1e-12 || count < 3)
        return out;

    out.reserve(count);

    int seg = 0;

    for (int k = 0; k < count; ++k)
    {
        const double s = total * static_cast<double>(k) / count;

        while (seg < n - 1 && arc[seg + 1] < s)
            ++seg;

        const double len = arc[seg + 1] - arc[seg];
        const double u = len > 1e-15 ? (s - arc[seg]) / len : 0.0;

        const Vec3 &a = in[seg];
        const Vec3 &b = in[(seg + 1) % n];

        out.push_back(Vec3(a.x + (b.x - a.x) * u,
                           a.y + (b.y - a.y) * u,
                           0.0));
    }

    return out;
}

void smoothClosed2D(QVector<Vec3> &p, int passes)
{
    const int n = p.size();

    if (n < 5)
        return;

    QVector<Vec3> tmp(n);

    // Taubin smoothing: shrink step then inflate step, so the loop keeps
    // its size.
    for (int pass = 0; pass < passes; ++pass)
    {
        for (int step = 0; step < 2; ++step)
        {
            const double lambda = (step == 0) ? 0.5 : -0.53;

            for (int i = 0; i < n; ++i)
            {
                const Vec3 &a = p[(i + n - 1) % n];
                const Vec3 &b = p[i];
                const Vec3 &c = p[(i + 1) % n];

                tmp[i] = Vec3(
                    b.x + lambda * (0.5 * (a.x + c.x) - b.x),
                    b.y + lambda * (0.5 * (a.y + c.y) - b.y),
                    0.0);
            }

            p = tmp;
        }
    }
}

double smooth01(double t)
{
    t = std::clamp(t, 0.0, 1.0);

    return t * t * (3.0 - 2.0 * t);
}

struct Pass
{
    double s = 0.0;
    int crossing = 0;
    bool isA = true;
};

QVector<Pass> sortedPasses(const KnotDiagram &diagram)
{
    QVector<Pass> passes;

    for (int k = 0; k < diagram.crossings.size(); ++k)
    {
        const DiagramCrossing &c = diagram.crossings.at(k);

        Pass a;
        a.s = c.segA + c.tA;
        a.crossing = k;
        a.isA = true;

        Pass b;
        b.s = c.segB + c.tB;
        b.crossing = k;
        b.isA = false;

        passes.push_back(a);
        passes.push_back(b);
    }

    std::sort(passes.begin(), passes.end(),
              [](const Pass &x, const Pass &y) { return x.s < y.s; });

    return passes;
}

// the height of the curve at s (in segments along it), between the passes
double heightBetweenPasses(
    const QVector<double> &nodeS,
    const QVector<double> &nodeZ,
    int n,
    double s)
{
    const int m = nodeS.size();

    if (m == 0)
        return 0.0;

    // find the node pair around s (cyclic)
    int next = 0;

    while (next < m && nodeS[next] <= s)
        ++next;

    double s0, z0, s1, z1;

    if (next == 0)
    {
        s0 = nodeS[m - 1] - n;
        z0 = nodeZ[m - 1];
        s1 = nodeS[0];
        z1 = nodeZ[0];
    }
    else if (next == m)
    {
        s0 = nodeS[m - 1];
        z0 = nodeZ[m - 1];
        s1 = nodeS[0] + n;
        z1 = nodeZ[0];
    }
    else
    {
        s0 = nodeS[next - 1];
        z0 = nodeZ[next - 1];
        s1 = nodeS[next];
        z1 = nodeZ[next];
    }

    const double span = s1 - s0;

    return span > 1e-9
               ? z0 + (z1 - z0) * smooth01((s - s0) / span)
               : z0;
}

/*
 * The signed Gauss code: for every pass along the curve, how many passes on
 * its partner in the same crossing lies, and on which side the partner runs.
 * Two pictures with the same code (up to where the curve starts) show the
 * same crossings in the same order.
 */
struct PassCode
{
    int toPartner = 0;
    bool partnerGoesLeft = false;
};

QVector<PassCode> gaussCode(const KnotDiagram &diagram, const QVector<Pass> &passes)
{
    const int m = passes.size();
    const int n = diagram.points.size();

    QVector<int> position(2 * diagram.crossings.size(), 0);

    for (int i = 0; i < m; ++i)
        position[2 * passes.at(i).crossing + (passes.at(i).isA ? 0 : 1)] = i;

    auto direction = [&diagram, n](int segment)
    {
        return diagram.points.at((segment + 1) % n) - diagram.points.at(segment);
    };

    QVector<PassCode> code(m);

    for (int i = 0; i < m; ++i)
    {
        const Pass &pass = passes.at(i);
        const DiagramCrossing &c = diagram.crossings.at(pass.crossing);

        const int other = position.at(2 * pass.crossing + (pass.isA ? 1 : 0));

        const Vec3 mine = direction(pass.isA ? c.segA : c.segB);
        const Vec3 theirs = direction(pass.isA ? c.segB : c.segA);

        code[i].toPartner = (other - i + m) % m;
        code[i].partnerGoesLeft = mine.x * theirs.y - mine.y * theirs.x > 0.0;
    }

    return code;
}

// the pass of `after` that is pass 0 of `before`, or -1 if the codes differ
int matchGaussCode(const QVector<PassCode> &before, const QVector<PassCode> &after)
{
    const int m = before.size();

    if (after.size() != m)
        return -1;

    if (m == 0)
        return 0;

    for (int shift = 0; shift < m; ++shift)
    {
        bool same = true;

        for (int i = 0; i < m && same; ++i)
        {
            const PassCode &a = before.at(i);
            const PassCode &b = after.at((i + shift) % m);

            same = a.toPartner == b.toPartner && a.partnerGoesLeft == b.partnerGoesLeft;
        }

        if (same)
            return shift;
    }

    return -1;
}

// `after` takes over and under from `before`, its passes `shift` further on
void copyOverUnder(
    const KnotDiagram &before,
    const QVector<Pass> &passesBefore,
    KnotDiagram &after,
    const QVector<Pass> &passesAfter,
    int shift)
{
    const int m = passesBefore.size();

    for (int i = 0; i < m; ++i)
    {
        const Pass &was = passesBefore.at(i);
        const Pass &now = passesAfter.at((i + shift) % m);

        if (!now.isA)
            continue;

        const bool aOver = before.crossings.at(was.crossing).aOver;

        after.crossings[now.crossing].aOver = was.isA ? aOver : !aOver;
    }
}

// the curve as far as the picture has room: only ever made smaller
void fitInside(QVector<Vec3> &points, double halfExtent)
{
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;

    for (const Vec3 &p : points)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    const double half = std::max(0.5 * (maxX - minX), 0.5 * (maxY - minY));
    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);
    const double scale = half > halfExtent ? halfExtent / half : 1.0;

    for (Vec3 &p : points)
        p = Vec3((p.x - cx) * scale, (p.y - cy) * scale, 0.0);
}

CurveData diagramCurveData(const KnotDiagram &diagram, double height)
{
    CurveData data;

    data.points = diagramToCurve(diagram, height);
    data.closed = true;

    return data;
}

// the height over the drawing of every point of the diagram
QVector<double> diagramHeights(const KnotDiagram &diagram, const QVector<Pass> &passes, double height)
{
    QVector<double> nodeS;
    QVector<double> nodeZ;

    for (const Pass &pass : passes)
    {
        const bool aOver = diagram.crossings.at(pass.crossing).aOver;
        const bool over = pass.isA ? aOver : !aOver;

        nodeS.push_back(pass.s);
        nodeZ.push_back(over ? height : -height);
    }

    const int n = diagram.points.size();

    QVector<double> heights(n);

    for (int i = 0; i < n; ++i)
        heights[i] = heightBetweenPasses(nodeS, nodeZ, n, i);

    return heights;
}

// how much of the arc the tube of this radius makes neighbours (never tight)
int neighbourPoints(const KnotDiagram &diagram, double tubeRadius)
{
    const int n = diagram.points.size();

    double length = 0.0;

    for (int i = 0; i < n; ++i)
        length += dist2D(diagram.points.at(i), diagram.points.at((i + 1) % n));

    return static_cast<int>(std::ceil(4.0 * tubeRadius / std::max(length / n, 1e-9)));
}

} // namespace

double diagramTubeRadius(const KnotDiagram &diagram, double tubeRadius, double height)
{
    return safeTubeRadius(diagramCurveData(diagram, height), tubeRadius);
}

QVector<TightSpot> findTightSpots(const KnotDiagram &diagram, double tubeRadius, double height)
{
    QVector<TightSpot> spots;

    const int n = diagram.points.size();

    if (n < 8)
        return spots;

    // as KnotMath::safeTubeRadius: the tube fits while 0.46 of the distance
    // is more than its radius
    constexpr double Factor = 0.46;

    // tight spots closer than this are one place
    constexpr double SamePlace = 0.45;

    /*
     * Measured on the curve itself, segment by segment, by the rule of
     * safeTubeRadius: the drawing shows every place the tube is thinned for.
     * (Between points of the drawing is not enough: where two crossings
     * nearly coincide, the strands change places at one spot and meet half
     * way between over and under.)
     */
    const QVector<Vec3> curve = diagramToCurve(diagram, height);

    const int m = curve.size();

    QVector<double> arc(m + 1, 0.0);

    for (int i = 0; i < m; ++i)
        arc[i + 1] = arc.at(i) + (curve.at((i + 1) % m) - curve.at(i)).length();

    const double limit = tubeRadius / Factor;
    const double neighbourArc = 4.0 * tubeRadius;

    struct Pair
    {
        int i = 0;
        int j = 0;
        double distance = 0.0;
    };

    QVector<Pair> pairs;

    for (int i = 0; i < m; ++i)
    {
        const Vec3 &a = curve.at(i);
        const Vec3 &a1 = curve.at((i + 1) % m);

        const double lengthA = arc.at(i + 1) - arc.at(i);

        for (int j = i + 1; j < m; ++j)
        {
            const double along = std::min(
                arc.at(j) - arc.at(i + 1),
                arc.at(m) - arc.at(j + 1) + arc.at(i));

            if (along < neighbourArc)
                continue;

            const Vec3 &b = curve.at(j);
            const Vec3 &b1 = curve.at((j + 1) % m);

            // far apart for sure
            if ((a - b).length() > limit + lengthA + (arc.at(j + 1) - arc.at(j)))
                continue;

            const double d = strandDistance(a, a1, b, b1);

            if (d < limit)
                pairs.push_back(Pair{i, j, d});
        }
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const Pair &x, const Pair &y) { return x.distance < y.distance; });

    // the point of the drawing under a point of the curve
    auto drawnPoint = [&diagram, n](const Vec3 &p)
    {
        int nearest = 0;
        double best = 1e300;

        for (int i = 0; i < n; ++i)
        {
            const double d = dist2D(diagram.points.at(i), p);

            if (d < best)
            {
                best = d;
                nearest = i;
            }
        }

        return nearest;
    };

    for (const Pair &pair : pairs)
    {
        const Vec3 &a = curve.at(pair.i);
        const Vec3 &b = curve.at(pair.j);

        const Vec3 middle((a.x + b.x) * 0.5, (a.y + b.y) * 0.5, 0.0);

        bool known = false;

        for (const TightSpot &spot : spots)
            known = known || dist2D(spot.position, middle) < SamePlace;

        if (known)
            continue;

        TightSpot spot;

        spot.position = middle;
        spot.radius = Factor * pair.distance;

        const int first = std::min(drawnPoint(a), drawnPoint(b));
        const int last = std::max(drawnPoint(a), drawnPoint(b));

        // the crossing with the smallest loop around both parts of the pair
        int shortest = n / 2;

        for (int k = 0; k < diagram.crossings.size(); ++k)
        {
            const DiagramCrossing &c = diagram.crossings.at(k);

            const double from = std::min(c.segA + c.tA, c.segB + c.tB);
            const double to = std::max(c.segA + c.tA, c.segB + c.tB);

            const bool inside = from <= first && last <= to;
            const bool outside = first <= from && to <= last;

            const int loop = static_cast<int>(inside ? to - from : outside ? n - (to - from) : n);

            if (loop < shortest)
            {
                shortest = loop;
                spot.crossing = k;
            }
        }

        spots.push_back(spot);
    }

    return spots;
}

bool spreadDiagram(KnotDiagram &diagram, double tubeRadius, double height, double halfExtent)
{
    const int drawn = diagram.points.size();

    if (drawn < 8)
        return false;

    constexpr int Steps = 900;
    constexpr int Finer = 3;
    constexpr double MostMove = 0.03;
    constexpr double SmallestStep = 0.04;
    constexpr int MostPin = 6;

    // a little more room than the tube needs: then it is thick right up to
    // the wanted radius
    const double wanted = 1.12 * tubeRadius / 0.46;

    // two crossings stand this far apart, for the strands to change places
    // between them
    const double crossingRoom = 0.4 * wanted;

    const double startRadius = diagramTubeRadius(diagram, tubeRadius, height);

    const QVector<Pass> startPasses = sortedPasses(diagram);
    const QVector<PassCode> code = gaussCode(diagram, startPasses);

    /*
     * Worked on with more points: two crossings can lie closer than one
     * step of the drawing, and the strands between them have to open up.
     * The points are added on the lines of the drawing, so its crossings
     * stay exactly where they are.
     */
    KnotDiagram current;

    current.points.reserve(drawn * Finer);

    for (int i = 0; i < drawn; ++i)
    {
        const Vec3 &a = diagram.points.at(i);
        const Vec3 &b = diagram.points.at((i + 1) % drawn);

        for (int k = 0; k < Finer; ++k)
            current.points.push_back(a + (b - a) * (static_cast<double>(k) / Finer));
    }

    const int n = current.points.size();

    detectDiagramCrossings(current);

    QVector<Pass> passes = sortedPasses(current);

    {
        const int shift = matchGaussCode(code, gaussCode(current, passes));

        if (shift < 0)
            return false;

        copyOverUnder(diagram, startPasses, current, passes, shift);
    }

    KnotDiagram best = current;
    double bestRadius = startRadius;

    double scale = 1.0;
    int accepted = 0;

    QVector<int> first;
    QVector<int> next(n);

    for (int step = 0; step < Steps; ++step)
    {
        const QVector<double> heights = diagramHeights(current, passes, height);

        const int neighbours = neighbourPoints(current, tubeRadius);

        // only points within reach of each other are compared: a grid
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;

        for (const Vec3 &p : current.points)
        {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        const int columns = static_cast<int>((maxX - minX) / wanted) + 1;
        const int rows = static_cast<int>((maxY - minY) / wanted) + 1;

        first.fill(-1, columns * rows);

        auto cellOf = [&](const Vec3 &p, int &column, int &row)
        {
            column = std::clamp(static_cast<int>((p.x - minX) / wanted), 0, columns - 1);
            row = std::clamp(static_cast<int>((p.y - minY) / wanted), 0, rows - 1);
        };

        for (int i = 0; i < n; ++i)
        {
            int column = 0;
            int row = 0;

            cellOf(current.points.at(i), column, row);

            next[i] = first.at(row * columns + column);
            first[row * columns + column] = i;
        }

        QVector<Vec3> push(n, Vec3(0.0, 0.0, 0.0));
        QVector<int> pushes(n, 0);

        bool tight = false;

        for (int i = 0; i < n; ++i)
        {
            int column = 0;
            int row = 0;

            cellOf(current.points.at(i), column, row);

            for (int r = std::max(row - 1, 0); r <= std::min(row + 1, rows - 1); ++r)
            {
                for (int c = std::max(column - 1, 0); c <= std::min(column + 1, columns - 1); ++c)
                {
                    for (int j = first.at(r * columns + c); j >= 0; j = next.at(j))
                    {
                        if (j <= i || j - i < neighbours || n - (j - i) < neighbours)
                            continue;

                        const double dx = current.points.at(i).x - current.points.at(j).x;
                        const double dy = current.points.at(i).y - current.points.at(j).y;
                        const double dz = heights.at(i) - heights.at(j);

                        const double across = std::hypot(dx, dy);
                        const double distance = std::hypot(across, dz);

                        if (distance >= wanted || across < 1e-9)
                            continue;

                        tight = true;

                        const double amount = 0.5 * (wanted - distance) / across;

                        push[i] += Vec3(dx * amount, dy * amount, 0.0);
                        push[j] -= Vec3(dx * amount, dy * amount, 0.0);

                        ++pushes[i];
                        ++pushes[j];
                    }
                }
            }
        }

        /*
         * Crossings too close to each other, too. Where two nearly coincide
         * the strands change places at one spot and meet half way between
         * over and under, which no pair of points shows.
         */
        const int crossings = current.crossings.size();

        QVector<Vec3> apart(crossings, Vec3(0.0, 0.0, 0.0));

        for (int k = 0; k < crossings; ++k)
        {
            for (int l = k + 1; l < crossings; ++l)
            {
                const Vec3 &p = current.crossings.at(k).position;
                const Vec3 &q = current.crossings.at(l).position;

                const double dx = p.x - q.x;
                const double dy = p.y - q.y;
                const double distance = std::hypot(dx, dy);

                if (distance >= crossingRoom)
                    continue;

                tight = true;

                // (on top of each other: along the strand)
                const int segment = current.crossings.at(k).segA;
                const Vec3 along = current.points.at((segment + 1) % n) - current.points.at(segment);

                const double scaleTo = 0.25 * (crossingRoom - distance) / std::max(distance, 1e-9);

                const Vec3 away = distance > 1e-9
                                      ? Vec3(dx * scaleTo, dy * scaleTo, 0.0)
                                      : along * (0.25 * crossingRoom / std::max(along.length(), 1e-9));

                apart[k] += away;
                apart[l] -= away;
            }
        }

        if (!tight)
            break;

        for (int i = 0; i < n; ++i)
        {
            if (pushes.at(i) > 0)
                push[i] *= 1.0 / pushes.at(i);
        }

        // a strand moves as one, not point by point
        for (int sweep = 0; sweep < 14; ++sweep)
        {
            QVector<Vec3> smooth(n);

            for (int i = 0; i < n; ++i)
                smooth[i] = (push.at((i + n - 1) % n) + push.at(i) * 2.0 + push.at((i + 1) % n)) * 0.25;

            push = smooth;
        }

        /*
         * Pushed apart everywhere, two strands would come off each other.
         * At a crossing they move together, so that it stays a crossing and
         * the strands between two crossings open up instead.
         */
        const int m = passes.size();

        for (int k = 0; k < current.crossings.size(); ++k)
        {
            const DiagramCrossing &c = current.crossings.at(k);

            const int a = (c.segA + (c.tA > 0.5 ? 1 : 0)) % n;
            const int b = (c.segB + (c.tB > 0.5 ? 1 : 0)) % n;

            const Vec3 together = (push.at(a) + push.at(b)) * 0.5 + apart.at(k);
            const Vec3 fromA = together - push.at(a);
            const Vec3 fromB = together - push.at(b);

            for (int side = 0; side < 2; ++side)
            {
                const int centre = side == 0 ? a : b;
                const Vec3 &change = side == 0 ? fromA : fromB;

                // not beyond half way to the next pass along the curve
                double room = n;

                for (int p = 0; p < m; ++p)
                {
                    const double gap = std::abs(passes.at(p).s - centre);

                    if (gap > 1.0)
                        room = std::min(room, std::min(gap, n - gap));
                }

                const int reach = std::clamp(static_cast<int>(0.5 * room), 1, MostPin);

                for (int t = -reach; t <= reach; ++t)
                    push[(centre + t + n) % n] += change * (1.0 - static_cast<double>(std::abs(t)) / (reach + 1));
            }
        }

        // with smoothing if it can be: a smoothed strand can lose a crossing
        // that is only a sliver yet
        bool done = false;

        for (int attempt = 0; attempt < 2 && !done; ++attempt)
        {
            KnotDiagram trial;

            trial.points = current.points;

            for (int i = 0; i < n; ++i)
            {
                Vec3 move = push.at(i) * scale;

                const double far = std::hypot(move.x, move.y);

                if (far > MostMove)
                    move *= MostMove / far;

                trial.points[i] += move;
            }

            if (attempt == 0)
            {
                smoothClosed2D(trial.points, 1);

                if (accepted % 16 == 15)
                    trial.points = resampleClosed2D(trial.points, n);
            }

            fitInside(trial.points, halfExtent);

            detectDiagramCrossings(trial);

            const QVector<Pass> trialPasses = sortedPasses(trial);
            const int shift = matchGaussCode(code, gaussCode(trial, trialPasses));

            if (shift < 0 || trial.points.size() != n)
                continue;

            // (the passes of the first picture stay the reference)
            copyOverUnder(diagram, startPasses, trial, trialPasses, shift);

            current = trial;
            passes = trialPasses;
            done = true;
        }

        // that step would have changed the crossings: a smaller one
        if (!done)
        {
            scale *= 0.5;

            if (scale < SmallestStep)
                break;

            continue;
        }

        scale = std::min(1.0, scale * 1.25);

        if (++accepted % 8 == 0)
        {
            const double radius = diagramTubeRadius(current, tubeRadius, height);

            if (radius > bestRadius)
            {
                bestRadius = radius;
                best = current;
            }
        }
    }

    const double radius = diagramTubeRadius(current, tubeRadius, height);

    if (radius > bestRadius)
    {
        bestRadius = radius;
        best = current;
    }

    if (bestRadius <= startRadius * 1.01)
        return false;

    /*
     * Corners and spikes left by the pushing are rounded off, as long as the
     * crossings stay and the tube does not get thinner for it.
     */
    for (int pass = 0; pass < 24; ++pass)
    {
        KnotDiagram trial;

        trial.points = best.points;

        smoothClosed2D(trial.points, 2);
        fitInside(trial.points, halfExtent);

        detectDiagramCrossings(trial);

        const QVector<Pass> trialPasses = sortedPasses(trial);
        const int shift = matchGaussCode(code, gaussCode(trial, trialPasses));

        if (shift < 0)
            break;

        copyOverUnder(diagram, startPasses, trial, trialPasses, shift);

        if (diagramTubeRadius(trial, tubeRadius, height) < 0.98 * bestRadius)
            break;

        best = trial;
    }

    // back to the number of points of the drawing, if nothing is lost by it
    KnotDiagram plain;

    plain.points = resampleClosed2D(best.points, drawn);

    detectDiagramCrossings(plain);

    const QVector<Pass> plainPasses = sortedPasses(plain);
    const int shift = matchGaussCode(code, gaussCode(plain, plainPasses));

    if (shift >= 0)
    {
        copyOverUnder(diagram, startPasses, plain, plainPasses, shift);

        if (diagramTubeRadius(plain, tubeRadius, height) >= 0.95 * bestRadius)
            best = plain;
    }

    diagram = best;

    return true;
}

bool removeDiagramLoop(KnotDiagram &diagram, int crossing, double halfExtent)
{
    const int n = diagram.points.size();

    if (crossing < 0 || crossing >= diagram.crossings.size())
        return false;

    const DiagramCrossing &c = diagram.crossings.at(crossing);

    const double first = std::min(c.segA + c.tA, c.segB + c.tB);
    const double second = std::max(c.segA + c.tA, c.segB + c.tB);

    // what stays: the longer way round, from one pass to the other
    const bool keepInside = second - first > 0.5 * n;
    const double from = keepInside ? first : second;
    const double to = keepInside ? second : first + n;

    auto kept = [from, to, n](double s)
    {
        return (s > from && s < to) || (s + n > from && s + n < to);
    };

    QVector<Vec3> points;

    for (int i = static_cast<int>(std::ceil(from)); i < to; ++i)
        points.push_back(diagram.points.at(i % n));

    if (points.size() < 8)
        return false;

    // the crossings that stay, in their order from the start of what stays
    struct Staying
    {
        double along = 0.0;
        int crossing = 0;
        bool isA = true;
    };

    QVector<Staying> staying;

    const auto alongKept = [from, n](double s)
    {
        return s > from ? s - from : s + n - from;
    };

    for (int k = 0; k < diagram.crossings.size(); ++k)
    {
        const DiagramCrossing &other = diagram.crossings.at(k);

        const double a = other.segA + other.tA;
        const double b = other.segB + other.tB;

        if (k == crossing || !kept(a) || !kept(b))
            continue;

        staying.push_back(Staying{alongKept(a), k, true});
        staying.push_back(Staying{alongKept(b), k, false});
    }

    std::sort(staying.begin(), staying.end(),
              [](const Staying &x, const Staying &y) { return x.along < y.along; });

    // the pairs of passes that stay
    const int m = staying.size();

    QVector<int> partnerWas(m, -1);

    for (int i = 0; i < m; ++i)
    {
        for (int j = 0; j < m; ++j)
        {
            if (i != j && staying.at(i).crossing == staying.at(j).crossing)
                partnerWas[i] = j;
        }
    }

    /*
     * Every crossing that stays keeps its over and under. The curve starts
     * where it starts now, so the passes are in the same order as `staying`
     * from that point on; the pairs must match, or the rest has changed.
     */
    auto takeOverUnder = [&](KnotDiagram &result)
    {
        if (result.crossings.size() * 2 != m)
            return false;

        const QVector<Pass> passes = sortedPasses(result);

        if (m == 0)
            return true;

        for (int shift = 0; shift < m; ++shift)
        {
            bool same = true;

            for (int i = 0; i < m && same; ++i)
            {
                const Pass &now = passes.at((i + shift) % m);
                const Pass &partner = passes.at((partnerWas.at(i) + shift) % m);

                same = partner.crossing == now.crossing && partner.isA != now.isA;
            }

            if (!same)
                continue;

            for (int i = 0; i < m; ++i)
            {
                const Pass &now = passes.at((i + shift) % m);

                if (!now.isA)
                    continue;

                const DiagramCrossing &was = diagram.crossings.at(staying.at(i).crossing);

                result.crossings[now.crossing].aOver = staying.at(i).isA ? was.aOver : !was.aOver;
            }

            return true;
        }

        return false;
    };

    /*
     * Only the corner left where the ends meet is rounded: smoothing it all
     * could lose a small part of the drawing that is meant to stay. Rounded
     * less if another strand runs close by, so as not to push it across.
     */
    for (const int reach : {4, 2, 1, 0})
    {
        QVector<Vec3> rounded = points;

        const int count = rounded.size();

        for (int sweep = 0; reach > 0 && sweep < 6; ++sweep)
        {
            const QVector<Vec3> before = rounded;

            for (int t = -reach; t <= reach; ++t)
            {
                const int i = (t + count) % count;

                rounded[i] = (before.at((i + count - 1) % count) + before.at(i) * 2.0 +
                              before.at((i + 1) % count)) * 0.25;
            }
        }

        fitInside(rounded, halfExtent);

        KnotDiagram result;

        result.points = rounded;

        detectDiagramCrossings(result);

        if (!takeOverUnder(result))
            continue;

        // evenly spaced again, if nothing is lost by it
        KnotDiagram even;

        even.points = resampleClosed2D(rounded, n);

        detectDiagramCrossings(even);

        diagram = takeOverUnder(even) ? even : result;

        return true;
    }

    return false;
}

namespace
{

/*
 * The faces of a drawing known only as a graph: `passes` along the curve (each
 * pass is an edge's start), `turns` which way round each crossing goes. A
 * dart is an edge walked one way: 2 e from pass e on, 2 e + 1 back towards it.
 * Each face is the darts with that face on their left.
 */
QVector<QVector<int>> graphFaces(const QVector<Pass> &passes, const QVector<double> &turns)
{
    const int m = passes.size();
    const int darts = 2 * m;

    QVector<int> first(turns.size(), -1);
    QVector<int> second(turns.size(), -1);

    for (int j = 0; j < m; ++j)
        (passes.at(j).isA ? first : second)[passes.at(j).crossing] = j;

    auto outOfNode = [](int pass) { return 2 * pass; };
    auto intoNode = [m](int pass) { return 2 * ((pass + m - 1) % m) + 1; };

    // the darts around every node, counter-clockwise
    QVector<int> before(darts, -1);

    for (int k = 0; k < turns.size(); ++k)
    {
        const int a = first.at(k);
        const int b = second.at(k);

        if (a < 0 || b < 0)
            continue;

        const int order[4] = {
            outOfNode(a),
            turns.at(k) > 0.0 ? outOfNode(b) : intoNode(b),
            intoNode(a),
            turns.at(k) > 0.0 ? intoNode(b) : outOfNode(b)};

        for (int q = 0; q < 4; ++q)
            before[order[(q + 1) % 4]] = order[q];
    }

    QVector<QVector<int>> faces;
    QVector<bool> walked(darts, false);

    for (int start = 0; start < darts; ++start)
    {
        if (walked.at(start))
            continue;

        QVector<int> face;

        int dart = start;

        do
        {
            walked[dart] = true;
            face.push_back(dart);

            dart = before.at(dart ^ 1);
        } while (dart != start && !walked.at(dart));

        faces.push_back(face);
    }

    return faces;
}

/*
 * Takes out what a drawing does not need, by two moves that leave the knot as
 * it is. A projection of a curve has plenty of both, and they are what cannot
 * be spread out: a twist has a face that meets itself at the crossing, and a
 * strand laid over another has a face with two sides.
 *
 * A crossing that no other crossing is interlaced with: the part of the curve
 * between its two passes hangs on the rest by that crossing alone. A half turn
 * of that part about the strand undoes it. (Turned over, every crossing in the
 * part has over and under the other way, and goes the other way round.)
 *
 * Two crossings that are the corners of a face with two sides, one strand
 * being over (or under) at both: that strand lifts off the other.
 */
void simplifyPasses(QVector<Pass> &passes, QVector<double> &turns, QVector<bool> &firstOver)
{
    auto isOver = [&firstOver](const Pass &pass)
    {
        return pass.isA ? firstOver.at(pass.crossing) : !firstOver.at(pass.crossing);
    };

    auto remove = [&passes](int crossing)
    {
        for (int j = passes.size() - 1; j >= 0; --j)
        {
            if (passes.at(j).crossing == crossing)
                passes.remove(j);
        }
    };

    bool changed = true;

    while (changed && !passes.isEmpty())
    {
        changed = false;

        const int m = passes.size();

        for (int i = 0; i < m && !changed; ++i)
        {
            int j = i + 1;

            while (j < m && passes.at(j).crossing != passes.at(i).crossing)
                ++j;

            if (j >= m)
                continue;

            QVector<int> inside(turns.size(), 0);

            for (int t = i + 1; t < j; ++t)
                ++inside[passes.at(t).crossing];

            if (std::find(inside.cbegin(), inside.cend(), 1) != inside.cend())
                continue;

            for (int k = 0; k < inside.size(); ++k)
            {
                if (inside.at(k) == 2)
                {
                    turns[k] = -turns.at(k);
                    firstOver[k] = !firstOver.at(k);
                }
            }

            remove(passes.at(i).crossing);

            changed = true;
        }

        if (changed)
            continue;

        for (const QVector<int> &face : graphFaces(passes, turns))
        {
            if (face.size() != 2)
                continue;

            const int edge = face.first() / 2;

            const Pass &here = passes.at(edge);
            const Pass &then = passes.at((edge + 1) % m);

            if (here.crossing == then.crossing || isOver(here) != isOver(then))
                continue;

            const int one = here.crossing;
            const int two = then.crossing;

            remove(one);
            remove(two);

            changed = true;

            break;
        }
    }

    // the crossings that are left, numbered from 0
    QVector<int> renumbered(turns.size(), -1);
    QVector<double> keptTurns;
    QVector<bool> keptOver;

    for (Pass &pass : passes)
    {
        if (renumbered.at(pass.crossing) < 0)
        {
            renumbered[pass.crossing] = keptTurns.size();

            keptTurns.push_back(turns.at(pass.crossing));
            keptOver.push_back(firstOver.at(pass.crossing));
        }

        pass.crossing = renumbered.at(pass.crossing);
    }

    turns = keptTurns;
    firstOver = keptOver;
}

} // namespace

bool openDiagram(KnotDiagram &diagram, int outside)
{
    const int drawn = diagram.points.size();

    if (drawn < 8)
        return false;

    /*
     * The drawing as what it really is, a plane graph: the crossings are its
     * nodes, each one single point that both strands pass through, and the
     * pieces of curve between two passes are its edges, all with the same
     * number of points. A crossing cannot come undone, or be counted wrongly,
     * and over and under stay with their nodes.
     */
    QVector<Pass> passes = sortedPasses(diagram);

    // Which way round each crossing goes, from the drawing's own segments:
    // the points of the edges may lie too far apart to tell where two
    // crossings almost touch.
    QVector<double> turns(diagram.crossings.size());
    QVector<bool> firstOver(diagram.crossings.size());

    for (int k = 0; k < diagram.crossings.size(); ++k)
    {
        const DiagramCrossing &c = diagram.crossings.at(k);

        const Vec3 a = diagram.points.at((c.segA + 1) % drawn) - diagram.points.at(c.segA);
        const Vec3 b = diagram.points.at((c.segB + 1) % drawn) - diagram.points.at(c.segB);

        turns[k] = a.x * b.y - a.y * b.x >= 0.0 ? 1.0 : -1.0;
        firstOver[k] = c.aOver;
    }

    // (a drawing that keeps its shape keeps its crossings)
    if (outside >= 0)
        simplifyPasses(passes, turns, firstOver);

    const int m = passes.size();

    QVector<double> arc(drawn + 1, 0.0);

    for (int i = 0; i < drawn; ++i)
        arc[i + 1] = arc.at(i) + dist2D(diagram.points.at(i), diagram.points.at((i + 1) % drawn));

    const double whole = arc.at(drawn);

    if (whole < 1e-12)
        return false;

    // no crossings at all: a round loop
    if (m == 0)
    {
        constexpr int Round = 120;

        const double r = 6.0 / (2.0 * 3.14159265358979);

        diagram.points.clear();
        diagram.crossings.clear();

        for (int i = 0; i < Round; ++i)
        {
            const double a = 2.0 * 3.14159265358979 * i / Round;

            diagram.points.push_back(Vec3(r * std::cos(a), r * std::sin(a), 0.0));
        }

        return true;
    }

    constexpr int PerEdge = 20;

    // Lengths in units of the wanted distance between two passes.
    const double length = std::max(m, 6);

    const int count = m * PerEdge;
    const double spacing = length / count;

    constexpr int Iterations = 1500;
    constexpr int CheckEvery = 5;
    constexpr int EvenEvery = 10;

    // strands push each other away within this distance
    constexpr double Reach = 0.62;

    // an edge of the graph is at least this long
    constexpr double WantedEdge = 0.85;

    // and no bend is tighter than this
    constexpr double WantedRadius = 0.3;

    auto arcAt = [&arc, drawn](double s)
    {
        const int segment = std::clamp(static_cast<int>(s), 0, drawn - 1);

        return arc.at(segment) + (s - segment) * (arc.at(segment + 1) - arc.at(segment));
    };

    auto pointAtArc = [&](double along)
    {
        along = std::fmod(along, whole);

        const int segment = std::clamp(
            static_cast<int>(std::upper_bound(arc.cbegin(), arc.cend(), along) - arc.cbegin()) - 1,
            0,
            drawn - 1);

        const double piece = arc.at(segment + 1) - arc.at(segment);
        const double t = piece > 0.0 ? (along - arc.at(segment)) / piece : 0.0;

        const Vec3 &a = diagram.points.at(segment);
        const Vec3 &b = diagram.points.at((segment + 1) % drawn);

        return a + (b - a) * t;
    };

    QVector<Vec3> p(count, Vec3(0.0, 0.0, 0.0));

    for (int j = 0; j < m && outside < 0; ++j)
    {
        const double from = arcAt(passes.at(j).s);

        double to = arcAt(passes.at((j + 1) % m).s);

        if (j == m - 1)
            to += whole;

        for (int t = 0; t < PerEdge; ++t)
            p[j * PerEdge + t] = pointAtArc(from + (to - from) * t / PerEdge);
    }

    struct Node
    {
        int a = 0;
        int b = 0;
        double sign = 0.0;
    };

    const int crossings = turns.size();

    QVector<Node> nodes(crossings);

    // which crossing is at the start of each edge
    QVector<int> nodeOf(m);

    for (int j = 0; j < m; ++j)
    {
        nodeOf[j] = passes.at(j).crossing;

        (passes.at(j).isA ? nodes[nodeOf.at(j)].a : nodes[nodeOf.at(j)].b) = j * PerEdge;
    }

    auto wrapped = [count](int i) { return (i % count + count) % count; };

    auto mergeNodes = [&]()
    {
        for (const Node &node : nodes)
        {
            const Vec3 middle = (p.at(node.a) + p.at(node.b)) * 0.5;

            p[node.a] = middle;
            p[node.b] = middle;
        }
    };

    /*
     * Which way round the strands pass through a node, from the order of its
     * four arms: +1 if the second strand's way out comes first counter-
     * clockwise from the first strand's way out, -1 if its way in does, 0 if
     * the strands no longer cross here at all. (Two strands bent at the node
     * still cross, whatever the directions of their chords say.)
     */
    auto turnAt = [&](const Node &node)
    {
        const Vec3 &centre = p.at(node.a);

        auto angle = [&](int i, double from)
        {
            const Vec3 out = p.at(wrapped(i)) - centre;

            double a = std::atan2(out.y, out.x) - from;

            while (a < 0.0)
                a += 2.0 * 3.14159265358979;

            return a;
        };

        const double from = angle(node.a + 1, 0.0);

        const double opposite = angle(node.a - 1, from);
        const double bOut = angle(node.b + 1, from);
        const double bIn = angle(node.b - 1, from);

        if ((bOut < opposite) == (bIn < opposite))
            return 0.0;

        return bOut < opposite ? 1.0 : -1.0;
    };

    auto fitLength = [&]()
    {
        double now = 0.0;

        Vec3 middle(0.0, 0.0, 0.0);

        for (int i = 0; i < count; ++i)
        {
            now += dist2D(p.at(i), p.at((i + 1) % count));
            middle += p.at(i);
        }

        middle = middle * (1.0 / count);

        const double scale = length / std::max(now, 1e-12);

        for (Vec3 &q : p)
            q = (q - middle) * scale;
    };

    mergeNodes();
    fitLength();

    for (int k = 0; k < crossings; ++k)
        nodes[k].sign = turns.at(k);

    // Still the same drawing: no two segments cross (they only meet at the
    // nodes), and the strands pass through every node the same way round.
    auto sound = [&]()
    {
        for (const Node &node : nodes)
        {
            if (turnAt(node) != node.sign)
                return false;
        }

        auto side = [](const Vec3 &a, const Vec3 &b, const Vec3 &c)
        {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        };

        for (int i = 0; i < count; ++i)
        {
            const Vec3 &a = p.at(i);
            const Vec3 &b = p.at((i + 1) % count);

            for (int j = i + 2; j < count; ++j)
            {
                if (i == 0 && j == count - 1)
                    continue;

                const Vec3 &c = p.at(j);
                const Vec3 &d = p.at((j + 1) % count);

                if (std::max(a.x, b.x) < std::min(c.x, d.x) || std::max(c.x, d.x) < std::min(a.x, b.x) ||
                    std::max(a.y, b.y) < std::min(c.y, d.y) || std::max(c.y, d.y) < std::min(a.y, b.y))
                {
                    continue;
                }

                if (side(a, b, c) * side(a, b, d) < 0.0 && side(c, d, a) * side(c, d, b) < 0.0)
                    return false;
            }
        }

        return true;
    };

    /*
     * A new start, from the graph alone: a drawing seen from one side of the
     * curve can have large empty places and tiny crowded ones, and no pushing
     * opens those. So the faces of the graph are found (the strands and the
     * way round they pass through every node say what lies next to what),
     * one becomes the outside on a circle, and every other point goes to the
     * mean of its neighbours (W. Tutte, 1963: a plane graph held like this
     * comes out with no two edges crossing, however tangled it was). Each
     * face gets a point of its own in the middle and each edge one too, which
     * keeps two edges between the same two nodes apart.
     */
    auto spreadFromFaces = [&]() -> bool
    {
        // the node a dart leaves from
        auto tail = [&](int dart)
        {
            const int edge = dart / 2;

            return nodeOf.at(dart % 2 == 0 ? edge : (edge + 1) % m);
        };

        const QVector<QVector<int>> faces = graphFaces(passes, turns);

        // Euler: a plane graph with these nodes and edges has this many faces
        if (faces.size() != crossings + 2)
            return false;

        // the outside: one that passes no node twice, the larger the better
        QVector<int> order;

        for (int f = 0; f < faces.size(); ++f)
        {
            QVector<int> seen;
            bool twice = false;

            for (const int dart : faces.at(f))
            {
                twice = twice || seen.contains(tail(dart));
                seen.push_back(tail(dart));
            }

            if (!twice)
                order.push_back(f);
        }

        std::stable_sort(order.begin(), order.end(), [&faces](int x, int y) { return faces.at(x).size() > faces.at(y).size(); });

        if (outside >= order.size())
            return false;

        const int outer = order.at(outside);

        // nodes first, then the middle of every edge, then of every face
        const int total = crossings + m + faces.size();

        auto edgePoint = [&](int edge) { return crossings + edge; };
        auto facePoint = [&](int f) { return crossings + m + f; };

        QVector<QVector<int>> neighbours(total);

        auto join = [&neighbours](int x, int y)
        {
            neighbours[x].push_back(y);
            neighbours[y].push_back(x);
        };

        for (int f = 0; f < faces.size(); ++f)
        {
            for (const int dart : faces.at(f))
            {
                join(tail(dart), edgePoint(dart / 2));
                join(facePoint(f), edgePoint(dart / 2));
                join(facePoint(f), tail(dart));
            }
        }

        // the outside on a circle, walked with the outer face on the left:
        // clockwise
        QVector<int> fixedAt(total, -1);
        QVector<double> placedX(total, 0.0);
        QVector<double> placedY(total, 0.0);

        const QVector<int> &rim = faces.at(outer);

        int onRim = 0;

        auto putOnRim = [&](int point, int steps)
        {
            const double angle = -2.0 * 3.14159265358979 * steps / (2.0 * rim.size());

            fixedAt[point] = 1;
            placedX[point] = std::cos(angle);
            placedY[point] = std::sin(angle);
        };

        for (const int dart : rim)
        {
            putOnRim(tail(dart), onRim++);
            putOnRim(edgePoint(dart / 2), onRim++);
        }

        fixedAt[facePoint(outer)] = 2;

        // every other point at the mean of its neighbours: a linear system
        QVector<int> unknown(total, -1);

        int size = 0;

        for (int i = 0; i < total; ++i)
        {
            if (fixedAt.at(i) < 0)
                unknown[i] = size++;
        }

        QVector<double> matrix(size * (size + 2), 0.0);

        auto entry = [&matrix, size](int row, int column) -> double &
        {
            return matrix[row * (size + 2) + column];
        };

        for (int i = 0; i < total; ++i)
        {
            const int row = unknown.at(i);

            if (row < 0)
                continue;

            for (const int other : neighbours.at(i))
            {
                if (fixedAt.at(other) == 2)
                    continue;

                entry(row, row) += 1.0;

                if (unknown.at(other) >= 0)
                {
                    entry(row, unknown.at(other)) -= 1.0;
                }
                else
                {
                    entry(row, size) += placedX.at(other);
                    entry(row, size + 1) += placedY.at(other);
                }
            }
        }

        for (int pivot = 0; pivot < size; ++pivot)
        {
            int best = pivot;

            for (int row = pivot + 1; row < size; ++row)
            {
                if (std::abs(entry(row, pivot)) > std::abs(entry(best, pivot)))
                    best = row;
            }

            if (std::abs(entry(best, pivot)) < 1e-12)
                return false;

            if (best != pivot)
            {
                for (int column = 0; column < size + 2; ++column)
                    std::swap(entry(best, column), entry(pivot, column));
            }

            for (int row = 0; row < size; ++row)
            {
                if (row == pivot)
                    continue;

                const double factor = entry(row, pivot) / entry(pivot, pivot);

                if (factor == 0.0)
                    continue;

                for (int column = pivot; column < size + 2; ++column)
                    entry(row, column) -= factor * entry(pivot, column);
            }
        }

        for (int i = 0; i < total; ++i)
        {
            const int row = unknown.at(i);

            if (row >= 0)
            {
                placedX[i] = entry(row, size) / entry(row, row);
                placedY[i] = entry(row, size + 1) / entry(row, row);
            }
        }

        const QVector<Vec3> own = p;

        for (int j = 0; j < m; ++j)
        {
            const int from = nodeOf.at(j);
            const int to = nodeOf.at((j + 1) % m);
            const int middle = edgePoint(j);

            const Vec3 a(placedX.at(from), placedY.at(from), 0.0);
            const Vec3 b(placedX.at(middle), placedY.at(middle), 0.0);
            const Vec3 c(placedX.at(to), placedY.at(to), 0.0);

            for (int t = 0; t < PerEdge; ++t)
            {
                const double u = 2.0 * t / PerEdge;

                p[j * PerEdge + t] = u < 1.0 ? a + (b - a) * u : b + (c - b) * (u - 1.0);
            }
        }

        mergeNodes();
        fitLength();

        if (!sound())
        {
            p = own;

            return false;
        }

        return true;
    };

    if (outside >= 0)
    {
        if (!spreadFromFaces())
            return false;
    }
    else if (!sound())
    {
        return false;
    }

    QVector<Vec3> good = p;

    double step = 0.5;

    QVector<int> first;
    QVector<int> next(count);

    // the two points nearest to the same node do not push each other: that
    // is what the turning of the arms below is for
    const int nodeWidth = static_cast<int>(0.45 * PerEdge);
    const int edgeWidth = static_cast<int>(0.9 * PerEdge);

    for (int iteration = 0; iteration < Iterations; ++iteration)
    {
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;

        for (const Vec3 &q : p)
        {
            minX = std::min(minX, q.x);
            maxX = std::max(maxX, q.x);
            minY = std::min(minY, q.y);
            maxY = std::max(maxY, q.y);
        }

        const int columns = static_cast<int>((maxX - minX) / Reach) + 1;
        const int rows = static_cast<int>((maxY - minY) / Reach) + 1;

        first.fill(-1, columns * rows);

        auto cellOf = [&](const Vec3 &q, int &column, int &row)
        {
            column = std::clamp(static_cast<int>((q.x - minX) / Reach), 0, columns - 1);
            row = std::clamp(static_cast<int>((q.y - minY) / Reach), 0, rows - 1);
        };

        for (int i = 0; i < count; ++i)
        {
            int column = 0;
            int row = 0;

            cellOf(p.at(i), column, row);

            next[i] = first.at(row * columns + column);
            first[row * columns + column] = i;
        }

        QVector<Vec3> push(count, Vec3(0.0, 0.0, 0.0));
        QVector<int> pushes(count, 0);

        for (int i = 0; i < count; ++i)
        {
            int column = 0;
            int row = 0;

            cellOf(p.at(i), column, row);

            const int nearI = wrapped(i + PerEdge / 2) / PerEdge;
            const int awayI = std::abs(i - nearI * PerEdge);

            for (int r = std::max(row - 1, 0); r <= std::min(row + 1, rows - 1); ++r)
            {
                for (int c = std::max(column - 1, 0); c <= std::min(column + 1, columns - 1); ++c)
                {
                    for (int j = first.at(r * columns + c); j >= 0; j = next.at(j))
                    {
                        if (j <= i || j - i < edgeWidth || count - (j - i) < edgeWidth)
                            continue;

                        const int nearJ = wrapped(j + PerEdge / 2) / PerEdge;
                        const int awayJ = std::abs(j - nearJ * PerEdge);

                        if (awayI < nodeWidth && awayJ < nodeWidth &&
                            std::min(awayI, count - awayI) < nodeWidth &&
                            nodeOf.at(nearI) == nodeOf.at(nearJ))
                        {
                            continue;
                        }

                        const double dx = p.at(i).x - p.at(j).x;
                        const double dy = p.at(i).y - p.at(j).y;
                        const double distance = std::hypot(dx, dy);

                        if (distance >= Reach || distance < 1e-9)
                            continue;

                        const double amount = 0.5 * (Reach - distance) / distance;

                        push[i] += Vec3(dx * amount, dy * amount, 0.0);
                        push[j] -= Vec3(dx * amount, dy * amount, 0.0);

                        ++pushes[i];
                        ++pushes[j];
                    }
                }
            }
        }

        for (int i = 0; i < count; ++i)
        {
            if (pushes.at(i) > 0)
                push[i] *= 1.0 / pushes.at(i);
        }

        // a strand moves as one, not point by point
        for (int sweep = 0; sweep < 6; ++sweep)
        {
            QVector<Vec3> smooth(count);

            for (int i = 0; i < count; ++i)
                smooth[i] = (push.at(wrapped(i - 1)) + push.at(i) * 2.0 + push.at(wrapped(i + 1))) * 0.25;

            push = smooth;
        }

        QVector<Vec3> shape(count, Vec3(0.0, 0.0, 0.0));

        /*
         * Edges that are too short: their ends move apart, with what is near
         * them. (A loop from a node back to itself swells instead.)
         */
        QVector<Vec3> apart(crossings, Vec3(0.0, 0.0, 0.0));

        for (int j = 0; j < m; ++j)
        {
            double edge = 0.0;

            for (int t = 0; t < PerEdge; ++t)
                edge += dist2D(p.at(wrapped(j * PerEdge + t)), p.at(wrapped(j * PerEdge + t + 1)));

            if (edge >= WantedEdge)
                continue;

            const int here = nodeOf.at(j);
            const int then = nodeOf.at((j + 1) % m);

            if (here == then)
            {
                const Vec3 &centre = p.at(j * PerEdge);

                for (int t = 1; t < PerEdge; ++t)
                {
                    const int i = wrapped(j * PerEdge + t);

                    const Vec3 out = p.at(i) - centre;

                    shape[i] += out * (0.5 * (WantedEdge - edge) / std::max(std::hypot(out.x, out.y), 1e-9) * std::sin(3.14159265 * t / PerEdge));
                }

                continue;
            }

            const Vec3 a = p.at(j * PerEdge);
            const Vec3 b = p.at(wrapped((j + 1) * PerEdge));

            const double distance = std::max(dist2D(a, b), 1e-9);

            const Vec3 away = (b - a) * (0.2 * (WantedEdge - edge) / distance);

            apart[here] -= away;
            apart[then] += away;
        }

        const int armReach = PerEdge / 2;

        for (int k = 0; k < crossings; ++k)
        {
            const Node &node = nodes.at(k);

            for (const int centre : {node.a, node.b})
            {
                for (int t = -armReach; t <= armReach; ++t)
                {
                    const double fade = 1.0 - static_cast<double>(std::abs(t)) / (armReach + 1);

                    shape[wrapped(centre + t)] += apart.at(k) * fade;
                }
            }
        }

        /*
         * The four arms of every node turn towards right angles: each to the
         * middle of the two arms beside it. Opposite arms then line up, so
         * that both strands run straight through.
         */
        const int armAt = PerEdge / 4;

        for (int k = 0; k < crossings; ++k)
        {
            const Node &node = nodes.at(k);

            const Vec3 centre = p.at(node.a);

            struct Arm
            {
                int start = 0;
                int way = 1;
                double angle = 0.0;
            };

            Arm arms[4];

            arms[0].start = node.a;
            arms[0].way = 1;
            arms[1].start = node.a;
            arms[1].way = -1;
            arms[2].start = node.b;
            arms[2].way = 1;
            arms[3].start = node.b;
            arms[3].way = -1;

            for (Arm &arm : arms)
            {
                const Vec3 out = p.at(wrapped(arm.start + arm.way * armAt)) - centre;

                arm.angle = std::atan2(out.y, out.x);
            }

            std::sort(std::begin(arms), std::end(arms), [](const Arm &x, const Arm &y) { return x.angle < y.angle; });

            for (int q = 0; q < 4; ++q)
            {
                double ahead = arms[(q + 1) % 4].angle - arms[q].angle;
                double behind = arms[q].angle - arms[(q + 3) % 4].angle;

                if (ahead < 0.0)
                    ahead += 2.0 * 3.14159265358979;

                if (behind < 0.0)
                    behind += 2.0 * 3.14159265358979;

                const double turn = 0.5 * (ahead - behind);

                for (int t = 1; t <= armReach; ++t)
                {
                    const int i = wrapped(arms[q].start + arms[q].way * t);

                    const Vec3 out = p.at(i) - centre;

                    const double fade = t <= armAt ? 1.0 : 1.0 - static_cast<double>(t - armAt) / (armReach - armAt + 1);

                    shape[i] += Vec3(-out.y, out.x, 0.0) * (0.5 * turn * fade);
                }
            }
        }

        /*
         * Tips that are too sharp: over a stretch of the curve a bend of the
         * wanted radius sags by no more than this; a point that lies further
         * from the middle of the chord is drawn back to it.
         */
        {
            const int chord = std::max(3, static_cast<int>(0.3 / spacing));

            const double half = chord * spacing;
            const double sag = half * half / (2.0 * WantedRadius);

            QVector<Vec3> round(count, Vec3(0.0, 0.0, 0.0));

            for (int i = 0; i < count; ++i)
            {
                const Vec3 middle = (p.at(wrapped(i - chord)) + p.at(wrapped(i + chord))) * 0.5;

                const Vec3 back = middle - p.at(i);

                const double distance = std::hypot(back.x, back.y);

                if (distance > sag)
                    round[i] = back * (0.5 * (distance - sag) / distance);
            }

            for (int sweep = 0; sweep < 3; ++sweep)
            {
                QVector<Vec3> smooth(count);

                for (int i = 0; i < count; ++i)
                    smooth[i] = (round.at(wrapped(i - 1)) + round.at(i) * 2.0 + round.at(wrapped(i + 1))) * 0.25;

                round = smooth;
            }

            for (int i = 0; i < count; ++i)
                shape[i] += round.at(i);
        }

        // never further in a step than part of the distance between points
        double largest = 0.0;
        double largestShape = 0.0;

        for (int i = 0; i < count; ++i)
        {
            largest = std::max(largest, std::hypot(push.at(i).x, push.at(i).y));
            largestShape = std::max(largestShape, std::hypot(shape.at(i).x, shape.at(i).y));
        }

        const double most = 0.3 * spacing;

        const double factor = largest * step > most ? most / largest : step;
        const double shapeFactor = largestShape * step > most ? most / largestShape : step;

        for (int i = 0; i < count; ++i)
            p[i] += push.at(i) * factor + shape.at(i) * shapeFactor;

        // the corners rounded off, the fine ones first
        for (int pass = 0; pass < 2; ++pass)
        {
            const QVector<Vec3> was = p;

            for (int i = 0; i < count; ++i)
            {
                const Vec3 &a = was.at(wrapped(i - 2));
                const Vec3 &b = was.at(wrapped(i - 1));
                const Vec3 &c = was.at(wrapped(i + 1));
                const Vec3 &d = was.at(wrapped(i + 2));

                const Vec3 here = was.at(i);

                p[i] = here + (b + c - here * 2.0) * 0.16 - (a + d + here * 6.0 - (b + c) * 4.0) * 0.03;
            }
        }

        mergeNodes();

        /*
         * Each strand runs straight through its node: the points the same
         * way along it on either side lie opposite each other.
         */
        for (const Node &node : nodes)
        {
            const Vec3 centre = p.at(node.a);

            for (const int start : {node.a, node.b})
            {
                for (int t = PerEdge / 4; t >= 1; --t)
                {
                    const int ahead = wrapped(start + t);
                    const int back = wrapped(start - t);

                    const Vec3 off = centre - (p.at(ahead) + p.at(back)) * 0.5;

                    const double weight = 0.5 * (1.0 - static_cast<double>(t - 1) / (PerEdge / 4));

                    p[ahead] += off * weight;
                    p[back] += off * weight;
                }
            }
        }

        // the points of every edge evenly along it (its ends stay)
        if ((iteration + 1) % EvenEvery == 0)
        {
            for (int j = 0; j < m; ++j)
            {
                double along[PerEdge + 1];

                along[0] = 0.0;

                Vec3 was[PerEdge + 1];

                for (int t = 0; t <= PerEdge; ++t)
                    was[t] = p.at(wrapped(j * PerEdge + t));

                for (int t = 0; t < PerEdge; ++t)
                    along[t + 1] = along[t] + dist2D(was[t], was[t + 1]);

                int piece = 0;

                for (int t = 1; t < PerEdge; ++t)
                {
                    const double target = along[PerEdge] * t / PerEdge;

                    while (piece < PerEdge - 1 && along[piece + 1] < target)
                        ++piece;

                    const double size = along[piece + 1] - along[piece];
                    const double u = size > 1e-12 ? (target - along[piece]) / size : 0.0;

                    p[wrapped(j * PerEdge + t)] = was[piece] + (was[piece + 1] - was[piece]) * u;
                }
            }
        }

        fitLength();

        if ((iteration + 1) % CheckEvery == 0)
        {
            if (sound())
            {
                good = p;

                step = std::min(step * 1.03, 0.5);
            }
            else
            {
                // too far: back to the last good shape, in smaller steps
                p = good;

                step = std::max(step * 0.75, 0.04);
            }
        }
    }

    p = good;

    /*
     * The small waves of the work above go. This smoothing takes out a wave
     * by the fourth power of how short it is, so that the shape of a whole
     * edge stays, where a plain average would pull it straight. It runs along
     * each strand right through the nodes, which stay where they are, so
     * that a bend is shared out along the strand on both sides of a node
     * instead of gathering beside it.
     */
    {
        QVector<bool> atNode(count, false);

        for (const Node &node : nodes)
        {
            atNode[node.a] = true;
            atNode[node.b] = true;
        }

        for (int round = 0; round < 12; ++round)
        {
            const QVector<Vec3> shaped = p;

            for (int pass = 0; pass < 50; ++pass)
            {
                const QVector<Vec3> was = p;

                for (int i = 0; i < count; ++i)
                {
                    if (atNode.at(i))
                        continue;

                    const Vec3 &a = was.at(wrapped(i - 2));
                    const Vec3 &b = was.at(wrapped(i - 1));
                    const Vec3 &c = was.at(wrapped(i + 1));
                    const Vec3 &d = was.at(wrapped(i + 2));

                    p[i] = was.at(i) - (a + d + was.at(i) * 6.0 - (b + c) * 4.0) * 0.05;
                }
            }

            if (!sound())
            {
                p = shaped;

                break;
            }
        }
    }

    // the sharpest bend of an edge (one over its radius)
    auto sharpest = [&](int start)
    {
        double most = 0.0;

        for (int t = 0; t <= PerEdge; ++t)
        {
            const Vec3 &a = p.at(wrapped(start + t - 2));
            const Vec3 &q = p.at(wrapped(start + t));
            const Vec3 &c = p.at(wrapped(start + t + 2));

            const double ab = dist2D(a, q);
            const double bc = dist2D(q, c);
            const double ca = dist2D(a, c);

            const double twice = std::abs((q.x - a.x) * (c.y - a.y) - (q.y - a.y) * (c.x - a.x));

            if (ab * bc * ca > 1e-30)
                most = std::max(most, 2.0 * twice / (ab * bc * ca));
        }

        return most;
    };

    /*
     * Where that leaves a tight bend, the edge may do better as one smooth
     * curve: it leaves a node along the strand, comes into the next along
     * the strand and is as long as it was (a cubic Bezier curve, its two
     * handles as long as that takes). It is taken only if it bends less
     * and runs into nothing.
     */
    {
        const QVector<Vec3> shaped = p;

        auto strandAt = [&shaped, &wrapped](int i)
        {
            const Vec3 d = shaped.at(wrapped(i + 2)) - shaped.at(wrapped(i - 2));

            return d * (1.0 / std::max(std::hypot(d.x, d.y), 1e-12));
        };

        constexpr int Fine = 80;

        for (int j = 0; j < m; ++j)
        {
            const int start = j * PerEdge;

            const Vec3 a = shaped.at(start);
            const Vec3 d = shaped.at(wrapped(start + PerEdge));

            const Vec3 leaving = strandAt(start);
            const Vec3 arriving = strandAt(start + PerEdge);

            double edge = 0.0;

            for (int t = 0; t < PerEdge; ++t)
                edge += dist2D(shaped.at(wrapped(start + t)), shaped.at(wrapped(start + t + 1)));

            Vec3 curve[Fine + 1];
            double along[Fine + 1];

            auto trace = [&](double handle)
            {
                const Vec3 b = a + leaving * handle;
                const Vec3 c = d - arriving * handle;

                along[0] = 0.0;

                for (int k = 0; k <= Fine; ++k)
                {
                    const double u = static_cast<double>(k) / Fine;
                    const double v = 1.0 - u;

                    curve[k] = a * (v * v * v) + b * (3.0 * v * v * u) + c * (3.0 * v * u * u) + d * (u * u * u);

                    if (k > 0)
                        along[k] = along[k - 1] + dist2D(curve[k - 1], curve[k]);
                }

                return along[Fine];
            };

            double low = 0.05 * edge;
            double high = 1.2 * edge;

            for (int attempt = 0; attempt < 18; ++attempt)
            {
                const double middle = 0.5 * (low + high);

                (trace(middle) < edge ? low : high) = middle;
            }

            trace(0.5 * (low + high));

            const double before = sharpest(start);

            QVector<Vec3> was(PerEdge - 1);

            int piece = 0;

            for (int t = 1; t < PerEdge; ++t)
            {
                was[t - 1] = p.at(wrapped(start + t));

                const double target = along[Fine] * t / PerEdge;

                while (piece < Fine - 1 && along[piece + 1] < target)
                    ++piece;

                const double size = along[piece + 1] - along[piece];
                const double u = size > 1e-12 ? (target - along[piece]) / size : 0.0;

                p[wrapped(start + t)] = curve[piece] + (curve[piece + 1] - curve[piece]) * u;
            }

            if (sharpest(start) >= before || !sound())
            {
                for (int t = 1; t < PerEdge; ++t)
                    p[wrapped(start + t)] = was.at(t - 1);
            }
        }
    }

    QVector<DiagramCrossing> opened(crossings);

    for (int k = 0; k < crossings; ++k)
    {
        opened[k].segA = nodes.at(k).a;
        opened[k].tA = 0.0;
        opened[k].segB = nodes.at(k).b;
        opened[k].tB = 0.0;
        opened[k].aOver = firstOver.at(k);
        opened[k].position = p.at(nodes.at(k).a);
    }

    diagram.points = p;
    diagram.crossings = opened;

    return true;
}

void detectDiagramCrossings(KnotDiagram &diagram)
{
    diagram.crossings.clear();

    const int n = diagram.points.size();

    if (n < 8)
        return;

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &p = diagram.points[i];
        const Vec3 &p2 = diagram.points[(i + 1) % n];

        const double rx = p2.x - p.x;
        const double ry = p2.y - p.y;

        for (int j = i + 2; j < n; ++j)
        {
            // neighbouring segments share an end point
            if (i == 0 && j == n - 1)
                continue;

            const Vec3 &q = diagram.points[j];
            const Vec3 &q2 = diagram.points[(j + 1) % n];

            const double sx = q2.x - q.x;
            const double sy = q2.y - q.y;

            const double denom = rx * sy - ry * sx;

            if (std::abs(denom) < 1e-14)
                continue;

            const double qpx = q.x - p.x;
            const double qpy = q.y - p.y;

            const double t = (qpx * sy - qpy * sx) / denom;
            const double u = (qpx * ry - qpy * rx) / denom;

            if (t < 0.0 || t >= 1.0 || u < 0.0 || u >= 1.0)
                continue;

            DiagramCrossing c;
            c.segA = i;
            c.tA = t;
            c.segB = j;
            c.tB = u;
            c.position = Vec3(p.x + rx * t, p.y + ry * t, 0.0);

            diagram.crossings.push_back(c);
        }
    }

    alternateDiagram(diagram);
}

void alternateDiagram(KnotDiagram &diagram)
{
    const QVector<Pass> passes = sortedPasses(diagram);

    for (int i = 0; i < passes.size(); ++i)
    {
        const Pass &pass = passes.at(i);

        if (!pass.isA)
            continue;

        // the pass at an even position is an over-pass
        diagram.crossings[pass.crossing].aOver = (i % 2 == 0);
    }
}

void mirrorDiagram(KnotDiagram &diagram)
{
    for (DiagramCrossing &c : diagram.crossings)
        c.aOver = !c.aOver;
}

int nearestDiagramCrossing(
    const KnotDiagram &diagram,
    double x,
    double y,
    double maxDistance)
{
    int best = -1;
    double bestDistance = maxDistance;

    for (int k = 0; k < diagram.crossings.size(); ++k)
    {
        const Vec3 &p = diagram.crossings.at(k).position;
        const double d = std::hypot(p.x - x, p.y - y);

        if (d <= bestDistance)
        {
            bestDistance = d;
            best = k;
        }
    }

    return best;
}

KnotDiagram makeDiagramFromStroke(
    const QVector<Vec3> &stroke,
    int samples,
    double halfExtent)
{
    KnotDiagram diagram;

    // drop repeated points
    QVector<Vec3> raw;

    for (const Vec3 &p : stroke)
    {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            continue;

        if (raw.isEmpty() || dist2D(raw.last(), p) > 1e-9)
            raw.push_back(Vec3(p.x, p.y, 0.0));
    }

    // a closing duplicate of the first point would make a zero segment
    if (raw.size() > 2 && dist2D(raw.first(), raw.last()) < 1e-9)
        raw.removeLast();

    if (raw.size() < 8)
        return diagram;

    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;

    for (const Vec3 &p : raw)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    const double extent = std::max(maxX - minX, maxY - minY);

    if (extent < 1e-9)
        return diagram;

    const int fine = std::max(samples * 3, 360);

    QVector<Vec3> p = resampleClosed2D(raw, fine);

    if (p.isEmpty())
        return diagram;

    smoothClosed2D(p, 10);

    // centre and scale
    minX = 1e300; maxX = -1e300; minY = 1e300; maxY = -1e300;

    for (const Vec3 &q : p)
    {
        minX = std::min(minX, q.x);
        maxX = std::max(maxX, q.x);
        minY = std::min(minY, q.y);
        maxY = std::max(maxY, q.y);
    }

    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);
    const double scale =
        halfExtent / std::max(0.5 * (maxX - minX), 0.5 * (maxY - minY));

    for (Vec3 &q : p)
        q = Vec3((q.x - cx) * scale, (q.y - cy) * scale, 0.0);

    diagram.points = resampleClosed2D(p, samples);

    if (diagram.points.size() < 8)
    {
        diagram.points.clear();
        return diagram;
    }

    detectDiagramCrossings(diagram);

    return diagram;
}

namespace
{

// The projected curve centred and scaled to `halfExtent`, with its
// crossings.
KnotDiagram makeDiagram(
    const QVector<Vec3> &projected,
    const QVector<CrossingInfo> &crossings,
    double halfExtent)
{
    KnotDiagram diagram;

    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;

    for (const Vec3 &p : projected)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    const double half = std::max(0.5 * (maxX - minX), 0.5 * (maxY - minY));

    if (half < 1e-12)
        return diagram;

    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);
    const double scale = halfExtent / half;

    for (const Vec3 &p : projected)
        diagram.points.push_back(Vec3((p.x - cx) * scale, (p.y - cy) * scale, 0.0));

    const int n = diagram.points.size();

    for (const CrossingInfo &info : crossings)
    {
        DiagramCrossing c;
        c.segA = info.segmentA;
        c.tA = info.parameterA;
        c.segB = info.segmentB;
        c.tB = info.parameterB;
        c.aOver = info.aIsOver;

        const Vec3 &a = diagram.points[c.segA];
        const Vec3 &b = diagram.points[(c.segA + 1) % n];

        c.position = Vec3(a.x + (b.x - a.x) * c.tA, a.y + (b.y - a.y) * c.tA, 0.0);

        diagram.crossings.push_back(c);
    }

    return diagram;
}

// How well the crossings are spread out (larger is better): the smaller
// of the closest distance of two crossings in the picture and the
// shortest piece of curve between two passes, both relative to the size.
double crossingSpread(const KnotDiagram &diagram, double halfExtent)
{
    const int c = diagram.crossings.size();

    if (c == 0)
        return 1.0;

    double closest = 2.0 * halfExtent;

    for (int i = 0; i < c; ++i)
    {
        for (int j = i + 1; j < c; ++j)
            closest = std::min(closest, dist2D(diagram.crossings[i].position,
                                               diagram.crossings[j].position));
    }

    const int n = diagram.points.size();

    QVector<double> cum(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
        cum[i + 1] = cum[i] + dist2D(diagram.points[i], diagram.points[(i + 1) % n]);

    auto lengthAt = [&](int segment, double t)
    {
        return cum[segment] + t * (cum[segment + 1] - cum[segment]);
    };

    QVector<double> passes;

    for (const DiagramCrossing &crossing : diagram.crossings)
        passes << lengthAt(crossing.segA, crossing.tA) << lengthAt(crossing.segB, crossing.tB);

    std::sort(passes.begin(), passes.end());

    double shortest = cum[n] - passes.last() + passes.first();

    for (int i = 0; i + 1 < passes.size(); ++i)
        shortest = std::min(shortest, passes[i + 1] - passes[i]);

    // the shortest piece compared with an even spacing of the passes
    return std::min(closest / (2.0 * halfExtent), shortest * passes.size() / cum[n]);
}

} // namespace

QVector<KnotDiagram> diagramsFromCurve(
    const QVector<Vec3> &curve,
    double halfExtent,
    QString &error,
    int maxCount)
{
    QVector<KnotDiagram> result;

    // Coarse curves (the knot table stores about a hundred points) would
    // be drawn with visible corners: a smooth spline through the points
    // first.
    QVector<Vec3> smooth = curve;

    if (smooth.size() >= 8 && smooth.size() < 300)
    {
        smooth = refineCurve(smooth, (300 + smooth.size() - 1) / smooth.size(), true);

        // a spline can carry one strand across another: the corners then stay
        const AlexanderPolynomial before = computeAlexanderPolynomial(curve);
        const AlexanderPolynomial after = computeAlexanderPolynomial(smooth);

        if (before.valid && (!after.valid || before.text != after.text))
            smooth = curve;
    }

    QVector<QVector<Vec3>> projected;
    QVector<QVector<CrossingInfo>> crossings;

    if (!cleanProjections(smooth, projected, crossings, error))
        return result;

    struct Candidate
    {
        KnotDiagram diagram;
        double spread = 0.0;
    };

    QVector<Candidate> candidates;

    for (int i = 0; i < projected.size(); ++i)
    {
        Candidate candidate;

        candidate.diagram = makeDiagram(projected[i], crossings[i], halfExtent);

        if (!candidate.diagram.isValid())
            continue;

        candidate.spread = crossingSpread(candidate.diagram, halfExtent);

        candidates.push_back(candidate);
    }

    if (candidates.isEmpty())
    {
        error = QCoreApplication::translate("KnotMath", "The curve is degenerate.");
        return result;
    }

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate &a, const Candidate &b)
        {
            if (a.diagram.crossings.size() != b.diagram.crossings.size())
                return a.diagram.crossings.size() < b.diagram.crossings.size();

            return a.spread > b.spread;
        });

    for (int i = 0; i < candidates.size() && i < maxCount; ++i)
        result.push_back(candidates[i].diagram);

    return result;
}

KnotDiagram diagramFromCurve(
    const QVector<Vec3> &curve,
    double halfExtent,
    QString &error)
{
    const QVector<KnotDiagram> diagrams =
        diagramsFromCurve(curve, halfExtent, error, 1);

    return diagrams.isEmpty() ? KnotDiagram() : diagrams.first();
}

QVector<Vec3> diagramToCurve(
    const KnotDiagram &diagram,
    double height)
{
    const int n = diagram.points.size();

    QVector<Vec3> curve;

    if (n < 8)
        return curve;

    const QVector<Pass> passes = sortedPasses(diagram);

    QVector<double> nodeS;
    QVector<double> nodeZ;

    for (const Pass &pass : passes)
    {
        const bool aOver = diagram.crossings.at(pass.crossing).aOver;
        const bool over = pass.isA ? aOver : !aOver;

        nodeS.push_back(pass.s);
        nodeZ.push_back(over ? height : -height);
    }

    auto heightAt = [&](double s)
    {
        return heightBetweenPasses(nodeS, nodeZ, n, s);
    };

    // The heights at the sample points alone are not enough: two passes
    // less than a sample apart would share one segment, and the height at
    // a crossing (interpolated along the segment) could come out on the
    // wrong side. So every pass gets two points of its own, just before and
    // just after it, at exactly its height: the crossing lies on the short
    // segment between them, and over and under are always the drawn ones.
    curve.reserve(n + 2 * passes.size());

    int nextPass = 0;

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &a = diagram.points[i];
        const Vec3 &b = diagram.points[(i + 1) % n];

        curve.push_back(Vec3(a.x, a.y, heightAt(static_cast<double>(i))));

        // the passes on segment i, in order (the passes are sorted by s)
        const int first = nextPass;

        while (nextPass < passes.size() && passes.at(nextPass).s < i + 1)
            ++nextPass;

        for (int p = first; p < nextPass; ++p)
        {
            const double t = passes.at(p).s - i;

            const double before = (p > first) ? passes.at(p - 1).s - i : 0.0;
            const double after = (p + 1 < nextPass) ? passes.at(p + 1).s - i : 1.0;

            const double e = std::min({0.1, (t - before) / 3.0, (after - t) / 3.0});

            if (e < 1e-6)
                continue;

            for (const double u : {t - e, t + e})
            {
                curve.push_back(Vec3(a.x + (b.x - a.x) * u,
                                     a.y + (b.y - a.y) * u,
                                     nodeZ.at(p)));
            }
        }
    }

    return curve;
}

QString diagramToCoordinateText(const QVector<Vec3> &curve)
{
    QString text;

    for (const Vec3 &p : curve)
    {
        text += QString::number(p.x, 'f', 4) + QStringLiteral(", ") +
                QString::number(p.y, 'f', 4) + QStringLiteral(", ") +
                QString::number(p.z, 'f', 4) + QStringLiteral("\n");
    }

    return text;
}

} // namespace KnotMath
