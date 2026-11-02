#include "traintrack.h"

#include "knotdiagram.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <utility>

using KnotMath::Vec3;

namespace Photo
{

namespace
{

constexpr double Pi = 3.14159265358979;

// the steepest a ramp may get (a smooth ramp is steepest in its middle,
// half as steep again as its mean) and how long that makes it
constexpr double SteepestSlope = 0.15;
const double RampLength = 1.5 * (BridgeLevelMm - GroundLevelMm) / SteepestSlope;

// the tightest bend of the track's middle line
constexpr double TightestBendMm = 110.0;

// No track goes on for longer than this: a layout that needs more is spoilt
// by one tight place, and another layout will do better. If none does, a long
// track is still better than none.
constexpr double LongestTrackMm = 60000.0;
constexpr double LongestLastResortMm = 200000.0;

// two pieces of track side by side, middle to middle
constexpr double SideBySideMm = BedWidthMm + 10.0;

// points of the track
constexpr double PointSpacingMm = 6.0;

// a stone pier under a bridge
constexpr double BlockAlongMm = 24.0;
constexpr double BlockAcrossMm = 56.0;

Vec3 pathAt(const QVector<Vec3> &path, double length, double along);

struct PassOnTrack
{
    // along the drawing (its own units), and how far along the track the
    // level has to stay as it is on either side of the crossing (mm)
    double s = 0.0;
    double hold = 0.0;
    bool over = false;
    int crossing = 0;
};

double smooth(double t)
{
    t = std::clamp(t, 0.0, 1.0);

    return t * t * (3.0 - 2.0 * t);
}

// the plan of the track, in the drawing's units (y of the drawing -> -z, so
// that seen from above it is the drawing itself)
struct Plan
{
    QVector<Vec3> points;
    QVector<double> arc;
    double length = 0.0;

    QVector<PassOnTrack> passes;
    int crossings = 0;

