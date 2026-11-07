// Attacks on the rope that tools/rope_test and tools/hand_test do not make.
// hand_test moves its mouse about a knot's width in 800 steps, under the hand's
// speed limit, so it never tests that limit. Nothing there drops a rope from a
// height either.
//
//   rope_stress fast  [count] [first] [moves] [m/s]   mouse faster than the hand
//   rope_stress whip  [count] [first]                 the hand shaken back and forth
//   rope_stress drop  [count] [first]                 tilted knots dropped from a height
//   rope_stress edge  [count] [first] [moves]         dragged hard at the table's edges
//   rope_stress thin  [count] [first]                 thin tubes, which make long ropes
//
// A closed rope must stay the same knot, whatever is done to it. Also printed:
// the closest approach of distant parts, and how far the rope ever stretched.

#include "knotmath.h"
#include "knottable.h"
#include "ropephysics.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

using namespace KnotMath;
using namespace Photo;

namespace
{

constexpr double Pi = 3.14159265358979323846;

struct Worst
{
    double closest = 1.0e30;
    double stretch = 0.0;
    double stretchLong = 0.0;
    double fastest = 0.0;
    int changed = 0;
    int checked = 0;
};

double closestApproach(const QVector<Vec3> &x)
{
    const int n = x.size();
    const int gap = 9;

    double best = 1.0e30;

    for (int i = 0; i < n; ++i)
    {
        for (int j = i + gap; j < n; ++j)
        {
            if (n - (j - i) <= gap)
                continue;

            best = std::min(best, (x.at(i) - x.at(j)).lengthSquared());
        }
    }

    return std::sqrt(best);
}

double stretchOf(const QVector<Vec3> &x, const QVector<double> &rest)
{
    const int n = x.size();

    double worst = 0.0;

    for (int i = 0; i < n; ++i)
        worst = std::max(worst, std::abs((x.at((i + 1) % n) - x.at(i)).length() / rest.at(i) - 1.0));

    return 100.0 * worst;
}

// the same over 5 radii of rope (10 pieces): what the eye can see
double stretchLongOf(const QVector<Vec3> &x, const QVector<double> &rest)
{
    const int n = x.size();
    const int run = 10;

    double worst = 0.0;

    for (int i = 0; i < n; ++i)
    {
        double now = 0.0;
        double then = 0.0;

        for (int k = 0; k < run; ++k)
        {
            now += (x.at((i + k + 1) % n) - x.at((i + k) % n)).length();
            then += rest.at((i + k) % n);
        }

        worst = std::max(worst, std::abs(now / then - 1.0));
    }

    return 100.0 * worst;
}

QVector<double> spacingOf(const QVector<Vec3> &x)
{
    const int n = x.size();

    QVector<double> rest(n);

    for (int i = 0; i < n; ++i)
        rest[i] = (x.at((i + 1) % n) - x.at(i)).length();

    return rest;
}

// a knot lying on the table, in radii
QVector<Vec3> laid(const QVector<Vec3> &curve, const RopeMaterial &material)
{
    CurveData data;
    data.points = curve;
    data.closed = true;

    const double radius = safeTubeRadius(data, 0.16);

    const RestingRope rest = layOnTable(curve, true, radius, material);

    QVector<Vec3> x;

    for (const Vec3 &p : rest.points)
    {
        const QVector3D q = rest.pose.map(QVector3D(float(p.x), float(p.y), float(p.z)));

        x.push_back(Vec3(q.x(), q.y(), q.z()) / radius);
    }

    return x;
}

struct Knot
{
    AlexanderPolynomial alexander;
    JonesPolynomial jones;
};

Knot knotOf(const QVector<Vec3> &x)
{
    return Knot{computeAlexanderPolynomial(x), computeJonesPolynomial(x)};
}

bool sameKnot(const Knot &a, const Knot &b)
{
    return a.alexander.valid && b.alexander.valid && a.alexander.text == b.alexander.text &&
           (!a.jones.valid || !b.jones.valid || a.jones.coefficients == b.jones.coefficients);
}

void box(const QVector<Vec3> &x, double &minX, double &maxX, double &minZ, double &maxZ)
{
    minX = minZ = 1.0e30;
    maxX = maxZ = -1.0e30;

    for (const Vec3 &p : x)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minZ = std::min(minZ, p.z);
        maxZ = std::max(maxZ, p.z);
    }
}

