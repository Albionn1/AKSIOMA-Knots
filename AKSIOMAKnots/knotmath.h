#ifndef KNOTMATH_H
#define KNOTMATH_H

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>
#include <map>

namespace KnotMath
{

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3() = default;

    Vec3(double xValue, double yValue, double zValue)
        : x(xValue), y(yValue), z(zValue)
    {
    }

    Vec3 operator+(const Vec3 &other) const;
    Vec3 operator-(const Vec3 &other) const;
    Vec3 operator*(double scalar) const;

    // NOTE: dividing by (almost) zero returns the zero vector instead of inf/nan.
    Vec3 operator/(double scalar) const;

    Vec3 &operator+=(const Vec3 &other);
    Vec3 &operator-=(const Vec3 &other);
    Vec3 &operator*=(double scalar);
    Vec3 &operator/=(double scalar);

    double length() const;
    double lengthSquared() const;

    Vec3 normalized() const;

    static double dot(const Vec3 &a, const Vec3 &b);
    static Vec3 cross(const Vec3 &a, const Vec3 &b);
};

struct CrossingInfo
{
    // Indices refer to the polyline the crossing was detected on
    // (which may be a resampled copy of the displayed curve).
    int segmentA = -1;
    int segmentB = -1;

    double parameterA = 0.0;
    double parameterB = 0.0;

    // Position along the closed curve as a fraction of the arc length (0..1).
    // These are resolution independent and are what the mesh colouring uses.
    double fractionA = 0.0;
    double fractionB = 0.0;

    double depthA = 0.0;
    double depthB = 0.0;

    Vec3 pointA;
    Vec3 pointB;

    Vec3 midpoint;

    // +1 / -1 : sign of the crossing for the oriented curve
    // (right-handed = +1). 0 if undetermined.
    int sign = 0;

    // true when strand A passes over strand B (viewer on the +depth side).
    bool aIsOver = true;

    int number = 0;
};

enum class ColorMode
{
    SingleColor = 0,
    CrossingColors,
    StrandColors
};

struct Mesh
{
    QVector<float> positions;
    QVector<float> normals;
    QVector<float> colors;
    QVector<unsigned int> indices;

    // Per vertex, for surface patterns (rope fibres): cos/sin of the angle
    // around the tube and cos/sin of the twist phase along it (whole turns
    // per closed tube). Angles are stored as cos/sin so that they
    // interpolate without a seam.
    QVector<float> texcoords;

    // Per vertex: 1 = open, smaller = shaded by nearby strands
    // (computeAmbientOcclusion). Empty: no occlusion.
    QVector<float> occlusion;

    // Centre line of every tube: x, y, z, radius per sample; the strand it
    // belongs to and the arc length along that strand.
    QVector<float> centerline;
    QVector<int> centerlineStrand;
    QVector<float> centerlineArc;

    // Per strand: total length (closed strands wrap around).
    QVector<float> strandLength;
    QVector<bool> strandClosed;

    // Per vertex: index of its centre-line sample.
    QVector<int> vertexSample;

    int ringCount = 0;
    int ringSegments = 0;

    bool isValid() const;
    void clear();

    // Gives every vertex the same colour (creates the colour array).
    void fillColor(const QColor &color);

    // Appends another mesh. If either mesh has colours, the other one is
    // given white vertex colours so that the arrays stay consistent.
    void append(const Mesh &other);
};

enum class Preset
{
    Unknot = 0,
    Trefoil,
    FigureEight,
    Cinquefoil,
    Stevedore,
    DNAHelix,
    CelticKnot,
    CustomTorus,
    CustomCoordinates,

    // a knot from the KnotInfo table (knottable.h), chosen by name
    KnotTable
};

struct CurveData
{
    // The main curve (for multi-strand structures: the first component).
    QVector<Vec3> points;

    // Multi-strand structures: every component is meshed separately.
    QVector<QVector<Vec3>> separateStrands;
    QVector<bool> strandClosed;
    QVector<double> strandRadiusScale;

    QString name;
    QString classification;
    QString polynomial;

    bool closed = true;
    bool exactBuiltIn = false;
    bool multiStrand = false;

    // Minimal crossing number if known (built-in knots), otherwise -1.
    int knownCrossingNumber = -1;

    // Preset::KnotTable: index in KnotTable, otherwise -1.
    int tableIndex = -1;
};

struct Invariants
{
    // Gauss-integral writhe of the displayed geometry.
    double writhe = 0.0;

    // Crossings of the current projection (diagram dependent). -1 = n/a.
    int crossingNumber = -1;

