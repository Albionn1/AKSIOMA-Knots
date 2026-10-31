// Laboratory measurements of Photo mode's rope (ropephysics.cpp), each with
// the answer it should give: the rope as a physical object, not a knot.
//
//   rope_lab
//
//   free fall      a rope dropped in the air falls with g
//   cantilever     ASTM D1388: the overhang that droops 41.5 degrees is twice
//                  the bending length of the material
//   hanging        a rope held at one end does not stretch
//   slope          a rope on a tilted table slips at atan(friction)
//   set            a rope bent round and let go keeps part of the bend

#include "knottable.h"
#include "ropephysics.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cmath>
#include <cstdio>

using namespace KnotMath;
using namespace Photo;

namespace
{

constexpr double Pi = 3.14159265358979323846;

RopeMaterial hemp()
{
    return sceneInfo(Scene::LoungeTable).rope;
}

RopeMaterial cotton()
{
    return sceneInfo(Scene::BallroomFloor).rope;
}

// a straight rope along x, half a radius between points
QVector<Vec3> straight(int n, double y)
{
    QVector<Vec3> x(n);

    for (int i = 0; i < n; ++i)
        x[i] = Vec3(i * 0.5, y, 0.0);

    return x;
}

// nothing to land on
const Ground NoGround = [](double, double, double &sx, double &sz)
{
    sx = 0.0;
    sz = 0.0;

    return -1.0e9;
};

void freeFall(RopeMaterial material)
{
    material.curlRadiusMm = 1.0e9;

    RopeSimulation rope(straight(60, 0.0), QVector<bool>(60, false), false, material, NoGround);

    const double each = rope.secondsPerStep();
    const double radius = 0.5 * material.diameterMm * 0.001;

    // (short of the highest speed a point may have in a step)
    const int steps = static_cast<int>(0.07 / each);

    for (int k = 0; k < steps; ++k)
        rope.step();

    // the rope as a whole: its middle of mass
    double middle = 0.0;

    for (const Vec3 &p : rope.points())
        middle += p.y / rope.points().size();

    const double fallen = -middle * radius;
    const double t = steps * each;

    std::printf("free fall   %4.1f mm rope: %.0f ms, fell %.0f mm, g = %.2f m/s2 (9.81)\n",
                material.diameterMm, t * 1000.0, fallen * 1000.0, 2.0 * fallen / (t * t));
}

// the droop of an overhang of `length` radii (degrees, chord of the tip)
double droop(RopeMaterial material, double length)
{
    // the elastic rope: no set, no curl left in it
    material.setRate = 0.0;
    material.curlRadiusMm = 1.0e9;

    const int held = 40;
    const int over = static_cast<int>(length / 0.5);
    const int n = held + over + 1;

    QVector<bool> pinned(n, false);

    for (int i = 0; i < held; ++i)
        pinned[i] = true;

    RopeSimulation rope(straight(n, 0.0), pinned, false, material, NoGround);

    for (int k = 0; k < 40000; ++k)
    {
        rope.step();

        if (rope.atRest())
            break;
    }

    const Vec3 edge = rope.points().at(held - 1);
    const Vec3 tip = rope.points().last();

    return std::atan2(edge.y - tip.y, tip.x - edge.x) * 180.0 / Pi;
}

void cantilever(const RopeMaterial &material)
{
    const double radius = 0.5 * material.diameterMm;

    // the overhang that droops 41.5 degrees, by bisection
    double low = 2.0;
    double high = 3.0 * material.bendingLengthMm / radius;

    for (int k = 0; k < 7; ++k)
    {
        const double middle = 0.5 * (low + high);

        (droop(material, middle) < 41.5 ? low : high) = middle;
    }

    const double measured = 0.25 * (low + high) * radius;

    std::printf("cantilever  %4.1f mm rope: bending length %.1f mm (%.1f)\n",
                material.diameterMm, measured, material.bendingLengthMm);
}

void hanging(const RopeMaterial &material)
{
    const int n = 400;

    QVector<Vec3> x(n);

    for (int i = 0; i < n; ++i)
        x[i] = Vec3(0.0, -i * 0.5, 0.0);

    QVector<bool> pinned(n, false);

    pinned[0] = true;

    RopeSimulation rope(x, pinned, false, material, NoGround);

    double before = 0.0;

    for (int i = 1; i < n; ++i)
        before += (x.at(i) - x.at(i - 1)).length();

    for (int k = 0; k < 6000; ++k)
        rope.step();

    double after = 0.0;

    for (int i = 1; i < n; ++i)
        after += (rope.points().at(i) - rope.points().at(i - 1)).length();

    std::printf("hanging     %4.1f mm rope, %.2f m: stretched %.3f %% (a real rope well under 1 %%)\n",
                material.diameterMm, before * 0.5 * material.diameterMm * 0.001,
                (after / before - 1.0) * 100.0);
}

// does a rope on a table tilted by `degrees` slide?
bool slides(const RopeMaterial &material, double degrees)
{
    const double slope = std::tan(degrees * Pi / 180.0);

    const Ground tilted = [slope](double x, double, double &sx, double &sz)
    {
        sx = slope;
        sz = 0.0;

        return x * slope;
    };

    const int n = 80;

    QVector<Vec3> x(n);

    const double along = std::cos(degrees * Pi / 180.0);
    const double rise = std::sin(degrees * Pi / 180.0);

    for (int i = 0; i < n; ++i)
    {
        x[i] = Vec3(i * 0.5 * along, i * 0.5 * rise, 0.0);
        x[i].y += 1.0 / along;
    }

    RopeSimulation rope(x, QVector<bool>(n, false), false, material, tilted);

    const double start = rope.points().at(n / 2).x;

    for (int k = 0; k < 4000; ++k)
        rope.step();

    return std::abs(rope.points().at(n / 2).x - start) > 2.0;
}

void slope(const RopeMaterial &material)
{
    double low = 1.0;
    double high = 70.0;

    for (int k = 0; k < 8; ++k)
    {
        const double middle = 0.5 * (low + high);

        (slides(material, middle) ? high : low) = middle;
    }

    std::printf("slope       %4.1f mm rope: slips at %.1f deg (atan %.2f = %.1f)\n",
                material.diameterMm, 0.5 * (low + high), material.frictionSurface,
                std::atan(material.frictionSurface) * 180.0 / Pi);
}

// a rope bent into a ring on the table and let go: how round it stays
void permanentSet(const RopeMaterial &material)
{
    // bent well past its yield
    const double bend = 0.5 * material.setRadiusMm / (0.5 * material.diameterMm);
    const int n = static_cast<int>(2.0 * Pi * bend * 0.75 / 0.5);

    QVector<Vec3> x(n);

    for (int i = 0; i < n; ++i)
    {
        const double a = 0.75 * 2.0 * Pi * i / n;

        x[i] = Vec3(bend * std::cos(a), 1.0, bend * std::sin(a));
    }

    RopeSimulation rope(x, QVector<bool>(n, false), false, material);

    // as if it had been tied this way
    rope.takeSet(material.tiedSet);

    const auto ends = [&rope]()
    {
        return (rope.points().first() - rope.points().last()).length();
    };

    const double before = ends();

    for (int k = 0; k < 20000; ++k)
    {
        rope.step();

        if (rope.atRest())
            break;
    }

    const double straightened = (ends() - before) / (0.75 * 2.0 * Pi * bend - before);

    std::printf("set         %4.1f mm rope: bent and let go, it straightened %.0f %% of the way\n",
                material.diameterMm, straightened * 100.0);
}

// a knot falling onto the table, step by step: how fast, how deep, how long
void trace(const QString &name, const RopeMaterial &material)
{
    const KnotTable &table = KnotTable::instance();
    const QVector<Vec3> curve = table.curve(table.indexOf(name));

    CurveData data;
    data.points = curve;

    const double radius = safeTubeRadius(data, 0.16);

    QVector<Vec3> smooth = refineCurve(curve, 4, true);

    double length = 0.0;

    for (int i = 0; i < smooth.size(); ++i)
        length += (smooth.at((i + 1) % smooth.size()) - smooth.at(i)).length();

    const int n = static_cast<int>(length / (0.5 * radius));
    const QVector<Vec3> start = resampleClosedCurve(smooth, n, true);

    QVector<QVector3D> flat;

    for (const Vec3 &p : start)
        flat.push_back(QVector3D(float(p.x), float(p.y), float(p.z)));

    const QMatrix4x4 pose = flatPose(flat);

    double lowest = 1.0e30;

    for (const QVector3D &p : flat)
        lowest = std::min(lowest, double(pose.map(p).y()));

    QVector<Vec3> x;

    for (const QVector3D &p : flat)
    {
        const QVector3D q = pose.map(p);

        x.push_back(Vec3(q.x(), q.y() - lowest + radius, q.z()) / radius);
    }

    RopeSimulation rope(x, QVector<bool>(n, false), true, material);

    QElapsedTimer clock;
    clock.start();

    const auto stretch = [&rope]()
    {
        const QVector<Vec3> &p = rope.points();

        double worst = 0.0;

        for (int i = 0; i < p.size(); ++i)
            worst = std::max(worst, std::abs((p.at((i + 1) % p.size()) - p.at(i)).length() - 0.5));

        return worst / 0.5 * 100.0;
    };

    const auto closest = [&rope]()
    {
        const QVector<Vec3> &p = rope.points();
        const int m = p.size();

        double best = 1.0e30;

        for (int i = 0; i < m; ++i)
        {
            for (int j = i + 10; j < m; ++j)
            {
                if (std::min(j - i, m - (j - i)) > 10)
                    best = std::min(best, (p.at(i) - p.at(j)).length());
            }
        }

        return best;
    };

    const AlexanderPolynomial before = computeAlexanderPolynomial(rope.points());

    for (int k = 0; k <= 4000; ++k)
    {
        if (k < 40 || k % 200 == 0)
        {
            std::printf("step %5d  %6.2f s  fastest %8.3f  worst stretch %6.2f %%  closest %.2f r\n",
                        k, k * rope.secondsPerStep(), rope.fastest(), stretch(), closest());
            std::fflush(stdout);
        }

        rope.step();

        if (rope.atRest())
        {
            std::printf("at rest after %d steps, %.2f s\n", k, k * rope.secondsPerStep());
            break;
        }
    }

    const AlexanderPolynomial after = computeAlexanderPolynomial(rope.points());

    std::printf("%s, %.3f ms a step\n", before.text == after.text ? "same knot" : "KNOT CHANGED",
                clock.nsecsElapsed() / 1.0e6 / 4000.0);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    if (argc >= 3 && QString(argv[1]) == QStringLiteral("trace"))
    {
        trace(QString(argv[2]), argc >= 4 ? cotton() : hemp());
        return 0;
    }

    for (const RopeMaterial &material : {hemp(), cotton()})
    {
        freeFall(material);
        cantilever(material);
        hanging(material);
        slope(material);
        permanentSet(material);

        std::printf("\n");
        std::fflush(stdout);
    }

    return 0;
}
