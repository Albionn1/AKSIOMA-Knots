#include "knotreidemeister.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>

namespace KnotMath
{

namespace
{

// Height of the over-passes above the plane (the Draw dialog hands its
// diagrams on with the same height). The moving strand gets one more on top.
constexpr double kHeight = 0.3;

double ease(double t)
{
    t = std::clamp(t, 0.0, 1.0);

    return t * t * (3.0 - 2.0 * t);
}

// Point of the closed polyline at parameter s (in samples, any real number).
Vec3 pointAt(const QVector<Vec3> &curve, double s)
{
    const int n = curve.size();

    double w = std::fmod(s, static_cast<double>(n));

    if (w < 0.0)
        w += n;

    const int i = static_cast<int>(std::floor(w)) % n;
    const double f = w - std::floor(w);

    const Vec3 &a = curve[i];
    const Vec3 &b = curve[(i + 1) % n];

    return Vec3(a.x + (b.x - a.x) * f,
                a.y + (b.y - a.y) * f,
                a.z + (b.z - a.z) * f);
}

int wrap(int i, int n)
{
    return ((i % n) + n) % n;
}

// Even spacing along the shadow (arc length in the plane), heights carried
// along.
QVector<Vec3> resampleWithHeights(const QVector<Vec3> &in, int count)
{
    const int n = in.size();

    QVector<double> arc(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &a = in[i];
        const Vec3 &b = in[(i + 1) % n];

        arc[i + 1] = arc[i] + std::hypot(b.x - a.x, b.y - a.y);
    }

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
                           a.z + (b.z - a.z) * u));
    }

    return out;
}

// Taubin smoothing of the points strictly between the (unwrapped) indices
// `from` and `to`; the rest of the curve stays put.
void smoothRange(QVector<Vec3> &p, int from, int to, int passes)
{
    const int n = p.size();

    if (to - from < 3)
        return;

    for (int pass = 0; pass < passes; ++pass)
    {
        for (int step = 0; step < 2; ++step)
        {
            const double lambda = (step == 0) ? 0.5 : -0.53;

            const QVector<Vec3> old = p;

            for (int k = from + 1; k < to; ++k)
            {
                const Vec3 &a = old[wrap(k - 1, n)];
                const Vec3 &b = old[wrap(k, n)];
                const Vec3 &c = old[wrap(k + 1, n)];

                Vec3 &q = p[wrap(k, n)];

                q.x = b.x + lambda * (0.5 * (a.x + c.x) - b.x);
                q.y = b.y + lambda * (0.5 * (a.y + c.y) - b.y);
            }
        }
    }
}

