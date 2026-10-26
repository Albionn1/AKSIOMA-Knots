// Headless check of the Sneaker scene (sneaker.cpp): every knot of the
// table with up to 7 crossings (and the unknot) is tied in the sneaker's
// lace. The whole lace, a closed loop through the eyelets, must be the
// same knot (Alexander and Jones polynomials), and no part of it may pass
// through another or into the shoe.
//
//   sneaker_test [max crossings]   all knots up to that many crossings
//   sneaker_test name 3_1 [file]   one knot; the lace (mm) written to file

#include "knotmath.h"
#include "knottable.h"
#include "photoscene.h"
#include "sneaker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>

#include <cmath>
#include <cstdio>

using namespace KnotMath;

namespace
{

struct Result
{
    bool same = false;
    double closest = 0.0;   // closest approach of distant parts, one free (radii)
    double closestHeld = 0.0;   // ... of two parts of the held lacing
    double sunk = 0.0;      // deepest a free point lies in the shoe (radii)
    int knotPoints = 0;
};

int failures = 0;

void tie(const QString &name, const QVector<Vec3> &curve, const QString &file)
{
    CurveData data;
    data.points = curve;
    data.closed = true;

    const double radius = safeTubeRadius(data, 0.16);

    const Photo::RopeMaterial material = Photo::sceneInfo(Photo::Scene::Sneaker).rope;

    const AlexanderPolynomial alexander = computeAlexanderPolynomial(curve);
    const JonesPolynomial jones = computeJonesPolynomial(curve);

    QElapsedTimer clock;
    clock.start();

    const Photo::TiedLace tied = Photo::tieOnSneaker(curve, radius, material);
    const Photo::RestingRope &rope = tied.rope;

    const qint64 ms = clock.elapsed();

    if (!rope.valid)
    {
        std::printf("%-8s FAILED: %s\n", qPrintable(name), qPrintable(rope.error));
        ++failures;
        return;
    }

    // the lace in millimetres, in the shoe's frame
    const double mm = 1.0 / tied.unitsPerMm;
    const double r = 0.5 * material.diameterMm;

    QVector<Vec3> lace;

    for (const Vec3 &p : rope.points)
    {
        const QVector3D s = rope.pose.map(QVector3D(float(p.x), float(p.y), float(p.z)));

        lace.push_back(Vec3(s.x(), s.y(), s.z()) * mm);
    }

    const AlexanderPolynomial a = computeAlexanderPolynomial(rope.points);
    const JonesPolynomial j = computeJonesPolynomial(rope.points);

    Result result;

    // (the Jones polynomial is computed up to 18 crossings: with the
    // lacing it often has more, and is then left out)
    result.same =
        a.valid && alexander.valid && a.text == alexander.text &&
        (!jones.valid || !j.valid || j.text == jones.text);

    // distant parts of the lace (more than 4 radii apart along it)
    const int n = lace.size();

    double length = 0.0;

    for (int i = 0; i < n; ++i)
        length += (lace.at((i + 1) % n) - lace.at(i)).length();

    const int gap = static_cast<int>(4.0 * r / (length / n)) + 1;

    double closest = 1.0e30;
    double closestHeld = 1.0e30;
    int closestI = 0;
    int closestK = 0;

    for (int i = 0; i < n; ++i)
    {
        for (int k = i + gap; k < n; ++k)
        {
            if (n - (k - i) <= gap)
                continue;

            const double d = (lace.at(i) - lace.at(k)).length();

            // the held lacing never moves: it only has to look right
            if (tied.pinned.value(i) && tied.pinned.value(k))
            {
                closestHeld = std::min(closestHeld, d);
                continue;
            }

            if (d < closest)
            {
                closest = d;
                closestI = i;
                closestK = k;
            }
        }
    }

    result.closest = closest / r;
    result.closestHeld = closestHeld / r;

    // the knot's part must rest on the shoe, not in it
    const Photo::Sneaker &shoe = Photo::sneaker();

    double sunk = 0.0;

    for (int i = tied.knotFrom; i <= tied.knotTo && i >= 0; ++i)
    {
        const Vec3 &p = lace.at(i);

        sunk = std::max(sunk, shoe.heightAt(p.x, p.z) + r - p.y);
    }

    result.sunk = sunk / r;
    result.knotPoints = tied.knotTo - tied.knotFrom + 1;

    const bool ok = result.same && result.closest > 1.9 && result.closestHeld > 1.75 && result.sunk < 0.2;

    if (!ok)
        ++failures;

    std::printf(
        "%-8s %5d pts (knot %4d)  %6lld ms  rest %s (%.2f mm/s)  closest %.2f r (lacing %.2f r)  in shoe %.2f r  %s%s\n",
        qPrintable(name), n, result.knotPoints, ms, rope.settled ? "yes" : "NO ",
        rope.finalSpeed, result.closest, result.closestHeld, result.sunk,
        result.same ? "same knot" : "CHANGED", ok ? "" : "  <-- FAIL");

    if (result.closest < 1.9)
    {
        const Vec3 &p = lace.at(closestI);
        std::printf("         closest at points %d and %d (knot %d..%d), at (%.1f %.1f %.1f)\n",
                    closestI, closestK, tied.knotFrom, tied.knotTo, p.x, p.y, p.z);
    }

    if (!result.same)
    {
        std::printf("         jones %s | lace %s (%s)\n", qPrintable(jones.text),
                    qPrintable(j.text), qPrintable(j.error));

        std::printf("         knot %s | lace %s (%s)\n", qPrintable(alexander.text),
                    qPrintable(a.text), qPrintable(a.error));
    }

    std::fflush(stdout);

    if (!file.isEmpty())
    {
        QFile out(file);

        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream stream(&out);

            stream << tied.knotFrom << " " << tied.knotTo << "\n";

            for (const Vec3 &p : lace)
                stream << p.x << " " << p.y << " " << p.z << "\n";
        }
    }
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

    if (!Photo::sneaker().valid)
    {
        std::printf("no sneaker: %s\n", qPrintable(Photo::sneaker().error));
        return 1;
    }

    if (argc >= 3 && QString(argv[1]) == QStringLiteral("name"))
    {
        const int index = table.indexOf(QString(argv[2]));

        if (index < 0)
        {
            std::printf("unknown knot\n");
            return 1;
        }

        tie(table.at(index).name, table.curve(index), argc >= 4 ? QString(argv[3]) : QString());

        return failures == 0 ? 0 : 1;
    }

    const int most = argc >= 2 ? QString(argv[1]).toInt() : 7;

    int count = 0;

    for (int index = 0; index < table.size(); ++index)
    {
        if (table.at(index).crossings > most)
            continue;

        tie(table.at(index).name, table.curve(index), QString());
        ++count;
    }

    std::printf("\n%d knots, %d failures\n", count, failures);

    return failures == 0 ? 0 : 1;
}
