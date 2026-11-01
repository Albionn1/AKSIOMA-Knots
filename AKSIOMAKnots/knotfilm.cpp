#include "knotseifert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace KnotMath
{

/*
 * The surface as one mesh, and the film it relaxes into.
 *
 * Disks and bands are meshed together: the disks by refining a triangulation
 * of their polygons (edges split and flipped until it is Delaunay), the bands
 * as grids that share their vertices with the disks. Every triangle runs
 * round its positive side, so that the knot, with its own direction, is the
 * oriented boundary of the surface.
 *
 * The film is that surface with the least area its wire allows: a soap film.
 * A surface is minimal exactly when its coordinates are harmonic on the
 * surface itself (Laplace-Beltrami x = 2 H n = 0), so, as Pinkall and Polthier
 * do, the cotangent weights of the surface as it is now are taken and every
 * inner vertex moved towards their weighted middle, over and over. Unlike a
 * real film, this one is held away from itself: sheets that come close are
 * pushed apart, so that it stays the same surface of the same genus.
 */

namespace
{

// the size of the triangles, times the extent
constexpr double MeshSize = 0.025;

// how close two sheets of the film may come, times the extent (more than the
// wire's radius, seifertTubeRadius; more than a triangle, so that no vertex
// slips between the vertices of the other sheet)
constexpr double FilmGap = 0.035;

// how far a vertex may move in one sweep, times the extent
constexpr double FilmStep = 0.006;

// The wire is taken out of its plane by a map of all of space (see
// raiseIntoSpace): the levels stretched by this much, and a harmonic wave of
// this height (times the extent) over the model.
constexpr double LevelStretch = 1.6;
constexpr double WaveHeight = 0.22;

constexpr int FilmRounds = 140;
constexpr int SweepsPerRound = 4;

// how often the film is checked for passing through itself
constexpr int FilmCheckEvery = 10;

struct PlanarMesh
{
    QVector<Vec3> points;
    QVector<int> triangles;

    // points before this index are on the polygon, and stay
    int fixedCount = 0;
};

// > 0 when a, b, c turn the way of the polygon
double turning(const Vec3 &a, const Vec3 &b, const Vec3 &c, double orientation)
{
    return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * orientation;
}

// d lies inside the circle through a, b, c (a, b, c turning the polygon's way)
bool inCircle(const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, double orientation)
{
    const double ax = a.x - d.x;
    const double ay = a.y - d.y;
    const double bx = b.x - d.x;
    const double by = b.y - d.y;
    const double cx = c.x - d.x;
    const double cy = c.y - d.y;

    const double determinant =
        (ax * ax + ay * ay) * (bx * cy - cx * by) -
        (bx * bx + by * by) * (ax * cy - cx * ay) +
        (cx * cx + cy * cy) * (ax * by - bx * ay);

    return determinant * orientation > 1.0e-12;
}

// the third corner of a triangle, starting from edge (a, b): rotates the
// triangle `t` so that it starts with a
void startAt(std::array<int, 3> &t, int a)
{
    while (t[0] != a)
        std::rotate(t.begin(), t.begin() + 1, t.end());
}

struct Edge
{
    int a = 0;
    int b = 0;
    int t0 = 0;
    int t1 = -1;
};

QVector<Edge> edgesOf(const QVector<int> &triangles)
{
    struct Side
    {
        qint64 key;
        int triangle;
    };

    const int count = triangles.size() / 3;

    QVector<Side> sides;

    sides.reserve(3 * count);

    for (int t = 0; t < count; ++t)
    {
        for (int k = 0; k < 3; ++k)
        {
            const int a = triangles[3 * t + k];
            const int b = triangles[3 * t + (k + 1) % 3];

            sides.push_back({(static_cast<qint64>(std::min(a, b)) << 32) | std::max(a, b), t});
        }
    }

    std::sort(sides.begin(), sides.end(), [](const Side &x, const Side &y) { return x.key < y.key; });

    QVector<Edge> edges;

    edges.reserve(sides.size() / 2 + 8);

    for (int i = 0; i < sides.size(); ++i)
    {
        Edge edge;

        edge.a = static_cast<int>(sides[i].key >> 32);
        edge.b = static_cast<int>(sides[i].key & 0xFFFFFFFFLL);
        edge.t0 = sides[i].triangle;

        if (i + 1 < sides.size() && sides[i + 1].key == sides[i].key)
        {
            edge.t1 = sides[i + 1].triangle;
            ++i;
        }

        edges.push_back(edge);
    }

    return edges;
}

std::array<int, 3> triangleAt(const QVector<int> &triangles, int t)
{
    return {triangles[3 * t], triangles[3 * t + 1], triangles[3 * t + 2]};
}

void setTriangle(QVector<int> &triangles, int t, int a, int b, int c)
{
    triangles[3 * t] = a;
    triangles[3 * t + 1] = b;
    triangles[3 * t + 2] = c;
}

// Lawson's flips: any triangulation of a polygon becomes its Delaunay one.
void makeDelaunay(PlanarMesh &mesh, double orientation)
{
    for (int pass = 0; pass < 40; ++pass)
    {
        const QVector<Edge> edges = edgesOf(mesh.triangles);

        QVector<bool> used(mesh.triangles.size() / 3, false);

        bool changed = false;

        for (const Edge &edge : edges)
        {
            const int t0 = edge.t0;
            const int t1 = edge.t1;

            if (t1 < 0 || used[t0] || used[t1])
                continue;

            const int a = edge.a;
            const int b = edge.b;

            std::array<int, 3> first = triangleAt(mesh.triangles, t0);
            std::array<int, 3> second = triangleAt(mesh.triangles, t1);

            // first runs a -> b -> c, second b -> a -> d
            int from = a;
            int to = b;

            startAt(first, from);

            if (first[1] != to)
            {
                std::swap(from, to);
                startAt(first, from);
            }

            startAt(second, to);

            const int c = first[2];
            const int d = second[2];

            const Vec3 &pa = mesh.points[from];
            const Vec3 &pb = mesh.points[to];
            const Vec3 &pc = mesh.points[c];
            const Vec3 &pd = mesh.points[d];

            if (!inCircle(pa, pb, pc, pd, orientation))
                continue;

            // the new triangles must turn the right way too
            if (turning(pa, pd, pc, orientation) <= 1.0e-12 || turning(pd, pb, pc, orientation) <= 1.0e-12)
                continue;

            setTriangle(mesh.triangles, t0, from, d, c);
            setTriangle(mesh.triangles, t1, d, to, c);

            used[t0] = true;
            used[t1] = true;

            changed = true;
        }

        if (!changed)
            return;
    }
}

// Inner edges longer than `size` are split until none is left.
void refine(PlanarMesh &mesh, double size, double orientation)
{
    const double longest = 1.35 * size;

    for (int round = 0; round < 24; ++round)
    {
        const QVector<Edge> edges = edgesOf(mesh.triangles);

        struct Long
        {
            double length;
            int a;
            int b;
            int t0;
            int t1;
        };

        QVector<Long> splits;

        for (const Edge &edge : edges)
        {
            if (edge.t1 < 0)
                continue;

            const double length = (mesh.points[edge.a] - mesh.points[edge.b]).length();

            if (length > longest)
                splits.push_back({length, edge.a, edge.b, edge.t0, edge.t1});
        }

        if (splits.isEmpty())
            break;

        std::sort(splits.begin(), splits.end(), [](const Long &x, const Long &y) { return x.length > y.length; });

        QVector<bool> used(mesh.triangles.size() / 3, false);

        for (const Long &split : splits)
        {
            if (used[split.t0] || used[split.t1])
                continue;

            used[split.t0] = true;
            used[split.t1] = true;

            std::array<int, 3> first = triangleAt(mesh.triangles, split.t0);
            std::array<int, 3> second = triangleAt(mesh.triangles, split.t1);

            int from = split.a;
            int to = split.b;

            startAt(first, from);

            if (first[1] != to)
            {
                std::swap(from, to);
                startAt(first, from);
            }

            startAt(second, to);

            const int c = first[2];
            const int d = second[2];

            const int middle = mesh.points.size();

            mesh.points.push_back((mesh.points[from] + mesh.points[to]) * 0.5);

            setTriangle(mesh.triangles, split.t0, from, middle, c);
            setTriangle(mesh.triangles, split.t1, middle, from, d);

            mesh.triangles << middle << to << c;
            mesh.triangles << to << middle << d;
        }

        makeDelaunay(mesh, orientation);
    }

    // the inner points evened out; a point only moves where its triangles
    // stay the right way round
    for (int round = 0; round < 4; ++round)
    {
        QVector<Vec3> sum(mesh.points.size());
        QVector<int> count(mesh.points.size(), 0);

        for (int t = 0; t < mesh.triangles.size() / 3; ++t)
        {
            for (int k = 0; k < 3; ++k)
            {
                const int a = mesh.triangles[3 * t + k];
                const int b = mesh.triangles[3 * t + (k + 1) % 3];

                sum[a] += mesh.points[b];
                sum[b] += mesh.points[a];
                ++count[a];
                ++count[b];
            }
        }

        const QVector<Vec3> before = mesh.points;

        for (int i = mesh.fixedCount; i < mesh.points.size(); ++i)
            mesh.points[i] = sum[i] / static_cast<double>(std::max(count[i], 1));

        for (int t = 0; t < mesh.triangles.size() / 3; ++t)
        {
            const int a = mesh.triangles[3 * t];
            const int b = mesh.triangles[3 * t + 1];
            const int c = mesh.triangles[3 * t + 2];

            if (turning(mesh.points[a], mesh.points[b], mesh.points[c], orientation) <= 1.0e-12)
            {
                mesh.points = before;
                break;
            }
        }

        makeDelaunay(mesh, orientation);
    }
}

/*
 * One vertex's neighbours and the weights of the edges to them, in one long
 * array, with for every corner of every triangle the two places of its
 * opposite edge.
 */
struct Neighbours
{
    QVector<int> start;
    QVector<int> vertex;
    QVector<double> weight;
    QVector<std::array<int, 2>> edgeOf;

    void build(int vertices, const QVector<int> &triangles)
    {
        QVector<QVector<int>> around(vertices);

        const int count = triangles.size() / 3;

        for (int t = 0; t < count; ++t)
        {
            for (int k = 0; k < 3; ++k)
            {
                const int a = triangles[3 * t + k];
                const int b = triangles[3 * t + (k + 1) % 3];

                if (!around[a].contains(b))
                    around[a].push_back(b);

                if (!around[b].contains(a))
                    around[b].push_back(a);
            }
        }

        start = QVector<int>(vertices + 1, 0);

        for (int i = 0; i < vertices; ++i)
            start[i + 1] = start[i] + around[i].size();

        vertex.resize(start[vertices]);
        weight = QVector<double>(start[vertices], 0.0);

        for (int i = 0; i < vertices; ++i)
        {
            for (int k = 0; k < around[i].size(); ++k)
                vertex[start[i] + k] = around[i][k];
        }

        auto place = [this](int a, int b)
        {
            for (int k = start[a]; k < start[a + 1]; ++k)
            {
                if (vertex[k] == b)
                    return k;
            }

            return -1;
        };

        edgeOf.resize(3 * count);

        for (int t = 0; t < count; ++t)
        {
            for (int k = 0; k < 3; ++k)
            {
                // the edge across from corner k
                const int a = triangles[3 * t + (k + 1) % 3];
                const int b = triangles[3 * t + (k + 2) % 3];

                edgeOf[3 * t + k] = {place(a, b), place(b, a)};
            }
        }
    }

    // cotangent weights of the surface as it is now (never quite 0 or
    // infinite: a thin triangle must not tear the sheet)
    void setCotangents(const QVector<Vec3> &x, const QVector<int> &triangles)
    {
        std::fill(weight.begin(), weight.end(), 0.0);

        const int count = triangles.size() / 3;

        for (int t = 0; t < count; ++t)
        {
            for (int k = 0; k < 3; ++k)
            {
                const Vec3 &p = x[triangles[3 * t + k]];
                const Vec3 u = x[triangles[3 * t + (k + 1) % 3]] - p;
                const Vec3 v = x[triangles[3 * t + (k + 2) % 3]] - p;

                const double sine = Vec3::cross(u, v).length();
                const double cotangent = sine > 1.0e-14 ? Vec3::dot(u, v) / sine : 0.0;

                const double w = 0.5 * std::clamp(cotangent, 0.05, 6.0);

                weight[edgeOf[3 * t + k][0]] += w;
                weight[edgeOf[3 * t + k][1]] += w;
            }
        }
    }
};

/*
 * Sheets pushed apart: any two vertices nearer than `gap` that are not close
 * along the surface itself.
 */
class Separation
{
public:
    Separation(const Neighbours &neighbours, int vertices, double gap, int rings)
        : gap_(gap)
    {
        // what lies within `rings` edges along the surface, sorted
        near_.resize(vertices);

        QVector<int> mark(vertices, -1);

        for (int i = 0; i < vertices; ++i)
        {
            QVector<int> ring{i};

            mark[i] = i;

            int head = 0;

            for (int r = 0; r < rings; ++r)
            {
                const int end = ring.size();

                for (; head < end; ++head)
                {
                    const int v = ring[head];

                    for (int k = neighbours.start[v]; k < neighbours.start[v + 1]; ++k)
                    {
                        const int w = neighbours.vertex[k];

                        if (mark[w] != i)
                        {
                            mark[w] = i;
                            ring.push_back(w);
                        }
                    }
                }
            }

            std::sort(ring.begin(), ring.end());

            near_[i] = ring;
        }

        head_ = QVector<int>(1 << 16, -1);
        next_ = QVector<int>(vertices, -1);
    }

    // `normals`: the positive sides, as they are now
    void apply(QVector<Vec3> &x, const QVector<Vec3> &normals, const QVector<bool> &fixed)
    {
        const int vertices = x.size();

        // (written straight into the arrays: this runs a great many times)
        Vec3 *const p = x.data();
        int *const head = head_.data();
        int *const next = next_.data();

        auto cell = [this](double value) { return static_cast<int>(std::floor(value / gap_)); };

        // (unsigned: the products are meant to run over)
        auto bucket = [](unsigned int a, unsigned int b, unsigned int c)
        {
            return (a * 73856093u ^ b * 19349663u ^ c * 83492791u) & 0xFFFFu;
        };

        std::fill(head_.begin(), head_.end(), -1);

        for (int i = 0; i < vertices; ++i)
        {
            const unsigned int b = bucket(cell(p[i].x), cell(p[i].y), cell(p[i].z));

            next[i] = head[b];
            head[b] = i;
        }

        for (int i = 0; i < vertices; ++i)
        {
            const int cx = cell(p[i].x);
            const int cy = cell(p[i].y);
            const int cz = cell(p[i].z);

            for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int j = head[bucket(cx + dx, cy + dy, cz + dz)]; j >= 0; j = next[j])
                {
                    if (j <= i || (fixed[i] && fixed[j]))
                        continue;

                    const double ex = p[i].x - p[j].x;
                    const double ey = p[i].y - p[j].y;
                    const double ez = p[i].z - p[j].z;

                    const double squared = ex * ex + ey * ey + ez * ez;

                    if (squared >= gap_ * gap_)
                        continue;

                    const Vec3 d(ex, ey, ez);
                    const double length = std::sqrt(squared);

                    if (length < 1.0e-12)
                        continue;

                    // Close along the surface as well: the same sheet, unless
                    // they face away from each other. Then it has folded over
                    // between them, and the fold must not pinch shut.
                    if (Vec3::dot(normals[i], normals[j]) > -0.2 &&
                        std::binary_search(near_[i].cbegin(), near_[i].cend(), j))
                    {
                        continue;
                    }

                    const Vec3 push = d * ((gap_ - length) / length);

                    if (fixed[j])
                    {
                        p[i] += push;
                    }
                    else if (fixed[i])
                    {
                        p[j] -= push;
                    }
                    else
                    {
                        p[i] += push * 0.5;
                        p[j] -= push * 0.5;
                    }
                }
            }
        }
    }

private:
    double gap_;
    QVector<QVector<int>> near_;
    QVector<int> head_;
    QVector<int> next_;
};