bool insidePolygon(const QVector<Vec3> &polygon, double x, double y)
{
    bool inside = false;

    const int m = polygon.size();

    for (int i = 0, j = m - 1; i < m; j = i++)
    {
        const Vec3 &a = polygon[i];
        const Vec3 &b = polygon[j];

        if ((a.y > y) != (b.y > y) &&
            x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
        {
            inside = !inside;
        }
    }

    return inside;
}

double distanceToPolygon(const QVector<Vec3> &polygon, double x, double y)
{
    double best = 1e300;

    const int m = polygon.size();

    for (int i = 0; i < m; ++i)
    {
        const Vec3 &a = polygon[i];
        const Vec3 &b = polygon[(i + 1) % m];

        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;

        double t = len2 > 1e-18 ? ((x - a.x) * dx + (y - a.y) * dy) / len2 : 0.0;

        t = std::clamp(t, 0.0, 1.0);

        best = std::min(best, std::hypot(a.x + t * dx - x, a.y + t * dy - y));
    }

    return best;
}

// The point of a grid over the region that lies deepest inside it.
Vec3 deepestPoint(const QVector<Vec3> &polygon)
{
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;

    for (const Vec3 &p : polygon)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    Vec3 best((minX + maxX) * 0.5, (minY + maxY) * 0.5, 0.0);
    double bestDepth = -1.0;

    constexpr int grid = 16;

    for (int i = 0; i <= grid; ++i)
    {
        for (int j = 0; j <= grid; ++j)
        {
            const double x = minX + (maxX - minX) * i / grid;
            const double y = minY + (maxY - minY) * j / grid;

            if (!insidePolygon(polygon, x, y))
                continue;

            const double depth = distanceToPolygon(polygon, x, y);

            if (depth > bestDepth)
            {
                bestDepth = depth;
                best = Vec3(x, y, 0.0);
            }
        }
    }

    return best;
}

/*
 * The passes of the diagram in order along the curve, and the pieces of
 * curve between them: piece i runs from pass i to pass i + 1. A face of the
 * diagram is bounded by pieces, so each move is a pattern of pieces:
 *   R1: one piece from a crossing back to the same crossing (a loop)
 *   R2: two pieces joining the same two crossings, one over at both
 *   R3: three pieces joining three crossings in a triangle
 */
struct Pass
{
    double s = 0.0;
    int crossing = 0;
    bool over = false;
};

struct Pieces
{
    QVector<Pass> passes;

    // index of the other pass at the same crossing
    QVector<int> other;

    int n = 0;

    int count() const
    {
        return passes.size();
    }

    int next(int i) const
    {
        return (i + 1) % count();
    }

    int previous(int i) const
    {
        return (i + count() - 1) % count();
    }

    double startS(int piece) const
    {
        return passes[piece].s;
    }

    double endS(int piece) const
    {
        const int j = next(piece);

        return passes[j].s + (j == 0 ? n : 0);
    }

    double length(int piece) const
    {
        return endS(piece) - startS(piece);
    }

    // the piece between two passes next to each other, or -1
    int pieceBetween(int a, int b) const
    {
        if (next(a) == b)
            return a;

        if (next(b) == a)
            return b;

        return -1;
    }
};

Pieces makePieces(const KnotDiagram &diagram)
{
    Pieces pieces;

    pieces.n = diagram.points.size();

    for (int k = 0; k < diagram.crossings.size(); ++k)
    {
        const DiagramCrossing &c = diagram.crossings.at(k);

        Pass a;
        a.s = c.segA + c.tA;
        a.crossing = k;
        a.over = c.aOver;

        Pass b;
        b.s = c.segB + c.tB;
        b.crossing = k;
        b.over = !c.aOver;

        pieces.passes << a << b;
    }

    std::sort(pieces.passes.begin(), pieces.passes.end(),
              [](const Pass &x, const Pass &y) { return x.s < y.s; });

    pieces.other.fill(-1, pieces.passes.size());

    for (int i = 0; i < pieces.passes.size(); ++i)
    {
        for (int j = 0; j < pieces.passes.size(); ++j)
        {
            if (j != i && pieces.passes[j].crossing == pieces.passes[i].crossing)
                pieces.other[i] = j;
        }
    }

    return pieces;
}

// The outline of a face: the pieces one after another, each walked from the
// pass where the previous one ended (at the same crossing).
QVector<Vec3> faceOutline(
    const QVector<Vec3> &curve,
    const Pieces &pieces,
    const QVector<int> &face,
    int firstPass)
{
    QVector<Vec3> outline;

    int at = firstPass;

    for (const int piece : face)
    {
        const bool forward = (at == piece);

        const double s0 = pieces.startS(piece);
        const double s1 = pieces.endS(piece);

        QVector<Vec3> part;

        part.push_back(pointAt(curve, s0));

        for (int k = static_cast<int>(std::floor(s0)) + 1; k < s1; ++k)
            part.push_back(curve[wrap(k, pieces.n)]);

        part.push_back(pointAt(curve, s1));

        if (!forward)
            std::reverse(part.begin(), part.end());

        // the corner is already there from the previous piece
        if (!outline.isEmpty())
            part.removeFirst();

        outline += part;

        const int endPass = forward ? pieces.next(piece) : piece;

        at = pieces.other[endPass];
    }

    for (Vec3 &p : outline)
        p.z = 0.0;

    return outline;
}

// true if no point of the curve off the face's own pieces lies inside it
bool faceIsEmpty(
    const QVector<Vec3> &curve,
    const Pieces &pieces,
    const QVector<int> &face,
    const QVector<Vec3> &outline)
{
    const int n = pieces.n;

    QVector<bool> boundary(n, false);

    for (const int piece : face)
    {
        for (int k = static_cast<int>(std::floor(pieces.startS(piece)));
             k <= static_cast<int>(std::ceil(pieces.endS(piece)));
             ++k)
        {
            boundary[wrap(k, n)] = true;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        if (!boundary[i] && insidePolygon(outline, curve[i].x, curve[i].y))
            return false;
    }

    return true;
}

// What the diagram before the move is like, so the picture after it stays
// as readable.
struct Goal
{
    int crossings = 0;

    // points of the diagram (the result is resampled to as many)
    int points = 0;

    // the closest two passes along the curve (in samples) and the closest two
    // crossings in the plane may not get much closer than this
    double minGap = 1.0;
    double minDistance = 0.06;
};

// Closest two passes along the curve (in samples) and closest two crossings
// in the plane.
void spacing(const KnotDiagram &diagram, double &gap, double &distance)
{
    QVector<double> s;

    for (const DiagramCrossing &c : diagram.crossings)
        s << c.segA + c.tA << c.segB + c.tB;

    std::sort(s.begin(), s.end());

    gap = 1e300;
    distance = 1e300;

    for (int i = 0; i < s.size(); ++i)
    {
        const double next = (i + 1 < s.size()) ? s[i + 1] : s[0] + diagram.points.size();

        gap = std::min(gap, next - s[i]);
    }

    for (int i = 0; i < diagram.crossings.size(); ++i)
    {
        for (int j = i + 1; j < diagram.crossings.size(); ++j)
        {
            const Vec3 &p = diagram.crossings[i].position;
            const Vec3 &q = diagram.crossings[j].position;

            distance = std::min(distance, std::hypot(p.x - q.x, p.y - q.y));
        }
    }
}

/*
 * The finished curve as an evenly spaced diagram. Over and under come from
 * the moved curve itself (its own crossings, which are exact), and are
 * carried over to the resampled picture crossing by crossing: resampling
 * only rounds the shape off by a hair, so each crossing stays where it was.
 * True if the picture has the expected crossings and is still readable.
 */
bool finishMove(
    ReidemeisterMove &move,
    const QVector<Vec3> &end,
    const Goal &goal,
    int expectedCrossings)
{
    const KnotDiagram exact = diagramFromHeights(end);

    if (exact.crossings.size() != expectedCrossings)
        return false;

    KnotDiagram result;

    result.points = resampleWithHeights(end, goal.points);

    for (Vec3 &p : result.points)
        p.z = 0.0;

    if (!result.isValid())
        return false;

    detectDiagramCrossings(result);

    if (result.crossings.size() != expectedCrossings)
        return false;

    // where the passes of the moved curve are, as fractions of its length
    const int n = end.size();

    QVector<double> arc(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &a = end[i];
        const Vec3 &b = end[(i + 1) % n];

        arc[i + 1] = arc[i] + std::hypot(b.x - a.x, b.y - a.y);
    }

    auto fraction = [&](int segment, double t)
    {
        return (arc[segment] + t * (arc[segment + 1] - arc[segment])) / arc[n];
    };

    auto apart = [](double f, double g)
    {
        const double d = std::abs(f - g);

        return std::min(d, 1.0 - d);
    };

    // Each crossing is matched by where its two passes are along the curve
    // (close crossings in the plane can be far apart along it): all pairs
    // by how far apart they are, the closest pairs first.
    struct Pair
    {
        double cost = 0.0;
        int picture = 0;
        int moved = 0;
        bool sameA = true;
    };

    QVector<Pair> pairs;

    for (int r = 0; r < result.crossings.size(); ++r)
    {
        const DiagramCrossing &c = result.crossings[r];

        const double fA = (c.segA + c.tA) / goal.points;
        const double fB = (c.segB + c.tB) / goal.points;

        for (int k = 0; k < exact.crossings.size(); ++k)
        {
            const DiagramCrossing &e = exact.crossings[k];

            const double gA = fraction(e.segA, e.tA);
            const double gB = fraction(e.segB, e.tB);

            const double same = apart(fA, gA) + apart(fB, gB);
            const double swapped = apart(fA, gB) + apart(fB, gA);

            if (std::min(same, swapped) < 0.02)
                pairs.push_back({std::min(same, swapped), r, k, same <= swapped});
        }
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const Pair &x, const Pair &y) { return x.cost < y.cost; });

    // crossing of the moved curve each crossing of the picture is
    QVector<int> match(result.crossings.size(), -1);
    QVector<bool> used(exact.crossings.size(), false);

    for (const Pair &pair : pairs)
    {
        if (match[pair.picture] >= 0 || used[pair.moved])
            continue;

        match[pair.picture] = pair.moved;
        used[pair.moved] = true;

        const bool aOver = exact.crossings[pair.moved].aOver;

        result.crossings[pair.picture].aOver = pair.sameA ? aOver : !aOver;
    }

    if (match.contains(-1))
        return false;

    // The two pictures must be the same diagram: the same passes in the same
    // order along the curve (the signed Gauss code), each crossing with the
    // same sign. That decides the knot, so the copy is exact.
    struct GaussPass
    {
        double f = 0.0;
        int crossing = 0;
        bool over = false;
    };

    auto gaussCode = [](const KnotDiagram &d, const QVector<int> &id, auto fractionOf)
    {
        QVector<GaussPass> code;

        for (int k = 0; k < d.crossings.size(); ++k)
        {
            const DiagramCrossing &c = d.crossings[k];

            code.push_back({fractionOf(c.segA, c.tA), id[k], c.aOver});
            code.push_back({fractionOf(c.segB, c.tB), id[k], !c.aOver});
        }

        std::sort(code.begin(), code.end(),
                  [](const GaussPass &x, const GaussPass &y) { return x.f < y.f; });

        return code;
    };

    QVector<int> identity(exact.crossings.size());

    for (int k = 0; k < identity.size(); ++k)
        identity[k] = k;

    const QVector<GaussPass> before = gaussCode(
        exact, identity, fraction);

    const QVector<GaussPass> after = gaussCode(
        result, match, [&](int segment, double t) { return (segment + t) / goal.points; });

    const int m = before.size();

    // the same code, maybe starting at another pass (an empty code, when
    // the move leaves no crossings, is the same as itself)
    int shift = (m == 0) ? 0 : -1;

    for (int s = 0; s < m && shift < 0; ++s)
    {
        bool same = true;

        for (int i = 0; i < m && same; ++i)
        {
            same = before[i].crossing == after[(i + s) % m].crossing &&
                   before[i].over == after[(i + s) % m].over;
        }

        if (same)
            shift = s;
    }

    if (shift < 0)
        return false;

    // sign of a crossing: does the under strand pass from right to left
    // of the over strand
    auto sign = [](const QVector<Vec3> &points, const DiagramCrossing &c)
    {
        const int n = points.size();

        auto direction = [&](int segment)
        {
            const Vec3 &a = points[segment];
            const Vec3 &b = points[(segment + 1) % n];

            return Vec3(b.x - a.x, b.y - a.y, 0.0);
        };

        const Vec3 over = direction(c.aOver ? c.segA : c.segB);
        const Vec3 under = direction(c.aOver ? c.segB : c.segA);

        return over.x * under.y - over.y * under.x > 0.0;
    };

    for (int k = 0; k < result.crossings.size(); ++k)
    {
        if (sign(result.points, result.crossings[k]) !=
            sign(end, exact.crossings[match[k]]))
        {
            return false;
        }
    }

    double gap = 0.0, distance = 0.0;

    spacing(result, gap, distance);

    if (gap < 0.9 * goal.minGap || distance < 0.9 * goal.minDistance)
        return false;

    move.end = end;
    move.result = result;

    return true;
}

bool segmentsCross(const Vec3 &p, const Vec3 &p2, const Vec3 &q, const Vec3 &q2)
{
    auto side = [](const Vec3 &a, const Vec3 &b, const Vec3 &c)
    {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };

    const double d1 = side(q, q2, p);
    const double d2 = side(q, q2, p2);
    const double d3 = side(p, p2, q);
    const double d4 = side(p, p2, q2);

    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

bool insideTriangle(const Vec3 &p, const Vec3 &a, const Vec3 &b, const Vec3 &c)
{
    return insidePolygon({a, b, c}, p.x, p.y);
}

/*
 * Everything strictly inside the moved stretch is lifted above (or below)
 * the rest, but the segment joining its first moved point M to the fixed
 * point F next to it climbs from the old height to the new one. While M
 * slides from M0 to M1 that segment sweeps the triangle F M0 M1, so no other
 * strand may enter that triangle (at the start or at the end of the move).
 */
bool joinSweepIsClear(
    const QVector<Vec3> &start,
    const QVector<Vec3> &end,
    int fixed,
    int moved)
{
    const int n = start.size();

    const Vec3 &F = start[wrap(fixed, n)];
    const Vec3 &M0 = start[wrap(moved, n)];
    const Vec3 &M1 = end[wrap(moved, n)];

    // samples of the moved strand next to M that move along with it
    constexpr int kAlongM = 8;

    for (const QVector<Vec3> *curve : {&start, &end})
    {
        for (int i = 0; i < n; ++i)
        {
            // the segments at F and M themselves
            const int j = (i + 1) % n;

            if (i == wrap(fixed, n) || j == wrap(fixed, n) ||
                i == wrap(moved, n) || j == wrap(moved, n))
            {
                continue;
            }

            const Vec3 &p = (*curve)[i];
            const Vec3 &q = (*curve)[j];

            // Heights never change during the move, and the joining segment
            // stays between the heights of F and M: a segment wholly above
            // or below that cannot meet it (like the moved stretch next to
            // M, which climbs on from M's height).
            if (std::min(p.z, q.z) > std::max(F.z, M0.z) ||
                std::max(p.z, q.z) < std::min(F.z, M0.z))
            {
                continue;
            }

            // The moved strand just past M slides along with M (often along
            // the very line M slides on, so the triangle is flat), and a few
            // samples on it cannot fold back onto the joining segment.
            const int fromM = std::min(wrap(i - moved, n), wrap(moved - j, n));

            if (fromM <= kAlongM &&
                (start[i].x != end[i].x || start[i].y != end[i].y) &&
                (start[j].x != end[j].x || start[j].y != end[j].y))
            {
                continue;
            }

            if (insideTriangle(p, F, M0, M1) ||
                segmentsCross(p, q, F, M0) ||
                segmentsCross(p, q, M0, M1) ||
                segmentsCross(p, q, M1, F))
            {
                return false;
            }
        }
    }

    return true;
}

// Tries the shape well smoothed first, then less, then as it is. Only the
// moved (lifted) points are smoothed, so this is part of the same motion.
// `from` and `to` are the fixed points just outside the moved stretch.
bool finishSmoothed(
    ReidemeisterMove &move,
    const QVector<Vec3> &end,
    int from,
    int to,
    const Goal &goal,
    int expectedCrossings,
    std::initializer_list<int> passList = {16, 6, 0})
{
    for (const int passes : passList)
    {
        QVector<Vec3> smooth = end;

        smoothRange(smooth, from, to, passes);

        if (joinSweepIsClear(move.start, smooth, from, from + 1) &&
            joinSweepIsClear(move.start, smooth, to, to - 1) &&
            finishMove(move, smooth, goal, expectedCrossings))
        {
            return true;
        }
    }

    return false;
}

// How far a moved stretch may reach into the next piece of curve (whose
// length is `length` samples): less than half of it, and never onto the
// points that carry the next pass.
double marginRoom(double length)
{
    return std::max(0.0, std::min(0.45 * length, length - 1.5));
}

// Weight of a moved point: 1 on the moving piece [s0, s1], easing down to 0
// over the margins out to a and b.
double pieceWeight(double s, double a, double s0, double s1, double b)
{
    if (s < s0)
        return ease((s - a) / std::max(1e-9, s0 - a));

    if (s > s1)
        return ease((b - s) / std::max(1e-9, b - s1));

    return 1.0;
}

// Average length of a sample of the curve in the plane.
double averageSample(const QVector<Vec3> &curve)
{
    const int n = curve.size();

    double perimeter = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &p = curve[i];
        const Vec3 &q = curve[(i + 1) % n];

        perimeter += std::hypot(q.x - p.x, q.y - p.y);
    }

    return std::max(1e-9, perimeter / std::max(1, n));
}

// Arc length (in the plane) from the start of an open polyline to each of
// its points.
QVector<double> arcLengths(const QVector<Vec3> &polyline)
{
    QVector<double> arc(polyline.size(), 0.0);

    for (int i = 1; i < polyline.size(); ++i)
    {
        const Vec3 &p = polyline[i - 1];
        const Vec3 &q = polyline[i];

        arc[i] = arc[i - 1] + std::hypot(q.x - p.x, q.y - p.y);
    }

    return arc;
}

// Point of an open polyline at arc length s (clamped to its ends).
Vec3 alongPolyline(const QVector<Vec3> &polyline, const QVector<double> &arc, double s)
{
    int i = 1;

    while (i < polyline.size() - 1 && arc[i] < s)
        ++i;

    const double len = arc[i] - arc[i - 1];
    const double f = len > 1e-15 ? std::clamp((s - arc[i - 1]) / len, 0.0, 1.0) : 0.0;

    return polyline[i - 1] + (polyline[i] - polyline[i - 1]) * f;
}

/*
 * The moving strand goes over (or under) everything it sweeps across, so
 * every frame of the animation is the same knot. Lifting it straight up
 * first is harmless: the moved part has no crossings with the rest. Every
 * moved point is lifted all the way, even where it hardly moves.
 *   R2, R3: the strand is over (under) at both its crossings; it is taken to
 *           height 2h (-2h), above (below) every other strand (at most h).
 *   R1: the loop is over at one end of its crossing and under at the other,
 *       so it is raised as a whole by 2.5h, which keeps that crossing (the
 *       two ends stay 2h apart, less the small climb).
 */
QVector<Vec3> liftPiece(
    const QVector<Vec3> &curve,
    const Pieces &pieces,
    double a,
    double b,
    int sign,
    bool wholeLoop)
{
    QVector<Vec3> lifted = curve;

    for (int k = static_cast<int>(std::floor(a)) + 1; k < b; ++k)
    {
        double &z = lifted[wrap(k, pieces.n)].z;

        // The height also climbs a little along the strand, so two parts
        // of the moving strand are never at the same height and cannot
        // pass through each other when they slide across one another.
        const double climb = 0.5 * kHeight * (k - a) / std::max(1e-9, b - a);

        z = wholeLoop ? z + 2.5 * kHeight + climb : sign * (2.0 * kHeight + climb);
    }

    return lifted;
}

/*
 * R1: the loop shrinks into a gentle bend. The points from a little before
 * the crossing to a little after it move onto a curve from A through (near)
 * the crossing X to B, a quadratic Bezier with X as its control point, so it
 * leaves and joins the strand along its own direction.
 */
bool makeTwistMove(
    ReidemeisterMove &move,
    const QVector<Vec3> &curve,
    const Pieces &pieces,
    int piece,
    const Goal &goal)
{
    const double s1 = pieces.startS(piece);
    const double s2 = pieces.endS(piece);

    const double a = s1 - std::min(3.0, marginRoom(pieces.length(pieces.previous(piece))));
    const double b = s2 + std::min(3.0, marginRoom(pieces.length(pieces.next(piece))));

    const Vec3 A = pointAt(curve, a);
    const Vec3 B = pointAt(curve, b);
    const Vec3 X = pointAt(curve, s1);

    move.start = liftPiece(curve, pieces, a, b, 1, true);

    QVector<Vec3> end = move.start;

    for (int k = static_cast<int>(std::floor(a)) + 1; k < b; ++k)
    {
        const double u = (k - a) / (b - a);

        Vec3 &q = end[wrap(k, pieces.n)];

        q.x = (1.0 - u) * (1.0 - u) * A.x + 2.0 * u * (1.0 - u) * X.x + u * u * B.x;
        q.y = (1.0 - u) * (1.0 - u) * A.y + 2.0 * u * (1.0 - u) * X.y + u * u * B.y;
    }

    return finishSmoothed(
        move,
        end,
        static_cast<int>(std::floor(a)),
        static_cast<int>(std::ceil(b)),
        goal,
        goal.crossings - 1);
}

/*
 * The moved stretch [a, b] laid along a new route: from where the move
 * begins (P) onto `path`, along it, and off it to where the move ends (Q),
 * the moved points evenly spaced along the way. The strand used to cross the
 * strand that `path` runs beside and now turns to follow it, so both corners
 * are cut: a quadratic Bezier with the end of the path as control point,
 * joining the path a margin's length (at most maxCut1, maxCut2) further on.
 */
QVector<Vec3> placeOnRoute(
    const QVector<Vec3> &start,
    const QVector<Vec3> &curve,
    double a,
    double b,
    const QVector<Vec3> &path,
    double maxCut1,
    double maxCut2)
{
    const QVector<double> pathArc = arcLengths(path);

    const Vec3 P = pointAt(curve, a);
    const Vec3 Q = pointAt(curve, b);

    const double cut1 = std::min(std::hypot(P.x - path.first().x, P.y - path.first().y), maxCut1);
    const double cut2 = std::min(std::hypot(Q.x - path.last().x, Q.y - path.last().y), maxCut2);

    auto bezier = [](const Vec3 &p0, const Vec3 &c, const Vec3 &p1, double u)
    {
        return p0 * ((1.0 - u) * (1.0 - u)) + c * (2.0 * u * (1.0 - u)) + p1 * (u * u);
    };

    const Vec3 E1 = alongPolyline(path, pathArc, cut1);
    const Vec3 E2 = alongPolyline(path, pathArc, pathArc.last() - cut2);

    QVector<Vec3> route;

    constexpr int bends = 24;

    for (int i = 0; i < bends; ++i)
        route << bezier(P, path.first(), E1, static_cast<double>(i) / bends);

    route << E1;

    for (int i = 0; i < path.size(); ++i)
    {
        if (pathArc[i] > cut1 && pathArc[i] < pathArc.last() - cut2)
            route << path[i];
    }

    for (int i = 0; i <= bends; ++i)
        route << bezier(E2, path.last(), Q, static_cast<double>(i) / bends);

    const QVector<double> routeArc = arcLengths(route);

    QVector<Vec3> end = start;

    const int n = curve.size();

    for (int k = static_cast<int>(std::floor(a)) + 1; k < b; ++k)
    {
        const Vec3 target = alongPolyline(route, routeArc, routeArc.last() * (k - a) / (b - a));

        Vec3 &q = end[wrap(k, n)];

        q.x = target.x;
        q.y = target.y;
    }

    return end;
}

/*
 * R2: the over strand (piece alpha) lies across the under strand (piece
 * beta) between the two crossings. It is pulled back over the empty
 * two-sided region until it runs beside beta, at distance d on the far side.
 */
bool makePokeMove(
    ReidemeisterMove &move,
    const QVector<Vec3> &curve,
    const Pieces &pieces,
    int alpha,
    int beta,
    const Goal &goal)
{
    const double s0 = pieces.startS(alpha);
    const double s1 = pieces.endS(alpha);
    const double length = s1 - s0;

    // beta from the first crossing of alpha to the second
    double b0, b1;

    if (pieces.other[alpha] == beta)
    {
        b0 = pieces.startS(beta);
        b1 = pieces.endS(beta);
    }
    else
    {
        b0 = pieces.endS(beta);
        b1 = pieces.startS(beta);
    }

    const double direction = (b1 > b0) ? 1.0 : -1.0;

    auto betaAt = [&](double u) { return pointAt(curve, b0 + u * (b1 - b0)); };
    auto alphaAt = [&](double u) { return pointAt(curve, s0 + u * length); };

    auto leftNormal = [&](double u)
    {
        const double s = b0 + u * (b1 - b0);

        Vec3 t = (pointAt(curve, s + 0.5 * direction) -
                  pointAt(curve, s - 0.5 * direction));

        t.z = 0.0;
        t = t.normalized();

        return Vec3(-t.y, t.x, 0.0);
    };

    // the side of beta the two-sided region (and alpha) is on
    double side = 0.0;

    for (const double u : {0.25, 0.5, 0.75})
    {
        Vec3 offset = alphaAt(u) - betaAt(u);

        offset.z = 0.0;

        side += Vec3::dot(offset, leftNormal(u));
    }

    const double outward = (side > 0.0) ? -1.0 : 1.0;

    // margins long enough to step out to the widest offset gently
    const double a = s0 - std::min(8.0, marginRoom(pieces.length(pieces.previous(alpha))));
    const double b = s1 + std::min(8.0, marginRoom(pieces.length(pieces.next(alpha))));

    move.start = liftPiece(curve, pieces, a, b, 1, false);

    const double sample = averageSample(curve);

    // a few samples away first, closer when the far side is crowded
    for (const double samples : {3.0, 2.2, 1.6, 1.1, 0.75, 0.5})
    {
        const double d = samples * sample;

        QVector<Vec3> path;

        constexpr int steps = 64;

        for (int i = 0; i <= steps; ++i)
        {
            const double u = static_cast<double>(i) / steps;

            path << betaAt(u) + leftNormal(u) * (outward * d);
        }

        const double cut = 0.35 * arcLengths(path).last();

        const QVector<Vec3> end = placeOnRoute(move.start, curve, a, b, path, cut, cut);

        if (finishSmoothed(
                move,
                end,
                static_cast<int>(std::floor(a)),
                static_cast<int>(std::ceil(b)),
                goal,
                goal.crossings - 2))
        {
            return true;
        }
    }

    return false;
}

// Point of a piece at fraction u of the way from the end at pass `from`
// (u = 0) to its other end (u = 1).
Vec3 pieceAt(const QVector<Vec3> &curve, const Pieces &pieces, int piece, int from, double u)
{
    const double s0 = pieces.startS(piece);
    const double s1 = pieces.endS(piece);

    return (piece == from) ? pointAt(curve, s0 + u * (s1 - s0))
                           : pointAt(curve, s1 - u * (s1 - s0));
}

// Unit normal of a piece at fraction u (as in pieceAt), on the side of the
// point `away` turned the other way: the side away from that point.
Vec3 pieceNormalAwayFrom(
    const QVector<Vec3> &curve,
    const Pieces &pieces,
    int piece,
    int from,
    double u,
    const Vec3 &away)
{
    const double step = 0.5 / std::max(1.0, pieces.length(piece));

    Vec3 t = pieceAt(curve, pieces, piece, from, u + step) -
             pieceAt(curve, pieces, piece, from, u - step);

    t.z = 0.0;
    t = t.normalized();

    Vec3 normal(-t.y, t.x, 0.0);

    Vec3 toAway = away - pieceAt(curve, pieces, piece, from, u);

    toAway.z = 0.0;

    if (Vec3::dot(normal, toAway) > 0.0)
        normal = normal * -1.0;

    return normal;
}

/*
 * R3, hugging: the moving strand (from A1 to A2) is pulled across the empty
 * triangle until it runs just outside its two other sides, at distance d:
 * first along side 1 (from A1 to the third corner V), then, past V, along
 * side 2 (from V to A2). The two offset sides meet at V', the point d away
 * from both strands on the far side of V, so the strand now crosses them
 * there, in the other order. Only the triangle and two thin strips beside
 * its sides are swept, so the rest of the picture keeps its shape.
 */
bool makeHugMove(
    ReidemeisterMove &move,
    const QVector<Vec3> &curve,
    const Pieces &pieces,
    int moving,
    int side1,
    int a1Pass,
    int side2,
    int v2Pass,
    const Vec3 &inside,
    const Goal &goal)
{
    const double s0 = pieces.startS(moving);
    const double s1 = pieces.endS(moving);

    // side 1 is walked from its pass at A1 to V, side 2 from its pass at V
    // to A2
    auto onSide1 = [&](double u) { return pieceAt(curve, pieces, side1, a1Pass, u); };
    auto onSide2 = [&](double u) { return pieceAt(curve, pieces, side2, v2Pass, u); };

    auto normal1 = [&](double u)
    {
        return pieceNormalAwayFrom(curve, pieces, side1, a1Pass, u, inside);
    };

    auto normal2 = [&](double u)
    {
        return pieceNormalAwayFrom(curve, pieces, side2, v2Pass, u, inside);
    };

    // margins long enough to step out to the widest offset gently
    const double a = s0 - std::min(8.0, marginRoom(pieces.length(pieces.previous(moving))));
    const double b = s1 + std::min(8.0, marginRoom(pieces.length(pieces.next(moving))));

    const bool over = pieces.passes[moving].over;

    move.start = liftPiece(curve, pieces, a, b, over ? 1 : -1, false);

    const Vec3 V = onSide1(1.0);

    const double sample = averageSample(curve);

    // A few samples out first, so the new crossings are not crowded
    // together at V; closer when the neighbouring faces are narrow.
    for (const double samples : {3.0, 2.2, 1.6, 1.1, 0.75, 0.5})
    {
        const double d = samples * sample;

        // V': where the two offset sides meet, from their directions at V
        const Vec3 n1 = normal1(1.0);
        const Vec3 n2 = normal2(0.0);

        const Vec3 t1(n1.y, -n1.x, 0.0);
        const Vec3 t2(n2.y, -n2.x, 0.0);

        const double det = t1.x * t2.y - t1.y * t2.x;

        Vec3 cornerV = V + (n1 + n2).normalized() * d;

        if (std::abs(det) > 1e-6)
        {
            // V + n1 d + x t1 = V + n2 d + y t2
            const Vec3 r = (n2 - n1) * d;
            const double x = (r.x * t2.y - r.y * t2.x) / det;

            cornerV = V + n1 * d + t1 * x;
        }

        // not too far out when the strands meet at a shallow angle
        Vec3 out = cornerV - V;

        out.z = 0.0;

        if (out.length() > 2.5 * d)
            cornerV = V + out.normalized() * (2.5 * d);

        // the hug: offset side 1, V', offset side 2
        QVector<Vec3> hug;

        constexpr int steps = 48;

        for (int i = 0; i <= steps; ++i)
        {
            const double u = static_cast<double>(i) / steps;

            hug << onSide1(u) + normal1(u) * d;
        }

        hug << cornerV;

        for (int i = 0; i <= steps; ++i)
        {
            const double u = static_cast<double>(i) / steps;

            hug << onSide2(u) + normal2(u) * d;
        }

        const QVector<double> hugArc = arcLengths(hug);

        const QVector<Vec3> end = placeOnRoute(
            move.start, curve, a, b, hug,
            0.35 * hugArc[steps],
            0.35 * (hugArc.last() - hugArc[steps + 1]));

        if (finishSmoothed(
                move,
                end,
                static_cast<int>(std::floor(a)),
                static_cast<int>(std::ceil(b)),
                goal,
                goal.crossings,
                {48, 16, 6, 0}))
        {
            return true;
        }
    }

    return false;
}

/*
 * R3, sliding: the moving strand crosses the two other sides of the triangle
 * at A1 and A2, and the third corner V is the crossing of the other two
 * strands. The strand is shifted past V (by a bit more than the way from the
 * middle of A1 A2 to V), so it crosses both strands on the other side of V.
 */
bool makeSlideMove(
    ReidemeisterMove &move,
    const QVector<Vec3> &curve,
    const Pieces &pieces,
    int moving,
    const Vec3 &V,
    const Goal &goal)
{
    const double s0 = pieces.startS(moving);
    const double s1 = pieces.endS(moving);

    const Vec3 A1 = pointAt(curve, s0);
    const Vec3 A2 = pointAt(curve, s1);

    Vec3 D = V - (A1 + A2) * 0.5;

    D.z = 0.0;

    const double sample = averageSample(curve);

    // margins about as long as the shift, so the strand bends gently
    const double wanted = std::max(4.0, 1.2 * D.length() / sample);

    const double a = s0 - std::min(wanted, marginRoom(pieces.length(pieces.previous(moving))));
    const double b = s1 + std::min(wanted, marginRoom(pieces.length(pieces.next(moving))));

    const bool over = pieces.passes[moving].over;

    move.start = liftPiece(curve, pieces, a, b, over ? 1 : -1, false);

    // just past V first: the smaller the shift, the less the picture changes
    for (const double lambda : {1.3, 1.5, 1.75, 1.15, 2.0})
    {
        QVector<Vec3> end = move.start;

        for (int k = static_cast<int>(std::floor(a)) + 1; k < b; ++k)
        {
            Vec3 &q = end[wrap(k, pieces.n)];

            const double w = pieceWeight(k, a, s0, s1, b);

            q.x += lambda * w * D.x;
            q.y += lambda * w * D.y;
        }

        if (finishSmoothed(
                move,
                end,
                static_cast<int>(std::floor(a)),
                static_cast<int>(std::ceil(b)),
                goal,
                goal.crossings))
        {
            return true;
        }
    }

    return false;
}

} // namespace

