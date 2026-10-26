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

} // namespace

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
        smooth = refineCurve(smooth, (300 + smooth.size() - 1) / smooth.size(), true);

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
        double z = 0.0;

        const int m = nodeS.size();

        if (m > 0)
        {
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

            z = span > 1e-9
                    ? z0 + (z1 - z0) * smooth01((s - s0) / span)
                    : z0;
        }

        return z;
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