    // smallest bend radius, in the drawing's units
    double tightest = 1.0;
};

Plan makePlan(const KnotMath::KnotDiagram &diagram)
{
    Plan plan;

    const int n = diagram.points.size();

    // the arc of the drawing's own points: where the passes are
    QVector<double> arc(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
        arc[i + 1] = arc.at(i) + (diagram.points.at((i + 1) % n) - diagram.points.at(i)).length();

    const double drawn = arc.at(n);

    auto direction = [&diagram, n](int segment)
    {
        return (diagram.points.at((segment + 1) % n) - diagram.points.at(segment)).normalized();
    };

    plan.crossings = diagram.crossings.size();

    for (int k = 0; k < plan.crossings; ++k)
    {
        const KnotMath::DiagramCrossing &c = diagram.crossings.at(k);

        const Vec3 a = direction(c.segA);
        const Vec3 b = direction(c.segB);

        const double cross = std::abs(a.x * b.y - a.y * b.x);
        const double dot = std::abs(a.x * b.x + a.y * b.y);

        /*
         * Both strands keep their level while either lies under the other's
         * width: the lower one's track with the train on it, under the upper
         * one's track, whichever way they meet.
         */
        const double hold =
            (0.5 * BedWidthMm + 4.0 + 0.5 * BedWidthMm * dot) / std::max(cross, 0.25) + 10.0;

        PassOnTrack pass;

        pass.hold = hold;
        pass.crossing = k;

        pass.s = (arc.at(c.segA) + c.tA * (arc.at(c.segA + 1) - arc.at(c.segA))) / drawn;
        pass.over = c.aOver;

        plan.passes.push_back(pass);

        pass.s = (arc.at(c.segB) + c.tB * (arc.at(c.segB + 1) - arc.at(c.segB))) / drawn;
        pass.over = !c.aOver;

        plan.passes.push_back(pass);
    }

    std::sort(
        plan.passes.begin(),
        plan.passes.end(),
        [](const PassOnTrack &a, const PassOnTrack &b) { return a.s < b.s; });

    // the smooth curve through the drawing's points; pass.s is a share of
    // the whole, which is the same on both to well within the holds
    QVector<Vec3> fine = KnotMath::refineCurve(diagram.points, 3, true);

    plan.points.reserve(fine.size());

    for (const Vec3 &p : fine)
        plan.points.push_back(Vec3(p.x, 0.0, -p.y));

    const int m = plan.points.size();

    plan.arc.resize(m + 1);
    plan.arc[0] = 0.0;

    for (int i = 0; i < m; ++i)
        plan.arc[i + 1] = plan.arc.at(i) + (plan.points.at((i + 1) % m) - plan.points.at(i)).length();

    plan.length = plan.arc.at(m);

    // the radius through three points some way apart (so that the small
    // steps of the points do not count as bends)
    const int stride = std::max(2, static_cast<int>(0.05 / (plan.length / m)));

    double most = 1.0e-9;

    for (int i = 0; i < m; ++i)
    {
        const Vec3 a = plan.points.at((i + m - stride) % m);
        const Vec3 b = plan.points.at(i);
        const Vec3 c = plan.points.at((i + stride) % m);

        const double ab = (b - a).length();
        const double bc = (c - b).length();
        const double ca = (a - c).length();
        const double area2 = Vec3::cross(b - a, c - a).length();

        if (ab * bc * ca > 1.0e-30)
            most = std::max(most, 2.0 * area2 / (ab * bc * ca));
    }

    plan.tightest = 1.0 / most;

    return plan;
}

// The level of the track at a share `t` of the way round.
class Levels
{
public:
    Levels(const Plan &plan, double scale)
    {
        const double total = plan.length * scale;

        for (const PassOnTrack &pass : plan.passes)
        {
            Stop stop;

            stop.at = pass.s * total;
            stop.hold = pass.hold;
            stop.level = pass.over ? BridgeLevelMm : GroundLevelMm;

            stops_.push_back(stop);
        }

        total_ = total;
    }

    // The room a ramp lacks: the largest share of the wanted length missing
    // anywhere (1: it all fits).
    double fit() const
    {
        const int m = stops_.size();

        double wanted = 1.0;

        for (int k = 0; k < m; ++k)
        {
            const Stop &a = stops_.at(k);
            const Stop &b = stops_.at((k + 1) % m);

            if (a.level == b.level)
                continue;

            double gap = b.at - a.at;

            if (gap <= 0.0)
                gap += total_;

            wanted = std::max(wanted, (a.hold + b.hold + RampLength) / std::max(gap, 1.0e-9));
        }

        return wanted;
    }

    double at(double s) const
    {
        const int m = stops_.size();

        if (m == 0)
            return GroundLevelMm;

        int k = 0;

        while (k < m && stops_.at(k).at <= s)
            ++k;

        Stop a = stops_.at((k + m - 1) % m);
        Stop b = stops_.at(k % m);

        if (k == 0)
            a.at -= total_;

        if (k == m)
            b.at += total_;

        if (a.level == b.level)
            return a.level;

        const double from = a.at + a.hold;
        const double to = b.at - b.hold;

        return a.level + (b.level - a.level) * smooth((s - from) / std::max(to - from, 1.0e-9));
    }

private:
    struct Stop
    {
        double at = 0.0;
        double hold = 0.0;
        double level = 0.0;
    };

    QVector<Stop> stops_;
    double total_ = 0.0;
};

// the track at this scale, a point every PointSpacingMm
QVector<Vec3> trackAtScale(const Plan &plan, double scale)
{
    const double total = plan.length * scale;
    const int count = std::max(32, static_cast<int>(total / PointSpacingMm));

    const Levels levels(plan, scale);

    const int m = plan.points.size();

    QVector<Vec3> path;

    path.reserve(count);

    int segment = 0;

    for (int i = 0; i < count; ++i)
    {
        const double target = plan.length * i / count;

        while (segment < m - 1 && plan.arc.at(segment + 1) < target)
            ++segment;

        const double piece = plan.arc.at(segment + 1) - plan.arc.at(segment);
        const double t = piece > 0.0 ? (target - plan.arc.at(segment)) / piece : 0.0;

        const Vec3 &a = plan.points.at(segment);
        const Vec3 &b = plan.points.at((segment + 1) % m);

        const Vec3 p = a + (b - a) * t;

        path.push_back(Vec3(p.x * scale, levels.at(target * scale), p.z * scale));
    }

    return path;
}

/*
 * Only the points near each other are compared, in a grid. Returns the
 * distance of the closest two pieces of track side by side, and whether any
 * two pieces lie in each other's way: over each other without room for the
 * train between them.
 */
struct Clearance
{
    double closest = 1.0e30;
    bool blocked = false;
};

Clearance measure(const QVector<Vec3> &path, double spacing)
{
    Clearance result;

    const int n = path.size();

    const double cell = SideBySideMm;

    double minX = 1.0e30;
    double minZ = 1.0e30;
    double maxX = -1.0e30;
    double maxZ = -1.0e30;

    for (const Vec3 &p : path)
    {
        minX = std::min(minX, p.x);
        minZ = std::min(minZ, p.z);
        maxX = std::max(maxX, p.x);
        maxZ = std::max(maxZ, p.z);
    }

    const int columns = static_cast<int>((maxX - minX) / cell) + 1;
    const int rows = static_cast<int>((maxZ - minZ) / cell) + 1;

    QVector<int> first(columns * rows, -1);
    QVector<int> next(n);

    auto cellOf = [&](const Vec3 &p, int &column, int &row)
    {
        column = std::clamp(static_cast<int>((p.x - minX) / cell), 0, columns - 1);
        row = std::clamp(static_cast<int>((p.z - minZ) / cell), 0, rows - 1);
    };

    for (int i = 0; i < n; ++i)
    {
        int column = 0;
        int row = 0;

        cellOf(path.at(i), column, row);

        next[i] = first.at(row * columns + column);
        first[row * columns + column] = i;
    }

    // farther apart along the track than a bend can bring them back
    const int neighbours = static_cast<int>(Pi * SideBySideMm / spacing) + 1;

    const double room = BridgeLevelMm - GroundLevelMm - 0.5;

    for (int i = 0; i < n; ++i)
    {
        int column = 0;
        int row = 0;

        cellOf(path.at(i), column, row);

        for (int r = std::max(row - 1, 0); r <= std::min(row + 1, rows - 1); ++r)
        {
            for (int c = std::max(column - 1, 0); c <= std::min(column + 1, columns - 1); ++c)
            {
                for (int j = first.at(r * columns + c); j >= 0; j = next.at(j))
                {
                    if (j <= i || j - i < neighbours || n - (j - i) < neighbours)
                        continue;

                    if (std::abs(path.at(i).y - path.at(j).y) >= room)
                        continue;

                    const double distance = std::hypot(path.at(i).x - path.at(j).x, path.at(i).z - path.at(j).z);

                    result.closest = std::min(result.closest, distance);

                    if (distance < SideBySideMm)
                        result.blocked = true;
                }
            }
        }
    }

    return result;
}

// The smallest scale at which the track fits on itself, or 0.
double fittingScale(const Plan &plan, double longest)
{
    double scale = TightestBendMm / std::max(plan.tightest, 1.0e-9);

    scale = std::max(scale, 200.0);

    for (int attempt = 0; attempt < 120; ++attempt)
    {
        if (!(plan.length * scale < longest))
            return 0.0;

        const double wanted = Levels(plan, scale).fit();

        if (wanted > 1.0)
        {
            scale *= wanted * 1.0001;

            continue;
        }

        const QVector<Vec3> path = trackAtScale(plan, scale);

        if (!measure(path, plan.length * scale / path.size()).blocked)
            return scale;

        scale *= 1.04;
    }

    return 0.0;
}

// ---------------------------------------------------------------------
// Pieces of geometry
// ---------------------------------------------------------------------

struct Builder
{
    QVector<float> positions;
    QVector<float> normals;
    QVector<float> extra;       // 4 per vertex
    QVector<float> occlusion;
    QVector<int> samples;
    QVector<unsigned int> indices;

    int vertexCount() const { return occlusion.size(); }

    void vertex(const Vec3 &p, const Vec3 &n, float u, float v, float w, float kind, float open, int sample)
    {
        positions << float(p.x) << float(p.y) << float(p.z);
        normals << float(n.x) << float(n.y) << float(n.z);
        extra << u << v << w << kind;
        occlusion << open;
        samples << sample;
    }

    // Two triangles, counter-clockwise from outside whichever way the
    // corners were given (their normals say which side that is).
    void quad(int a, int b, int c, int d)
    {
        const auto at = [this](int i) { return Vec3(positions.at(3 * i), positions.at(3 * i + 1), positions.at(3 * i + 2)); };

        const Vec3 face = Vec3::cross(at(b) - at(a), at(c) - at(a));
        const Vec3 n(normals.at(3 * a), normals.at(3 * a + 1), normals.at(3 * a + 2));

        if (Vec3::dot(face, n) >= 0.0)
            indices << a << b << c << a << c << d;
        else
            indices << a << c << b << a << d << c;
    }
};

// A block with rounded edges, turned to `along`/`up`: u, v in its faces (mm).
void addBlock(
    Builder &out,
    const Vec3 &centre,
    const Vec3 &along,
    const Vec3 &up,
    const Vec3 &half,
    double bevel,
    int colour,
    int sample)
{
    const Vec3 across = Vec3::cross(along, up);

    const Vec3 axes[3] = {along, up, across};

    const double h[3] = {half.x, half.y, half.z};

    const double steps[] = {0.0, 0.42, 0.78, 1.0};

    for (int axis = 0; axis < 3; ++axis)
    {
        for (int sign = -1; sign <= 1; sign += 2)
        {
            const int a = (axis + 1) % 3;
            const int b = (axis + 2) % 3;

            // the places on the face, from edge to edge
            auto places = [&steps, bevel](double size)
            {
                QVector<double> list;

                for (int k = 3; k >= 0; --k)
                    list << -(size - bevel) - bevel * steps[k];

                for (int k = 0; k < 4; ++k)
                    list << (size - bevel) + bevel * steps[k];

                return list;
            };

            const QVector<double> us = places(h[a]);
            const QVector<double> vs = places(h[b]);

            const int base = out.vertexCount();

            for (int i = 0; i < us.size(); ++i)
            {
                for (int j = 0; j < vs.size(); ++j)
                {
                    double p[3];

                    p[axis] = sign * h[axis];
                    p[a] = us.at(i);
                    p[b] = vs.at(j);

                    // the point on the rounded box: the inner box plus the
                    // way out from it
                    double inner[3];
                    double way[3];

                    for (int k = 0; k < 3; ++k)
                    {
                        inner[k] = std::clamp(p[k], -(h[k] - bevel), h[k] - bevel);
                        way[k] = p[k] - inner[k];
                    }

                    const double l = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);

                    double n[3] = {0.0, 0.0, 0.0};

                    for (int k = 0; k < 3; ++k)
                        n[k] = way[k] / std::max(l, 1.0e-9);

                    double q[3];

                    for (int k = 0; k < 3; ++k)
                        q[k] = inner[k] + n[k] * bevel;

                    const Vec3 position = centre + axes[0] * q[0] + axes[1] * q[1] + axes[2] * q[2];
                    const Vec3 normal = axes[0] * n[0] + axes[1] * n[1] + axes[2] * n[2];

                    out.vertex(
                        position,
                        normal,
                        float(us.at(i)),
                        float(vs.at(j)),
                        float(colour),
                        1.0f,
                        // the lowest part sees the floor, not the room
                        float(0.55 + 0.45 * smooth(position.y / 16.0)),
                        sample);
                }
            }

            const int count = vs.size();

            for (int i = 0; i + 1 < us.size(); ++i)
            {
                for (int j = 0; j + 1 < count; ++j)
                {
                    out.quad(
                        base + i * count + j,
                        base + (i + 1) * count + j,
                        base + (i + 1) * count + j + 1,
                        base + i * count + j + 1);
                }
            }
        }
    }
}

/*
 * The railway seen across the track, one thing at a time. A face is a stretch
 * of the outline with a normal of its own; `part` tells the shader what it is:
 * 0 the ballast's top, 0.1 its sloping side, 0.2 a sleeper, 0.3 the top of a
 * rail, 0.35 a rail's side, 0.4 what lies underneath, 0.5 the steel of a
 * bridge.
 */
struct Face
{
    double x0, y0, x1, y1;
    float open;
    float part;
};

/*
 * The bed under the rails. On the ground and on the low part of a ramp it is
 * a bank of ballast, its foot on the floor (`rise`: how far above the floor
 * the sleepers are; the foot spreads with it). Higher up it is the deck of a
 * bridge: a steel trough of ballast between two plate girders. Both have the
 * same number of faces, so that one runs into the other.
 */
QVector<Face> bedProfile(bool bank, double rise)
{
    constexpr double Crest = 21.0;
    constexpr double Top = -3.8;

    QVector<Face> right;

    if (bank)
    {
        const double foot = 0.5 * BedWidthMm + 0.32 * rise;

        // (the foot's height is set to the floor when the ring is placed)
        right.push_back({foot, -rise, 0.5 * BedWidthMm - 2.0, Top - 0.8, 0.7f, 0.1f});
        right.push_back({0.5 * BedWidthMm - 2.0, Top - 0.8, Crest + 2.0, Top - 0.3, 0.9f, 0.1f});
        right.push_back({Crest + 2.0, Top - 0.3, Crest, Top, 0.95f, 0.1f});
    }
    else
    {
        const double wall = Crest + 3.0;

        right.push_back({wall, -DeckDepthMm, wall, -1.2, 0.85f, 0.5f});
        right.push_back({wall, -1.2, Crest + 1.0, -1.2, 1.0f, 0.5f});
        right.push_back({Crest + 1.0, -1.2, Crest, Top, 0.6f, 0.5f});
    }

    QVector<Face> faces;

    const double under = bank ? rise : DeckDepthMm;
    const double wide = right.first().x0;

    faces.push_back({-wide, -under, wide, -under, 0.25f, 0.4f});

    faces += right;

    faces.push_back({Crest, Top, -Crest, Top, 1.0f, 0.0f});

    for (int k = right.size() - 1; k >= 0; --k)
    {
        const Face &f = right.at(k);

        faces.push_back({-f.x1, f.y1, -f.x0, f.y0, f.open, f.part});
    }

    return faces;
}

// one rail, as a section of real rail: foot, thin web, head
QVector<Face> railProfile(double x)
{
    const double h = 1.05;
    const double w = 0.45;
    const double f = 1.9;

    const double top = 0.0;
    const double under = -1.05;
    const double foot = -3.0;

    const QVector<QPair<double, double>> right = {
        {f, foot - 0.3}, {f, foot}, {w, foot + 0.55}, {w, under - 0.2}, {h, under}, {h, top - 0.15}, {h - 0.15, top}};

    QVector<Face> faces;

    for (int k = 0; k + 1 < right.size(); ++k)
        faces.push_back({x + right.at(k).first, right.at(k).second, x + right.at(k + 1).first, right.at(k + 1).second, 0.85f, 0.35f});

    faces.push_back({x + h - 0.15, top, x - h + 0.15, top, 1.0f, 0.3f});

    for (int k = right.size() - 2; k >= 0; --k)
        faces.push_back({x - right.at(k + 1).first, right.at(k + 1).second, x - right.at(k).first, right.at(k).second, 0.85f, 0.35f});

    return faces;
}

// how much of the room each ring of the track sees: less under a bridge
QVector<float> ringOpenness(const QVector<Vec3> &path, double spacing)
{
    const int n = path.size();

    QVector<float> open(n, 1.0f);

    const int neighbours = static_cast<int>(Pi * SideBySideMm / spacing) + 1;

    for (int i = 0; i < n; ++i)
    {
        if (path.at(i).y > GroundLevelMm + 8.0)
            continue;

        for (int j = 0; j < n; j += 2)
        {
            const int gap = std::abs(i - j);

            if (gap < neighbours || n - gap < neighbours || path.at(j).y < path.at(i).y + 12.0)
                continue;

            const double distance = std::hypot(path.at(i).x - path.at(j).x, path.at(i).z - path.at(j).z);

            if (distance < 34.0)
                open[i] = std::min(open.at(i), float(0.5 + 0.5 * smooth(distance / 34.0)));
        }
    }

    for (int pass = 0; pass < 3; ++pass)
    {
        const QVector<float> was = open;

        for (int i = 0; i < n; ++i)
            open[i] = 0.25f * (was.at((i + n - 1) % n) + 2.0f * was.at(i) + was.at((i + 1) % n));
    }

    return open;
}

struct Frame
{
    Vec3 along;
    Vec3 side;
    Vec3 up;
};

Frame frameAt(const QVector<Vec3> &path, int i)
{
    const int n = path.size();

    Frame frame;

    frame.along = (path.at((i + 1) % n) - path.at((i + n - 1) % n)).normalized();
    frame.side = Vec3::cross(Vec3(0.0, 1.0, 0.0), frame.along).normalized();
    frame.up = Vec3::cross(frame.along, frame.side);

    return frame;
}

/*
 * Where the blocks stand: on both sides of every bridge, as near to it as
 * the track below leaves room, and under the long raised stretches. A stack
 * never stands on or under another piece of track.
 */
void addPiers(Builder &out, TrainTrack &track, const QVector<Vec3> &path, double spacing)
{
    const int n = path.size();

    const int neighbours = static_cast<int>(Pi * SideBySideMm / spacing) + 1;

    // half the diagonal of a pier, and half the bed's width beside it
    const double keep = 0.5 * std::hypot(BlockAlongMm, BlockAcrossMm) + 0.5 * BedWidthMm + 3.0;

    auto clear = [&](int i)
    {
        for (int j = 0; j < n; ++j)
        {
            const int gap = std::abs(i - j);

            if (gap < neighbours || n - gap < neighbours)
                continue;

            if (std::hypot(path.at(i).x - path.at(j).x, path.at(i).z - path.at(j).z) < keep)
                return false;
        }

        return true;
    };

    // (lower, the track stands on its bank)
    auto tall = [&](int i) { return path.at(i).y - GroundLevelMm >= BankHeightMm + 2.0; };

    QVector<bool> placed(n, false);

    auto build = [&](int i)
    {
        const Frame frame = frameAt(path, i);

        // straight along the ground, whatever the slope above
        const Vec3 flat = Vec3(frame.along.x, 0.0, frame.along.z).normalized();

        const double top = path.at(i).y - DeckDepthMm + 1.0;

        // a plinth, the pier, and a cap for the girders to rest on
        addBlock(out, Vec3(path.at(i).x, 2.0, path.at(i).z), flat, Vec3(0.0, 1.0, 0.0),
                 Vec3(0.5 * BlockAlongMm + 2.5, 2.0, 0.5 * BlockAcrossMm + 2.5), 0.5, 0, i);

        addBlock(out, Vec3(path.at(i).x, 0.5 * top, path.at(i).z), flat, Vec3(0.0, 1.0, 0.0),
                 Vec3(0.5 * BlockAlongMm, 0.5 * top, 0.5 * BlockAcrossMm), 0.6, 0, i);

        addBlock(out, Vec3(path.at(i).x, top - 1.2, path.at(i).z), flat, Vec3(0.0, 1.0, 0.0),
                 Vec3(0.5 * BlockAlongMm + 1.8, 1.4, 0.5 * BlockAcrossMm + 1.8), 0.5, 0, i);

        ++track.blocks;
        ++track.piers;

        placed[i] = true;
    };

    const int aside = std::max(1, static_cast<int>(16.0 / spacing));

    // at the bridges: from the crossing outwards to the first free place
    for (int i = 0; i < n; ++i)
    {
        const bool highHere = path.at(i).y > BridgeLevelMm - 0.5;

        if (!highHere)
            continue;

        // is another piece of track under this one, here?
        bool over = false;

        for (int j = 0; j < n && !over; ++j)
        {
            const int gap = std::abs(i - j);

            if (gap < neighbours || n - gap < neighbours)
                continue;

            over = path.at(j).y < GroundLevelMm + 0.5 &&
                   std::hypot(path.at(i).x - path.at(j).x, path.at(i).z - path.at(j).z) < 2.0 * spacing;
        }

        if (!over)
            continue;

        // (the middle of the crossing only: the point right above the other track)
        bool nearerAhead = false;

        for (int j = 0; j < n && !nearerAhead; ++j)
        {
            const int gap = std::abs(i - j);

            if (gap < neighbours || n - gap < neighbours)
                continue;

            const double here = std::hypot(path.at(i).x - path.at(j).x, path.at(i).z - path.at(j).z);

            if (here >= 2.0 * spacing)
                continue;

            for (const int k : {(i + 1) % n, (i + n - 1) % n})
            {
                if (std::hypot(path.at(k).x - path.at(j).x, path.at(k).z - path.at(j).z) < here)
                    nearerAhead = true;
            }
        }

        if (nearerAhead)
            continue;

        for (const int direction : {1, -1})
        {
            for (int step = aside; step < static_cast<int>(RampLength / spacing); ++step)
            {
                const int k = ((i + direction * step) % n + n) % n;

                if (!tall(k))
                    break;

                if (placed.at(k) || !clear(k))
                    continue;

                build(k);

                break;
            }
        }
    }

    // the raised stretches between them, about a hand apart
    const int apart = std::max(1, static_cast<int>(150.0 / spacing));

    for (int i = 0; i < n; ++i)
    {
        if (!tall(i) || placed.at(i))
            continue;

        bool near = false;

        for (int k = -apart; k <= apart && !near; ++k)
            near = placed.at(((i + k) % n + n) % n);

        if (!near && clear(i))
            build(i);
    }
}

void buildMesh(TrainTrack &track)
{
    const QVector<Vec3> &path = track.path;
    const int n = path.size();
    const double spacing = track.length / n;

    const QVector<float> open = ringOpenness(path, spacing);

    Builder out;

    // the rail joints (the shader marks them), every 144 mm
    constexpr double PieceMm = 144.0;

    const int pieces = std::max(1, static_cast<int>(track.length / PieceMm + 0.5));
    const double piece = track.length / pieces;

    // every ring: bank or bridge, by how high it is
    auto onBank = [&path](int i) { return path.at(i).y - GroundLevelMm < BankHeightMm; };

    int strips = 0;

    auto addStrip = [&](auto profileOf)
    {
        const int first = out.vertexCount();

        const int faceCount = profileOf(0).size();

        for (int i = 0; i < n; ++i)
        {
            const Frame frame = frameAt(path, i);

            const double along = i * spacing;

            for (const Face &face : profileOf(i))
            {
                const double dx = face.x1 - face.x0;
                const double dy = face.y1 - face.y0;
                const double l = std::max(std::hypot(dx, dy), 1.0e-6);

                const Vec3 normal = (frame.side * (dy / l) + frame.up * (-dx / l)).normalized();

                for (int end = 0; end < 2; ++end)
                {
                    const double x = end == 0 ? face.x0 : face.x1;
                    const double y = end == 0 ? face.y0 : face.y1;

                    Vec3 place = path.at(i) + frame.side * x + frame.up * y;

                    // a bank stands on the floor, whatever the slope above it
                    if (onBank(i) && y < -path.at(i).y + 0.01)
                        place.y = 0.0;

                    out.vertex(place, normal, float(along), float(x + 0.5 * y), float(piece), face.part, face.open * open.at(i), i);
                }
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const int next = (i + 1) % n;

            for (int f = 0; f < faceCount; ++f)
            {
                const int a = first + i * 2 * faceCount + 2 * f;
                const int b = a + 1;
                const int c = first + next * 2 * faceCount + 2 * f + 1;
                const int d = first + next * 2 * faceCount + 2 * f;

                out.quad(a, b, c, d);
            }
        }

        ++strips;
    };

    addStrip([&](int i) { return bedProfile(onBank(i), path.at(i).y); });
    addStrip([](int) { return railProfile(-RailApartMm * 0.5); });
    addStrip([](int) { return railProfile(RailApartMm * 0.5); });

    // the sleepers, each one a beam of timber lying in the ballast
    {
        constexpr double Pitch = 10.5;
        constexpr double Wide = 5.4;
        constexpr double Top = -3.0;
        constexpr double Under = -5.6;

        const double half = 0.5 * TrackWidthMm - 1.0;

        const int count = static_cast<int>(track.length / Pitch);

        for (int s = 0; s < count; ++s)
        {
            const double along = track.length * s / count;

            const int i = std::min(static_cast<int>(along / spacing), n - 1);

            const Frame frame = frameAt(path, i);

            const Vec3 middle = pathAt(path, track.length, along);

            // a little out of true, as real ones lie
            const double skew = 0.09 * std::sin(s * 12.9898);

            const Vec3 across = (frame.side + frame.along * skew).normalized();
            const Vec3 along2 = frame.along * (0.5 * Wide);

            const Vec3 a = middle - across * half;
            const Vec3 b = middle + across * half;

            auto corner = [&](const Vec3 &end, double sign, double y) { return end + along2 * sign + frame.up * y; };

            auto face = [&](const Vec3 &p, const Vec3 &q, const Vec3 &r, const Vec3 &t, const Vec3 &normal)
            {
                const int base = out.vertexCount();

                const double seed = 3.0 * s;

                out.vertex(p, normal, float(p.x + p.z), float(seed), float(s), 0.2f, 0.9f, i);
                out.vertex(q, normal, float(q.x + q.z), float(seed + 1.0), float(s), 0.2f, 0.9f, i);
                out.vertex(r, normal, float(r.x + r.z), float(seed + 1.0), float(s), 0.2f, 0.9f, i);
                out.vertex(t, normal, float(t.x + t.z), float(seed), float(s), 0.2f, 0.9f, i);

                out.quad(base, base + 1, base + 2, base + 3);
            };

            face(corner(a, -1.0, Top), corner(b, -1.0, Top), corner(b, 1.0, Top), corner(a, 1.0, Top), frame.up);
            face(corner(a, 1.0, Under), corner(b, 1.0, Under), corner(b, 1.0, Top), corner(a, 1.0, Top), frame.along);
            face(corner(a, -1.0, Top), corner(b, -1.0, Top), corner(b, -1.0, Under), corner(a, -1.0, Under), frame.along * -1.0);
            face(corner(b, -1.0, Under), corner(b, 1.0, Under), corner(b, 1.0, Top), corner(b, -1.0, Top), across);
            face(corner(a, -1.0, Top), corner(a, 1.0, Top), corner(a, 1.0, Under), corner(a, -1.0, Under), across * -1.0);
        }
    }

    addPiers(out, track, path, spacing);

    // stone abutments, where a bank ends and a bridge begins
    for (int i = 0; i < n; ++i)
    {
        if (onBank(i) == onBank((i + 1) % n))
            continue;

        const Frame frame = frameAt(path, i);

        const Vec3 flat = Vec3(frame.along.x, 0.0, frame.along.z).normalized();

        const double top = path.at(i).y - 4.0;

        addBlock(
            out,
            Vec3(path.at(i).x, 0.5 * top, path.at(i).z),
            flat,
            Vec3(0.0, 1.0, 0.0),
            Vec3(11.0, 0.5 * top, 0.5 * BedWidthMm + 4.0),
            0.8,
            0,
            i);
    }

    const float units = float(track.unitsPerMm);

    KnotMath::Mesh mesh;

    mesh.positions = out.positions;

    for (float &value : mesh.positions)
        value *= units;

    mesh.normals = out.normals;
    mesh.texcoords = out.extra;
    mesh.occlusion = out.occlusion;
    mesh.indices = out.indices;
    mesh.vertexSample = out.samples;

    /*
     * The "tube" the view knows about: the centre line at the track's half
     * width. On the ground it lies as a tube of that size would, so that the
     * shade on the floor comes out right beside it.
     */
    const float radius = float(0.5 * TrackWidthMm) * units;

    for (int i = 0; i < n; ++i)
    {
        mesh.centerline << float(path.at(i).x) * units
                        << std::max(float(path.at(i).y) * units, radius)
                        << float(path.at(i).z) * units
                        << radius;

        mesh.centerlineStrand << 0;
        mesh.centerlineArc << float(i * spacing) * units;
    }

    mesh.strandLength << float(track.length) * units;
    mesh.strandClosed << true;
    mesh.ringCount = n;
    mesh.ringSegments = strips;

    track.mesh = mesh;
}

// ---------------------------------------------------------------------
// The train
// ---------------------------------------------------------------------

struct ModelBuilder
{
    QVector<float> v;
    QVector<quint32> index;

    int count() const { return v.size() / TrainModel::VertexFloats; }

    int add(const Vec3 &p, const Vec3 &n, int material)
    {
        // the underside and what is low down see the track, not the room
        const float open = float(0.5 + 0.5 * smooth((p.y + 2.0) / 14.0)) * float(0.72 + 0.28 * smooth(0.5 + 0.5 * n.y + 0.3));

        v << float(p.x) << float(p.y) << float(p.z) << float(n.x) << float(n.y) << float(n.z) << open << float(material);

        return count() - 1;
    }

    void quad(int a, int b, int c, int d)
    {
        const auto at = [this](int i)
        {
            const float *f = v.constData() + i * TrainModel::VertexFloats;

            return Vec3(f[0], f[1], f[2]);
        };

        const float *f = v.constData() + a * TrainModel::VertexFloats;

        const Vec3 n(f[3], f[4], f[5]);

        if (Vec3::dot(Vec3::cross(at(b) - at(a), at(c) - at(a)), n) >= 0.0)
            index << a << b << c << a << c << d;
        else
            index << a << c << b << a << d << c;
    }

    // a box with rounded edges and corners
    void box(const Vec3 &centre, const Vec3 &half, double bevel, int material)
    {
        const double h[3] = {half.x, half.y, half.z};
        const double c[3] = {centre.x, centre.y, centre.z};

        const double steps[] = {0.0, 0.38, 0.72, 0.93, 1.0};

        for (int axis = 0; axis < 3; ++axis)
        {
            for (int sign = -1; sign <= 1; sign += 2)
            {
                const int a = (axis + 1) % 3;
                const int b = (axis + 2) % 3;

                auto places = [&steps, bevel](double size)
                {
                    QVector<double> list;

                    for (int k = 4; k >= 0; --k)
                        list << -(size - bevel) - bevel * steps[k];

                    for (int k = 0; k < 5; ++k)
                        list << (size - bevel) + bevel * steps[k];

                    return list;
                };

                const QVector<double> us = places(h[a]);
                const QVector<double> vs = places(h[b]);

                const int base = count();

                for (int i = 0; i < us.size(); ++i)
                {
                    for (int j = 0; j < vs.size(); ++j)
                    {
                        double p[3];

                        p[axis] = sign * h[axis];
                        p[a] = us.at(i);
                        p[b] = vs.at(j);

                        double inner[3];
                        double way[3];

                        for (int k = 0; k < 3; ++k)
                        {
                            inner[k] = std::clamp(p[k], -(h[k] - bevel), h[k] - bevel);
                            way[k] = p[k] - inner[k];
                        }

                        const double l = std::max(std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]), 1.0e-9);

                        const Vec3 n(way[0] / l, way[1] / l, way[2] / l);

                        const Vec3 q(c[0] + inner[0] + n.x * bevel, c[1] + inner[1] + n.y * bevel, c[2] + inner[2] + n.z * bevel);

                        add(q, n, material);
                    }
                }

                const int rows = vs.size();

                for (int i = 0; i + 1 < us.size(); ++i)
                {
                    for (int j = 0; j + 1 < rows; ++j)
                    {
                        quad(base + i * rows + j, base + (i + 1) * rows + j, base + (i + 1) * rows + j + 1,
                             base + i * rows + j + 1);
                    }
                }
            }
        }
    }