    // Sum of signs of the projection crossings (diagram writhe). Only valid
    // when crossingNumber >= 0.
    int signedCrossingSum = 0;

    // Minimal crossing number for built-in knots, -1 if unknown.
    int knownCrossingNumber = -1;

    bool multiStrand = false;

    QString classification;
    QString alexanderPolynomial;
    QString jonesPolynomial;
    QString methodology;
};

using ScalarFunction = std::function<double(double)>;

QVector<Vec3> sampleParametricCurve(
    const ScalarFunction &fx,
    const ScalarFunction &fy,
    const ScalarFunction &fz,
    int samples,
    double tMin = 0.0,
    double tMax = 2.0 * 3.1415926535897932384626433832795);

// Smooth, denser copy of a polyline for drawing: a centripetal Catmull-Rom
// spline through the points, every segment split into `factor` pieces.
// Only for display - the analysis keeps using the original points.
QVector<Vec3> refineCurve(
    const QVector<Vec3> &curve,
    int factor,
    bool closed = true);

// Per-point values (e.g. highlight weights) for a curve refined with the
// same factor, linearly interpolated.
QVector<float> refineValues(
    const QVector<float> &values,
    int factor,
    bool closed = true);

// Subdivision factor that keeps the drawn tube pieces shorter than about
// 0.6 tube radii (at most 8, and at most about 2400 pieces in total).
int refineFactorForTube(
    const QVector<Vec3> &curve,
    double radius,
    bool closed = true);

// Resamples a polyline at (approximately) uniform arc length.
QVector<Vec3> resampleClosedCurve(
    const QVector<Vec3> &input,
    int targetCount,
    bool closed = true);

QVector<Vec3> torusKnotCurve(int p, int q, int samples);
QVector<Vec3> unknotCurve(int samples);
QVector<Vec3> trefoilCurve(int samples);
QVector<Vec3> figureEightCurve(int samples);
QVector<Vec3> cinquefoilCurve(int samples);

// Closure of the 4-braid s1 s1 s2 s1^-1 s3^-1 s2 s3^-1, which is the
// stevedore knot 6_1 (Alexander polynomial verified numerically).
QVector<Vec3> stevedoreCurve(int samples);

// Toroidal (closed) double helix with base-pair rungs.
CurveData buildDNAStructure(int samples);

// Closed chain of four interlocked rings ("chain mail").
CurveData buildArmorStructure(int samples);

CurveData buildPreset(
    Preset preset,
    int samples,
    int torusP,
    int torusQ,
    const QString &customText);

bool parseCoordinateSequence(
    const QString &text,
    QVector<Vec3> &points,
    QString &errorMessage);

bool parseFormula(
    const QString &text,
    ScalarFunction &fx,
    ScalarFunction &fy,
    ScalarFunction &fz,
    QString &errorMessage);

// If maxSamples <= 0 or the curve has at most maxSamples points the curve is
// used as it is (no resampling), otherwise it is resampled to maxSamples.
QVector<CrossingInfo> detectProjectionCrossings(
    const QVector<Vec3> &curve,
    int projectionAxis = 2,
    int maxSamples = 900);

QColor crossingColor(int crossingIndex);
QColor strandColor(int strandIndex);

// Colour of a component of a multi-strand structure (DNA strands, rings).
QColor componentColor(int componentIndex);

// Keeps the orientation of the tube rings continuous between the frames of
// an animation (otherwise the ring vertices can jump around the tube and
// the shading "pops"). Pass the same hint when building every frame.
// The first frame uses the usual parallel-transport frame; after that every
// ring keeps its own normal from the previous frame (projected onto its new
// cross-section), so a ring only turns as much as the curve itself does.
struct TubeFrameHint
{
    // ring normals of the previous frame (empty: no previous frame)
    QVector<Vec3> ringNormals;
};

// Builds a tube around the curve. In ColorMode::CrossingColors and
// ColorMode::StrandColors the crossings are detected internally.
Mesh buildTubeMesh(
    const QVector<Vec3> &curve,
    double radius,
    int ringSegments,
    bool closed = true,
    ColorMode colorMode = ColorMode::SingleColor,
    int projectionAxis = 2,
    TubeFrameHint *frameHint = nullptr);

// Same, but re-using crossings that were already detected for this curve.
Mesh buildTubeMeshWithCrossings(
    const QVector<Vec3> &curve,
    double radius,
    int ringSegments,
    bool closed,
    ColorMode colorMode,
    const QVector<CrossingInfo> &crossings,
    TubeFrameHint *frameHint = nullptr);

