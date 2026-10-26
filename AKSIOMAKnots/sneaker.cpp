#include "sneaker.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Photo
{

namespace
{

using KnotMath::Vec3;

// the grid of the shoe's top: one sample per millimetre
constexpr double GridStep = 1.0;

// the lace is built from points this far apart (mm), then resampled half
// a radius apart for the physics
constexpr double PathStep = 0.4;

// height of an eyelet's ring above the flap (the model's torus)
constexpr double RingTop = 1.4;

QVector3D q(const Vec3 &v)
{
    return QVector3D(float(v.x), float(v.y), float(v.z));
}

Vec3 v3(const QVector3D &v)
{
    return Vec3(v.x(), v.y(), v.z());
}

double smoothstep(double edge0, double edge1, double x)
{
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);

    return t * t * (3.0 - 2.0 * t);
}

// bilinear value of a grid (row-major, x the slow index) at (u, v) in cells
double bilinear(const QVector<float> &grid, int nx, int nz, double u, double v)
{
    if (nx < 2 || nz < 2)
        return 0.0;

    u = std::clamp(u, 0.0, double(nx - 1));
    v = std::clamp(v, 0.0, double(nz - 1));

    const int i = std::min(static_cast<int>(u), nx - 2);
    const int j = std::min(static_cast<int>(v), nz - 2);

    const double fu = u - i;
    const double fv = v - j;

    const double a = grid.at(i * nz + j);
    const double b = grid.at((i + 1) * nz + j);
    const double c = grid.at(i * nz + j + 1);
    const double d = grid.at((i + 1) * nz + j + 1);

    return (a * (1.0 - fu) + b * fu) * (1.0 - fv) + (c * (1.0 - fu) + d * fu) * fv;
}

QVector3D vector(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();

    return QVector3D(float(a.at(0).toDouble()), float(a.at(1).toDouble()), float(a.at(2).toDouble()));
}

Sneaker load()
{
    Sneaker shoe;

    QFile surface(QStringLiteral(":/data/photo/sneaker.bin"));

    if (!surface.open(QIODevice::ReadOnly))
    {
        shoe.error = QStringLiteral("no sneaker.bin");
        return shoe;
    }

    const QByteArray data = surface.readAll();

    if (data.size() < 16 || !data.startsWith("AKSHOE01"))
    {
        shoe.error = QStringLiteral("sneaker.bin is not a shoe");
        return shoe;
    }

    quint32 counts[2] = {0, 0};

    std::memcpy(counts, data.constData() + 8, sizeof(counts));

    const quint32 vertexCount = qFromLittleEndian(counts[0]);
    const quint32 indexCount = qFromLittleEndian(counts[1]);

    const qint64 expected =
        16 + qint64(vertexCount) * Sneaker::VertexFloats * 4 + qint64(indexCount) * 4;

    if (data.size() != expected || vertexCount == 0 || indexCount % 3 != 0)
    {
        shoe.error = QStringLiteral("sneaker.bin has the wrong size");
        return shoe;
    }

    shoe.vertices.resize(int(vertexCount) * Sneaker::VertexFloats);
    shoe.indices.resize(int(indexCount));

    std::memcpy(shoe.vertices.data(), data.constData() + 16, size_t(vertexCount) * Sneaker::VertexFloats * 4);
    std::memcpy(
        shoe.indices.data(),
        data.constData() + 16 + qint64(vertexCount) * Sneaker::VertexFloats * 4,
        size_t(indexCount) * 4);

    for (const quint32 index : shoe.indices)
    {
        if (index >= vertexCount)
        {
            shoe.error = QStringLiteral("sneaker.bin has a broken triangle");
            return shoe;
        }
    }

    shoe.minimum = QVector3D(1.0e9f, 1.0e9f, 1.0e9f);
    shoe.maximum = -shoe.minimum;

    for (quint32 i = 0; i < vertexCount; ++i)
    {
        const float *v = shoe.vertices.constData() + i * Sneaker::VertexFloats;
        const QVector3D p(v[0], v[1], v[2]);

        shoe.minimum = QVector3D(std::min(shoe.minimum.x(), p.x()), std::min(shoe.minimum.y(), p.y()),
                                 std::min(shoe.minimum.z(), p.z()));
        shoe.maximum = QVector3D(std::max(shoe.maximum.x(), p.x()), std::max(shoe.maximum.y(), p.y()),
                                 std::max(shoe.maximum.z(), p.z()));
    }

    // the eyelets and the guide for the lacing
    QFile guideFile(QStringLiteral(":/data/photo/sneaker.json"));

    if (!guideFile.open(QIODevice::ReadOnly))
    {
        shoe.error = QStringLiteral("no sneaker.json");
        return shoe;
    }

    const QJsonObject guide = QJsonDocument::fromJson(guideFile.readAll()).object();

    shoe.laceDiameter = guide.value(QStringLiteral("lace")).toDouble(4.5);
    shoe.holeRadius = guide.value(QStringLiteral("hole")).toDouble(2.7);
    shoe.shell = guide.value(QStringLiteral("shell")).toDouble(2.2);

    for (const QJsonValue &value : guide.value(QStringLiteral("eyelets")).toArray())
    {
        const QJsonObject o = value.toObject();

        Sneaker::Eyelet e;
        e.row = o.value(QStringLiteral("row")).toInt();
        e.left = o.value(QStringLiteral("side")).toString() == QStringLiteral("left");
        e.centre = vector(o.value(QStringLiteral("centre")));
        e.axis = vector(o.value(QStringLiteral("axis"))).normalized();
        e.edge = o.value(QStringLiteral("edge")).toDouble();

        shoe.eyelets.push_back(e);
        shoe.rows = std::max(shoe.rows, e.row + 1);
    }

    const QJsonObject tongue = guide.value(QStringLiteral("tongue")).toObject();

    shoe.tongueX0 = tongue.value(QStringLiteral("x0")).toDouble();
    shoe.tongueZ0 = tongue.value(QStringLiteral("z0")).toDouble();
    shoe.tongueStep = tongue.value(QStringLiteral("step")).toDouble(2.0);
    shoe.tongueNx = tongue.value(QStringLiteral("nx")).toInt();
    shoe.tongueNz = tongue.value(QStringLiteral("nz")).toInt();

    for (const QJsonValue &row : tongue.value(QStringLiteral("top")).toArray())
    {
        for (const QJsonValue &h : row.toArray())
            shoe.tongue.push_back(float(h.toDouble()));
    }

    if (shoe.tongue.size() != shoe.tongueNx * shoe.tongueNz || shoe.rows < 2)
    {
        shoe.error = QStringLiteral("sneaker.json is incomplete");
        return shoe;
    }

    for (int row = 0; row < shoe.rows; ++row)
    {
        if (shoe.eyelet(row, true) == nullptr || shoe.eyelet(row, false) == nullptr)
        {
            shoe.error = QStringLiteral("sneaker.json misses an eyelet");
            return shoe;
        }
    }

    const QJsonObject floor = guide.value(QStringLiteral("floor")).toObject();

    const double x0 = floor.value(QStringLiteral("x0")).toDouble();
    const double x1 = floor.value(QStringLiteral("x1")).toDouble();
    const double z0 = floor.value(QStringLiteral("z0")).toDouble();
    const double z1 = floor.value(QStringLiteral("z1")).toDouble();

    shoe.floorArea = QRectF(x0, z0, x1 - x0, z1 - z0);
    shoe.floorShade = QStringLiteral(":/data/photo/sneaker_floor.png");

    /*
     * The top of the shoe seen from above: every triangle drawn into a
     * grid, each sample keeping the highest point over it.
     */
    shoe.gridX0 = std::floor(shoe.minimum.x()) - 2.0;
    shoe.gridZ0 = std::floor(shoe.minimum.z()) - 2.0;
    shoe.gridNx = static_cast<int>(std::ceil((shoe.maximum.x() + 2.0 - shoe.gridX0) / GridStep)) + 1;
    shoe.gridNz = static_cast<int>(std::ceil((shoe.maximum.z() + 2.0 - shoe.gridZ0) / GridStep)) + 1;

    shoe.height.fill(0.0f, shoe.gridNx * shoe.gridNz);

    const float *v = shoe.vertices.constData();

    for (int t = 0; t + 2 < shoe.indices.size(); t += 3)
    {
        const float *a = v + shoe.indices.at(t) * Sneaker::VertexFloats;
        const float *b = v + shoe.indices.at(t + 1) * Sneaker::VertexFloats;
        const float *c = v + shoe.indices.at(t + 2) * Sneaker::VertexFloats;

        // in grid cells
        const double ax = (a[0] - shoe.gridX0) / GridStep, az = (a[2] - shoe.gridZ0) / GridStep;
        const double bx = (b[0] - shoe.gridX0) / GridStep, bz = (b[2] - shoe.gridZ0) / GridStep;
        const double cx = (c[0] - shoe.gridX0) / GridStep, cz = (c[2] - shoe.gridZ0) / GridStep;

        const double area = (bx - ax) * (cz - az) - (cx - ax) * (bz - az);

        // seen edge on from above
        if (std::abs(area) < 1.0e-9)
            continue;

        const int i0 = std::max(0, static_cast<int>(std::ceil(std::min({ax, bx, cx}))));
        const int i1 = std::min(shoe.gridNx - 1, static_cast<int>(std::floor(std::max({ax, bx, cx}))));
        const int j0 = std::max(0, static_cast<int>(std::ceil(std::min({az, bz, cz}))));
        const int j1 = std::min(shoe.gridNz - 1, static_cast<int>(std::floor(std::max({az, bz, cz}))));

        for (int i = i0; i <= i1; ++i)
        {
            for (int j = j0; j <= j1; ++j)
            {
                const double wa = ((bx - i) * (cz - j) - (cx - i) * (bz - j)) / area;
                const double wb = ((cx - i) * (az - j) - (ax - i) * (cz - j)) / area;
                const double wc = 1.0 - wa - wb;

                if (wa < -1.0e-6 || wb < -1.0e-6 || wc < -1.0e-6)
                    continue;

                const float y = float(wa * a[1] + wb * b[1] + wc * c[1]);

                float &cell = shoe.height[i * shoe.gridNz + j];

                cell = std::max(cell, y);
            }
        }
    }

    shoe.valid = true;

    return shoe;
}

/*
 * A piece of the lace as it is built: points and whether each is held
 * fast (the lacing) or free (the knot and the parts joining it).
 */
struct Path
{
    QVector<Vec3> points;
    QVector<bool> pinned;

    void add(const Vec3 &p, bool pin)
    {
        // where two pieces meet the point is held if either holds it (the
        // top of an eyelet, where a free join starts)
        if (!points.isEmpty() && (points.last() - p).length() < 0.05)
        {
            pinned.last() = pinned.last() || pin;
            return;
        }

        points.push_back(p);
        pinned.push_back(pin);
    }

    void add(const QVector<Vec3> &piece, bool pin)
    {
        for (const Vec3 &p : piece)
            add(p, pin);
    }
};

QVector<Vec3> reversed(QVector<Vec3> points)
{
    std::reverse(points.begin(), points.end());

    return points;
}

// a cubic Bezier curve from p0 to p3
QVector<Vec3> bezier(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2, const Vec3 &p3)
{
    const double length = (p1 - p0).length() + (p2 - p1).length() + (p3 - p2).length();
    const int count = std::max(8, static_cast<int>(length / PathStep));

    QVector<Vec3> points;

    for (int i = 0; i <= count; ++i)
    {
        const double t = double(i) / count;
        const double u = 1.0 - t;

        points.push_back(p0 * (u * u * u) + p1 * (3.0 * u * u * t) + p2 * (3.0 * u * t * t) + p3 * (t * t * t));
    }

    return points;
}

} // namespace