    // a round bar along `axis` with softly rounded rims
    void cylinder(const Vec3 &centre, const Vec3 &axis, double radius, double halfLength, double bevel, int material)
    {
        constexpr int Around = 28;

        const Vec3 helper = std::abs(axis.y) < 0.9 ? Vec3(0.0, 1.0, 0.0) : Vec3(1.0, 0.0, 0.0);
        const Vec3 u = Vec3::cross(axis, helper).normalized();
        const Vec3 w = Vec3::cross(axis, u);

        // the outline from the middle of one end to the middle of the other:
        // radius, along, and which way the surface looks there
        struct Ring
        {
            double r, x, nr, nx;
        };

        QVector<Ring> rings;

        rings.push_back({0.0, -halfLength, 0.0, -1.0});

        constexpr int RimSteps = 4;

        for (int k = 0; k <= RimSteps; ++k)
        {
            const double a = 0.5 * Pi * k / RimSteps;

            rings.push_back({radius - bevel + bevel * std::sin(a), -halfLength + bevel - bevel * std::cos(a), std::sin(a), -std::cos(a)});
        }

        for (int k = RimSteps; k >= 0; --k)
        {
            const double a = 0.5 * Pi * k / RimSteps;

            rings.push_back({radius - bevel + bevel * std::sin(a), halfLength - bevel + bevel * std::cos(a), std::sin(a), std::cos(a)});
        }

        rings.push_back({0.0, halfLength, 0.0, 1.0});

        const int base = count();

        for (const Ring &ring : rings)
        {
            for (int k = 0; k < Around; ++k)
            {
                const double a = 2.0 * Pi * k / Around;

                const Vec3 out = u * std::cos(a) + w * std::sin(a);

                add(centre + axis * ring.x + out * ring.r, (out * ring.nr + axis * ring.nx).normalized(), material);
            }
        }

        for (int r = 0; r + 1 < rings.size(); ++r)
        {
            for (int k = 0; k < Around; ++k)
            {
                const int next = (k + 1) % Around;

                // (the rings at the ends have no size: a triangle each)
                const int a = base + r * Around + k;
                const int b = base + r * Around + next;
                const int c = base + (r + 1) * Around + next;
                const int d = base + (r + 1) * Around + k;

                quad(d, c, b, a);
            }
        }
    }

