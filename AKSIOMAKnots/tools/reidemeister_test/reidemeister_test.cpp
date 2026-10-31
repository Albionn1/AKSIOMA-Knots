// Headless check of the Reidemeister moves: random drawn diagrams, every
// move found is checked with the Alexander and Jones polynomials (after the
// move and half way through it), and random walks of moves.
#include "knotreidemeister.h"
#include "knotmath.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cmath>
#include <cstdio>
#include <random>

using namespace KnotMath;

static KnotDiagram randomDiagram(std::mt19937 &rng, int terms)
{
    std::normal_distribution<double> g(0.0, 1.0);
    std::uniform_int_distribution<int> coin(0, 1);

    double ax[8], bx[8], ay[8], by[8];

    for (int k = 1; k <= terms; ++k)
    {
        const double f = 1.0 / k;
        ax[k] = g(rng) * f; bx[k] = g(rng) * f;
        ay[k] = g(rng) * f; by[k] = g(rng) * f;
    }

    QVector<Vec3> stroke;

    for (int i = 0; i < 600; ++i)
    {
        const double t = 2.0 * M_PI * i / 600.0;
        double x = 0, y = 0;

        for (int k = 1; k <= terms; ++k)
        {
            x += ax[k] * std::cos(k * t) + bx[k] * std::sin(k * t);
            y += ay[k] * std::cos(k * t) + by[k] * std::sin(k * t);
        }

        stroke.push_back(Vec3(x, y, 0));
    }

    KnotDiagram d = makeDiagramFromStroke(stroke, 160, 2.0);

    for (DiagramCrossing &c : d.crossings)
        c.aOver = coin(rng) == 1;

    return d;
}

struct Inv
{
    AlexanderPolynomial a;
    JonesPolynomial j;
};

static Inv invariants(const KnotDiagram &d)
{
    const QVector<Vec3> c = diagramToCurve(d, 0.3);
    return {computeAlexanderPolynomial(c), computeJonesPolynomial(c)};
}

// comparisons where a polynomial could not be computed (not counted as
// changes; reported at the end)
static int g_unknown = 0;

static bool same(const Inv &x, const Inv &y)
{
    if (!x.a.valid || !y.a.valid || !x.j.valid || !y.j.valid)
    {
        ++g_unknown;
        return true;
    }

    return x.a.lowestPower == y.a.lowestPower && x.a.coefficients == y.a.coefficients &&
           x.j == y.j;
}

// the thickness the main window really gives the drawing: its curve as text,
// read back as a Custom Curve (see MainWindow::drawKnotDialog)
static double appRadius(const KnotDiagram &d, double radius, int density)
{
    const QString text = diagramToCoordinateText(diagramToCurve(d, 0.30));

    const CurveData data = buildPreset(Preset::CustomCoordinates, density, 2, 3, text);

    return safeTubeRadius(data, radius);
}

// a drawing done in a hurry: few mouse positions, strands doubling back over
// each other, so that crossings lie almost on top of each other
static KnotDiagram hurriedDiagram(std::mt19937 &rng)
{
    std::uniform_real_distribution<double> wobble(10.0, 45.0);
    std::uniform_real_distribution<double> phase(0.0, 6.28);
    std::uniform_int_distribution<int> coin(0, 1);

    const double a = wobble(rng);
    const double b = wobble(rng);
    const double p = phase(rng);
    const double q = phase(rng);

    QVector<Vec3> stroke;

    for (int i = 0; i < 90; ++i)
    {
        const double t = 2.0 * M_PI * i / 90.0;

        stroke.push_back(Vec3(
            150.0 * std::cos(t) + a * std::cos(4.0 * t + p) + b * std::sin(7.0 * t + q),
            120.0 * std::sin(2.0 * t) + a * std::sin(5.0 * t + q) + b * std::cos(7.0 * t + p),
            0.0));
    }

    KnotDiagram d = makeDiagramFromStroke(stroke, 160, 2.0);

    for (DiagramCrossing &c : d.crossings)
        c.aOver = coin(rng) == 1;

    return d;
}

