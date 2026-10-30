// Headless check of the rope in the hand (Photo::RopeSimulation): knots
// from the table are laid on the table as hemp rope, then picked up at
// random points and carried about, lifted over the rest or pushed under
// it (as with the mouse in Photo mode). The rope must never pass through
// itself: a closed rope stays the same knot (Alexander and Jones
// polynomials) and no two distant parts ever come closer than the rope
// allows. Then the rope is cut where MainWindow::cutPhotoRope cuts it:
// its ends joined over the top must give the same knot again; moved about
// with its ends free it may become another knot (counted).
//
//   hand_test [count] [first] [moves]

#include "knotmath.h"
#include "knottable.h"
#include "ropephysics.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cmath>
#include <cstdio>
#include <map>
#include <random>

using namespace KnotMath;

namespace
{

// closest approach (radii) of parts of the rope more than 4 radii apart
// along it
double closestApproach(const QVector<Vec3> &x, bool closed)
{
    const int n = x.size();
    const int gap = 9;

    double best = 1.0e30;

    for (int i = 0; i < n; ++i)
    {
        for (int j = i + gap; j < n; ++j)
        {
            if (closed && n - (j - i) <= gap)
                continue;

            best = std::min(best, (x.at(i) - x.at(j)).lengthSquared());
        }
    }

    return std::sqrt(best);
}

// as MainWindow::cutPhotoRope: where the rope lies most on its own
QVector<Vec3> cutOpen(const QVector<Vec3> &points)
{
    const int n = points.size();
    const int apart = 16;

    int cut = 0;
    double loneliest = -1.0;

    for (int i = 0; i < n; ++i)
    {
        double nearest = 1.0e30;

        for (int j = 0; j < n; ++j)
        {
            const int gap = std::abs(i - j);

            if (std::min(gap, n - gap) <= apart)
                continue;

            const double dx = points.at(i).x - points.at(j).x;
            const double dz = points.at(i).z - points.at(j).z;

            nearest = std::min(nearest, dx * dx + dz * dz);
        }

        if (nearest > loneliest)
        {
            loneliest = nearest;
            cut = i;
        }
    }

    QVector<Vec3> open;

    for (int k = 0; k < n - 3; ++k)
        open.push_back(points.at((cut + 2 + k) % n));

    return open;
}

// as MainWindow::photoRopeCurve: the ends joined by an arch over the top
QVector<Vec3> joined(QVector<Vec3> table)
{
    double top = 0.0;

    for (const Vec3 &p : table)
        top = std::max(top, p.y);

    const double bridge = top + 4.0;

    const Vec3 last = table.last();
    const Vec3 first = table.first();

    const Vec3 corners[4] = {last, Vec3(last.x, bridge, last.z), Vec3(first.x, bridge, first.z), first};

    for (int side = 0; side < 3; ++side)
    {
        const Vec3 a = corners[side];
        const Vec3 b = corners[side + 1];

        const int pieces = std::max(2, static_cast<int>(std::ceil((b - a).length() / 0.5)));

        for (int k = 1; k < pieces; ++k)
            table.push_back(a + (b - a) * (static_cast<double>(k) / pieces));

        if (side < 2)
            table.push_back(b);
    }

    return table;
}

struct Moves
{
    double closest = 1.0e30;
    int steps = 0;

    // closed: its Alexander polynomial, and the first move that changed it
    QString reference;
    int changedAt = -1;
    bool changedUnder = false;
};

// `moves` times: grab a random point, carry it along a path of two random
// places over the knot's area, let go and let it settle
Moves moveAbout(Photo::RopeSimulation &rope, int moves, std::mt19937 &random, const QString &reference = QString())
{
    Moves result;

    result.reference = reference;

    std::uniform_real_distribution<double> unit(0.0, 1.0);

    double minX = 1.0e30, maxX = -1.0e30, minZ = 1.0e30, maxZ = -1.0e30;

    for (const Vec3 &p : rope.points())
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minZ = std::min(minZ, p.z);
        maxZ = std::max(maxZ, p.z);
    }

    auto check = [&]()
    {
        result.closest = std::min(result.closest, closestApproach(rope.points(), rope.closed()));
    };

    for (int m = 0; m < moves; ++m)
    {
        const int n = rope.points().size();
        const int index = std::min(static_cast<int>(unit(random) * n), n - 1);
        const bool under = unit(random) < 0.5;
        const double level = under ? 1.0 : 6.5;

        Vec3 hand = rope.points().at(index);
        hand.y = level;

        rope.hold(index, hand, under);

        for (int leg = 0; leg < 2; ++leg)
        {
            const Vec3 goal(
                minX + (maxX - minX) * unit(random),
                level,
                minZ + (maxZ - minZ) * unit(random));

            // the mouse moves there over a second or so (frames of 20 steps)
            for (int frame = 0; frame < 40; ++frame)
            {
                const double t = (frame + 1) / 40.0;

                rope.moveHand(hand + (goal - hand) * t);

                for (int k = 0; k < 20; ++k)
                    rope.step();

                result.steps += 20;

                if (frame % 4 == 0)
                    check();
            }

            hand = goal;
        }

        rope.letGo();

        for (int k = 0; k < 6000 && !rope.atRest(); ++k)
        {
            rope.step();

            ++result.steps;

            if (k % 200 == 0)
                check();
        }

        check();

        // a closed rope must still be the same knot after every move
        if (rope.closed() && !result.reference.isEmpty())
        {
            const AlexanderPolynomial now = computeAlexanderPolynomial(rope.points());

            if (now.valid && now.text != result.reference && result.changedAt < 0)
            {
                result.changedAt = m;
                result.changedUnder = under;
            }
        }
    }