void watch(Worst &worst, const RopeSimulation &rope, const QVector<double> &rest)
{
    worst.closest = std::min(worst.closest, closestApproach(rope.points()));
    worst.stretch = std::max(worst.stretch, stretchOf(rope.points(), rest));
    worst.stretchLong = std::max(worst.stretchLong, stretchLongOf(rope.points(), rest));
}

void settle(Worst &worst, RopeSimulation &rope, const QVector<double> &rest)
{
    for (int k = 0; k < 4000 && !rope.atRest(); ++k)
    {
        rope.step();

        if (k % 25 == 0)
            watch(worst, rope, rest);
    }

    watch(worst, rope, rest);
}

// the mouse, one place per frame at `metres` m/s, between random places over
// the knot's area, or (`shake`) back and forth along one line
void moveFast(
    RopeSimulation &rope,
    const RopeMaterial &material,
    int moves,
    double metres,
    bool shake,
    std::mt19937 &random,
    const Knot &start,
    Worst &worst)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    const QVector<double> rest = spacingOf(rope.points());

    double minX, maxX, minZ, maxZ;

    box(rope.points(), minX, maxX, minZ, maxZ);

    const double radiusMm = 0.5 * material.diameterMm;
    const double each = rope.secondsPerStep();
    const int perFrame = std::max(1, static_cast<int>(std::lround(1.0 / 60.0 / each)));
    const double perFrameMove = metres * 1000.0 / 60.0 / radiusMm;

    for (int m = 0; m < moves; ++m)
    {
        const int n = rope.points().size();
        const int index = std::min(static_cast<int>(unit(random) * n), n - 1);
        const bool under = m % 2 == 1;
        const double level = under ? 1.0 : 6.5;

        Vec3 hand = rope.points().at(index);

        hand.y = level;

        rope.grab(index, hand, under);

        // up first, as long as a hand takes
        for (int k = 0; k < 30 * perFrame; ++k)
            rope.step();

        const Vec3 line(unit(random) - 0.5, 0.0, unit(random) - 0.5);

        for (int leg = 0; leg < (shake ? 14 : 5); ++leg)
        {
            Vec3 goal(minX + (maxX - minX) * unit(random), level, minZ + (maxZ - minZ) * unit(random));

            if (shake)
                goal = hand + line.normalized() * (leg % 2 == 0 ? 30.0 : -30.0);

            const int frames = std::max(1, static_cast<int>((goal - hand).length() / perFrameMove));
            const Vec3 from = hand;

            for (int frame = 0; frame < frames; ++frame)
            {
                rope.moveGrab(from + (goal - from) * (static_cast<double>(frame + 1) / frames));

                for (int k = 0; k < perFrame; ++k)
                {
                    rope.step();

                    worst.fastest = std::max(worst.fastest, rope.fastest());
                }

                watch(worst, rope, rest);
            }

            hand = goal;
        }

        rope.release();

        settle(worst, rope, rest);

        ++worst.checked;

        if (!sameKnot(start, knotOf(rope.points())))
            ++worst.changed;
    }
}

// the rope's reach beyond the table's edge (radii, the rope's own width
// counted; negative: that far inside), for the table `half` either way
double overEdge(const QVector<Vec3> &x, const Vec3 &middle, double halfX, double halfZ)
{
    double worst = -1.0e30;

    for (const Vec3 &p : x)
    {
        worst = std::max(worst, std::abs(p.x - middle.x) + 1.0 - halfX);
        worst = std::max(worst, std::abs(p.z - middle.z) + 1.0 - halfZ);
    }

    return worst;
}