/*
 * (x, y, z) -> (x, y, k z + h(x, y)), k > 0, is a smooth, orientation keeping
 * map of all of space onto itself, so it cannot change the knot, the surface
 * or any linking number: what lay over something lies over it still. It only
 * lifts the flat model into space.
 *
 * h is the real part of a (x + i y)^m, a harmonic function: a saddle with m
 * rises and m falls, the very shape of a minimal surface. m and the turn of a
 * follow the wire: the harmonic it has most of, so a trefoil's three lobes all
 * rise alike.
 */
struct Wave
{
    int order = 3;
    double re = 1.0;
    double im = 0.0;
    double scale = 1.0;
    double height = 0.0;
    double stretch = LevelStretch;

    Vec3 raise(const Vec3 &p) const
    {
        double zr = 1.0;
        double zi = 0.0;

        const double x = p.x / scale;
        const double y = p.y / scale;

        for (int k = 0; k < order; ++k)
        {
            const double r = zr * x - zi * y;

            zi = zr * y + zi * x;
            zr = r;
        }

        return Vec3(p.x, p.y, stretch * p.z + height * (re * zr - im * zi));
    }
};

Wave waveOf(const QVector<Vec3> &wire, double extent)
{
    double reach = 0.0;

    double cx = 0.0;
    double cy = 0.0;

    for (const Vec3 &p : wire)
    {
        cx += p.x;
        cy += p.y;
    }

    cx /= std::max<int>(1, wire.size());
    cy /= std::max<int>(1, wire.size());

    for (const Vec3 &p : wire)
        reach = std::max(reach, std::hypot(p.x - cx, p.y - cy));

    Wave wave;

    wave.scale = std::max(reach, 1.0e-9);
    wave.height = WaveHeight * extent;

    double best = -1.0;

    for (const int order : {2, 3})
    {
        double re = 0.0;
        double im = 0.0;
        double total = 0.0;

        for (const Vec3 &p : wire)
        {
            const double r = std::hypot(p.x, p.y) / wave.scale;
            const double angle = std::atan2(p.y, p.x);
            const double size = std::pow(r, order);

            re += size * std::cos(order * angle);
            im += size * std::sin(order * angle);
            total += size;
        }

        const double strength = std::hypot(re, im) / std::max(total, 1.0e-12);

        // (three-fold is the livelier of the two when neither stands out)
        if (strength * (order == 3 ? 1.3 : 1.0) > best)
        {
            best = strength * (order == 3 ? 1.3 : 1.0);

            wave.order = order;
            wave.re = strength > 1.0e-9 ? re / std::hypot(re, im) : 1.0;
            wave.im = strength > 1.0e-9 ? -im / std::hypot(re, im) : 0.0;
        }
    }

    return wave;
}

