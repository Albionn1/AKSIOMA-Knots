// tools/math_audit: acceptance test of the mathematics. Every line must say ok.
// math_audit.exe        about a minute on a modern PC
#include "knotmath.h"
#include "knottable.h"

#include <QCoreApplication>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <future>
#include <thread>
#include <vector>

using namespace KnotMath;

static int failures = 0;

static void check(bool passed, const char *what)
{
    std::printf("%s  %s\n", passed ? "ok  " : "FAIL", what);
    std::fflush(stdout);

    if (!passed)
        ++failures;
}

static std::map<int, long long> termsOf(const QVector<long long> &c, int lowest)
{
    std::map<int, long long> t;

    for (int i = 0; i < c.size(); ++i)
    {
        if (c[i] != 0)
            t[lowest + i] = c[i];
    }

    return t;
}

// how many table knots are not the knot they are named at this density
static int wrongAt(int samples, int step)
{
    const KnotTable &table = KnotTable::instance();

    std::atomic<int> next{0};
    std::atomic<int> wrong{0};
    std::vector<std::future<void>> jobs;

    const unsigned threads = std::max(2u, std::thread::hardware_concurrency());

    for (unsigned t = 0; t < threads; ++t)
    {
        jobs.push_back(std::async(std::launch::async, [&]()
        {
            for (;;)
            {
                const int i = step * next.fetch_add(1);

                if (i >= table.size())
                    return;

                const KnotTableRecord &r = table.at(i);
                const CurveData d = buildPreset(Preset::KnotTable, samples, 3, 2, r.name);

                const AlexanderPolynomial a = computeAlexanderPolynomial(d.points);
                const JonesPolynomial j = computeJonesPolynomial(d.points);

                bool same = a.valid && termsOf(a.coefficients, a.lowestPower) == KnotTable::normalisedAlexander(r);

                if (j.valid)
                {
                    const std::map<int, long long> v = termsOf(j.coefficients, j.lowestPower);
                    std::map<int, long long> mirror;

                    for (const auto &term : r.jones)
                        mirror[-term.first] = term.second;

                    same = same && (v == r.jones || v == mirror);
                }

                if (!same)
                    ++wrong;
            }
        }));
    }

    for (auto &job : jobs)
        job.get();

    return wrong.load();
}

// what the app reports for a Custom Curve
static Invariants customCurve(const char *formula)
{
    const CurveData d = buildPreset(Preset::CustomCoordinates, 160, 3, 2, QString::fromLatin1(formula));

    return computeInvariantsWithCrossings(
        Preset::CustomCoordinates, d.points, detectProjectionCrossings(d.points, 2, 700), true);
}

static bool saysUnknot(const char *formula)
{
    try
    {
        return customCurve(formula).classification.contains(QStringLiteral("unknot"));
    }
    catch (const std::exception &)
    {
        return false; // refused with a message: also right
    }
}

// a formula as the Custom Curve box reads it
static bool reads(const char *formula, QString *message = nullptr)
{
    ScalarFunction fx;
    ScalarFunction fy;
    ScalarFunction fz;
    QString error;

    const bool ok = parseFormula(QString::fromUtf8(formula), fx, fy, fz, error);

    if (message != nullptr)
        *message = error;

    return ok;
}

static double valueOf(const char *formula, double t)
{
    ScalarFunction fx;
    ScalarFunction fy;
    ScalarFunction fz;
    QString error;

    if (!parseFormula(QString::fromUtf8(formula), fx, fy, fz, error))
        return std::nan("");

    return fx(t);
}

static double outerSize(const CurveData &data)
{
    double size = 0.0;

    for (const Vec3 &p : data.points)
        size = std::max(size, p.length());

    return size;
}