const Sneaker::Eyelet *Sneaker::eyelet(int row, bool left) const
{
    for (const Eyelet &e : eyelets)
    {
        if (e.row == row && e.left == left)
            return &e;
    }

    return nullptr;
}

double Sneaker::heightAt(double x, double z, double *slopeX, double *slopeZ) const
{
    auto at = [this](double px, double pz)
    {
        const double u = (px - gridX0) / GridStep;
        const double v = (pz - gridZ0) / GridStep;

        // the floor beyond the shoe
        if (u < 0.0 || v < 0.0 || u > gridNx - 1 || v > gridNz - 1)
            return 0.0;

        return bilinear(height, gridNx, gridNz, u, v);
    };

    // the slope over two samples, a little smoother than the grid
    if (slopeX != nullptr)
        *slopeX = (at(x + GridStep, z) - at(x - GridStep, z)) / (2.0 * GridStep);

    if (slopeZ != nullptr)
        *slopeZ = (at(x, z + GridStep) - at(x, z - GridStep)) / (2.0 * GridStep);

    return at(x, z);
}

double Sneaker::tongueAt(double x, double z) const
{
    const double u = (x - tongueX0) / tongueStep;
    const double v = (z - tongueZ0) / tongueStep;

    if (u < 0.0 || v < 0.0 || u > tongueNx - 1 || v > tongueNz - 1)
        return heightAt(x, z);

    return bilinear(tongue, tongueNx, tongueNz, u, v);
}