double meshArea(const QVector<Vec3> &x, const QVector<int> &triangles)
{
    double area = 0.0;

    for (int t = 0; t + 2 < triangles.size(); t += 3)
    {
        area += 0.5 * Vec3::cross(x[triangles[t + 1]] - x[triangles[t]], x[triangles[t + 2]] - x[triangles[t]]).length();
    }

    return area;
}

// the point of triangle a, b, c nearest to p, as (b, c) weights
void nearestOnTriangle(const Vec3 &p, const Vec3 &a, const Vec3 &b, const Vec3 &c, double &wb, double &wc)
{
    const Vec3 e0 = b - a;
    const Vec3 e1 = c - a;
    const Vec3 d = p - a;

    const double d00 = Vec3::dot(e0, e0);
    const double d01 = Vec3::dot(e0, e1);
    const double d11 = Vec3::dot(e1, e1);
    const double d20 = Vec3::dot(d, e0);
    const double d21 = Vec3::dot(d, e1);

    const double determinant = d00 * d11 - d01 * d01;

    if (determinant <= 1.0e-30)
    {
        wb = 0.0;
        wc = 0.0;
        return;
    }

    wb = (d11 * d20 - d01 * d21) / determinant;
    wc = (d00 * d21 - d01 * d20) / determinant;

    if (wb >= 0.0 && wc >= 0.0 && wb + wc <= 1.0)
        return;

    // outside: the nearest of the three edges
    auto clamp01 = [](double t) { return std::clamp(t, 0.0, 1.0); };

    const double t0 = clamp01(d20 / std::max(d00, 1.0e-30));
    const double t1 = clamp01(d21 / std::max(d11, 1.0e-30));

    const Vec3 bc = c - b;
    const double t2 = clamp01(Vec3::dot(p - b, bc) / std::max(Vec3::dot(bc, bc), 1.0e-30));

    const double to0 = (p - (a + e0 * t0)).lengthSquared();
    const double to1 = (p - (a + e1 * t1)).lengthSquared();
    const double to2 = (p - (b + bc * t2)).lengthSquared();

    if (to0 <= to1 && to0 <= to2)
    {
        wb = t0;
        wc = 0.0;
    }
    else if (to1 <= to2)
    {
        wb = 0.0;
        wc = t1;
    }
    else
    {
        wb = 1.0 - t2;
        wc = t2;
    }
}

