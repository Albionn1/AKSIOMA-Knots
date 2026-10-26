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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

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