/*
 * The rope with the rail MainWindow::startPhotoRope gives it. Every move goes for
 * a far place, in every direction, at a mouse speed well over the hand's, stays
 * there and runs along the edge.
 */
double dragAtEdges(
    RopeSimulation &rope,
    const RopeMaterial &material,
    const Vec3 &middle,
    int moves,
    std::mt19937 &random)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    const double halfX = 1800.0 / material.diameterMm;
    const double halfZ = 900.0 / material.diameterMm;

    rope.stayWithin(middle.x - halfX + 1.5, middle.x + halfX - 1.5, middle.z - halfZ + 1.5, middle.z + halfZ - 1.5);

    const double each = rope.secondsPerStep();
    const int perFrame = std::max(1, static_cast<int>(std::lround(1.0 / 60.0 / each)));
    const double perFrameMove = 2000.0 / 60.0 / (0.5 * material.diameterMm);

    double worst = -1.0e30;

    for (int m = 0; m < moves; ++m)
    {
        const int n = rope.points().size();
        const int index = std::min(static_cast<int>(unit(random) * n), n - 1);
        const bool under = m % 3 == 2;
        const double level = under ? 1.0 : 6.5;

        const Vec3 start = rope.points().at(index);

        Vec3 mouse(start.x, level, start.z);

        rope.grab(index, mouse, under);

        for (int k = 0; k < 20 * perFrame; ++k)
            rope.step();

        const double angle = 2.0 * Pi * unit(random);
        const Vec3 far(middle.x + 500.0 * std::cos(angle), level, middle.z + 500.0 * std::sin(angle));

        const Vec3 along(-std::sin(angle), 0.0, std::cos(angle));

        for (int leg = 0; leg < 3; ++leg)
        {
            const Vec3 goal = leg == 0 ? far : far + along * (leg == 1 ? 250.0 : -250.0);
            const Vec3 from = mouse;
            const int frames = std::max(1, static_cast<int>((goal - from).length() / perFrameMove));

            for (int frame = 0; frame < frames; ++frame)
            {
                mouse = from + (goal - from) * (static_cast<double>(frame + 1) / frames);

                rope.moveGrab(mouse);

                for (int k = 0; k < perFrame; ++k)
                    rope.step();

                if (frame % 4 == 0)
                    worst = std::max(worst, overEdge(rope.points(), middle, halfX, halfZ));
            }
        }

        rope.release();

        for (int k = 0; k < 4000 && !rope.atRest(); ++k)
            rope.step();

        worst = std::max(worst, overEdge(rope.points(), middle, halfX, halfZ));
    }

    return worst;
}