/*
 * The loops of the Seifert matrix as places on the mesh: found on the model,
 * where they were made, they go wherever the mesh goes.
 */
void placeLoops(SeifertSheet &sheet, const SeifertSurface &surface, double size)
{
    const int count = sheet.triangles.size() / 3;

    const double cell = 2.0 * size;

    auto key = [cell](const Vec3 &p, int dx, int dy, int dz)
    {
        const qint64 x = static_cast<qint64>(std::floor(p.x / cell)) + dx + 100000;
        const qint64 y = static_cast<qint64>(std::floor(p.y / cell)) + dy + 100000;
        const qint64 z = static_cast<qint64>(std::floor(p.z / cell)) + dz + 100000;

        return (x << 42) ^ (y << 21) ^ z;
    };

    std::map<qint64, QVector<int>> grid;

    for (int t = 0; t < count; ++t)
    {
        const Vec3 middle = (sheet.model[sheet.triangles[3 * t]] + sheet.model[sheet.triangles[3 * t + 1]] +
                             sheet.model[sheet.triangles[3 * t + 2]]) / 3.0;

        grid[key(middle, 0, 0, 0)].push_back(t);
    }

    for (const QVector<Vec3> &loop : surface.loops)
    {
        // no longer steps than the triangles: the film bends under them
        QVector<Vec3> fine;

        const int n = loop.size();

        for (int i = 0; i < n; ++i)
        {
            const Vec3 &a = loop[i];
            const Vec3 &b = loop[(i + 1) % n];

            const double length = (b - a).length();

            if (length < 1.0e-12)
                continue;

            const int parts = std::max(1, static_cast<int>(std::ceil(length / size)));

            for (int k = 0; k < parts; ++k)
                fine.push_back(a + (b - a) * (static_cast<double>(k) / parts));
        }

        QVector<SeifertSheet::Place> places;

        places.reserve(fine.size());

        for (const Vec3 &p : fine)
        {
            SeifertSheet::Place best;

            double nearest = std::numeric_limits<double>::max();

            for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
            {
                const auto it = grid.find(key(p, dx, dy, dz));

                if (it == grid.end())
                    continue;

                for (const int t : it->second)
                {
                    const Vec3 &a = sheet.model[sheet.triangles[3 * t]];
                    const Vec3 &b = sheet.model[sheet.triangles[3 * t + 1]];
                    const Vec3 &c = sheet.model[sheet.triangles[3 * t + 2]];

                    double wb = 0.0;
                    double wc = 0.0;

                    nearestOnTriangle(p, a, b, c, wb, wc);

                    const double distance =
                        (a * (1.0 - wb - wc) + b * wb + c * wc - p).lengthSquared();

                    if (distance < nearest)
                    {
                        nearest = distance;

                        best.triangle = t;
                        best.b = wb;
                        best.c = wc;
                    }
                }
            }

            places.push_back(best);
        }

        sheet.loopPlaces.push_back(places);
    }
}