const Sneaker &sneaker()
{
    static const Sneaker shoe = load();

    return shoe;
}

TiedLace tieOnSneaker(
    const QVector<Vec3> &curve,
    double radius,
    const RopeMaterial &material)
{
    TiedLace tied;
    RestingRope &result = tied.rope;

    const Sneaker &shoe = sneaker();

    if (!shoe.valid)
    {
        result.error = shoe.error;
        return tied;
    }

    if (curve.size() < 4 || radius <= 0.0)
    {
        result.error = QStringLiteral("no curve");
        return tied;
    }

    // the lace's radius (mm), and millimetres per unit of the knot
    const double r = 0.5 * material.diameterMm;
    const double scale = r / radius;

    tied.unitsPerMm = 1.0 / scale;

    /*
     * ---------------------------------------------------------
     * The lacing, in millimetres: criss-cross, the usual way. A bar across
     * the bottom; from there each strand crosses over the tongue, slips
     * under the opposite flap and comes up through the next eyelet on
     * that side. Strand A starts on the left, strand B on the right; at
     * every crossing A lies on top.
     * ---------------------------------------------------------
     */
    const int rows = shoe.rows;

    // where the lace comes out of an eyelet (above its ring) and where it
    // runs under the flap below it
    auto top = [&](const Sneaker::Eyelet &e)
    {
        return v3(e.centre + e.axis * float(RingTop + r + 0.2));
    };

    auto under = [&](const Sneaker::Eyelet &e)
    {
        return v3(e.centre - e.axis * float(shoe.shell + r + 0.4));
    };

    // up through an eyelet's hole, along its axis
    auto through = [&](const Sneaker::Eyelet &e)
    {
        const Vec3 a = under(e);
        const Vec3 b = top(e);

        const int count = std::max(2, static_cast<int>((b - a).length() / PathStep));

        QVector<Vec3> points;

        for (int i = 0; i <= count; ++i)
            points.push_back(a + (b - a) * (double(i) / count));

        return points;
    };

    /*
     * Across the tongue from the eyelet `from` to under the flap of `to`:
     * over the flap it lies on (on the canvas and the eyelet's ring), down
     * onto the tongue in the slot, then under the other flap on the
     * tongue. It hangs over the edge of the flap (no steeper than about
     * 50 degrees).
     */
    auto across = [&](const Sneaker::Eyelet &from, const Sneaker::Eyelet &to)
    {
        const Vec3 a = top(from);
        const Vec3 b = under(to);

        const double dx = b.x - a.x;
        const double dz = b.z - a.z;
        const double length = std::hypot(dx, dz);

        const int count = std::max(4, static_cast<int>(length / PathStep));

        QVector<Vec3> points;

        for (int i = 0; i <= count; ++i)
        {
            const double t = double(i) / count;

            const double x = a.x + dx * t;
            const double z = a.z + dz * t;

            // on the flap it starts from (and its folded edge, which
            // reaches a millimetre into the slot) it lies on the shoe;
            // in the slot and under the other flap on the tongue
            const bool onFlap =
                (z > 0.0) == (from.centre.z() > 0.0) && std::abs(z) > from.edge - 1.0;

            const double y = onFlap ? shoe.heightAt(x, z) + r + 0.2 : shoe.tongueAt(x, z) + r + 0.15;

            points.push_back(Vec3(x, y, z));
        }

        points.first() = a;
        points.last() = b;

        // draped over the edge
        const double step = length / count;
        const double slope = 1.2 * step;

        for (int i = 1; i <= count; ++i)
            points[i].y = std::max(points.at(i).y, points.at(i - 1).y - slope);

        for (int i = count - 1; i >= 0; --i)
            points[i].y = std::max(points.at(i).y, points.at(i + 1).y - slope);

        return points;
    };

    auto eyeletOf = [&](bool strandA, int row)
    {
        // strand A: left, right, left, ...; strand B the other way
        const bool left = (row % 2 == 0) == strandA;

        return shoe.eyelet(row, left);
    };

    QVector<QVector<Vec3>> diagonalA;
    QVector<QVector<Vec3>> diagonalB;

    for (int k = 0; k + 1 < rows; ++k)
    {
        diagonalA.push_back(across(*eyeletOf(true, k), *eyeletOf(true, k + 1)));
        diagonalB.push_back(across(*eyeletOf(false, k), *eyeletOf(false, k + 1)));
    }

    /*
     * Where the two strands cross (seen from above) A is lifted over B,
     * by two radii, as far along A as their centre lines come within two
     * radii of each other sideways (2 r / sin of the angle between them),
     * easing back down over 1.5 radii.
     */
    for (int k = 0; k < diagonalA.size(); ++k)
    {
        QVector<Vec3> &over = diagonalA[k];
        const QVector<Vec3> &lower = diagonalB.at(k);

        const Vec3 a0 = over.first();
        const Vec3 a1 = over.last();
        const Vec3 b0 = lower.first();
        const Vec3 b1 = lower.last();

        const double ux = a1.x - a0.x, uz = a1.z - a0.z;
        const double vx = b1.x - b0.x, vz = b1.z - b0.z;

        const double denominator = ux * vz - uz * vx;

        if (std::abs(denominator) < 1.0e-9)
            continue;

        const double s = ((b0.x - a0.x) * vz - (b0.z - a0.z) * vx) / denominator;
        const double t = ((b0.x - a0.x) * uz - (b0.z - a0.z) * ux) / denominator;

        if (s < 0.0 || s > 1.0 || t < 0.0 || t > 1.0)
            continue;

        const double cx = a0.x + ux * s;
        const double cz = a0.z + uz * s;

        // B's height at the crossing
        double lowerY = lower.first().y;
        double nearest = 1.0e30;

        for (const Vec3 &p : lower)
        {
            const double d = std::hypot(p.x - cx, p.z - cz);

            if (d < nearest)
            {
                nearest = d;
                lowerY = p.y;
            }
        }

        const double sine =
            std::abs(denominator) / (std::hypot(ux, uz) * std::hypot(vx, vz));

        const double reach = 2.0 * r / std::max(sine, 0.3);
        const double target = lowerY + 2.0 * r + 0.3;

        for (Vec3 &p : over)
        {
            const double d = std::hypot(p.x - cx, p.z - cz);
            const double w = 1.0 - smoothstep(reach, reach + 1.5 * r, d);

            if (p.y < target)
                p.y += (target - p.y) * w;
        }
    }

    auto strand = [&](bool strandA)
    {
        QVector<Vec3> points;

        for (int k = 0; k < rows; ++k)
        {
            points += through(*eyeletOf(strandA, k));

            if (k + 1 < rows)
                points += strandA ? diagonalA.at(k) : diagonalB.at(k);
        }

        return points;
    };

    const QVector<Vec3> strandA = strand(true);
    const QVector<Vec3> strandB = strand(false);

    // the bar across the bottom, on the tongue (under both flaps)
    QVector<Vec3> bar;

    {
        const Vec3 a = under(*shoe.eyelet(0, true));
        const Vec3 b = under(*shoe.eyelet(0, false));

        const int count = std::max(4, static_cast<int>((b - a).length() / PathStep));

        for (int i = 0; i <= count; ++i)
        {
            Vec3 p = a + (b - a) * (double(i) / count);

            p.y = std::max(p.y, shoe.tongueAt(p.x, p.z) + r + 0.15);

            bar.push_back(p);
        }
    }

    /*
     * ---------------------------------------------------------
     * The knot, laid down flat (turned, never mirrored), its longest
     * direction across the shoe, over the lacing just in front of the top
     * eyelets, high enough to clear everything under it.
     * ---------------------------------------------------------
     */
    const QVector<Vec3> smooth = KnotMath::refineCurve(curve, 4, true);

    QVector<QVector3D> flat;

    for (const Vec3 &p : smooth)
        flat.push_back(q(p));

    QMatrix4x4 lay = flatPose(flat);

    QMatrix4x4 turn;

    turn.rotate(90.0f, 0.0f, 1.0f, 0.0f);
    turn.scale(float(scale));

    lay = turn * lay;

    // evenly spaced, in millimetres
    QVector<Vec3> knot;

    for (const QVector3D &p : flat)
        knot.push_back(v3(lay.map(p)));

    double knotLength = 0.0;

    for (int i = 0; i < knot.size(); ++i)
        knotLength += (knot.at((i + 1) % knot.size()) - knot.at(i)).length();

    const int knotCount = std::max(32, static_cast<int>(knotLength / PathStep));

    knot = KnotMath::resampleClosedCurve(knot, knotCount, true);

    if (knot.size() != knotCount)
    {
        result.error = QStringLiteral("resampling failed");
        return tied;
    }

    // the point nearest the heel: there it is cut open
    int cut = 0;

    for (int i = 1; i < knotCount; ++i)
    {
        if (knot.at(i).x < knot.at(cut).x)
            cut = i;
    }

    const Sneaker::Eyelet &topLeft = *shoe.eyelet(rows - 1, true);
    const Sneaker::Eyelet &topRight = *shoe.eyelet(rows - 1, false);

    const double topX = 0.5 * (topLeft.centre.x() + topRight.centre.x());

    const double moveX = topX + 7.0 - knot.at(cut).x;
    const double moveZ = -knot.at(cut).z;

    // every point at least two lace thicknesses above the shoe under it
    double moveY = -1.0e30;

    for (const Vec3 &p : knot)
    {
        const double x = p.x + moveX;
        const double z = p.z + moveZ;

        double ground = 0.0;

        for (const double ox : {-r, 0.0, r})
        {
            for (const double oz : {-r, 0.0, r})
                ground = std::max(ground, shoe.heightAt(x + ox, z + oz));
        }

        moveY = std::max(moveY, ground + 4.0 * r + 1.5 - p.y);
    }

    QMatrix4x4 place;

    place.translate(float(moveX), float(moveY), float(moveZ));

    lay = place * lay;

    for (Vec3 &p : knot)
        p += Vec3(moveX, moveY, moveZ);

    // the knot's units in the shoe's frame
    QMatrix4x4 down;

    down.scale(float(1.0 / scale));

    result.pose = down * lay;

    /*
     * Cut open at that point: the knot runs from S, 1.5 lace radii after
     * it, all the way round to E, 1.5 radii before it.
     */
    const int gap = std::max(2, static_cast<int>(std::lround(1.5 * r / (knotLength / knotCount))));

    const int start = (cut + gap) % knotCount;
    const int end = (cut - gap + knotCount) % knotCount;

    QVector<Vec3> knotArc;

    for (int i = start; ; i = (i + 1) % knotCount)
    {
        knotArc.push_back(knot.at(i));

        if (i == end)
            break;
    }

    // where S lies along the knot as given (the first point of `curve`
    // is the first of `smooth`, and so of the resampled knot)
    tied.cutFraction = double(start) / knotCount;
    tied.gapFraction = double(2 * gap) / knotCount;

    const Vec3 s = knotArc.first();
    const Vec3 e = knotArc.last();

    // S joins the top eyelet on its own side, so the two joins do not
    // cross (left is -z)
    const bool leftFirst = s.z < e.z;

    const Sneaker::Eyelet &first = leftFirst ? topLeft : topRight;
    const Sneaker::Eyelet &second = leftFirst ? topRight : topLeft;

    /*
     * The joins: out of the eyelet along its axis, into the knot from the
     * heel's side. They stay behind the knot's heel-most point, so the
     * knot itself lies in a ball the rest of the lace stays out of: it is
     * the same knot.
     */
    const Vec3 heelward(-1.0, 0.0, 0.0);

    const Vec3 firstTop = top(first);
    const Vec3 secondTop = top(second);

    const QVector<Vec3> join1 =
        bezier(firstTop, firstTop + v3(first.axis) * 10.0, s + heelward * 8.0, s);
    const QVector<Vec3> join2 =
        bezier(e, e + heelward * 8.0, secondTop + v3(second.axis) * 10.0, secondTop);

    /*
     * The whole lace, one closed loop: down the strand that ends at the
     * second top eyelet, across the bar, up the other strand, out of the
     * first top eyelet into the knot, and from the knot back into the
     * second.
     */
    // is the second strand A (which starts on the left at the bottom)?
    const bool secondIsA = second.left == eyeletOf(true, rows - 1)->left;

    const QVector<Vec3> &secondStrand = secondIsA ? strandA : strandB;
    const QVector<Vec3> &firstStrand = secondIsA ? strandB : strandA;

    Path lace;

    lace.add(reversed(secondStrand), true);
    lace.add(secondIsA ? bar : reversed(bar), true);
    lace.add(firstStrand, true);
    lace.add(join1, false);

    // (S itself was the last point of join1)
    const int knotFirstPoint = lace.points.size() - 1;

    lace.add(knotArc, false);

    const int knotLastPoint = lace.points.size() - 1;

    lace.add(join2, false);

    // the end of join2 is the start of the loop
    if ((lace.points.last() - lace.points.first()).length() < 0.05)
    {
        lace.points.removeLast();
        lace.pinned.removeLast();
    }

    /*
     * Evenly spaced points, half a radius apart, for the physics; a piece
     * between two held points is held.
     */
    const int m = lace.points.size();

    QVector<double> arc(m + 1, 0.0);

    for (int i = 0; i < m; ++i)
        arc[i + 1] = arc.at(i) + (lace.points.at((i + 1) % m) - lace.points.at(i)).length();

    const double total = arc.at(m);
    const int n = std::max(16, static_cast<int>(std::lround(total / (0.5 * r))));

    QVector<Vec3> x(n);
    QVector<bool> pinned(n);

    tied.knotFrom = -1;
    tied.knotTo = -1;

    {
        int segment = 0;

        for (int i = 0; i < n; ++i)
        {
            const double target = total * i / n;

            while (segment + 1 < m && arc.at(segment + 1) < target)
                ++segment;

            const double piece = arc.at(segment + 1) - arc.at(segment);
            const double t = piece > 0.0 ? (target - arc.at(segment)) / piece : 0.0;

            const Vec3 a = lace.points.at(segment);
            const Vec3 b = lace.points.at((segment + 1) % m);

            x[i] = (a + (b - a) * t) / r;
            pinned[i] = lace.pinned.at(segment) && lace.pinned.at((segment + 1) % m);

            if (segment >= knotFirstPoint && segment < knotLastPoint)
            {
                if (tied.knotFrom < 0)
                    tied.knotFrom = i;

                tied.knotTo = i;
            }
        }
    }

    /*
     * The held lacing's corners rounded a little (where it turns out of
     * an eyelet across the tongue).
     */
    for (int pass = 0; pass < 4; ++pass)
    {
        QVector<Vec3> smoothed = x;

        for (int i = 0; i < n; ++i)
        {
            const int a = (i - 1 + n) % n;
            const int b = (i + 1) % n;

            if (pinned.at(i) && pinned.at(a) && pinned.at(b))
                smoothed[i] = x.at(i) * 0.5 + (x.at(a) + x.at(b)) * 0.25;
        }

        x = smoothed;
    }

    // the shoe in units of the lace's radius
    const Ground ground = [&shoe, r](double px, double pz, double &slopeX, double &slopeZ)
    {
        return shoe.heightAt(px * r, pz * r, &slopeX, &slopeZ) / r;
    };

    const Settling settling = settleRope(x, pinned, true, material, ground);

    tied.pinned = pinned;

    result.steps = settling.steps;
    result.seconds = settling.seconds;
    result.finalSpeed = settling.finalSpeed;
    result.settled = settling.settled;

    // back to the knot's coordinates
    const QMatrix4x4 back = result.pose.inverted();

    result.points.reserve(n);

    for (const Vec3 &p : x)
        result.points.push_back(v3(back.map(q(p * (r / scale)))));

    result.valid = true;

    return tied;
}

} // namespace Photo