    /*
     * A shape turned on a lathe: `outline` is (place along the axis, radius),
     * from one end to the other, the surface looking outward. `flatten`
     * squashes it across (a roof), `shift` moves it there.
     */
    void lathe(
        const Vec3 &base,
        const Vec3 &axis,
        const QVector<QPointF> &outline,
        int material,
        double flatten = 1.0,
        const Vec3 &shift = Vec3(0.0, 0.0, 0.0))
    {
        const int rows = outline.size();

        if (rows < 2)
            return;

        constexpr int Around = 32;

        const Vec3 helper = std::abs(axis.y) < 0.9 ? Vec3(0.0, 1.0, 0.0) : Vec3(1.0, 0.0, 0.0);
        const Vec3 u = Vec3::cross(axis, helper).normalized();
        const Vec3 w = Vec3::cross(axis, u);

        const int start = count();

        for (int r = 0; r < rows; ++r)
        {
            const QPointF before = outline.at(std::max(r - 1, 0));
            const QPointF after = outline.at(std::min(r + 1, rows - 1));

            const double da = after.x() - before.x();
            const double dr = after.y() - before.y();
            const double l = std::max(std::hypot(da, dr), 1.0e-9);

            const double nr = da / l;
            const double na = -dr / l;

            for (int k = 0; k < Around; ++k)
            {
                const double a = 2.0 * Pi * k / Around;

                Vec3 out = u * std::cos(a) + w * std::sin(a);

                // (the squashing is across y, as a roof is)
                Vec3 place = out * outline.at(r).y();

                place.y *= flatten;

                Vec3 normal = out * nr + axis * na;

                normal.y /= std::max(flatten, 0.05);

                add(base + shift + axis * outline.at(r).x() + place, normal.normalized(), material);
            }
        }

        for (int r = 0; r + 1 < rows; ++r)
        {
            for (int k = 0; k < Around; ++k)
            {
                const int next = (k + 1) % Around;

                quad(start + r * Around + k, start + r * Around + next, start + (r + 1) * Around + next, start + (r + 1) * Around + k);
            }
        }
    }