// Triangles of an outline with points on its straight edges: of the outline
// without them (they would be ears of no area), each then split along its
// edge to take them in.
QVector<int> triangulateOutline(const QVector<Vec3> &outline, double orientation)
{
    const int n = outline.size();

    QVector<int> corners;

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &a = outline[(i + n - 1) % n];
        const Vec3 &b = outline[i];
        const Vec3 &c = outline[(i + 1) % n];

        const double area = std::abs(turning(a, b, c, orientation));

        if (area > 1.0e-9 * (b - a).length() * (c - b).length())
            corners.push_back(i);
    }

    if (corners.size() < 3)
        return {};

    QVector<Vec3> polygon;

    for (const int i : corners)
        polygon.push_back(outline[i]);

    const QVector<int> coarse = triangulatePolygon(polygon);

    if (coarse.size() != 3 * (corners.size() - 2))
        return {};

    QVector<int> triangles;

    for (const int index : coarse)
        triangles.push_back(corners[index]);

    for (int k = 0; k < corners.size(); ++k)
    {
        const int from = corners[k];
        const int to = corners[(k + 1) % corners.size()];

        if ((from + 1) % n == to)
            continue;

        // the triangle on this edge becomes a fan
        for (int t = 0; t < triangles.size(); t += 3)
        {
            for (int e = 0; e < 3; ++e)
            {
                if (triangles[t + e] != from || triangles[t + (e + 1) % 3] != to)
                    continue;

                const int apex = triangles[t + (e + 2) % 3];

                int a = from;
                int b = (from + 1) % n;

                triangles[t] = a;
                triangles[t + 1] = b;
                triangles[t + 2] = apex;

                while (b != to)
                {
                    a = b;
                    b = (b + 1) % n;

                    triangles << a << b << apex;
                }

                t = triangles.size();

                break;
            }
        }
    }

    return triangles;
}

// The polygon of a disk at the mesh's size. Band `band` joins it along the
// edge from polygon point `from`; those edges are split evenly, as the band's
// own grid will be.
struct DiskEdge
{
    int band = -1;
    int side = 0;
    int pieces = 2;
};

} // namespace