// "spread": random drawings spread apart (the same knot, a thicker tube), and
// loops taken out of them (a loop nothing else crosses keeps the knot)
static int spreadTest(int count)
{
    constexpr double Radius = 0.16;

    std::mt19937 rng(20261031);
    std::uniform_int_distribution<int> terms(3, 7);

    // the worst drawings: where the tube all but vanishes
    int slivers = 0;
    int sliversRepaired = 0;
    int sliversChanged = 0;

    for (int k = 0; k < count; ++k)
    {
        const KnotDiagram d = k % 2 == 0 ? hurriedDiagram(rng) : randomDiagram(rng, terms(rng));

        if (!d.isValid() || d.crossings.size() > 22)
            continue;

        if (diagramTubeRadius(d, Radius) >= 0.04)
            continue;

        ++slivers;

        KnotDiagram spread = d;

        if (!spreadDiagram(spread, Radius))
            continue;

        if (diagramTubeRadius(spread, Radius) >= 0.08)
            ++sliversRepaired;

        if (spread.crossings.size() != d.crossings.size() || !same(invariants(d), invariants(spread)))
            ++sliversChanged;
    }

    std::printf("tube under 0.04: %d drawings, %d brought to 0.08 or more, knot changed in %d\n",
                slivers, sliversRepaired, sliversChanged);

    // does the number shown in the editor hold in the main window?
    {
        std::mt19937 again(7);

        int compared = 0;
        int differs = 0;
        int unringed = 0;
        double worst = 0.0;

        for (int k = 0; k < count; ++k)
        {
            KnotDiagram d = hurriedDiagram(again);

            if (!d.isValid() || d.crossings.size() > 22)
                continue;

            spreadDiagram(d, Radius);

            const double shown = diagramTubeRadius(d, Radius);
            const double real = appRadius(d, Radius, 200);

            ++compared;

            // no ring must mean a full tube, in the main window as well
            if (findTightSpots(d, Radius).isEmpty() && real < Radius * 0.9)
            {
                ++unringed;
                std::printf("  NO RING but the main window has %.3f (editor %.3f)\n", real, shown);
            }

            if (real < shown * 0.8)
                ++differs;

            worst = std::max(worst, shown - real);

            if (k < 6)
                std::printf("  editor %.3f, main window %.3f\n", shown, real);
        }

        std::printf("editor against main window: %d compared, %d more than 20 %% thinner, worst %.3f; "
                    "thin with no ring: %d\n",
                    compared, differs, worst, unringed);
    }

    int tight = 0;
    int better = 0;
    int full = 0;
    int changed = 0;
    int skipped = 0;
    int loopsTaken = 0;
    int loopsRefused = 0;
    int loopsChanged = 0;
    int twists = 0;
    int twistsChanged = 0;
    double before = 0.0;
    double after = 0.0;
    double slowest = 0.0;

    for (int k = 0; k < count; ++k)
    {
        const KnotDiagram d = randomDiagram(rng, terms(rng));

        if (!d.isValid() || d.crossings.size() > 22)
        {
            ++skipped;
            continue;
        }

        const double start = diagramTubeRadius(d, Radius);

        // taking out a loop
        for (const TightSpot &spot : findTightSpots(d, Radius))
        {
            if (spot.crossing < 0)
                continue;

            KnotDiagram cut = d;

            if (!removeDiagramLoop(cut, spot.crossing))
            {
                ++loopsRefused;
                continue;
            }

            ++loopsTaken;

            if (cut.crossings.size() >= d.crossings.size())
                ++loopsChanged;

            // only its own crossing went: nothing else touched the loop, so
            // that was a twist undone and it is the same knot
            if (cut.crossings.size() == d.crossings.size() - 1)
            {
                ++twists;

                if (!same(invariants(d), invariants(cut)))
                    ++twistsChanged;
            }
        }

        if (start >= Radius * 0.999)
            continue;

        ++tight;

        KnotDiagram spread = d;

        QElapsedTimer clock;

        clock.start();

        const bool done = spreadDiagram(spread, Radius);

        slowest = std::max(slowest, static_cast<double>(clock.elapsed()));

        if (!done)
        {
            before += start;
            after += start;
            continue;
        }

        const double end = diagramTubeRadius(spread, Radius);

        before += start;
        after += end;

        if (end > start * 1.05)
            ++better;

        if (end >= Radius * 0.9)
            ++full;

        if (spread.crossings.size() != d.crossings.size() || !same(invariants(d), invariants(spread)))
        {
            ++changed;
            std::printf("KNOT CHANGED at %d: %d crossings\n", k, static_cast<int>(d.crossings.size()));
        }
    }

    std::printf("%d drawings, %d too large skipped, %d too tight for radius %.2f\n", count, skipped, tight, Radius);
    std::printf("spread: %d thicker, %d reach 90 %% of the radius, average radius %.3f -> %.3f, slowest %.0f ms\n",
                better, full, before / std::max(tight, 1), after / std::max(tight, 1), slowest);
    std::printf("spread changed the knot: %d (%d not computed)\n", changed, g_unknown);
    std::printf("loops taken out: %d, refused %d, wrong crossing count %d\n", loopsTaken, loopsRefused, loopsChanged);
    std::printf("simple twists among them: %d, knot changed in %d\n", twists, twistsChanged);

    return changed == 0 && twistsChanged == 0 && sliversChanged == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    if (argc > 1 && QString::fromLatin1(argv[1]) == QStringLiteral("spread"))
        return spreadTest(argc > 2 ? QString::fromLatin1(argv[2]).toInt() : 200);

    // "trefoil": where things are in a drawn trefoil (for clicking them in
    // the real app): the crossings, and the moves after flipping each one
    if (argc > 1 && QString::fromLatin1(argv[1]) == QStringLiteral("trefoil"))
    {
        QVector<Vec3> stroke;

        for (int i = 0; i < 400; ++i)
        {
            const double t = 2.0 * M_PI * i / 400.0;
            stroke.push_back(Vec3(std::sin(t) + 2 * std::sin(2 * t),
                                  std::cos(t) - 2 * std::cos(2 * t), 0));
        }

        const KnotDiagram d = makeDiagramFromStroke(stroke, 160, 2.0);

        for (int k = 0; k < d.crossings.size(); ++k)
            std::printf("crossing %d %.4f %.4f\n", k, d.crossings[k].position.x, d.crossings[k].position.y);

        for (int k = -1; k < d.crossings.size(); ++k)
        {
            KnotDiagram f = d;

            if (k >= 0)
                f.crossings[k].aOver = !f.crossings[k].aOver;

            for (const ReidemeisterMove &m : findReidemeisterMoves(f))
                std::printf("flip %d move R%d %.4f %.4f\n", k, m.type, m.centre.x, m.centre.y);
        }

        // flip the top crossing, R2 on the right: what is left
        KnotDiagram f = d;
        f.crossings[1].aOver = !f.crossings[1].aOver;
        const QVector<ReidemeisterMove> ms = findReidemeisterMoves(f);
        const int i = reidemeisterMoveAt(ms, 1.2499, 1.0797);

        if (i >= 0)
        {
            for (const ReidemeisterMove &m : findReidemeisterMoves(ms[i].result))
                std::printf("after R2 move R%d %.4f %.4f\n", m.type, m.centre.x, m.centre.y);
        }

        return 0;
    }

    const int trials = argc > 1 ? std::atoi(argv[1]) : 300;
    std::mt19937 rng(12345);

    int found[4] = {0, 0, 0, 0}, ok[4] = {0, 0, 0, 0}, bad[4] = {0, 0, 0, 0};
    int midBad = 0, midChecked = 0, invalid = 0, walks = 0, walkSteps = 0, walkBad = 0;
    int diagrams = 0, withMoves = 0;
    qint64 worstFindMs = 0;

    for (int trial = 0; trial < trials; ++trial)
    {
        KnotDiagram d = randomDiagram(rng, 2 + trial % 5);

        if (!d.isValid() || d.crossings.isEmpty() || d.crossings.size() > 14)
            continue;

        ++diagrams;

        {
            // is the height curve faithful to the diagram?
            const KnotDiagram back = diagramFromHeights(diagramToCurve(d, 0.3));
            int mismatch = back.crossings.size() != d.crossings.size() ? 99 : 0;

            // crossings are matched by position (the curve has extra points)
            for (int k = 0; !mismatch && k < back.crossings.size(); ++k)
            {
                const DiagramCrossing &b = back.crossings[k];
                const int j = nearestDiagramCrossing(d, b.position.x, b.position.y, 1e-6);

                if (j < 0)
                {
                    ++mismatch;
                    continue;
                }

                // the over strand runs the same way in both
                auto overDirection = [](const KnotDiagram &g, const DiagramCrossing &c)
                {
                    const int s = c.aOver ? c.segA : c.segB;
                    const Vec3 &p = g.points[s];
                    const Vec3 &q = g.points[(s + 1) % g.points.size()];
                    return Vec3(q.x - p.x, q.y - p.y, 0).normalized();
                };

                const Vec3 u = overDirection(back, b);
                const Vec3 v = overDirection(d, d.crossings[j]);

                if (std::abs(u.x * v.y - u.y * v.x) > 0.2)
                    ++mismatch;
            }

            if (mismatch)
                std::printf("unfaithful height curve trial %d (%d)\n", trial, mismatch);
        }

        const Inv before = invariants(d);

        if (!before.a.valid || !before.j.valid)
        {
            ++invalid;
            continue;
        }

        QElapsedTimer timer;
        timer.start();
        const QVector<ReidemeisterMove> moves = findReidemeisterMoves(d);
        worstFindMs = std::max(worstFindMs, timer.elapsed());

        if (!moves.isEmpty())
            ++withMoves;

        for (int i = 0; i < moves.size(); ++i)
        {
            const ReidemeisterMove &m = moves[i];
            ++found[m.type];

            if (same(before, invariants(m.result)))
                ++ok[m.type];
            else
            {
                ++bad[m.type];

                // which step changes the knot: the lifted start curve, the
                // moved curve, or writing the result down as a diagram
                auto alex = [](const QVector<Vec3> &c)
                {
                    const AlexanderPolynomial p = computeAlexanderPolynomial(c);
                    return p.valid ? p.text : QStringLiteral("?");
                };

                std::printf("BAD R%d trial %d (c=%lld -> %lld)  diagram %s | start %s | end %s | result %s\n",
                            m.type, trial,
                            (long long)d.crossings.size(), (long long)m.result.crossings.size(),
                            qPrintable(before.a.text), qPrintable(alex(m.start)),
                            qPrintable(alex(m.end)), qPrintable(invariants(m.result).a.text));
            }

            // half way: the frame is a real curve of the same knot
            if (i < 2)
            {
                for (const double t : {0.3, 0.6, 0.9})
                {
                    const KnotDiagram f = reidemeisterFrame(m, t);

                    if (f.crossings.size() > 16)
                        continue;

                    ++midChecked;

                    if (!same(before, invariants(f)))
                    {
                        ++midBad;
                        std::printf("BAD frame R%d t=%.1f trial %d\n", m.type, t, trial);
                    }
                }
            }
        }

        // a random walk of moves until none is left
        if (!moves.isEmpty())
        {
            ++walks;
            KnotDiagram w = d;

            for (int step = 0; step < 40; ++step)
            {
                const QVector<ReidemeisterMove> ms = findReidemeisterMoves(w);

                if (ms.isEmpty())
                    break;

                std::uniform_int_distribution<int> pick(0, ms.size() - 1);
                const ReidemeisterMove &chosen = ms[pick(rng)];
                const KnotDiagram previous = w;
                w = chosen.result;
                ++walkSteps;

                const Inv now = invariants(w);

                if (!same(before, now))
                {
                    ++walkBad;
                    std::printf("BAD walk trial %d step %d: R%d c=%lld -> %lld, alexander %s (%d) -> %s (%d), "
                                "jones %s (%d) -> %s (%d), previous %s, start %s, end %s\n",
                                trial, step, chosen.type,
                                (long long)previous.crossings.size(), (long long)w.crossings.size(),
                                qPrintable(before.a.text), before.a.valid, qPrintable(now.a.text), now.a.valid,
                                qPrintable(before.j.text), before.j.valid, qPrintable(now.j.text), now.j.valid,
                                qPrintable(invariants(previous).a.text),
                                qPrintable(computeAlexanderPolynomial(chosen.start).text),
                                qPrintable(computeAlexanderPolynomial(chosen.end).text));
                    break;
                }
            }
        }
    }

    std::printf("diagrams %d (with moves %d, invariants not computable %d)\n",
                diagrams, withMoves, invalid);

    for (int t = 1; t <= 3; ++t)
        std::printf("R%d: found %d, same knot %d, changed %d\n", t, found[t], ok[t], bad[t]);

    std::printf("frames checked %d, changed %d\n", midChecked, midBad);
    std::printf("walks %d, steps %d, changed %d\n", walks, walkSteps, walkBad);
    std::printf("slowest search %lld ms\n", (long long)worstFindMs);
    std::printf("comparisons not computable %d\n", g_unknown);

    return 0;
}
