// Headless check of Photo mode's rope physics (ropephysics.cpp): knots from
// the table are dropped onto the table as hemp rope and as a shoelace; the
// rope at rest must be the same knot (Alexander and Jones polynomials of
// the centre line before and after), lie on the table and nowhere pass
// through itself.
//
//   rope_test [count] [first]     knots `first` .. `first + count - 1`
//   rope_test name 3_1            one knot, with the rest positions printed

#include "knotmath.h"
#include "knottable.h"
#include "ropephysics.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>

using namespace KnotMath;

namespace
{

struct Check
{
    bool same = false;
    double lowest = 0.0;
    double highest = 0.0;
    double closest = 0.0;
};

// closest approach of parts of the centre line more than 4 radii apart
// along it, in radii
double closestApproach(const QVector<Vec3> &points, double radius)
{
    const int n = points.size();

    double length = 0.0;

    for (int i = 0; i < n; ++i)
        length += (points.at((i + 1) % n) - points.at(i)).length();

    const int gap = static_cast<int>(4.0 * radius / (length / n)) + 1;

    double best = 1.0e30;

    for (int i = 0; i < n; ++i)
    {
        for (int j = i + gap; j < n; ++j)
        {
            if (n - (j - i) <= gap)
                continue;

            best = std::min(best, (points.at(i) - points.at(j)).length());
        }
    }

    return best / radius;
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

    int first = 0;
    int count = 36;
    int only = -1;

    if (argc >= 3 && QString(argv[1]) == QStringLiteral("name"))
    {
        only = table.indexOf(QString(argv[2]));
        first = only;
        count = 1;
    }
    else
    {
        if (argc >= 2)
            count = QString(argv[1]).toInt();

        if (argc >= 3)
            first = QString(argv[2]).toInt();
    }

    Photo::RopeMaterial materials[2] = {
        Photo::sceneInfo(Photo::Scene::LoungeTable).rope,
        Photo::sceneInfo(Photo::Scene::BallroomFloor).rope};

    int failures = 0;
    int unsettled = 0;
    double slowest = 0.0;

    for (int index = first; index < first + count && index < table.size(); ++index)
    {
        const QVector<Vec3> curve = table.curve(index);

        CurveData data;
        data.points = curve;
        data.closed = true;

        const double radius = safeTubeRadius(data, 0.16);

        const AlexanderPolynomial alexander = computeAlexanderPolynomial(curve);
        const JonesPolynomial jones = computeJonesPolynomial(curve);

        for (int k = 0; k < 2; ++k)
        {
            QElapsedTimer clock;
            clock.start();

            const Photo::RestingRope rope =
                Photo::layOnTable(curve, true, radius, materials[k]);

            const double ms = clock.elapsed();

            slowest = std::max(slowest, ms);

            if (!rope.valid)
            {
                std::printf("%-8s FAILED: %s\n", qPrintable(table.at(index).name), qPrintable(rope.error));
                ++failures;
                continue;
            }

            const AlexanderPolynomial a = computeAlexanderPolynomial(rope.points);
            const JonesPolynomial j = computeJonesPolynomial(rope.points);

            const bool same =
                a.valid && alexander.valid && a.text == alexander.text &&
                (!jones.valid || (j.valid && j.text == jones.text));

            // heights above the table, in radii
            double lowest = 1.0e30;
            double highest = -1.0e30;

            for (const Vec3 &p : rope.points)
            {
                const double y = rope.pose.map(QVector3D(float(p.x), float(p.y), float(p.z))).y() / radius;

                lowest = std::min(lowest, y);
                highest = std::max(highest, y);
            }

            const double closest = closestApproach(rope.points, radius);

            if (!same)
                ++failures;

            if (!rope.settled)
                ++unsettled;

            std::printf(
                "%-8s %-5s r %.3f  %4d pts  %5.0f ms  %5d steps  %.2f s  rest %s (%.2f mm/s)  "
                "height %.2f..%.2f r  closest %.2f r  %s\n",
                qPrintable(table.at(index).name),
                k == 0 ? "rope" : "lace",
                radius,
                static_cast<int>(rope.points.size()),
                ms,
                rope.steps,
                rope.seconds,
                rope.settled ? "yes" : "NO",
                rope.finalSpeed,
                lowest,
                highest,
                closest,
                same ? "same knot" : qPrintable(QStringLiteral("CHANGED: %1 / %2 -> %3 / %4")
                                                    .arg(alexander.text, jones.text, a.text, j.text)));

            if (only >= 0)
            {
                for (const Vec3 &p : rope.points)
                {
                    const QVector3D q = rope.pose.map(QVector3D(float(p.x), float(p.y), float(p.z)));

                    std::printf("  %.4f %.4f %.4f\n", q.x(), q.y(), q.z());
                }
            }

            std::fflush(stdout);
        }
    }

    std::printf("\n%d failures, %d not at rest at the time limit, slowest %.0f ms\n",
                failures, unsettled, slowest);

    return failures == 0 ? 0 : 1;
}