namespace
{

// linking numbers of the loops on this shape of the sheet with the loops lifted
// off it: the Seifert matrix of the surface, entry for entry
bool sameSeifertMatrix(const SeifertSheet &sheet, const SeifertSurface &surface, const QVector<Vec3> &x)
{
    if (!surface.matrixValid)
        return true;

    const QVector<Vec3> normals = seifertSheetNormals(x, sheet.triangles);

    const int size = surface.seifertMatrix.size();

    if (sheet.loopPlaces.size() < size)
        return false;

    // (well above what a loop's straight pieces cut off the curved sheet)
    const double lift = 0.008 * surface.extent;

    QVector<QVector<Vec3>> on;
    QVector<QVector<Vec3>> off;

    for (int i = 0; i < size; ++i)
    {
        on.push_back(seifertSheetLoop(sheet, x, normals, i, 0.0));
        off.push_back(seifertSheetLoop(sheet, x, normals, i, lift));
    }

    for (int i = 0; i < size; ++i)
    {
        for (int j = 0; j < size; ++j)
        {
            if (std::lround(computeLinkingNumber(on[i], off[j], 0)) != surface.seifertMatrix[i][j])
                return false;
        }
    }

    return true;
}

SeifertSheet buildSheetAt(const SeifertSurface &surface, double size, bool everyPoint)
{
    SeifertSheet sheet;

    const int c = surface.bands.size();

    // how many pieces every band is across
    QVector<int> across(c, 2);

    for (int k = 0; k < c; ++k)
    {
        const SeifertSurface::Band &band = surface.bands[k];

        const double width = std::max((band.a1 - band.a0).length(), (band.b1 - band.b0).length());

        across[k] = std::clamp(static_cast<int>(std::lround(width / size)), 2, 12);
    }

    // the vertices of every disk along the edges its bands are joined to
    QVector<std::array<QVector<int>, 2>> joins(c);

    QVector<bool> onKnot;

    for (int i = 0; i < surface.circles; ++i)
    {
        const QVector<Vec3> &polygon = surface.circlePolygons[i];
        const int m = polygon.size();

        QVector<DiskEdge> edges(m);

        for (int k = 0; k < c; ++k)
        {
            const SeifertSurface::Band &band = surface.bands[k];

            if (band.circleA == i)
                edges[band.cornerA % m] = {k, 0, across[k]};

            if (band.circleB == i)
                edges[band.cornerB % m] = {k, 1, across[k]};
        }

        // the outline: band edges split evenly, the knot's own edges at about
        // the mesh's size
        PlanarMesh mesh;

        QVector<bool> outlineOnKnot;

        // a small circle (a loop between two close crossings) gets smaller
        // triangles, or nothing would be left of its outline
        double perimeter = 0.0;

        for (int v = 0; v < m; ++v)
            perimeter += (polygon[(v + 1) % m] - polygon[v]).length();

        const double diskSize = std::min(size, perimeter / 16.0);

        double sinceLast = 0.0;

        for (int v = 0; v < m; ++v)
        {
            const Vec3 &a = polygon[v];
            const Vec3 &b = polygon[(v + 1) % m];

            const bool endOfBand = edges[(v + m - 1) % m].band >= 0;
            const bool startOfBand = edges[v].band >= 0;

            if (everyPoint || startOfBand || endOfBand || sinceLast >= 0.7 * diskSize || mesh.points.isEmpty())
            {
                mesh.points.push_back(a);
                outlineOnKnot.push_back(true);

                sinceLast = 0.0;
            }

            if (startOfBand)
            {
                const DiskEdge &edge = edges[v];

                QVector<int> &join = joins[edge.band][edge.side];

                join.push_back(mesh.points.size() - 1);

                for (int j = 1; j < edge.pieces; ++j)
                {
                    mesh.points.push_back(a + (b - a) * (static_cast<double>(j) / edge.pieces));
                    outlineOnKnot.push_back(false);

                    join.push_back(mesh.points.size() - 1);
                }

                // (the point at its end comes next, or is the first one)
                join.push_back(((v + 1) % m == 0) ? 0 : mesh.points.size());

                continue;
            }

            const double length = (b - a).length();
            const int parts = std::max(1, static_cast<int>(std::lround(length / diskSize)));

            for (int j = 1; j < parts; ++j)
            {
                mesh.points.push_back(a + (b - a) * (static_cast<double>(j) / parts));
                outlineOnKnot.push_back(true);
            }

            sinceLast = parts > 1 ? length / parts : sinceLast + length;
        }

        const int outline = mesh.points.size();

        mesh.fixedCount = outline;

        // the outline's triangles, refined
        const double orientation = surface.circleCounterClockwise[i] ? 1.0 : -1.0;

        mesh.triangles = triangulateOutline(mesh.points, orientation);

        if (mesh.triangles.size() != 3 * (outline - 2))
            return sheet;

        makeDelaunay(mesh, orientation);
        refine(mesh, diskSize, orientation);

        const int base = sheet.model.size();

        sheet.model += mesh.points;

        onKnot += outlineOnKnot;
        onKnot += QVector<bool>(mesh.points.size() - outline, false);

        for (const int index : mesh.triangles)
            sheet.triangles.push_back(base + index);

        for (int k = 0; k < c; ++k)
        {
            for (int side = 0; side < 2; ++side)
            {
                const SeifertSurface::Band &band = surface.bands[k];

                if ((side == 0 ? band.circleA : band.circleB) != i)
                    continue;

                for (int &index : joins[k][side])
                    index += base;
            }
        }
    }

    // the bands: grids that take the disks' vertices along their two ends
    for (int k = 0; k < c; ++k)
    {
        const SeifertSurface::Band &band = surface.bands[k];

        const int nv = across[k];

        const double length = ((band.a0 + band.a1) - (band.b0 + band.b1)).length() * 0.5 +
                              2.0 * std::abs(band.twist) + std::abs(band.zA - band.zB);

        const int nu = std::clamp(static_cast<int>(std::lround(length / size)), 8, 60);

        QVector<int> grid((nu + 1) * (nv + 1), -1);

        auto at = [&grid, nv](int i, int j) -> int & { return grid[i * (nv + 1) + j]; };

        for (int j = 0; j <= nv; ++j)
        {
            at(0, j) = joins[k][0][j];

            // circle B's edge runs from b1 to b0
            at(nu, j) = joins[k][1][nv - j];
        }

        for (int i = 1; i < nu; ++i)
        {
            for (int j = 0; j <= nv; ++j)
            {
                at(i, j) = sheet.model.size();

                sheet.model.push_back(
                    seifertBandPoint(band, static_cast<double>(i) / nu, static_cast<double>(j) / nv));

                onKnot.push_back(j == 0 || j == nv);
            }
        }

        for (int i = 0; i < nu; ++i)
        {
            for (int j = 0; j < nv; ++j)
            {
                const int a = at(i, j);
                const int b = at(i + 1, j);
                const int d = at(i + 1, j + 1);
                const int e = at(i, j + 1);

                // the shorter diagonal, so that the twist is not folded
                if ((sheet.model[a] - sheet.model[d]).length() <= (sheet.model[b] - sheet.model[e]).length())
                {
                    sheet.triangles << a << b << d << a << d << e;
                }
                else
                {
                    sheet.triangles << a << b << e << b << d << e;
                }
            }
        }
    }

    const int vertices = sheet.model.size();

    /*
     * The boundary: the edges with one triangle only, which run the way of
     * the knot.
     */
    std::map<std::pair<int, int>, int> directed;

    for (int t = 0; t < sheet.triangles.size() / 3; ++t)
    {
        for (int k = 0; k < 3; ++k)
        {
            ++directed[{sheet.triangles[3 * t + k], sheet.triangles[3 * t + (k + 1) % 3]}];
        }
    }

    QVector<int> nextOnBoundary(vertices, -1);

    int boundaryEdges = 0;
    int first = -1;

    for (const auto &entry : directed)
    {
        if (directed.count({entry.first.second, entry.first.first}) != 0)
            continue;

        nextOnBoundary[entry.first.first] = entry.first.second;

        first = entry.first.first;

        ++boundaryEdges;
    }

    if (first < 0)
        return sheet;

    {
        int v = first;

        do
        {
            sheet.boundary.push_back(v);

            v = nextOnBoundary[v];
        }
        while (v >= 0 && v != first && sheet.boundary.size() <= boundaryEdges);
    }

    QVector<bool> fixed(vertices, false);

    for (const int v : sheet.boundary)
        fixed[v] = true;

    /*
     * The wire: its kinks (where a band leaves a circle) rounded along the
     * curve. Small moves; the crossings stay as they are.
     */
    sheet.fair = sheet.model;

    {
        const int n = sheet.boundary.size();

        QVector<Vec3> start(n);

        for (int k = 0; k < n; ++k)
            start[k] = sheet.fair[sheet.boundary[k]];

        const AlexanderPolynomial knot = computeAlexanderPolynomial(surface.boundary);

        // The mesh leaves out points of the outline. At a very tight crossing
        // that can be enough to change the knot: a finer mesh is needed.
        {
            const AlexanderPolynomial meshed = computeAlexanderPolynomial(start);

            if (knot.valid && (!meshed.valid || meshed.text != knot.text))
                return sheet;
        }

        if (!seifertSheetCrossings(sheet.model, sheet.triangles, 3.2 * size).isEmpty())
            return sheet;

        // Where crossings are tight, rounding can take a strand past another,
        // which would be another knot: then it is rounded less, or not at all.
        for (const int rounds : {24, 12, 6, 0})
        {
            QVector<Vec3> wire = start;

            for (int round = 0; round < rounds; ++round)
            {
                const QVector<Vec3> before = wire;

                for (int k = 0; k < n; ++k)
                {
                    const Vec3 middle = (before[(k + n - 1) % n] + before[(k + 1) % n]) * 0.5;

                    wire[k] = before[k] + (middle - before[k]) * 0.6;
                }
            }

            const AlexanderPolynomial rounded = computeAlexanderPolynomial(wire);

            if (rounds > 0 && (!knot.valid || !rounded.valid || rounded.text != knot.text))
                continue;

            for (int k = 0; k < n; ++k)
                sheet.fair[sheet.boundary[k]] = wire[k];

            break;
        }
    }

    Neighbours neighbours;

    neighbours.build(vertices, sheet.triangles);

    // (`fixed` grows: a place that went wrong stays where it was good)
    auto sweep = [&neighbours, &fixed, vertices](QVector<Vec3> &x, double limit, double overRelax)
    {
        Vec3 *const p = x.data();

        const int *const start = neighbours.start.constData();
        const int *const vertex = neighbours.vertex.constData();
        const double *const weight = neighbours.weight.constData();

        for (int i = 0; i < vertices; ++i)
        {
            if (fixed[i])
                continue;

            double sx = 0.0;
            double sy = 0.0;
            double sz = 0.0;
            double total = 0.0;

            for (int k = start[i]; k < start[i + 1]; ++k)
            {
                const Vec3 &q = p[vertex[k]];
                const double w = weight[k];

                sx += q.x * w;
                sy += q.y * w;
                sz += q.z * w;
                total += w;
            }

            if (total <= 0.0)
                continue;

            Vec3 move((sx / total - p[i].x) * overRelax,
                      (sy / total - p[i].y) * overRelax,
                      (sz / total - p[i].z) * overRelax);

            const double length = move.length();

            if (length > limit)
                move = move * (limit / length);

            p[i] += move;
        }
    };

    /*
     * The one thing that must not happen: the surface passing through
     * itself. Where it has, the mesh goes back to `good` and stays there.
     */
    const double crossingCell = 3.2 * size;

    auto settle = [&](QVector<Vec3> &x, const QVector<Vec3> &good)
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const QVector<int> crossed = seifertSheetCrossings(x, sheet.triangles, crossingCell);

            if (crossed.isEmpty())
                return true;

            QVector<int> ring;

            for (const int t : crossed)
                ring << sheet.triangles[3 * t] << sheet.triangles[3 * t + 1] << sheet.triangles[3 * t + 2];

            // and what lies around them, or the patch would stick out
            for (int r = 0; r < 3; ++r)
            {
                const int end = ring.size();

                for (int i = 0; i < end; ++i)
                {
                    for (int k = neighbours.start[ring[i]]; k < neighbours.start[ring[i] + 1]; ++k)
                        ring.push_back(neighbours.vertex[k]);
                }

                std::sort(ring.begin(), ring.end());
                ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
            }

            for (const int v : ring)
            {
                x[v] = good[v];
                fixed[v] = true;
            }
        }