    // a round bar from one point to another
    void bar(const Vec3 &a, const Vec3 &b, double radius, int material)
    {
        const Vec3 along = b - a;

        cylinder((a + b) * 0.5, along.normalized(), radius, 0.5 * along.length(), 0.35 * radius, material);
    }

    void magnet(double x)
    {
        lathe(Vec3(x > 0.0 ? x - 3.0 : x + 3.0, 11.0, 0.0), Vec3(x > 0.0 ? 1.0 : -1.0, 0.0, 0.0),
              {{0.0, 2.4}, {3.6, 2.4}, {3.8, 3.4}, {5.6, 3.4}, {6.0, 2.8}, {6.1, 0.0}}, 8);
    }
};

TrainModel makeTrain()
{
    TrainModel model;

    ModelBuilder b;

    const Vec3 forward(1.0, 0.0, 0.0);
    const Vec3 up(0.0, 1.0, 0.0);
    const Vec3 across(0.0, 0.0, 1.0);

    enum
    {
        Red = 0,
        Blue = 1,
        Cream = 2,
        Wood = 3,
        Black = 4,
        Brass = 5,
        Green = 6,
        Glass = 7,
        Steel = 8,
        Lamp = 9
    };

    // the locomotive: 94 mm long, three coupled axles
    b.box(Vec3(0.0, 11.5, 0.0), Vec3(45.0, 3.0, 10.0), 1.0, Black);

    // the running board, with a thin brass edge
    b.box(Vec3(-2.0, 15.0, 0.0), Vec3(47.0, 0.9, 14.0), 0.6, Black);

    for (const double z : {-14.1, 14.1})
        b.box(Vec3(-2.0, 15.0, z), Vec3(47.0, 0.5, 0.25), 0.2, Brass);

    // the boiler, its bands, and the smokebox with its round door
    b.cylinder(Vec3(11.0, 25.0, 0.0), forward, 10.0, 26.0, 0.4, Red);

    for (const double x : {-11.0, 0.5, 12.0, 23.5, 34.5})
        b.lathe(Vec3(x, 25.0, 0.0), forward, {{-1.0, 10.0}, {-0.85, 10.42}, {0.85, 10.42}, {1.0, 10.0}}, Brass);

    b.lathe(
        Vec3(36.0, 25.0, 0.0),
        forward,
        {{0.0, 10.0}, {0.3, 10.6}, {8.0, 10.6}, {9.2, 10.2}, {10.0, 9.2}, {10.4, 8.0}, {10.5, 0.0}},
        Black);

    b.lathe(Vec3(46.4, 25.0, 0.0), forward, {{0.0, 7.6}, {0.5, 7.6}, {0.7, 7.0}, {0.7, 0.0}}, Steel);
    b.lathe(Vec3(47.0, 25.0, 0.0), forward, {{0.0, 1.5}, {0.9, 1.3}, {1.3, 0.0}}, Brass);

    // the funnel, flaring at the top
    b.lathe(
        Vec3(38.5, 33.0, 0.0),
        up,
        {{0.0, 5.6}, {1.0, 4.1}, {2.0, 3.5}, {4.5, 3.4}, {5.7, 3.9}, {6.4, 5.0}, {6.8, 5.3}, {6.9, 4.1}, {6.0, 3.1}, {5.0, 0.0}},
        Black);

    b.lathe(Vec3(38.5, 39.0, 0.0), up, {{0.0, 4.9}, {0.5, 5.4}, {0.8, 5.35}, {1.0, 4.7}}, Brass);

    // the two brass domes, the whistle
    for (const double x : {-1.0, 19.0})
    {
        b.lathe(
            Vec3(x, 33.0, 0.0),
            up,
            {{0.0, 5.0}, {1.0, 3.7}, {1.6, 3.5}, {3.0, 3.4}, {4.2, 2.9}, {5.0, 1.9}, {5.4, 0.8}, {5.5, 0.0}},
            Brass);
    }

    b.lathe(Vec3(-10.0, 34.5, 0.0), up, {{0.0, 0.9}, {2.0, 0.7}, {2.1, 1.2}, {3.4, 1.0}, {3.7, 0.0}}, Brass);

    // handrails along the boiler, on their posts
    for (const double z : {-11.3, 11.3})
    {
        b.bar(Vec3(-13.0, 28.5, z), Vec3(35.0, 28.5, z), 0.5, Brass);

        for (const double x : {-8.0, 8.0, 24.0})
            b.bar(Vec3(x, 28.5, z), Vec3(x, 27.0, z * 0.84), 0.4, Brass);
    }

    // the cylinders that drive the wheels
    for (const double z : {-13.2, 13.2})
    {
        b.lathe(
            Vec3(28.0, 11.0, z),
            forward,
            {{0.0, 0.0}, {0.1, 3.0}, {0.7, 3.6}, {1.5, 3.6}, {1.7, 3.3}, {10.3, 3.3}, {10.5, 3.6}, {11.3, 3.6}, {11.9, 3.0}, {12.0, 0.0}},
            Green);

        b.bar(Vec3(18.0, 8.2, z), Vec3(28.0, 8.2, z), 0.7, Steel);
    }

    // the cab: walls, framed windows, a roof with an overhang
    b.box(Vec3(-29.0, 25.0, 0.0), Vec3(14.0, 10.5, 12.8), 1.4, Blue);

    b.lathe(Vec3(-45.5, 29.5, 0.0), forward, {{0.0, 12.4}, {0.5, 14.2}, {32.5, 14.2}, {33.0, 12.4}}, Black, 0.5);
    b.box(Vec3(-29.0, 32.6, 0.0), Vec3(16.4, 1.6, 15.0), 0.4, Black);

    for (const double x : {-45.6, -12.4})
        b.box(Vec3(x, 34.4, 0.0), Vec3(0.3, 0.4, 13.0), 0.15, Brass);

    for (const double z : {-12.95, 12.95})
    {
        for (const double x : {-34.0, -22.5})
        {
            b.box(Vec3(x, 27.5, z), Vec3(4.6, 5.4, 0.28), 0.2, Brass);
            b.box(Vec3(x, 27.5, z * 1.012), Vec3(3.8, 4.6, 0.25), 0.15, Glass);
        }

        // the number plate
        b.box(Vec3(-28.5, 18.6, z), Vec3(5.5, 1.9, 0.3), 0.3, Brass);
    }

    for (const double z : {-6.2, 6.2})
    {
        b.box(Vec3(-14.85, 28.5, z), Vec3(0.3, 4.0, 4.0), 0.2, Brass);
        b.box(Vec3(-14.7, 28.5, z), Vec3(0.28, 3.3, 3.3), 0.15, Glass);
    }

    // the buffer beam, its buffers and lamp, and the cow catcher
    b.box(Vec3(45.5, 13.0, 0.0), Vec3(1.6, 3.6, 13.0), 0.9, Red);

    for (const double z : {-8.0, 8.0})
    {
        b.lathe(
            Vec3(46.8, 13.0, z),
            forward,
            {{0.0, 1.8}, {2.4, 1.6}, {2.5, 2.7}, {3.3, 2.7}, {3.6, 2.2}, {3.7, 0.0}},
            Brass);
    }

    b.lathe(Vec3(45.5, 19.0, 0.0), forward, {{0.0, 0.0}, {0.1, 2.6}, {3.2, 2.9}, {3.6, 3.2}, {3.8, 2.8}}, Brass);
    b.lathe(Vec3(49.1, 19.0, 0.0), forward, {{0.0, 2.8}, {0.5, 2.0}, {0.7, 0.0}}, Lamp);

    for (const double z : {-10.0, -6.0, -2.0, 2.0, 6.0, 10.0})
        b.bar(Vec3(46.5, 10.0, z), Vec3(55.0, 1.6, z * 0.12), 0.75, Cream);

    b.magnet(-48.0);

    model.parts[0] = TrainModel::Part{0, static_cast<int>(b.index.size())};
    model.behind[0] = 0.0;
    model.axles[0] = 26.0;
    model.axleX[0] = {26.0, 0.0, -26.0};

    // a wagon of logs, in a tub of planks, held by stakes
    {
        const int first = b.index.size();

        b.box(Vec3(0.0, 11.0, 0.0), Vec3(31.0, 2.6, 10.5), 1.0, Black);
        b.box(Vec3(0.0, 15.0, 0.0), Vec3(32.0, 1.2, 13.5), 0.7, Wood);

        for (const double y : {18.0, 21.2, 24.4})
        {
            for (const double z : {-12.9, 12.9})
                b.box(Vec3(0.0, y, z), Vec3(31.0, 1.45, 0.75), 0.55, Cream);

            for (const double x : {-30.5, 30.5})
                b.box(Vec3(x, y, 0.0), Vec3(0.75, 1.45, 12.4), 0.55, Cream);
        }

        for (const double x : {-22.0, 0.0, 22.0})
        {
            for (const double z : {-13.8, 13.8})
                b.box(Vec3(x, 21.0, z), Vec3(1.1, 6.6, 0.55), 0.35, Red);
        }

        struct Log
        {
            double y, z, r;
        };

        for (const Log &log : {Log{27.0, -7.2, 4.7}, Log{27.0, 0.0, 4.9}, Log{27.0, 7.2, 4.6}, Log{33.4, -3.6, 4.6}, Log{33.4, 3.8, 4.5}})
            b.lathe(Vec3(-27.0, log.y, log.z), forward, {{0.0, 0.0}, {0.05, log.r - 0.5}, {0.5, log.r}, {53.5, log.r}, {54.0, log.r - 0.5}, {54.05, 0.0}}, Wood);

        b.magnet(34.5);
        b.magnet(-34.5);

        model.parts[1] = TrainModel::Part{first, static_cast<int>(b.index.size()) - first};
        model.behind[1] = 46.0 + 8.0 + 33.0;
        model.axles[1] = 20.0;
        model.axleX[1] = {20.0, -20.0};
    }

    // a green carriage with framed windows, doors, a gold line and a lamp
    {
        const int first = b.index.size();

        b.box(Vec3(0.0, 11.0, 0.0), Vec3(31.0, 2.6, 10.5), 1.0, Black);
        b.box(Vec3(0.0, 15.0, 0.0), Vec3(32.0, 1.2, 13.5), 0.7, Black);
        b.box(Vec3(0.0, 25.0, 0.0), Vec3(30.0, 9.6, 12.5), 1.6, Green);

        // gold lines along the sides
        for (const double z : {-12.55, 12.55})
        {
            b.box(Vec3(0.0, 19.6, z), Vec3(29.0, 0.4, 0.2), 0.15, Brass);
            b.box(Vec3(0.0, 31.6, z), Vec3(29.0, 0.4, 0.2), 0.15, Brass);

            for (const double x : {-19.0, 0.0, 19.0})
            {
                b.box(Vec3(x, 26.0, z), Vec3(6.0, 4.4, 0.28), 0.2, Cream);
                b.box(Vec3(x, 26.0, z * 1.012), Vec3(5.2, 3.6, 0.25), 0.15, Glass);
            }

            for (const double x : {-9.5, 9.5})
            {
                b.box(Vec3(x, 24.0, z), Vec3(2.6, 7.0, 0.22), 0.2, Cream);
                b.bar(Vec3(x + 1.6, 22.0, z * 1.03), Vec3(x + 1.6, 22.0, z * 1.05), 0.5, Brass);
            }
        }

        // a curved roof, and a raised strip along it
        b.lathe(Vec3(-33.0, 29.5, 0.0), forward, {{0.0, 12.2}, {0.8, 13.6}, {65.2, 13.6}, {66.0, 12.2}}, Red, 0.4);
        b.box(Vec3(0.0, 36.3, 0.0), Vec3(24.0, 1.1, 3.6), 0.9, Red);

        b.magnet(34.5);

        model.parts[2] = TrainModel::Part{first, static_cast<int>(b.index.size()) - first};
        model.behind[2] = model.behind[1] + 33.0 + 8.0 + 33.0;
        model.axles[2] = 20.0;
        model.axleX[2] = {20.0, -20.0};
    }

    // two wheels and their axle: tyre with its flange inside the rail, spokes,
    // hub. They turn as they roll.
    {
        const int first = b.index.size();

        const double r = TrainModel::WheelRadius;

        for (const double z : {-10.0, 10.0})
        {
            const double inward = z > 0.0 ? -1.0 : 1.0;

            // from the flange on the inner side to the outer face
            b.lathe(
                Vec3(0.0, 0.0, z + 2.4 * inward),
                across * -inward,
                {{0.0, r - 1.4}, {0.1, r + 1.0}, {0.9, r + 1.0}, {1.2, r}, {4.6, r - 0.15}, {4.8, r - 1.4}},
                Steel);

            for (int spoke = 0; spoke < 8; ++spoke)
            {
                const double a = spoke * Pi / 4.0;

                b.bar(Vec3(0.0, 0.0, z), Vec3((r - 1.3) * std::cos(a), (r - 1.3) * std::sin(a), z), 0.55, Black);
            }

            b.lathe(Vec3(0.0, 0.0, z - 1.8), across, {{0.0, r - 1.7}, {0.3, r - 1.1}, {3.3, r - 1.1}, {3.6, r - 1.7}}, Black);

            // the hub
            b.lathe(Vec3(0.0, 0.0, z + 1.8 * inward), across * -inward, {{0.0, 0.0}, {0.1, 1.8}, {3.4, 1.8}, {4.0, 1.2}, {4.2, 0.0}}, Steel);
        }

        b.bar(Vec3(0.0, 0.0, -9.0), Vec3(0.0, 0.0, 9.0), 1.1, Steel);

        model.wheels = TrainModel::Part{first, static_cast<int>(b.index.size()) - first};
    }

    // the side rods, which go round with the crank pins
    {
        const int first = b.index.size();

        for (const double z : {-14.6, 14.6})
        {
            b.box(Vec3(0.0, 0.0, z), Vec3(28.0, 0.95, 0.55), 0.35, Steel);

            for (const double x : {-26.0, 0.0, 26.0})
                b.lathe(Vec3(x, 0.0, z > 0.0 ? z - 2.4 : z - 1.0), across, {{0.0, 1.0}, {2.4, 1.0}, {2.5, 1.7}, {3.3, 1.7}, {3.5, 0.0}}, Brass);
        }

        model.rods = TrainModel::Part{first, static_cast<int>(b.index.size()) - first};
    }

    model.vertices = b.v;
    model.indices = b.index;

    return model;
}

Vec3 pathAt(const QVector<Vec3> &path, double length, double along)
{
    const int n = path.size();

    double u = along / length;

    u = (u - std::floor(u)) * n;

    const int i = std::min(static_cast<int>(u), n - 1);
    const double t = u - i;

    return path.at(i) + (path.at((i + 1) % n) - path.at(i)) * t;
}

} // namespace