// Largest tube radius (never more than `requestedRadius`) for which the
// tubes around the curve(s) of `data` do not touch each other: 0.46 x the
// smallest centre-line distance between two parts that are more than four
// radii apart along a curve (the rule the unknotting animation uses).
// Open strands of a multi-strand structure (DNA rungs) end on the other
// strands on purpose and are ignored.
double safeTubeRadius(
    const CurveData &data,
    double requestedRadius);

// Depth cue: darkens every vertex by the parts of the tube(s) close to it
// (other strands, or the same strand further along), treating the centre
// line as a chain of spheres. Fills mesh.occlusion.
void computeAmbientOcclusion(
    Mesh &mesh,
    double strength = 1.0);

// Exact Gauss writhe of the closed polygon (Klenin-Langowski formula).
double computeWrithe(
    const QVector<Vec3> &curve,
    int maxSamples = 450);

// Gauss linking number of two closed polygons (not rounded).
double computeLinkingNumber(
    const QVector<Vec3> &curveA,
    const QVector<Vec3> &curveB,
    int maxSamples = 200);

int estimateCrossingNumber(
    const QVector<Vec3> &curve,
    int projectionAxis = 2,
    int maxSamples = 700);

QString alexanderPolynomialForTorusKnot(int p, int q);

// Laurent polynomial in t (power -> coefficient) as display text, highest
// power first: "t² − t + 1 − t⁻¹ + t⁻²".
QString formatPolynomial(const std::map<int, long long> &coefficients);

struct AlexanderPolynomial
{
    bool valid = false;

    // Coefficient of t^(lowestPower + i). Normalised: symmetric
    // (Delta(t) = Delta(1/t)) and Delta(1) = +1.
    QVector<long long> coefficients;
    int lowestPower = 0;

    // Crossings of the projection the polynomial was computed from
    // (-1 if no usable projection was found).
    int diagramCrossings = -1;

    // true if a second, different projection gave the same polynomial.
    bool confirmed = false;

    QString text;

    // Why it could not be computed (empty if valid).
    QString error;
};

// Exact Alexander polynomial of a closed curve (a knot), computed from the
// regular projection with the fewest crossings among several viewing
// directions: Wirtinger presentation -> Fox calculus -> determinant of the
// Alexander matrix with one row and one column removed (exact integer
// arithmetic). Diagrams with more than `maxCrossings` crossings are not
// computed.
AlexanderPolynomial computeAlexanderPolynomial(
    const QVector<Vec3> &curve,
    int maxCrossings = 80);

struct JonesPolynomial
{
    bool valid = false;

    // Coefficient of t^(lowestPower + i). Convention: Jones' skein relation
    // t^-1 V(L+) - t V(L-) = (t^1/2 - t^-1/2) V(L0), V(unknot) = 1, so the
    // right-handed (positive) trefoil has V = t + t³ - t⁴.
    QVector<long long> coefficients;
    int lowestPower = 0;

    int diagramCrossings = -1;
    bool confirmed = false;

    QString text;
    QString error;

    // V(1/t): the polynomial of the mirror image.
    JonesPolynomial mirrored() const;

    // V(t) == V(1/t). If false the knot is certainly chiral.
    bool isSymmetric() const;

    bool operator==(const JonesPolynomial &other) const;
};

// Exact Jones polynomial of a closed curve (a knot) from the Kauffman
// bracket state sum of the cleanest projection (2^c states, so diagrams
// with more than `maxCrossings` crossings are not computed). Checked:
// V(1) = 1, V'(1) = 0, V(e^(2 pi i/3)) = 1, and a second projection.
JonesPolynomial computeJonesPolynomial(
    const QVector<Vec3> &curve,
    int maxCrossings = 18);

// Jones polynomial of the positive (right-handed) torus knot T(p,q),
// gcd(p,q) = 1:  t^((p-1)(q-1)/2) (1 - t^(p+1) - t^(q+1) + t^(p+q)) / (1 - t²).
JonesPolynomial torusKnotJonesPolynomial(int p, int q);

struct KnotTableEntry
{
    // Table notation, e.g. "4₁", "11n₃₄", or "3₁ # 3₁" for a composite knot.
    QString notation;

    // Common name if there is one (may be empty).
    QString name;

    int crossingNumber = 0;

    // Prime knot: its index in the knot table (knottable.h), otherwise -1.
    int tableIndex = -1;

    // Composite knot: the table indices of its two summands.
    QVector<int> summands;
};

// Every prime knot with up to 13 crossings (the knot table) and every
// composite knot with at most 8 crossings whose Alexander polynomial equals
// `polynomial` (mirror images not distinguished), sorted by crossing number.
// Empty if `polynomial` is not valid or nothing matches.
QVector<KnotTableEntry> knotsWithAlexanderPolynomial(
    const AlexanderPolynomial &polynomial);