        x = good;

        return false;
    };

    // the seams between disks and bands smoothed a little (the disks stay
    // flat away from them)
    neighbours.setCotangents(sheet.fair, sheet.triangles);

    for (int round = 0; round < 12; ++round)
        sweep(sheet.fair, 0.02 * surface.extent, 1.0);

    settle(sheet.fair, sheet.model);

    fixed.fill(false);

    for (const int v : sheet.boundary)
        fixed[v] = true;

    /*
     * The film.
     */
    {
        QVector<Vec3> wire;

        for (const int v : sheet.boundary)
            wire.push_back(sheet.fair[v]);

        Wave wave = waveOf(wire, surface.extent);

        // circles nested deep would stack into a dome
        int deepest = 1;

        for (const int depth : surface.circleDepth)
            deepest = std::max(deepest, depth);

        wave.stretch /= std::sqrt(static_cast<double>(deepest));

        // Space itself cannot pass through anything, but the flat triangles
        // of a tight fold may not follow a curved map: then a lower wave. (No
        // wave at all is a mere stretch, which even triangles follow.)
        for (const double share : {1.0, 0.5, 0.0})
        {
            Wave lower = wave;

            lower.height *= share;

            sheet.film = sheet.fair;

            for (Vec3 &p : sheet.film)
                p = lower.raise(p);

            if (seifertSheetCrossings(sheet.film, sheet.triangles, 3.2 * size).isEmpty())
                break;
        }
    }

    sheet.areaBefore = meshArea(sheet.film, sheet.triangles);

    placeLoops(sheet, surface, size);

    const double gap = FilmGap * surface.extent;

    Separation separation(neighbours, vertices, gap, static_cast<int>(std::ceil(1.6 * gap / size)));

    QVector<Vec3> good = sheet.film;

    // the shapes it had on the way, the newest last
    QVector<QVector<Vec3>> kept{good};

    for (int round = 1; round <= FilmRounds; ++round)
    {
        neighbours.setCotangents(sheet.film, sheet.triangles);

        for (int k = 0; k < SweepsPerRound; ++k)
            sweep(sheet.film, FilmStep * surface.extent, 1.5);

        separation.apply(sheet.film, seifertSheetNormals(sheet.film, sheet.triangles), fixed);

        if (round % FilmCheckEvery == 0)
        {
            settle(sheet.film, good);

            good = sheet.film;

            if (round % (4 * FilmCheckEvery) == 0)
                kept.push_back(good);
        }
    }

    // what the last pushes left behind is smoothed out by the film itself
    neighbours.setCotangents(sheet.film, sheet.triangles);

    for (int k = 0; k < 3; ++k)
        sweep(sheet.film, 0.25 * gap, 1.0);

    settle(sheet.film, good);

    kept.push_back(sheet.film);

    /*
     * No triangles through each other at the checks is not yet the same
     * surface: a small piece could have gone right through another between
     * two of them. The Seifert matrix would tell, so it is worked out again
     * on the film. The newest shape that gives it, entry for entry, is the
     * film. If none does, not even the model lifted by a map of all of space,
     * then the loops found no true place on this mesh, and it is not used: a
     * loop on the screen must never disagree with its number in the matrix.
     */
    bool same = false;

    for (int k = kept.size() - 1; k >= 0 && !same; --k)
    {
        if (sameSeifertMatrix(sheet, surface, kept[k]))
        {
            sheet.film = kept[k];
            same = true;
        }
    }

    sheet.areaAfter = meshArea(sheet.film, sheet.triangles);

    sheet.valid = same && sheet.boundary.size() == boundaryEdges;

    return sheet;
}

} // namespace

SeifertSheet buildSeifertSheet(const SeifertSurface &surface)
{
    if (!surface.valid)
        return SeifertSheet();

    // finer where the coarse mesh does not hold the knot, and in the end
    // with every point of the outline
    for (const double size : {MeshSize, 0.6 * MeshSize, 0.4 * MeshSize})
    {
        SeifertSheet sheet = buildSheetAt(surface, size * surface.extent, false);

        if (sheet.valid)
            return sheet;
    }

    return buildSheetAt(surface, 0.4 * MeshSize * surface.extent, true);
}