TrainTrack buildTrainTrack(const QVector<Vec3> &curve, RopeProgress *progress)
{
    TrainTrack result;

    auto cancelled = [progress]() { return progress != nullptr && progress->cancel.load(); };
    auto report = [progress](int percent)
    {
        if (progress != nullptr)
            progress->percent = percent;
    };

    QString error;

    /*
     * The clean drawings with the fewest crossings: each opened up, and the
     * one that takes the least floor is built.
     */
    QVector<KnotMath::KnotDiagram> diagrams = KnotMath::diagramsFromCurve(curve, 2.0, error, 3);

    if (diagrams.isEmpty())
    {
        result.error = error;

        return result;
    }

    const int fewest = diagrams.first().crossings.size();

    const KnotMath::AlexanderPolynomial alexander = KnotMath::computeAlexanderPolynomial(curve);
    const KnotMath::JonesPolynomial jones = KnotMath::computeJonesPolynomial(curve);

    double bestLength = 1.0e30;

    const int passes = 2 * fewest;

    // a layout this long is as good as one gets for so many crossings
    const double goodLength = std::max(3000.0, passes * 620.0);

    int attempts = 0;

    /*
     * One layout of one drawing: built, and kept if it is shorter than the
     * best so far and really is this knot.
     */
    auto attempt = [&](KnotMath::KnotDiagram diagram, int outside, double longest) -> bool
    {
        ++attempts;

        if (!KnotMath::openDiagram(diagram, outside))
            return false;

        const Plan plan = makePlan(diagram);

        const double scale = fittingScale(plan, longest);

        if (scale <= 0.0 || plan.length * scale >= bestLength)
            return true;

        const QVector<Vec3> path = trackAtScale(plan, scale);

        /*
         * The finished centre line has to be this knot. (A knot with a
         * mirror image of its own: the Jones polynomial tells them apart.)
         */
        QVector<Vec3> coarse;

        const int stride = std::max(1, static_cast<int>(path.size()) / 900);

        for (int i = 0; i < path.size(); i += stride)
            coarse.push_back(path.at(i));

        const KnotMath::AlexanderPolynomial a = KnotMath::computeAlexanderPolynomial(coarse);

        if (alexander.valid && (!a.valid || a.text != alexander.text))
            return true;

        if (jones.valid)
        {
            const KnotMath::JonesPolynomial j = KnotMath::computeJonesPolynomial(coarse);

            if (j.valid && j.text != jones.text)
                return true;
        }

        bestLength = plan.length * scale;

        result.path = path;
        result.length = bestLength;

        result.closestStrands = measure(path, bestLength / path.size()).closest;
        result.tightestBend = plan.tightest * scale;

        double steepest = 0.0;

        const double spacing = bestLength / path.size();

        for (int i = 0; i < path.size(); ++i)
            steepest = std::max(steepest, std::abs(path.at((i + 1) % path.size()).y - path.at(i).y) / spacing);

        result.steepest = steepest;

        // the bridges: over the crossing of the two strands
        result.bridges.clear();

        QVector<bool> seen(plan.crossings, false);
        QVector<int> numberOf(plan.crossings, 0);

        // numbered in the order the train first meets them, over or under
        int number = 0;

        for (const PassOnTrack &pass : plan.passes)
        {
            if (numberOf.at(pass.crossing) == 0)
                numberOf[pass.crossing] = ++number;
        }

        for (const PassOnTrack &pass : plan.passes)
        {
            if (!pass.over || seen.at(pass.crossing))
                continue;

            seen[pass.crossing] = true;

            const Vec3 p = pathAt(path, bestLength, pass.s * bestLength);

            KnotMath::CrossingInfo bridge;

            bridge.number = numberOf.at(pass.crossing);
            bridge.pointA = p;
            bridge.pointB = Vec3(p.x, GroundLevelMm, p.z);
            bridge.midpoint = p;
            bridge.aIsOver = true;

            result.bridges.push_back(bridge);
        }

        result.journey.clear();

        for (const PassOnTrack &pass : plan.passes)
            result.journey.push_back({pass.s * bestLength, numberOf.at(pass.crossing), pass.over});

        result.alternating = true;

        for (int k = 0; k < result.journey.size(); ++k)
        {
            if (result.journey.at(k).over == result.journey.at((k + 1) % result.journey.size()).over)
                result.alternating = false;
        }

        result.valid = true;

        return true;
    };

    /*
     * Each face of each drawing on the outside is another layout of the same
     * knot. The first few are all tried for the shortest; after that, only
     * until one fits. Then the same again with room for a long track, and a
     * bad layout is only ever a last resort: it takes time, and the first
     * faces are the best.
     */
    constexpr int WantedAttempts = 3;

    const int mostFaces = std::min(fewest + 2, 6);

    for (int outside = 0; outside < mostFaces; ++outside)
    {
        bool anyLeft = false;

        for (const KnotMath::KnotDiagram &diagram : std::as_const(diagrams))
        {
            if (diagram.crossings.size() != fewest)
                continue;

            if (cancelled())
            {
                result.cancelled = true;
                result.valid = false;

                return result;
            }

            report(std::min(10 + 12 * attempts, 80));

            anyLeft = attempt(diagram, outside, LongestTrackMm) || anyLeft;

            if (result.valid && (bestLength <= goodLength || attempts >= WantedAttempts))
                break;
        }

        if (!anyLeft || (result.valid && (bestLength <= goodLength || attempts >= WantedAttempts)))
            break;
    }

    // last, the drawings as they were seen
    for (int outside : {0, 1, 2, -1})
    {
        for (const KnotMath::KnotDiagram &diagram : std::as_const(diagrams))
        {
            if (result.valid)
                break;

            if (cancelled())
            {
                result.cancelled = true;

                return result;
            }

            if (diagram.crossings.size() == fewest)
                attempt(diagram, outside, LongestLastResortMm);
        }
    }

    if (!result.valid)
    {
        result.error = QCoreApplication::translate(
            "Photo",
            "No track could be laid for this knot.");

        return result;
    }

    report(85);

    if (cancelled())
    {
        result.cancelled = true;
        result.valid = false;

        return result;
    }

    // the same scale as any rope: the half width is 0.16 view units
    result.unitsPerMm = 0.16 / (0.5 * TrackWidthMm);

    buildMesh(result);

    const float units = float(result.unitsPerMm);

    for (KnotMath::CrossingInfo &bridge : result.bridges)
    {
        bridge.pointA = bridge.pointA * units;
        bridge.pointB = bridge.pointB * units;
        bridge.midpoint = bridge.midpoint * units;
    }

    result.rope.valid = true;
    result.rope.settled = true;
    result.rope.points.reserve(result.path.size());

    for (const Vec3 &p : result.path)
        result.rope.points.push_back(p * units);

    report(100);

    return result;
}