// One line for the classification panel, e.g.
// "Probably the figure-eight knot (4₁)". A valid Jones polynomial is used
// to rule out candidates, to tell granny and square knot apart and to name
// the handedness of torus knots. Knots with more crossings than the
// diagrams the polynomials were computed from are ruled out as well.
QString identifyKnot(
    const AlexanderPolynomial &polynomial,
    const JonesPolynomial &jones = JonesPolynomial());

QString normalizedAlexanderPolynomialForPreset(Preset preset);

// Minimal crossing number of a built-in preset (or torus knot), -1 if unknown.
int knownCrossingNumber(Preset preset, int torusP = 0, int torusQ = 0);

Invariants computeInvariants(
    Preset preset,
    const QVector<Vec3> &curve,
    bool closed = true);

// Same, re-using already detected crossings of `curve`.
// tableIndex: the knot's index in KnotTable for Preset::KnotTable.
Invariants computeInvariantsWithCrossings(
    Preset preset,
    const QVector<Vec3> &curve,
    const QVector<CrossingInfo> &crossings,
    bool closed = true,
    int torusP = 0,
    int torusQ = 0,
    int tableIndex = -1);

// Invariants of a multi-strand structure (linking numbers between components).
Invariants computeMultiStrandInvariants(
    Preset preset,
    const CurveData &data);

QString presetDisplayName(Preset preset);

QString mathematicalDescription(Preset preset);

/*
 * =============================================================
 * UNKNOTTING ANIMATION
 * =============================================================
 *
 * A knotted curve can never be undone by a smooth motion; the number of
 * times a strand has to be passed through another strand ("crossing
 * change") is the unknotting number. The animation therefore does this:
 *
 *   1. Align    - rotate to the viewing direction that needs the fewest
 *                 crossing changes.
 *   2. Layer    - flatten the curve into a clean knot diagram (every
 *                 strand piece is either "over" or "under").
 *   3. Change   - one crossing at a time: the two strands pass through
 *                 each other (a flash marks the event).
 *   4. Settle   - the diagram is now descending (every crossing is first
 *                 met as an over-crossing when walking along the curve from
 *                 a base point), which is always an unknot.
 *   5. Untangle - a real isotopy (no more pass-throughs): the descending
 *                 curve is shrunk onto a vertical line and swung open into
 *                 a single round loop.
 *   6. Flatten  - the loop is laid flat into a planar circle.
 *
 * Works for any closed curve (presets, torus knots, custom formulas or
 * coordinates). The number of crossing changes used is only an UPPER BOUND
 * for the unknotting number of the knot. If it is 0, the curve is
 * provably an unknot.
 */

struct UnknottingEvent
{
    // Animation time (0..1) at which the two strands pass through each other.
    double time = 0.0;

    // World position of the pass-through.
    Vec3 position;

    // 1-based number of the crossing change.
    int number = 0;
};

class UnknottingAnimation
{
public:
    enum class Stage
    {
        Align = 0,
        Layer,
        Change,
        Settle,
        Untangle,
        Flatten,
        Done
    };

    // `tubeRadius` is the tube radius the caller would like to show; the
    // plan is chosen so that tubes of (at most) this radius do not touch.
    static UnknottingAnimation build(
        const QVector<Vec3> &curve,
        double tubeRadius = 0.16,
        int samples = 240);

    bool isValid() const;

    // true if the animation has been checked against its own diagrams
    // (right number of crossings, right crossing changes) AND the strands
    // never touch (apart from the announced pass-throughs).
    bool isVerified() const;

    // Number of crossing changes the animation uses.
    int crossingChanges() const;

    // Crossings of the chosen projection.
    int diagramCrossings() const;

    // Duration at speed 1.
    double totalSeconds() const;

    // Smallest centre-line distance between two different parts of the curve
    // during the whole animation (the pass-through moments are not counted).
    double minimumClearance() const;

    // Tube radius that keeps the tubes from touching during the animation
    // (never larger than the radius that was requested in build()).
    double suggestedTubeRadius() const;

    const QVector<UnknottingEvent> &events() const;

    // Radius of the plain circle the animation ends with.
    double finalRingRadius() const;

    // Bounding box of the whole animation (centre line only).
    Vec3 boundsMin() const;
    Vec3 boundsMax() const;

    // Where the stages and the crossing changes start (time 0..1), in order.
    struct TimelineMark
    {
        double time = 0.0;

        // stage that starts here
        Stage stage = Stage::Align;

        // crossing change that starts here (1-based), 0 for a stage start
        int change = 0;
    };