QVector<Vec3> seifertSheetLoop(
    const SeifertSheet &sheet,
    const QVector<Vec3> &positions,
    const QVector<Vec3> &normals,
    int index,
    double lift)
{
    QVector<Vec3> curve;

    if (index < 0 || index >= sheet.loopPlaces.size())
        return curve;

    for (const SeifertSheet::Place &place : sheet.loopPlaces[index])
    {
        const int a = sheet.triangles[3 * place.triangle];
        const int b = sheet.triangles[3 * place.triangle + 1];
        const int c = sheet.triangles[3 * place.triangle + 2];

        const double u = 1.0 - place.b - place.c;

        const Vec3 p = positions[a] * u + positions[b] * place.b + positions[c] * place.c;

        Vec3 n = normals[a] * u + normals[b] * place.b + normals[c] * place.c;

        curve.push_back(p + n.normalized() * lift);
    }

    return curve;
}

namespace
{

// does segment p-q pass through triangle a, b, c?
bool segmentHitsTriangle(const Vec3 &p, const Vec3 &q, const Vec3 &a, const Vec3 &b, const Vec3 &c)
{
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    const Vec3 d = q - p;

    const Vec3 h = Vec3::cross(d, e2);
    const double det = Vec3::dot(e1, h);

    if (std::abs(det) < 1.0e-18)
        return false;

    const double f = 1.0 / det;
    const Vec3 s = p - a;
    const double u = f * Vec3::dot(s, h);

    if (u < 0.0 || u > 1.0)
        return false;

    const Vec3 r = Vec3::cross(s, e1);
    const double v = f * Vec3::dot(d, r);

    if (v < 0.0 || u + v > 1.0)
        return false;

    const double t = f * Vec3::dot(e2, r);

    return t > 0.0 && t < 1.0;
}

} // namespace

QVector<int> seifertSheetCrossings(const QVector<Vec3> &x, const QVector<int> &triangles, double cell)
{
    const int count = triangles.size() / 3;

    struct Entry
    {
        qint64 key;
        int triangle;
    };

    struct Box
    {
        float lo[3];
        float hi[3];
    };

    QVector<Entry> entries;
    QVector<Box> boxes(count);

    entries.reserve(2 * count);

    const int *const t3 = triangles.constData();

    for (int t = 0; t < count; ++t)
    {
        const Vec3 &a = x[t3[3 * t]];
        const Vec3 &b = x[t3[3 * t + 1]];
        const Vec3 &c = x[t3[3 * t + 2]];

        const double low[3] = {std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})};
        const double high[3] = {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})};

        for (int k = 0; k < 3; ++k)
        {
            boxes[t].lo[k] = static_cast<float>(low[k]);
            boxes[t].hi[k] = static_cast<float>(high[k]);
        }

        for (qint64 i = static_cast<qint64>(std::floor(low[0] / cell)); i <= static_cast<qint64>(std::floor(high[0] / cell)); ++i)
        for (qint64 j = static_cast<qint64>(std::floor(low[1] / cell)); j <= static_cast<qint64>(std::floor(high[1] / cell)); ++j)
        for (qint64 k = static_cast<qint64>(std::floor(low[2] / cell)); k <= static_cast<qint64>(std::floor(high[2] / cell)); ++k)
            entries.push_back({((i + 100000) << 42) ^ ((j + 100000) << 21) ^ (k + 100000), t});
    }

    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.key < b.key; });

    QVector<int> crossed;

    for (int from = 0; from < entries.size();)
    {
        int to = from;

        while (to < entries.size() && entries[to].key == entries[from].key)
            ++to;

        for (int i = from; i < to; ++i)
        {
            const int s = entries[i].triangle;
            const Box &one = boxes[s];

            for (int j = i + 1; j < to; ++j)
            {
                const int t = entries[j].triangle;
                const Box &two = boxes[t];

                if (one.hi[0] < two.lo[0] || two.hi[0] < one.lo[0] ||
                    one.hi[1] < two.lo[1] || two.hi[1] < one.lo[1] ||
                    one.hi[2] < two.lo[2] || two.hi[2] < one.lo[2])
                {
                    continue;
                }

                bool shares = false;

                for (int m = 0; m < 3; ++m)
                {
                    for (int n = 0; n < 3; ++n)
                        shares = shares || t3[3 * s + m] == t3[3 * t + n];
                }

                if (shares)
                    continue;

                const Vec3 &a = x[t3[3 * s]];
                const Vec3 &b = x[t3[3 * s + 1]];
                const Vec3 &c = x[t3[3 * s + 2]];
                const Vec3 &d = x[t3[3 * t]];
                const Vec3 &e = x[t3[3 * t + 1]];
                const Vec3 &f = x[t3[3 * t + 2]];

                if (segmentHitsTriangle(a, b, d, e, f) || segmentHitsTriangle(b, c, d, e, f) ||
                    segmentHitsTriangle(c, a, d, e, f) || segmentHitsTriangle(d, e, a, b, c) ||
                    segmentHitsTriangle(e, f, a, b, c) || segmentHitsTriangle(f, d, a, b, c))
                {
                    crossed << s << t;
                }
            }
        }

        from = to;
    }

    std::sort(crossed.begin(), crossed.end());
    crossed.erase(std::unique(crossed.begin(), crossed.end()), crossed.end());

    return crossed;
}

QVector<Vec3> seifertSheetNormals(const QVector<Vec3> &positions, const QVector<int> &triangles)
{
    QVector<Vec3> normals(positions.size());

    for (int t = 0; t + 2 < triangles.size(); t += 3)
    {
        const int a = triangles[t];
        const int b = triangles[t + 1];
        const int c = triangles[t + 2];

        const Vec3 n = Vec3::cross(positions[b] - positions[a], positions[c] - positions[a]);

        normals[a] += n;
        normals[b] += n;
        normals[c] += n;
    }

    for (Vec3 &n : normals)
        n = n.normalized();

    return normals;
}

} // namespace KnotMath