    return result;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const KnotTable &table = KnotTable::instance();

    if (!table.isLoaded())
    {
        std::printf("no knot table: %s\n", qPrintable(table.error()));
        return 1;
    }

    const int count = argc >= 2 ? QString(argv[1]).toInt() : 12;
    const int first = argc >= 3 ? QString(argv[2]).toInt() : 0;
    const int moves = argc >= 4 ? QString(argv[3]).toInt() : 4;

    const Photo::RopeMaterial material = Photo::sceneInfo(Photo::Scene::LoungeTable).rope;

    std::mt19937 random(12345);

    int failures = 0;
    double closestEver = 1.0e30;

    std::map<QString, int> tied;

    for (int k = first; k < std::min(first + count, table.size()); ++k)
    {
        const QVector<Vec3> curve = table.curve(k);

        CurveData data;
        data.points = curve;

        const double radius = safeTubeRadius(data, 0.16);

        const Photo::RestingRope rest = Photo::layOnTable(curve, true, radius, material);

        if (!rest.valid)
        {
            std::printf("%-8s could not be laid down\n", qPrintable(table.at(k).name));
            ++failures;
            continue;
        }

        // on the table, in radii
        QVector<Vec3> points;

        for (const Vec3 &p : rest.points)
        {
            const QVector3D q = rest.pose.map(QVector3D(float(p.x), float(p.y), float(p.z)));

            points.push_back(Vec3(q.x(), q.y(), q.z()) / radius);
        }

        const AlexanderPolynomial before = computeAlexanderPolynomial(points);
        const JonesPolynomial beforeJones = computeJonesPolynomial(points);

        QElapsedTimer clock;
        clock.start();

        // 1. the closed rope carried about
        Photo::RopeSimulation rope(points, {}, true, material);

        const Moves closedMoves = moveAbout(rope, moves, random, before.text);

        const AlexanderPolynomial after = computeAlexanderPolynomial(rope.points());
        const JonesPolynomial afterJones = computeJonesPolynomial(rope.points());

        const bool same =
            before.valid && after.valid && after.text == before.text &&
            (!beforeJones.valid || !afterJones.valid || beforeJones.coefficients == afterJones.coefficients);

        // 2. cut open where nothing lies over it: joined, the same knot
        const QVector<Vec3> open = cutOpen(rope.points());
        const AlexanderPolynomial cut = computeAlexanderPolynomial(joined(open));

        const bool cutSame = cut.valid && cut.text == after.text;

        // 3. with free ends, moved about again: perhaps another knot
        Photo::RopeSimulation loose(open, {}, false, material);

        const Moves openMoves = moveAbout(loose, moves, random);

        const QVector<Vec3> ending = joined(loose.points());
        const AlexanderPolynomial end = computeAlexanderPolynomial(ending);

        const QString name = end.valid ? identifyKnot(end, computeJonesPolynomial(ending)) : QStringLiteral("(no polynomial)");

        ++tied[name];

        const double closest = std::min(closedMoves.closest, openMoves.closest);

        closestEver = std::min(closestEver, closest);

        // the rope keeps two radii between its centre lines (a little less
        // for a moment while a contact is being pushed apart)
        const bool apart = closest > 1.5;

        if (!same || !cutSame || !apart)
            ++failures;

        std::printf(
            "%-8s %4d pts  %6lld ms  %6d steps  closest %.2f r  %s  cut %s  then: %s\n",
            qPrintable(table.at(k).name),
            static_cast<int>(points.size()),
            clock.elapsed(),
            closedMoves.steps + openMoves.steps,
            closest,
            same ? "same knot"
                 : qPrintable(QStringLiteral("CHANGED (move %1, %2)")
                                  .arg(closedMoves.changedAt)
                                  .arg(closedMoves.changedUnder ? "under" : "over")),
            cutSame ? "same" : "CHANGED",
            qPrintable(name));

        std::fflush(stdout);
    }

    std::printf("\n%d failures, closest approach %.2f r\nknots tied with free ends:\n", failures, closestEver);

    for (const auto &[name, number] : tied)
        std::printf("  %3d  %s\n", number, qPrintable(name));

    return failures == 0 ? 0 : 1;
}