    QVector<TimelineMark> timeline() const;

    // The knot diagram the animation works with (the projection that is
    // layered in the Layer stage), for drawing it in 2D.
    struct DiagramCrossing
    {
        QPointF position;

        // positions of the two passes along the curve (0..1, walking from
        // the base point)
        double u[2] = {0.0, 0.0};

        // pass (0 or 1) that is on top before any crossing change
        int overAtStart = 0;

        // number of the crossing change at this crossing (1-based), 0 if
        // it is not changed
        int change = 0;
    };

    struct Diagram
    {
        // the curve in the diagram plane, walking order from the base point
        QVector<QPointF> points;

        QVector<DiagramCrossing> crossings;
    };

    Diagram diagram() const;

    // Curve (closed polyline) at animation time t in [0,1].
    // highlight (optional): per point -1..1, non-zero near the two strands
    // that are currently passing through each other: positive on the strand
    // that was on top before this crossing change, negative on the other.
    QVector<Vec3> curveAt(
        double t,
        QVector<float> *highlight = nullptr,
        Stage *stage = nullptr,
        int *changeNumber = nullptr) const;

private:
    static UnknottingAnimation buildOnce(
        const QVector<Vec3> &curve,
        double tubeRadius,
        int samples);

    bool verify() const;

    // Smallest distance between two parts of the curve that are further
    // apart than a few tube radii along the curve. Stops early (returning a
    // value below `stopBelow`) as soon as a frame is worse than that.
    double measureClearance(int frames, double stopBelow) const;

    struct Node
    {
        double u = 0.0;
        int crossing = 0;
        int which = 0;
    };

    QVector<double> nodeValuesAt(double changeProgress) const;

    QVector<double> layerHeights(const QVector<double> &nodeValues) const;

    bool valid_ = false;
    bool verified_ = false;

    int n_ = 0;
    int changes_ = 0;
    int diagramCrossings_ = 0;

    Vec3 center_;
    Vec3 axis_ = Vec3(1.0, 0.0, 0.0);
    double angle_ = 0.0;

    double layerHalfGap_ = 0.3;

    // traversal ordered points (relative to center_)
    QVector<Vec3> originalRel_;
    QVector<Vec3> rotatedRel_;
    QVector<double> targetHeight_;

    // Final loop. The curve is shrunk onto the vertical line through the
    // base point (the "return" strand is swung out along `outward`), the
    // result is a "D" shape in the vertical plane through the base point,
    // which is rounded into a circle of radius ringRadius_ and finally laid
    // flat.
    Vec3 baseRel_;
    double outwardX_ = 1.0;
    double outwardY_ = 0.0;
    double ringRadius_ = 1.0;
    int returnSamples_ = 4;

    // Angle along the final circle of every sample: first as the samples
    // sit after the shrink phase, then evenly spread.
    QVector<double> alpha_;
    QVector<double> alphaUniform_;

    double requestedRadius_ = 0.16;
    double minClearance_ = 0.0;

    // height nodes (one pair per crossing)
    QVector<Node> nodes_;
    QVector<double> nodeInit_;
    QVector<int> sampleLow_;
    QVector<int> sampleHigh_;
    QVector<double> sampleWeight_;

    // per crossing
    QVector<double> crossingU_[2];
    QVector<int> crossingNode_[2];

    // crossings that need a change, in animation order
    QVector<int> flipOrder_;

    QVector<UnknottingEvent> events_;

    // timeline in seconds
    double alignEnd_ = 0.0;
    double layerEnd_ = 0.0;
    double changeEnd_ = 0.0;
    double settleEnd_ = 0.0;
    double shrinkEnd_ = 0.0;
    double untangleEnd_ = 0.0;
    double flattenEnd_ = 0.0;
    double slot_ = 1.0;

    Vec3 boundsMin_;
    Vec3 boundsMax_;
};

// Gives the tube rings a colour: `base`, blended towards `hot` by weight.
// weights has one entry per ring (point of the curve).
void colorMeshByHighlight(
    Mesh &mesh,
    const QColor &base,
    const QColor &hot,
    const QVector<float> &weights);

// Same with signed weights (UnknottingAnimation::curveAt): blended towards
// `over` where the weight is positive and towards `under` where it is
// negative.
void colorMeshByStrandHighlight(
    Mesh &mesh,
    const QColor &base,
    const QColor &over,
    const QColor &under,
    const QVector<float> &weights);

// Known unknotting number (-1 if not known).
int knownUnknottingNumber(
    Preset preset,
    int torusP = 0,
    int torusQ = 0);

} // namespace KnotMath

#endif // KNOTMATH_H