// turned about z and lifted: the low side `low`, the high side `high` radii up
QVector<Vec3> tilted(const QVector<Vec3> &x, double low, double high)
{
    double minX, maxX, minZ, maxZ;

    box(x, minX, maxX, minZ, maxZ);

    const double middle = 0.5 * (minX + maxX);
    const double half = std::max(0.5 * (maxX - minX), 1.0);
    const double angle = std::atan2(0.5 * (high - low), half);
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    QVector<Vec3> out;

    for (const Vec3 &p : x)
    {
        const double u = p.x - middle;

        out.push_back(Vec3(middle + c * u - s * (p.y - 1.0), s * u + c * (p.y - 1.0) + 0.5 * (low + high), p.z));
    }

    return out;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    setvbuf(stdout, nullptr, _IONBF, 0);

    const KnotTable &table = KnotTable::instance();

    if (!table.isLoaded())
    {
        std::printf("no knot table: %s\n", qPrintable(table.error()));
        return 1;
    }

    const QString what = argc >= 2 ? QString::fromLatin1(argv[1]) : QStringLiteral("fast");

    /*
     * The Tube thickness slider goes down to 0.02. The rope keeps its real
     * width, so a thin tube is a long rope: a plain trefoil at 0.05 has over
     * a thousand points. These must come to rest like any other (they once
     * boiled: the rod's step was limited point by point).
     */
    if (what == QLatin1String("thin"))
    {
        const int many = argc >= 3 ? QString(argv[2]).toInt() : 12;
        const int from = argc >= 4 ? QString(argv[3]).toInt() : 0;

        const RopeMaterial ropes[2] = {sceneInfo(Scene::LoungeTable).rope, sceneInfo(Scene::BallroomFloor).rope};

        int cases = 0;
        int boiled = 0;
        int differ = 0;
        double fastest = 0.0;

        for (int index = from; index < std::min(from + many, table.size()); ++index)
        {
            const QVector<Vec3> curve = table.curve(index);

            const AlexanderPolynomial wanted = computeAlexanderPolynomial(curve);

            for (const double tube : {0.05, 0.03, 0.02})
            {
                for (int k = 0; k < 2; ++k)
                {
                    CurveData data;
                    data.points = curve;
                    data.closed = true;

                    const double radius = safeTubeRadius(data, tube);

                    const RestingRope rope = layOnTable(curve, true, radius, ropes[k]);

                    const AlexanderPolynomial now = computeAlexanderPolynomial(rope.points);

                    const bool same = wanted.valid && now.valid && wanted.text == now.text;
                    const bool still = rope.finalSpeed < 50.0;

                    ++cases;
                    boiled += still ? 0 : 1;
                    differ += same ? 0 : 1;
                    fastest = std::max(fastest, rope.finalSpeed);

                    if (!same || !still)
                    {
                        std::printf("%-6s tube %.2f %s %4d pts: ended at %.1f mm/s, %s\n", qPrintable(table.at(index).name),
                                    tube, k == 0 ? "hemp" : "lace", static_cast<int>(rope.points.size()), rope.finalSpeed,
                                    same ? "same knot" : "KNOT CHANGED");
                    }
                }
            }
        }

        std::printf("\nthin: %d ropes, %d boiling, %d knots changed, fastest at the end %.1f mm/s\n", cases, boiled, differ, fastest);

        return boiled == 0 && differ == 0 ? 0 : 1;
    }
    const int count = argc >= 3 ? QString(argv[2]).toInt() : 12;
    const int first = argc >= 4 ? QString(argv[3]).toInt() : 0;
    const int moves = argc >= 5 ? QString(argv[4]).toInt() : 4;
    const double metres = argc >= 6 ? QString(argv[5]).toDouble() : 3.0;

    const RopeMaterial materials[2] = {sceneInfo(Scene::LoungeTable).rope, sceneInfo(Scene::BallroomFloor).rope};

    int changed = 0;
    int checked = 0;
    double closest = 1.0e30;
    double stretch = 0.0;
    double stretchLong = 0.0;
    double worstOver = -1.0e30;

    std::mt19937 random(2026);

    for (int index = first; index < std::min(first + count, table.size()); ++index)
    {
        const QVector<Vec3> curve = table.curve(index);

        for (int k = 0; k < 2; ++k)
        {
            const RopeMaterial &material = materials[k];
            const QVector<Vec3> x = laid(curve, material);
            const Knot start = knotOf(x);

            if (what == QLatin1String("thin"))
            {
                continue;
            }

            if (what == QLatin1String("edge"))
            {
                // a rope that lay well inside the table to begin with
                double minX, maxX, minZ, maxZ;

                box(x, minX, maxX, minZ, maxZ);

                const Vec3 middle(0.5 * (minX + maxX), 0.0, 0.5 * (minZ + maxZ));

                RopeSimulation rope(x, QVector<bool>(), true, material);

                rope.takeSet(material.tiedSet);

                const double closedOver = dragAtEdges(rope, material, middle, moves, random);

                const bool same = sameKnot(start, knotOf(rope.points()));

                // with two ends: the longest thing a rope can be
                QVector<Vec3> open;

                const int n = rope.points().size();

                for (int i = 0; i < n - 3; ++i)
                    open.push_back(rope.points().at((i + 2) % n));

                RopeSimulation loose(open, QVector<bool>(), false, material);

                const double openOver = dragAtEdges(loose, material, middle, moves, random);

                ++checked;
                changed += same ? 0 : 1;
                worstOver = std::max({worstOver, closedOver, openOver});

                std::printf("%-6s %s %4d pts  closed reach %7.1f r  cut %7.1f r (0 is the edge)  %s\n",
                            qPrintable(table.at(index).name), k == 0 ? "hemp" : "lace", n, closedOver, openOver,
                            same ? "same knot" : "KNOT CHANGED");

                continue;
            }

            if (what == QLatin1String("drop"))
            {
                for (const double height : {30.0, 60.0, 120.0, 200.0, 400.0})
                {
                    for (const double lean : {0.0, 0.5, 1.0})
                    {
                        const QVector<Vec3> air = tilted(x, height * (1.0 - lean) + 12.0, height + 12.0);

                        RopeSimulation rope(air, QVector<bool>(), true, material);

                        rope.takeSet(material.tiedSet);

                        const QVector<double> rest = spacingOf(air);

                        Worst worst;

                        settle(worst, rope, rest);

                        for (int more = 0; more < 8000 && !rope.atRest(); ++more)
                            rope.step();

                        const bool same = sameKnot(start, knotOf(rope.points()));

                        ++checked;
                        changed += same ? 0 : 1;
                        closest = std::min(closest, worst.closest);
                        stretch = std::max(stretch, worst.stretch);
                        stretchLong = std::max(stretchLong, worst.stretchLong);

                        if (!same || worst.closest < 1.2)
                        {
                            std::printf("%-6s %s from %3.0f r lean %.1f: closest %.2f r  stretch %.1f %%  %s\n",
                                        qPrintable(table.at(index).name), k == 0 ? "hemp" : "lace", height, lean,
                                        worst.closest, worst.stretch, same ? "same knot" : "KNOT CHANGED");
                        }
                    }
                }

                continue;
            }

            QElapsedTimer clock;
            clock.start();

            RopeSimulation rope(x, QVector<bool>(), true, material);

            rope.takeSet(material.tiedSet);

            Worst worst;

            moveFast(rope, material, moves, metres, what == QLatin1String("whip"), random, start, worst);

            checked += worst.checked;
            changed += worst.changed;
            closest = std::min(closest, worst.closest);
            stretch = std::max(stretch, worst.stretch);
            stretchLong = std::max(stretchLong, worst.stretchLong);

            std::printf("%-6s %s %4d pts %6lld ms  closest %.2f r  stretch %6.1f %%, over 5 r %5.1f %%  fastest %.2f r/step  %s\n",
                        qPrintable(table.at(index).name), k == 0 ? "hemp" : "lace", static_cast<int>(x.size()),
                        clock.elapsed(), worst.closest, worst.stretch, worst.stretchLong, worst.fastest * 0.08,
                        worst.changed == 0 ? "same knot" : qPrintable(QStringLiteral("KNOT CHANGED %1 of %2").arg(worst.changed).arg(worst.checked)));
        }
    }

    if (what == QLatin1String("edge"))
    {
        std::printf("\nedge: %d ropes, %d knots changed, furthest any rope reached from the table's edge: %.1f r\n", checked,
                    changed, worstOver);
    }
    else
    {
        std::printf("\n%s: %d cases, %d knots changed, closest %.2f r, worst stretch %.1f %%, over 5 r %.1f %%\n", qPrintable(what),
                    checked, changed, closest, stretch, stretchLong);
    }

    return changed == 0 ? 0 : 1;
}