KnotDiagram diagramFromHeights(const QVector<Vec3> &curve)
{
    KnotDiagram diagram;

    const int n = curve.size();

    diagram.points.reserve(n);

    for (const Vec3 &p : curve)
        diagram.points.push_back(Vec3(p.x, p.y, 0.0));

    detectDiagramCrossings(diagram);

    for (DiagramCrossing &c : diagram.crossings)
    {
        const double zA = curve[c.segA].z +
                          (curve[(c.segA + 1) % n].z - curve[c.segA].z) * c.tA;
        const double zB = curve[c.segB].z +
                          (curve[(c.segB + 1) % n].z - curve[c.segB].z) * c.tB;

        c.aOver = (zA >= zB);
    }

    return diagram;
}

QVector<ReidemeisterMove> findReidemeisterMoves(const KnotDiagram &diagram)
{
    QVector<ReidemeisterMove> moves;

    if (!diagram.isValid() || diagram.crossings.isEmpty())
        return moves;

    // The moves work on the curve with heights (which has extra points at
    // the passes, so over and under are exact) and on its own diagram.
    const QVector<Vec3> curve = diagramToCurve(diagram, kHeight);
    const KnotDiagram work = diagramFromHeights(curve);

    if (work.crossings.size() != diagram.crossings.size())
        return moves;

    const Pieces pieces = makePieces(work);
    const int count = pieces.count();

    Goal goal;

    goal.crossings = diagram.crossings.size();
    goal.points = diagram.points.size();

    spacing(diagram, goal.minGap, goal.minDistance);

    goal.minGap = std::min(goal.minGap, 1.0);
    goal.minDistance = std::min(goal.minDistance, 0.06);

    // crossing of a pass, numbered as in the diagram
    QVector<int> original(work.crossings.size(), 0);

    for (int k = 0; k < work.crossings.size(); ++k)
    {
        original[k] = nearestDiagramCrossing(
            diagram, work.crossings[k].position.x, work.crossings[k].position.y, 1e300);
    }

    auto crossingOf = [&](int pass) { return pieces.passes[pass].crossing; };

    auto addMove = [&](ReidemeisterMove &move, const QVector<int> &face, int firstPass)
    {
        move.region = faceOutline(curve, pieces, face, firstPass);

        if (move.region.size() < 3 || !faceIsEmpty(curve, pieces, face, move.region))
            return false;

        move.centre = deepestPoint(move.region);

        return true;
    };

    // R1: a piece from a crossing back to itself
    for (int i = 0; i < count; ++i)
    {
        const int j = pieces.next(i);

        if (crossingOf(i) != crossingOf(j))
            continue;

        ReidemeisterMove move;

        move.type = 1;
        move.crossings = {original[crossingOf(i)]};

        if (addMove(move, {i}, i) &&
            makeTwistMove(move, curve, pieces, i, goal))
        {
            moves.push_back(move);
        }
    }

    // R2: a piece over at both ends whose crossings are joined by a second
    // piece (under at both ends, then)
    for (int i = 0; i < count; ++i)
    {
        const int j = pieces.next(i);

        if (crossingOf(i) == crossingOf(j) ||
            !pieces.passes[i].over ||
            !pieces.passes[j].over)
        {
            continue;
        }

        const int beta = pieces.pieceBetween(pieces.other[i], pieces.other[j]);

        if (beta < 0)
            continue;

        ReidemeisterMove move;

        move.type = 2;
        move.crossings = {original[crossingOf(i)], original[crossingOf(j)]};

        if (addMove(move, {i, beta}, i) &&
            makePokeMove(move, curve, pieces, i, beta, goal))
        {
            moves.push_back(move);
        }
    }

    // R3: a triangle of three pieces at three different crossings, one of
    // them over (or under) at both of its ends; one move per triangle
    std::set<std::set<int>> triangles;

    for (int m = 0; m < count; ++m)
    {
        const int p = m;
        const int q = pieces.next(m);

        if (crossingOf(p) == crossingOf(q) ||
            pieces.passes[p].over != pieces.passes[q].over)
        {
            continue;
        }

        const int q2 = pieces.other[q];

        for (const int r : {pieces.next(q2), pieces.previous(q2)})
        {
            const int k3 = crossingOf(r);

            if (k3 == crossingOf(p) || k3 == crossingOf(q))
                continue;

            const int second = pieces.pieceBetween(q2, r);
            const int third = pieces.pieceBetween(pieces.other[r], pieces.other[p]);

            if (second < 0 || third < 0)
                continue;

            const std::set<int> key = {m, second, third};

            if (key.size() != 3 || triangles.count(key) > 0)
                continue;

            ReidemeisterMove move;

            move.type = 3;
            move.crossings = {original[crossingOf(p)], original[crossingOf(q)], original[k3]};

            // the triangle's other sides: `third` from A1 to V, `second`
            // from V to A2
            if (addMove(move, {m, second, third}, p) &&
                (makeHugMove(move, curve, pieces, m, third, pieces.other[p],
                             second, r, move.centre, goal) ||
                 makeSlideMove(move, curve, pieces, m,
                               work.crossings.at(k3).position, goal)))
            {
                triangles.insert(key);
                moves.push_back(move);
            }
        }
    }

    return moves;
}

int reidemeisterMoveAt(
    const QVector<ReidemeisterMove> &moves,
    double x,
    double y)
{
    for (int i = 0; i < moves.size(); ++i)
    {
        if (insidePolygon(moves.at(i).region, x, y))
            return i;
    }

    return -1;
}

KnotDiagram reidemeisterFrame(const ReidemeisterMove &move, double t)
{
    const double e = ease(t);

    QVector<Vec3> curve = move.start;

    for (int i = 0; i < curve.size() && i < move.end.size(); ++i)
    {
        curve[i].x += (move.end[i].x - curve[i].x) * e;
        curve[i].y += (move.end[i].y - curve[i].y) * e;
    }

    return diagramFromHeights(curve);
}

bool reidemeisterMoveKeepsKnot(
    const KnotDiagram &before,
    const ReidemeisterMove &move)
{
    const AlexanderPolynomial p = computeAlexanderPolynomial(diagramToCurve(before, kHeight));
    const AlexanderPolynomial q = computeAlexanderPolynomial(diagramToCurve(move.result, kHeight));

    if (!p.valid || !q.valid)
        return true;

    return p.lowestPower == q.lowestPower && p.coefficients == q.coefficients;
}

} // namespace KnotMath
