#ifndef KNOTTABLE_H
#define KNOTTABLE_H

#include "knotmath.h"

#include <QString>
#include <QVector>

#include <map>

namespace KnotMath
{

/*
 * The table of all prime knots with up to 13 crossings (12,965 knots) from
 * KnotInfo: C. Livingston and A. H. Moore, KnotInfo: Table of Knot
 * Invariants, knotinfo.org. Built into the program as data/knottable.dat by
 * tools/make_knot_table.cpp.
 */
struct KnotTableRecord
{
    // KnotInfo name: "3_1", ..., "10_165", "11a_1", "13n_5110"
    QString name;

    // classical (Conway) name where there is one, e.g. "11_471"
    QString conwayName;

    int crossings = 0;
    bool alternating = false;
    bool fibered = false;

    // a number, or a range such as "[1,2]" where it is not known exactly
    QString unknotting;

    int genus = -1;
    int braidIndex = -1;
    QString braidNotation;
    int signature = 0;
    int determinant = 0;

    // hyperbolic volume, 0 for non-hyperbolic knots
    double volume = 0.0;

    // "reversible", "chiral", "fully amphicheiral", ...
    QString symmetry;

    // Conway notation "[3;3;2-]", two-bridge "[7,3]", pretzel "P(3,3,-2)",
    // Montesinos "K(1/3;1/3;-1/2)" (empty or "Not Montesinos" if none),
    // geometric type "hyperbolic", "torus knot T(2,5)", "satellite ..."
    QString conwayNotation;
    QString twoBridge;
    QString pretzel;
    QString montesinos;
    QString geometricType;

    // power -> coefficient, as in KnotInfo (Alexander starting at t^0)
    std::map<int, long long> alexander;
    std::map<int, long long> jones;

    // KnotInfo's 3D curve, quantised (see curve())
    QVector<quint16> quantised;
    float boxLow[3] = {0.0f, 0.0f, 0.0f};
    float boxHigh[3] = {0.0f, 0.0f, 0.0f};
};

class KnotTable
{
public:
    // Loads the table on first use.
    static const KnotTable &instance();

    bool isLoaded() const;
    QString error() const;

    int size() const;
    const KnotTableRecord &at(int index) const;

    // index of a KnotInfo name, -1 if unknown
    int indexOf(const QString &name) const;

    // The knot's closed 3D curve from KnotInfo, centred and scaled to the
    // size of the built-in presets.
    QVector<Vec3> curve(int index) const;

    // "3_1" -> "3₁", "11n_34" -> "11n₃₄"
    static QString displayName(const QString &name);

    // Common name of the famous knots ("Trefoil knot", "Conway knot"),
    // otherwise empty. Only about a dozen knots have one; the others are
    // known by their number.
    static QString commonName(const QString &name);

    // The family a knot belongs to, worked out from KnotInfo: "Torus knot
    // T(2,7)", "Twist knot (5 half-twists)", "Two-bridge knot 13/5",
    // "Pretzel knot P(−2,3,7)", "Montesinos knot", "Cable of the trefoil",
    // otherwise "Hyperbolic knot".
    static QString familyName(const KnotTableRecord &record);

    // The common name if there is one, otherwise the family.
    static QString descriptiveName(const KnotTableRecord &record);

    // Alexander polynomial normalised like the program's own: symmetric
    // (Delta(t) = Delta(1/t)) and Delta(1) = 1.
    static std::map<int, long long> normalisedAlexander(const KnotTableRecord &record);

    // The normalised Alexander polynomial and the Jones polynomial, both
    // formatted for display.
    static QString alexanderText(const KnotTableRecord &record);
    static QString jonesText(const KnotTableRecord &record);

private:
    KnotTable();

    bool loaded_ = false;
    QString error_;
    int levels_ = 4095;

    QVector<KnotTableRecord> records_;
};

} // namespace KnotMath

#endif // KNOTTABLE_H
