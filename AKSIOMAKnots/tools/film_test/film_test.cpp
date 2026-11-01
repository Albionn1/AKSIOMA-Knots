// Checks the Seifert sheet and its film for knots of the table, against what
// is known from elsewhere:
//
//   mesh    every edge in one or two triangles, the two running opposite ways
//           (an orientable surface), one boundary loop, and an Euler
//           characteristic of 1 - 2 g
//   wire    the film's boundary still has the knot's Alexander and Jones
//           polynomials
//   film    no two triangles pass through each other (an embedded surface)
//   matrix  linking numbers of the loops on the film with the loops lifted
//           off it: the Seifert matrix again, entry for entry
//
//   film_test [count [first]]     table knots from `first` on
//   film_test name <knot>         one knot, with details

#include "knotseifert.h"
#include "knottable.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

using namespace KnotMath;

namespace
{

int intersections(const QVector<Vec3> &x, const QVector<int> &triangles, double cell)
{
    return seifertSheetCrossings(x, triangles, cell).size();
}

struct Result
{
    int euler = 0;
    bool manifold = true;
    bool oriented = true;
    int crossed = 0;
    int wrongEntries = 0;
    bool wire = true;
    bool jones = true;
};

Result check(const SeifertSurface &surface, const QVector<Vec3> &curve)
{
    Result result;

    const SeifertSheet &sheet = surface.sheet;

    std::map<std::pair<int, int>, int> directed;

    for (int t = 0; t < sheet.triangles.size() / 3; ++t)
    {
        for (int k = 0; k < 3; ++k)
            ++directed[{sheet.triangles[3 * t + k], sheet.triangles[3 * t + (k + 1) % 3]}];
    }

    int edges = 0;

    for (const auto &entry : directed)
    {
        if (entry.second != 1)
            result.oriented = false;

        const int other = directed.count({entry.first.second, entry.first.first}) != 0
                              ? directed.at({entry.first.second, entry.first.first})
                              : 0;

        if (other > 1)
            result.manifold = false;

        if (other == 0 || entry.first.first < entry.first.second)
            ++edges;
    }

    result.euler = sheet.film.size() - edges + sheet.triangles.size() / 3;

    result.crossed = intersections(sheet.film, sheet.triangles, 0.12 * surface.extent);

    if (qEnvironmentVariableIsSet("FILM_STAGES"))
    {
        std::printf("  model %d, fair %d, film %d\n",
                    intersections(sheet.model, sheet.triangles, 0.12 * surface.extent),
                    intersections(sheet.fair, sheet.triangles, 0.12 * surface.extent),
                    result.crossed);
    }

    QVector<Vec3> wire;

    for (const int v : sheet.boundary)
        wire.push_back(sheet.film[v]);

    const AlexanderPolynomial before = computeAlexanderPolynomial(curve);
    const AlexanderPolynomial after = computeAlexanderPolynomial(wire);

    result.wire = !before.valid || (after.valid && before.text == after.text);

    const JonesPolynomial jonesBefore = computeJonesPolynomial(curve);
    const JonesPolynomial jonesAfter = computeJonesPolynomial(wire);

    result.jones = !jonesBefore.valid || !jonesAfter.valid || jonesBefore == jonesAfter;

    if (surface.matrixValid && qEnvironmentVariableIsSet("FILM_LIFTS"))
    {
        for (const double scale : {0.008, 0.003, 0.001, 0.0003})
        {
            for (const QVector<Vec3> *shape : {&sheet.model, &sheet.fair, &sheet.film})
            {
                const QVector<Vec3> n = seifertSheetNormals(*shape, sheet.triangles);

                int wrong = 0;

                const int size = surface.seifertMatrix.size();

                for (int i = 0; i < size; ++i)
                {
                    const QVector<Vec3> a = seifertSheetLoop(sheet, *shape, n, i, 0.0);

                    for (int j = 0; j < size; ++j)
                    {
                        const QVector<Vec3> b = seifertSheetLoop(sheet, *shape, n, j, scale * surface.extent);

                        if (std::lround(computeLinkingNumber(a, b, 0)) != surface.seifertMatrix[i][j])
                            ++wrong;
                    }
                }

                std::printf("  lift %.4f: %d wrong  ", scale, wrong);
            }

            std::printf("\n");
        }
    }

    if (surface.matrixValid)
    {
        const QVector<Vec3> normals = seifertSheetNormals(sheet.film, sheet.triangles);

        const int size = surface.seifertMatrix.size();

        const double lift = 0.008 * surface.extent;

        for (int i = 0; i < size && i < sheet.loopPlaces.size(); ++i)
        {
            const QVector<Vec3> onFilm = seifertSheetLoop(sheet, sheet.film, normals, i, 0.0);

            for (int j = 0; j < size && j < sheet.loopPlaces.size(); ++j)
            {
                const QVector<Vec3> lifted = seifertSheetLoop(sheet, sheet.film, normals, j, lift);

                const double lk = computeLinkingNumber(onFilm, lifted, 0);

                if (std::lround(lk) != surface.seifertMatrix[i][j] || std::abs(lk - std::round(lk)) > 0.2)
                    ++result.wrongEntries;
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

    // film_test presets: the knots of the list, as the program makes them
    if (argc >= 2 && QString(argv[1]) == QStringLiteral("presets"))
    {
        const std::pair<Preset, const char *> presets[] = {
            {Preset::Trefoil, "trefoil"},
            {Preset::FigureEight, "figure eight"},
            {Preset::Cinquefoil, "cinquefoil"},
            {Preset::Stevedore, "stevedore"}};

        for (const auto &entry : presets)
        {
            const CurveData data = buildPreset(entry.first, 160, 3, 2, QString());

            QString error;

            const QVector<KnotDiagram> diagrams = diagramsFromCurve(data.points, 2.0, error, 12);

            QString counts;

            for (const KnotDiagram &d : diagrams)
                counts += QStringLiteral("%1 ").arg(d.crossings.size());

            SeifertSurface surface = buildSeifertSurface(data.points, true);

            int depth = 0;

            for (const int d : surface.circleDepth)
                depth = std::max(depth, d);

            std::printf("%-14s diagrams: %s-> chosen %d crossings, %d circles, nested %d, matrix %d\n",
                        entry.second, qPrintable(counts), surface.crossings, surface.circles, depth, surface.matrixValid);
        }

        return 0;
    }

    int first = 0;
    int count = 80;
    bool details = false;

    if (argc >= 3 && QString(argv[1]) == QStringLiteral("name"))
    {
        first = table.indexOf(QString(argv[2]));
        count = 1;
        details = true;
    }
    else
    {
        if (argc >= 2)
            count = QString(argv[1]).toInt();

        if (argc >= 3)
            first = QString(argv[2]).toInt();
    }

    if (first < 0)
        return 1;

    int failures = 0;
    int skipped = 0;
    int done = 0;

    qint64 slowest = 0;
    qint64 total = 0;

    QVector<QString> bad;

    for (int k = first; k < std::min(first + count, table.size()); ++k)
    {
        const QVector<Vec3> curve = table.curve(k);

        QElapsedTimer clock;

        clock.start();

        SeifertSurface surface = buildSeifertSurface(curve, true);

        if (surface.valid)
            surface.sheet = buildSeifertSheet(surface);

        const qint64 took = clock.elapsed();

        if (!surface.valid || !surface.sheet.valid)
        {
            ++skipped;

            if (surface.valid)
            {
                bad.push_back(table.at(k).name + QStringLiteral(" no sheet"));
                ++failures;
            }

            continue;
        }

        slowest = std::max(slowest, took);
        total += took;

        const Result r = check(surface, curve);

        const bool euler = r.euler == 1 - 2 * surface.genus;

        const bool good = euler && r.manifold && r.oriented && r.wire && r.jones && r.crossed == 0 && r.wrongEntries == 0;

        if (details || !good)
        {
            std::printf("%-8s c %2d s %2d g %d points %5d tris %6d %5lld ms  chi %3d (%s) manifold %d oriented %d wire %d jones %d crossed %d wrong entries %d area %.2f -> %.2f\n",
                        qPrintable(table.at(k).name),
                        surface.crossings,
                        surface.circles,
                        surface.genus,
                        static_cast<int>(surface.sheet.film.size()),
                        static_cast<int>(surface.sheet.triangles.size() / 3),
                        took,
                        r.euler,
                        euler ? "ok" : "WRONG",
                        r.manifold,
                        r.oriented,
                        r.wire,
                        r.jones,
                        r.crossed,
                        r.wrongEntries,
                        surface.sheet.areaBefore,
                        surface.sheet.areaAfter);

            std::fflush(stdout);
        }

        if (!good)
            ++failures;

        ++done;
    }

    for (const QString &line : bad)
        std::printf("%s\n", qPrintable(line));

    std::printf("\n%d knots, %d failures, %d skipped, surface + film %lld ms on average, slowest %lld ms\n",
                done, failures, skipped, done > 0 ? total / done : 0, slowest);

    return failures == 0 ? 0 : 1;
}