const TrainModel &trainModel()
{
    static const TrainModel model = makeTrain();

    return model;
}

QMatrix4x4 placeVehicle(
    const QVector<Vec3> &path,
    double length,
    double along,
    double halfAxles)
{
    const Vec3 ahead = pathAt(path, length, along + halfAxles);
    const Vec3 back = pathAt(path, length, along - halfAxles);

    const Vec3 forward = (ahead - back).normalized();
    const Vec3 side = Vec3::cross(forward, Vec3(0.0, 1.0, 0.0)).normalized();
    const Vec3 up = Vec3::cross(side, forward);

    const Vec3 middle = (ahead + back) * 0.5;

    return QMatrix4x4(
        float(forward.x), float(up.x), float(side.x), float(middle.x),
        float(forward.y), float(up.y), float(side.y), float(middle.y),
        float(forward.z), float(up.z), float(side.z), float(middle.z),
        0.0f, 0.0f, 0.0f, 1.0f);
}

QVector<TrainPiece> placeTrain(
    const QVector<Vec3> &path,
    double length,
    double along)
{
    const TrainModel &train = trainModel();

    QVector<TrainPiece> pieces;

    pieces.reserve(16);

    // a wheel that rolls without slipping: forward along x, it turns from
    // +y towards +x, which is clockwise about z
    const double turned = along / TrainModel::WheelRadius;

    const float degrees = float(turned * 180.0 / Pi);

    for (int k = 0; k < TrainModel::Vehicles; ++k)
    {
        const QMatrix4x4 place = placeVehicle(path, length, along - train.behind[k], train.axles[k]);

        pieces.push_back(TrainPiece{place, train.parts[k].firstIndex, train.parts[k].indexCount});

        for (const double x : train.axleX[k])
        {
            QMatrix4x4 wheel = place;

            wheel.translate(float(x), float(TrainModel::AxleHeight), 0.0f);
            wheel.rotate(-degrees, 0.0f, 0.0f, 1.0f);

            pieces.push_back(TrainPiece{wheel, train.wheels.firstIndex, train.wheels.indexCount});
        }

        if (k == 0)
        {
            QMatrix4x4 rods = place;

            rods.translate(
                float(TrainModel::CrankRadius * std::cos(-turned)),
                float(TrainModel::AxleHeight + TrainModel::CrankRadius * std::sin(-turned)),
                0.0f);

            pieces.push_back(TrainPiece{rods, train.rods.firstIndex, train.rods.indexCount});
        }
    }

    return pieces;
}

} // namespace Photo