static int crossingsOf(Preset preset)
{
    return detectProjectionCrossings(buildPreset(preset, 160, 3, 2, QString()).points, 2, 700).size();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // M1: a curve that passes through itself is not a knot
    check(!saysUnknot("x=sin(t); y=sin(2*t); z=0"), "M1 a figure-8 in a plane is not called the unknot");
    check(!saysUnknot("x=(2+cos(3*t))*cos(2*t); y=(2+cos(3*t))*sin(2*t); z=0"),
          "M1 the flat shadow of a trefoil is not called the unknot");
    check(!saysUnknot("x=cos(2*t); y=sin(2*t); z=0"), "M1 a circle run through twice is not called the unknot");
    check(saysUnknot("x=cos(t); y=sin(t); z=0"), "M1 a plain circle still is the unknot");

    // M2: every knot of the table is the knot it is named
    check(wrongAt(160, 1) == 0, "M2 all 12,965 table knots right at the default density 160");
    check(wrongAt(320, 1) == 0, "M2 all 12,965 table knots right at the highest density 320");
    check(wrongAt(48, 7) == 0, "M2 table knots right at the lowest density 48");

    // M3: T(p,q) with p, q > 0 is the positive torus knot
    const QVector<Vec3> trefoil = buildPreset(Preset::Trefoil, 160, 3, 2, QString()).points;
    const JonesPolynomial jones = computeJonesPolynomial(trefoil);
    const KnotTable &table = KnotTable::instance();

    check(jones.valid && termsOf(jones.coefficients, jones.lowestPower) == table.at(table.indexOf(QStringLiteral("3_1"))).jones,
          "M3 the Trefoil preset has the Jones polynomial of table knot 3_1 (t + t^3 - t^4)");

    const CurveData link = buildPreset(Preset::CustomTorus, 160, 4, 2, QString());

    check(computeMultiStrandInvariants(Preset::CustomTorus, link).methodology.contains(QStringLiteral("+2")),
          "M3 the torus link T(4,2) has linking number +2");

    // M4: the presets open on their textbook pictures
    check(crossingsOf(Preset::Trefoil) == 3, "M4 the Trefoil preset shows 3 crossings");
    check(crossingsOf(Preset::Cinquefoil) == 5, "M4 the Cinquefoil preset shows 5 crossings");

    // M5: writhe does not depend on the size of the curve
    QVector<Vec3> small = trefoil;
    QVector<Vec3> large = trefoil;

    for (Vec3 &p : small)
        p *= 1.0e-5;

    for (Vec3 &p : large)
        p *= 1.0e5;

    const double writhe = computeWrithe(trefoil);

    check(std::abs(computeWrithe(small) - writhe) < 1.0e-6, "M5 writhe of a curve 100000 times smaller is the same");
    check(std::abs(computeWrithe(large) - writhe) < 1.0e-6, "M5 writhe of a curve 100000 times larger is the same");

    // M6: a curve that does not close is said to
    const auto curve = [](const char *formula)
    {
        return buildPreset(Preset::CustomCoordinates, 160, 3, 2, QString::fromUtf8(formula));
    };

    check(!curve("x=cos(3*t); y=sin(3*t); z=t").warning.isEmpty(), "M6 a helix that does not close is said so");
    check(curve("x=(2+cos(2*t))*cos(3*t); y=(2+cos(2*t))*sin(3*t); z=sin(4*t)").warning.isEmpty(),
          "M6 the example formula closes");

    // M10: enough points for every turn
    check(buildPreset(Preset::CustomTorus, 160, 20, 19, QString()).points.size() >= 24 * 20,
          "M10 T(20,19) has at least 24 points a turn");
    check(buildPreset(Preset::CustomTorus, 160, 3, 2, QString()).points.size() == 160,
          "M10 a small torus knot keeps the density asked for");

    // M11: only curves far from the size of a tube are scaled
    check(std::abs(outerSize(curve("x=0.00001*cos(t); y=0.00001*sin(t); z=0.00001*sin(2*t)")) - 2.7) < 0.2,
          "M11 a tiny curve comes to the size of the presets");
    check(std::abs(outerSize(curve("x=90000*cos(t); y=90000*sin(t); z=90000*sin(2*t)")) - 2.7) < 0.2,
          "M11 a huge curve too");
    check(std::abs(outerSize(curve("x=cos(t); y=sin(t); z=0")) - 1.0) < 0.01, "M11 an ordinary curve keeps its size");

    // M12: what the box reads, and what it says when it cannot
    QString said;

    check(!reads("x=1 2;y=t;z=0"), "M12 a number after a number is refused");
    check(reads("x=2t;y=2(t+1);z=sin(t)cos(t)"), "M12 products without a star still read");
    check(std::abs(valueOf("x=sec(t);y=t;z=t", 0.5) - 1.0 / std::cos(0.5)) < 1e-12, "M12 sec");
    check(std::abs(valueOf("x=cot(t);y=t;z=t", 0.5) - 1.0 / std::tan(0.5)) < 1e-12, "M12 cot");
    check(valueOf("x=sign(t-3);y=t;z=t", 1.0) == -1.0, "M12 sign");
    check(std::abs(valueOf("x=log10(100);y=t;z=t", 1.0) - 2.0) < 1e-12, "M12 log10");
    check(std::abs(valueOf("x=atan2(1,1);y=t;z=t", 1.0) - std::atan(1.0)) < 1e-12, "M12 atan2");
    check(valueOf("x=min(t,2)+max(t,2);y=t;z=t", 1.0) == 3.0, "M12 min and max");
    check(valueOf("x=pow(2,t);y=t;z=t", 3.0) == 8.0, "M12 pow");
    check(valueOf("x=mod(t,3);y=t;z=t", 7.0) == 1.0, "M12 mod");
    check(valueOf("x=t\xC2\xB2;y=t;z=t", 3.0) == 9.0, "M12 a small 2 is a square");
    check(valueOf("x=2t\xE2\x81\xBB\xC2\xB9;y=t;z=t", 2.0) == 1.0, "M12 a polynomial copied from the app reads");
    check(std::abs(valueOf("x=2\xCF\x80 t;y=t;z=t", 1.0) - 8.0 * std::atan(1.0)) < 1e-12, "M12 the sign for pi");
    check(valueOf("x=\xE2\x88\x92t\xC3\x97" "2;y=t;z=t", 1.0) == -2.0, "M12 a printed minus and times");

    check(!reads("x=sinn(t);y=t;z=t", &said) && said.contains(QStringLiteral("sinn")), "M12 names an unknown word");
    check(!reads("x=t;y=cos(t;z=t", &said) && said.contains(QStringLiteral("for y")), "M12 names the line");
    check(!reads("x=0,5*t;y=t;z=t", &said) && said.contains(QStringLiteral("comma")), "M12 explains a decimal comma");

    std::printf("\n%d failure(s)\n", failures);

    return failures == 0 ? 0 : 1;
}
