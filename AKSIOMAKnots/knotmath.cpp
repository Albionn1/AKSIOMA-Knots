#include "knotmath.h"
#include "knottable.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace KnotMath
{

namespace
{

constexpr double Pi = 3.1415926535897932384626433832795028841971693993751;
constexpr double Epsilon = 1.0e-10;

/*
 * knotmath.cpp is not a QObject subclass, so QObject::tr() cannot be used.
 * QCoreApplication::translate() gives all KnotMath strings a stable
 * "KnotMath" context. lupdate finds the knotTr() calls through the
 * -tr-function-alias option in CMakeLists.txt.
 */
QString knotTr(const char *text)
{
    return QCoreApplication::translate("KnotMath", text);
}

bool finiteValue(double value)
{
    return std::isfinite(value);
}

Vec3 safeNormalize(const Vec3 &value, const Vec3 &fallback)
{
    const double length = value.length();

    if (length < Epsilon)
        return fallback;

    return value / length;
}

double coordinate(const Vec3 &v, int axis)
{
    if (axis == 0)
        return v.x;

    if (axis == 1)
        return v.y;

    return v.z;
}

Vec3 axisUnit(int axis)
{
    if (axis == 0)
        return Vec3(1.0, 0.0, 0.0);

    if (axis == 1)
        return Vec3(0.0, 1.0, 0.0);

    return Vec3(0.0, 0.0, 1.0);
}

/*
 * =============================================================
 * EXPRESSION PARSER
 * =============================================================
 *
 * Grammar (lowest to highest precedence):
 *
 *   expression := term (('+' | '-') term)*
 *   term       := unary (('*' | '/' | implicit) unary)*
 *   unary      := ('-' | '+') unary | power
 *   power      := primary ('^' unary)?          (right associative)
 *   primary    := number | constant | function '(' expression ')'
 *                 | '(' expression ')'
 *
 * Therefore -2^2 = -4 and 2^-1 = 0.5. Implicit multiplication
 * ("2t", "3pi", "2(t+1)", "sin(t)cos(t)") is supported.
 * Variable: t.  Constants: pi, e.
 */
struct ExprParser
{
    ExprParser(const QString &expression, double variableT)
        : source(expression), t(variableT)
    {
    }

    QString source;
    int position = 0;
    bool error = false;
    double t = 0.0;

    void skipSpaces()
    {
        while (position < source.size() && source.at(position).isSpace())
            ++position;
    }

    bool consume(QChar character)
    {
        skipSpaces();

        if (position < source.size() && source.at(position) == character)
        {
            ++position;
            return true;
        }

        return false;
    }

    QString parseIdentifier()
    {
        skipSpaces();

        const int start = position;

        // letters first; digits only after the first character
        while (position < source.size())
        {
            const QChar c = source.at(position);

            if (!c.isLetter() && c != '_' &&
                !(position > start && c.isDigit()))
            {
                break;
            }

            ++position;
        }

        return source.mid(start, position - start);
    }

    double parseNumber()
    {
        skipSpaces();

        const int start = position;
        bool hasDigits = false;
        bool hasExponent = false;

        while (position < source.size())
        {
            const QChar c = source.at(position);

            if (c.isDigit() || c == '.')
            {
                hasDigits = true;
                ++position;
                continue;
            }

            if ((c == 'e' || c == 'E') && !hasExponent && hasDigits)
            {
                // Only an exponent if followed by digits ("1e-3"),
                // otherwise it is the constant e ("2e", "2exp(t)").
                int look = position + 1;

                if (look < source.size() &&
                    (source.at(look) == '+' || source.at(look) == '-'))
                {
                    ++look;
                }

                if (look < source.size() && source.at(look).isDigit())
                {
                    hasExponent = true;
                    position = look;
                    continue;
                }
            }

            break;
        }

        if (!hasDigits)
        {
            error = true;
            return 0.0;
        }

        bool ok = false;

        const double value =
            source.mid(start, position - start).toDouble(&ok);

        if (!ok || !finiteValue(value))
        {
            error = true;
            return 0.0;
        }

        return value;
    }

    double applyFunction(const QString &name, double argument)
    {
        if (name == QStringLiteral("sin"))
            return std::sin(argument);

        if (name == QStringLiteral("cos"))
            return std::cos(argument);

        if (name == QStringLiteral("tan"))
            return std::tan(argument);

        if (name == QStringLiteral("asin"))
            return std::asin(argument);

        if (name == QStringLiteral("acos"))
            return std::acos(argument);

        if (name == QStringLiteral("atan"))
            return std::atan(argument);

        if (name == QStringLiteral("sinh"))
            return std::sinh(argument);

        if (name == QStringLiteral("cosh"))
            return std::cosh(argument);

        if (name == QStringLiteral("tanh"))
            return std::tanh(argument);

        if (name == QStringLiteral("abs"))
            return std::abs(argument);

        if (name == QStringLiteral("exp"))
            return std::exp(argument);

        if (name == QStringLiteral("floor"))
            return std::floor(argument);

        if (name == QStringLiteral("ceil"))
            return std::ceil(argument);

        if (name == QStringLiteral("sqrt"))
        {
            if (argument < 0.0)
            {
                error = true;
                return 0.0;
            }

            return std::sqrt(argument);
        }

        if (name == QStringLiteral("log") || name == QStringLiteral("ln"))
        {
            if (argument <= 0.0)
            {
                error = true;
                return 0.0;
            }

            return std::log(argument);
        }

        error = true;
        return 0.0;
    }

    double parsePrimary()
    {
        skipSpaces();

        if (position >= source.size())
        {
            error = true;
            return 0.0;
        }

        if (consume('('))
        {
            const double value = parseExpression();

            if (!consume(')'))
                error = true;

            return value;
        }

        const QChar c = source.at(position);

        if (c.isDigit() || c == '.')
            return parseNumber();

        if (c.isLetter() || c == '_')
        {
            const QString lower = parseIdentifier().toLower();

            if (lower == QStringLiteral("t"))
                return t;

            if (lower == QStringLiteral("pi"))
                return Pi;

            if (lower == QStringLiteral("e"))
                return std::exp(1.0);

            if (consume('('))
            {
                const double argument = parseExpression();

                if (!consume(')'))
                {
                    error = true;
                    return 0.0;
                }

                return applyFunction(lower, argument);
            }

            // unknown identifier (this includes p and q, which are not
            // variables of the curve language)
            error = true;
            return 0.0;
        }

        error = true;
        return 0.0;
    }

    double parseUnary()
    {
        if (consume('-'))
            return -parseUnary();

        if (consume('+'))
            return parseUnary();

        return parsePower();
    }

    double parsePower()
    {
        double base = parsePrimary();

        if (!error && consume('^'))
        {
            const double exponent = parseUnary();
            const double result = std::pow(base, exponent);

            if (!finiteValue(result))
                error = true;

            base = result;
        }

        return base;
    }

    bool implicitMultiplicationFollows()
    {
        skipSpaces();

        if (position >= source.size())
            return false;

        const QChar c = source.at(position);

        return c.isLetter() || c.isDigit() || c == '.' || c == '(' ||
               c == '_';
    }

    double parseTerm()
    {
        double value = parseUnary();

        while (!error)
        {
            if (consume('*'))
            {
                value *= parseUnary();
            }
            else if (consume('/'))
            {
                const double divisor = parseUnary();

                if (std::abs(divisor) < Epsilon)
                {
                    error = true;
                    return 0.0;
                }

                value /= divisor;
            }
            else if (implicitMultiplicationFollows())
            {
                value *= parsePower();
            }
            else
            {
                break;
            }
        }

        return value;
    }

    double parseExpression()
    {
        double value = parseTerm();

        while (!error)
        {
            if (consume('+'))
                value += parseTerm();
            else if (consume('-'))
                value -= parseTerm();
            else
                break;
        }

        return value;
    }

    double evaluate()
    {
        const double result = parseExpression();

        skipSpaces();

        if (position != source.size())
            error = true;

        if (!finiteValue(result))
            error = true;

        return result;
    }
};

double evaluateExpression(const QString &expression, double t, bool &ok)
{
    ExprParser parser(expression, t);

    const double value = parser.evaluate();

    ok = !parser.error && finiteValue(value);

    return value;
}

QString normalizeFormula(const QString &formula)
{
    QString result = formula.trimmed();

    if (result.startsWith('='))
        result.remove(0, 1);

    return result.trimmed();
}

/*
 * =============================================================
 * POLYNOMIALS
 * =============================================================
 */

QString superscriptNumber(int value)
{
    static const QString digits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");

    QString result;

    if (value < 0)
    {
        result += QStringLiteral("⁻");
        value = -value;
    }

    const QString text = QString::number(value);

    for (const QChar c : text)
        result += digits.at(c.digitValue());

    return result;
}

QString formatLaurentPolynomial(const std::map<int, long long> &coefficients)
{
    QString result;
    bool firstTerm = true;

    for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it)
    {
        const int power = it->first;
        const long long coefficient = it->second;

        if (coefficient == 0)
            continue;

        const bool negative = coefficient < 0;
        const long long absolute = std::llabs(coefficient);

        if (!firstTerm)
            result += negative ? QStringLiteral(" − ") : QStringLiteral(" + ");
        else if (negative)
            result += QStringLiteral("−");

        if (absolute != 1 || power == 0)
            result += QString::number(absolute);

        if (power != 0)
        {
            result += QStringLiteral("t");

            if (power != 1)
                result += superscriptNumber(power);
        }

        firstTerm = false;
    }

    if (firstTerm)
        return QStringLiteral("0");

    return result;
}

// Exact division of integer polynomials (coefficient index = power).
// The divisor must be monic. The remainder is expected to be zero.
std::vector<long long> polynomialQuotient(
    std::vector<long long> numerator,
    const std::vector<long long> &denominator)
{
    const int degreeNumerator = static_cast<int>(numerator.size()) - 1;
    const int degreeDenominator = static_cast<int>(denominator.size()) - 1;

    if (degreeNumerator < degreeDenominator || degreeDenominator < 0)
        return std::vector<long long>{0};

    std::vector<long long> quotient(
        static_cast<size_t>(degreeNumerator - degreeDenominator + 1),
        0);

    for (int i = degreeNumerator - degreeDenominator; i >= 0; --i)
    {
        const long long c = numerator[static_cast<size_t>(i + degreeDenominator)];

        quotient[static_cast<size_t>(i)] = c;

        if (c == 0)
            continue;

        for (int j = 0; j <= degreeDenominator; ++j)
            numerator[static_cast<size_t>(i + j)] -=
                c * denominator[static_cast<size_t>(j)];
    }

    return quotient;
}

/*
 * =============================================================
 * GEOMETRY HELPERS
 * =============================================================
 */

Vec3 derivativeAt(const QVector<Vec3> &curve, int index, bool closed)
{
    const int count = curve.size();

    if (count < 2)
        return Vec3();

    if (closed)
    {
        const int previous = (index - 1 + count) % count;
        const int next = (index + 1) % count;

        return (curve.at(next) - curve.at(previous)) * 0.5;
    }

    if (index == 0)
        return curve.at(1) - curve.at(0);

    if (index == count - 1)
        return curve.at(count - 1) - curve.at(count - 2);

    return (curve.at(index + 1) - curve.at(index - 1)) * 0.5;
}

QVector<Vec3> resampleByArcLength(
    const QVector<Vec3> &input,
    int targetCount,
    bool closed)
{
    if (input.size() < 3 || targetCount < 3)
        return input;

    QVector<Vec3> work = input;

    if (closed && (work.first() - work.last()).length() < 1.0e-7)
        work.removeLast();

    const int n = work.size();

    if (n < 3)
        return work;

    QVector<double> cumulative(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
    {
        const int j = closed ? (i + 1) % n : i + 1;

        if (j >= n)
        {
            cumulative[i + 1] = cumulative[i];
            continue;
        }

        cumulative[i + 1] = cumulative[i] + (work.at(j) - work.at(i)).length();
    }

    const double totalLength = cumulative.last();

    if (totalLength < Epsilon)
        return work;

    const int outputCount = closed ? targetCount : std::max(2, targetCount);

    QVector<Vec3> output;
    output.reserve(outputCount);

    for (int sample = 0; sample < outputCount; ++sample)
    {
        const double normalized =
            closed
                ? static_cast<double>(sample) / static_cast<double>(outputCount)
                : static_cast<double>(sample) /
                      static_cast<double>(outputCount - 1);

        const double targetDistance = normalized * totalLength;

        auto upper = std::lower_bound(
            cumulative.constBegin(),
            cumulative.constEnd(),
            targetDistance);

        int segment = static_cast<int>(
            std::distance(cumulative.constBegin(), upper));

        segment = std::max(1, std::min(segment, n));

        const int i0 = segment - 1;

        const int i1 =
            closed ? segment % n : std::min(segment, n - 1);

        const double segmentStart = cumulative.at(i0);
        const double segmentLength = cumulative.at(segment) - segmentStart;

        const double localAlpha =
            segmentLength > Epsilon
                ? (targetDistance - segmentStart) / segmentLength
                : 0.0;

        output.push_back(
            work.at(i0) * (1.0 - localAlpha) + work.at(i1) * localAlpha);
    }

    return output;
}

/*
 * Signed solid angle subtended by two straight segments
 * (p1,p2) and (p3,p4) — Klenin & Langowski, Biopolymers 54 (2000).
 *
 *   writhe  = 1/(2*pi) * sum over unordered pairs i<j of Omega_ij
 *   linking = 1/(4*pi) * sum over pairs (i in A, j in B) of Omega_ij
 */
double segmentPairSolidAngle(
    const Vec3 &p1,
    const Vec3 &p2,
    const Vec3 &p3,
    const Vec3 &p4)
{
    const Vec3 r13 = p3 - p1;
    const Vec3 r14 = p4 - p1;
    const Vec3 r23 = p3 - p2;
    const Vec3 r24 = p4 - p2;
    const Vec3 r12 = p2 - p1;
    const Vec3 r34 = p4 - p3;

    const Vec3 c1 = Vec3::cross(r13, r14);
    const Vec3 c2 = Vec3::cross(r14, r24);
    const Vec3 c3 = Vec3::cross(r24, r23);
    const Vec3 c4 = Vec3::cross(r23, r13);

    const double l1 = c1.length();
    const double l2 = c2.length();
    const double l3 = c3.length();
    const double l4 = c4.length();

    constexpr double tiny = 1.0e-14;

    if (l1 < tiny || l2 < tiny || l3 < tiny || l4 < tiny)
        return 0.0;

    const Vec3 n1 = c1 / l1;
    const Vec3 n2 = c2 / l2;
    const Vec3 n3 = c3 / l3;
    const Vec3 n4 = c4 / l4;

    auto safeAsin = [](double value)
    {
        return std::asin(std::max(-1.0, std::min(1.0, value)));
    };

    const double omega =
        safeAsin(Vec3::dot(n1, n2)) +
        safeAsin(Vec3::dot(n2, n3)) +
        safeAsin(Vec3::dot(n3, n4)) +
        safeAsin(Vec3::dot(n4, n1));

    const double orientation =
        Vec3::dot(Vec3::cross(r34, r12), r13);

    if (orientation > 0.0)
        return omega;

    if (orientation < 0.0)
        return -omega;

    return 0.0;
}

bool segmentsIntersect2D(
    const QPointF &a,
    const QPointF &b,
    const QPointF &c,
    const QPointF &d,
    double &ta,
    double &tb)
{
    const double rX = b.x() - a.x();
    const double rY = b.y() - a.y();
    const double sX = d.x() - c.x();
    const double sY = d.y() - c.y();

    const double denominator = rX * sY - rY * sX;

    if (std::abs(denominator) < 1.0e-12)
        return false;

    const double qpx = c.x() - a.x();
    const double qpy = c.y() - a.y();

    ta = (qpx * sY - qpy * sX) / denominator;
    tb = (qpx * rY - qpy * rX) / denominator;

    /*
     * Half-open intervals [0,1): a crossing that lies exactly on a vertex
     * of the polyline (very common for symmetric curves) belongs to exactly
     * one of the two segments that meet there (open intervals would miss
     * it on both).
     */
    constexpr double tolerance = 1.0e-9;

    if (ta >= -tolerance && ta < 1.0 - tolerance &&
        tb >= -tolerance && tb < 1.0 - tolerance)
    {
        ta = std::max(ta, 0.0);
        tb = std::max(tb, 0.0);

        return true;
    }

    return false;
}

QVector<CrossingInfo> detectProjectionCrossingsInternal(
    const QVector<Vec3> &curve,
    int projectionAxis,
    int maxSamples)
{
    QVector<CrossingInfo> crossings;

    if (curve.size() < 4)
        return crossings;

    projectionAxis = std::clamp(projectionAxis, 0, 2);

    QVector<Vec3> sampled;

    if (maxSamples > 0 && curve.size() > maxSamples)
        sampled = resampleByArcLength(curve, std::max(32, maxSamples), true);
    else
        sampled = curve;

    if (sampled.size() > 3 &&
        (sampled.first() - sampled.last()).length() < 1.0e-9)
    {
        sampled.removeLast();
    }

    const int n = sampled.size();

    if (n < 4)
        return crossings;

    QVector<QPointF> projected;
    projected.reserve(n);

    for (const Vec3 &point : sampled)
    {
        if (projectionAxis == 2)
            projected.push_back(QPointF(point.x, point.y));
        else if (projectionAxis == 1)
            projected.push_back(QPointF(point.x, point.z));
        else
            projected.push_back(QPointF(point.y, point.z));
    }

    double maximumExtent = 0.0;

    for (const Vec3 &point : sampled)
        maximumExtent = std::max(maximumExtent, point.length());

    const double depthTolerance =
        std::max(1.0e-8, maximumExtent * 1.0e-5);

    const Vec3 viewAxis = axisUnit(projectionAxis);

    for (int i = 0; i < n; ++i)
    {
        const int iNext = (i + 1) % n;

        const QPointF &a = projected.at(i);
        const QPointF &b = projected.at(iNext);

        const double minAX = std::min(a.x(), b.x());
        const double maxAX = std::max(a.x(), b.x());
        const double minAY = std::min(a.y(), b.y());
        const double maxAY = std::max(a.y(), b.y());

        for (int j = i + 2; j < n; ++j)
        {
            if (i == 0 && j == n - 1)
                continue; // adjacent through the closing segment

            const int jNext = (j + 1) % n;

            const QPointF &c = projected.at(j);
            const QPointF &d = projected.at(jNext);

            // cheap bounding-box rejection
            if (std::max(c.x(), d.x()) < minAX ||
                std::min(c.x(), d.x()) > maxAX ||
                std::max(c.y(), d.y()) < minAY ||
                std::min(c.y(), d.y()) > maxAY)
            {
                continue;
            }

            double ta = 0.0;
            double tb = 0.0;

            if (!segmentsIntersect2D(a, b, c, d, ta, tb))
                continue;

            const Vec3 pointA =
                sampled.at(i) * (1.0 - ta) + sampled.at(iNext) * ta;

            const Vec3 pointB =
                sampled.at(j) * (1.0 - tb) + sampled.at(jNext) * tb;

            const double depthA = coordinate(pointA, projectionAxis);
            const double depthB = coordinate(pointB, projectionAxis);

            if (std::abs(depthA - depthB) <= depthTolerance)
                continue;

            CrossingInfo crossing;

            crossing.segmentA = i;
            crossing.segmentB = j;
            crossing.parameterA = ta;
            crossing.parameterB = tb;

            crossing.fractionA =
                (static_cast<double>(i) + ta) / static_cast<double>(n);

            crossing.fractionB =
                (static_cast<double>(j) + tb) / static_cast<double>(n);

            crossing.depthA = depthA;
            crossing.depthB = depthB;
            crossing.pointA = pointA;
            crossing.pointB = pointB;
            crossing.midpoint = (pointA + pointB) * 0.5;

            const Vec3 tangentA = sampled.at(iNext) - sampled.at(i);
            const Vec3 tangentB = sampled.at(jNext) - sampled.at(j);

            crossing.aIsOver = depthA > depthB;

            const Vec3 orientation =
                crossing.aIsOver
                    ? Vec3::cross(tangentA, tangentB)
                    : Vec3::cross(tangentB, tangentA);

            crossing.sign =
                Vec3::dot(orientation, viewAxis) > 0.0 ? 1 : -1;

            crossings.push_back(crossing);
        }
    }

    std::sort(
        crossings.begin(),
        crossings.end(),
        [](const CrossingInfo &a, const CrossingInfo &b)
        {
            return a.fractionA < b.fractionA;
        });

    for (int i = 0; i < crossings.size(); ++i)
        crossings[i].number = i + 1;

    return crossings;
}

/*
 * =============================================================
 * BRAID CLOSURE CURVES
 * =============================================================
 *
 * The strands of a braid with `strands` strands run around a circle
 * (core radius `centerRadius`). Every strand occupies a radial slot.
 * A generator s_i swaps slots i and i+1 by rotating both strands by
 * 180 degrees (smoothly) about their midpoint in the (radius, z)
 * plane; the sign of the generator decides which strand goes over.
 * The closure (strand leaves lap k in slot s, enters lap k+1 in slot s)
 * is followed until the curve closes. Works for braids whose closure
 * is a knot (single component).
 */
QVector<Vec3> braidClosureCurve(
    const std::vector<int> &word,
    int strands,
    int samplesPerGenerator,
    double centerRadius,
    double spacing)
{
    QVector<Vec3> points;

    const int length = static_cast<int>(word.size());

    if (length == 0 || strands < 2)
        return points;

    points.reserve(strands * length * samplesPerGenerator);

    int slot = 0;

    for (int lap = 0; lap < strands; ++lap)
    {
        for (int k = 0; k < length; ++k)
        {
            const int generator = word[static_cast<size_t>(k)];
            const int i = std::abs(generator) - 1;
            const double handed = generator > 0 ? 1.0 : -1.0;

            for (int m = 0; m < samplesPerGenerator; ++m)
            {
                const double u =
                    static_cast<double>(m) /
                    static_cast<double>(samplesPerGenerator);

                const double theta =
                    2.0 * Pi * (static_cast<double>(k) + u) /
                    static_cast<double>(length);

                const double smooth = u * u * (3.0 - 2.0 * u);
                const double phi = Pi * smooth;

                const double middle =
                    centerRadius +
                    (static_cast<double>(i) + 0.5 -
                     0.5 * static_cast<double>(strands - 1)) *
                        spacing;

                const double half = 0.5 * spacing;

                double radius;
                double z;

                if (slot == i)
                {
                    radius = middle - half * std::cos(phi);
                    z = handed * half * std::sin(phi);
                }
                else if (slot == i + 1)
                {
                    radius = middle + half * std::cos(phi);
                    z = -handed * half * std::sin(phi);
                }
                else
                {
                    radius =
                        centerRadius +
                        (static_cast<double>(slot) -
                         0.5 * static_cast<double>(strands - 1)) *
                            spacing;

                    z = 0.0;
                }

                points.push_back(Vec3(
                    radius * std::cos(theta),
                    radius * std::sin(theta),
                    z));
            }

            if (slot == i)
                slot = i + 1;
            else if (slot == i + 1)
                slot = i;
        }
    }

    return points;
}

// Periodic Gaussian smoothing of a closed polyline (sigma in samples).
QVector<Vec3> smoothClosedCurve(const QVector<Vec3> &points, double sigma)
{
    const int n = points.size();

    if (n < 3 || sigma <= 0.0)
        return points;

    const int reach = static_cast<int>(std::ceil(3.0 * sigma));

    std::vector<double> weights(static_cast<size_t>(2 * reach + 1));
    double total = 0.0;

    for (int k = -reach; k <= reach; ++k)
    {
        const double w = std::exp(-0.5 * k * k / (sigma * sigma));

        weights[static_cast<size_t>(k + reach)] = w;
        total += w;
    }

    QVector<Vec3> smoothed(n);

    for (int i = 0; i < n; ++i)
    {
        Vec3 sum;

        for (int k = -reach; k <= reach; ++k)
            sum += points.at(((i + k) % n + n) % n) * weights[static_cast<size_t>(k + reach)];

        smoothed[i] = sum / total;
    }

    return smoothed;
}

QVector<Vec3> circlePoints(
    const Vec3 &center,
    const Vec3 &u,
    const Vec3 &v,
    double radius,
    int count)
{
    QVector<Vec3> points;
    points.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        const double a = 2.0 * Pi * static_cast<double>(i) /
                         static_cast<double>(count);

        points.push_back(
            center + u * (radius * std::cos(a)) + v * (radius * std::sin(a)));
    }

    return points;
}

QString signedInteger(int value)
{
    return value > 0 ? QStringLiteral("+%1").arg(value)
                     : QString::number(value);
}

} // namespace

/*
 * =============================================================
 * Vec3
 * =============================================================
 */

Vec3 Vec3::operator+(const Vec3 &other) const
{
    return Vec3(x + other.x, y + other.y, z + other.z);
}

Vec3 Vec3::operator-(const Vec3 &other) const
{
    return Vec3(x - other.x, y - other.y, z - other.z);
}

Vec3 Vec3::operator*(double scalar) const
{
    return Vec3(x * scalar, y * scalar, z * scalar);
}

Vec3 Vec3::operator/(double scalar) const
{
    if (std::abs(scalar) < Epsilon)
        return Vec3();

    return Vec3(x / scalar, y / scalar, z / scalar);
}

Vec3 &Vec3::operator+=(const Vec3 &other)
{
    x += other.x;
    y += other.y;
    z += other.z;

    return *this;
}

Vec3 &Vec3::operator-=(const Vec3 &other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;

    return *this;
}

Vec3 &Vec3::operator*=(double scalar)
{
    x *= scalar;
    y *= scalar;
    z *= scalar;

    return *this;
}

Vec3 &Vec3::operator/=(double scalar)
{
    if (std::abs(scalar) >= Epsilon)
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
    }

    return *this;
}

double Vec3::length() const
{
    return std::sqrt(x * x + y * y + z * z);
}

double Vec3::lengthSquared() const
{
    return x * x + y * y + z * z;
}

Vec3 Vec3::normalized() const
{
    return safeNormalize(*this, Vec3());
}

double Vec3::dot(const Vec3 &a, const Vec3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Vec3::cross(const Vec3 &a, const Vec3 &b)
{
    return Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

/*
 * =============================================================
 * Mesh
 * =============================================================
 */

bool Mesh::isValid() const
{
    return !positions.isEmpty() &&
           !normals.isEmpty() &&
           !indices.isEmpty() &&
           positions.size() % 3 == 0 &&
           normals.size() == positions.size() &&
           indices.size() % 3 == 0;
}

void Mesh::clear()
{
    positions.clear();
    normals.clear();
    colors.clear();
    indices.clear();
    texcoords.clear();
    occlusion.clear();
    sheet.clear();
    centerline.clear();
    centerlineStrand.clear();
    centerlineArc.clear();
    strandLength.clear();
    strandClosed.clear();
    vertexSample.clear();

    ringCount = 0;
    ringSegments = 0;
}

void Mesh::fillColor(const QColor &color)
{
    const int vertexCount = positions.size() / 3;

    colors.clear();
    colors.reserve(vertexCount * 3);

    for (int i = 0; i < vertexCount; ++i)
    {
        colors.push_back(static_cast<float>(color.redF()));
        colors.push_back(static_cast<float>(color.greenF()));
        colors.push_back(static_cast<float>(color.blueF()));
    }
}

void Mesh::append(const Mesh &other)
{
    if (other.positions.isEmpty())
        return;

    const int existingVertices = positions.size() / 3;
    const int otherVertices = other.positions.size() / 3;

    const bool anyColors =
        !colors.isEmpty() || !other.colors.isEmpty();

    if (anyColors)
    {
        if (colors.size() != existingVertices * 3)
            colors.fill(1.0f, existingVertices * 3);
    }

    // per-vertex extras: keep them aligned with the vertices
    auto appendPerVertex = [&](QVector<float> &mine,
                               const QVector<float> &theirs,
                               int width,
                               float fill)
    {
        if (mine.isEmpty() && theirs.isEmpty())
            return;

        if (mine.size() != existingVertices * width)
            mine.fill(fill, existingVertices * width);

        if (theirs.size() == otherVertices * width)
            mine += theirs;
        else
            mine += QVector<float>(otherVertices * width, fill);
    };

    appendPerVertex(texcoords, other.texcoords, 4, 0.0f);
    appendPerVertex(occlusion, other.occlusion, 1, 1.0f);
    appendPerVertex(sheet, other.sheet, 1, 0.0f);

    // centre lines: sample and strand numbers continue after ours
    const int existingSamples = centerline.size() / 4;
    const int existingStrands = strandLength.size();

    if (vertexSample.size() == existingVertices &&
        other.vertexSample.size() == otherVertices)
    {
        centerline += other.centerline;
        centerlineArc += other.centerlineArc;
        strandLength += other.strandLength;
        strandClosed += other.strandClosed;

        for (const int strand : other.centerlineStrand)
            centerlineStrand.push_back(strand + existingStrands);

        for (const int sample : other.vertexSample)
            vertexSample.push_back(sample + existingSamples);
    }
    else
    {
        centerline.clear();
        centerlineArc.clear();
        centerlineStrand.clear();
        strandLength.clear();
        strandClosed.clear();
        vertexSample.clear();
    }

    positions += other.positions;
    normals += other.normals;

    if (anyColors)
    {
        if (other.colors.size() == otherVertices * 3)
            colors += other.colors;
        else
        {
            QVector<float> white(otherVertices * 3, 1.0f);
            colors += white;
        }
    }

    indices.reserve(indices.size() + other.indices.size());

    for (const unsigned int index : other.indices)
        indices.push_back(index + static_cast<unsigned int>(existingVertices));

    ringCount += other.ringCount;
    ringSegments = std::max(ringSegments, other.ringSegments);
}

/*
 * =============================================================
 * CURVES
 * =============================================================
 */

QVector<Vec3> sampleParametricCurve(
    const ScalarFunction &fx,
    const ScalarFunction &fy,
    const ScalarFunction &fz,
    int samples,
    double tMin,
    double tMax)
{
    const int count = std::max(8, samples);

    QVector<Vec3> points;
    points.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        const double alpha =
            static_cast<double>(i) / static_cast<double>(count);

        const double t = tMin + (tMax - tMin) * alpha;

        points.push_back(Vec3(fx(t), fy(t), fz(t)));
    }

    return points;
}

QVector<Vec3> resampleClosedCurve(
    const QVector<Vec3> &input,
    int targetCount,
    bool closed)
{
    return resampleByArcLength(input, targetCount, closed);
}

QVector<Vec3> refineCurve(
    const QVector<Vec3> &curve,
    int factor,
    bool closed)
{
    const int n = curve.size();

    if (factor <= 1 || n < 3)
        return curve;

    const int segments = closed ? n : n - 1;

    auto point = [&](int i)
    {
        if (closed)
            return curve.at(((i % n) + n) % n);

        return curve.at(std::clamp(i, 0, n - 1));
    };

    QVector<Vec3> refined;
    refined.reserve(segments * factor + 1);

    for (int i = 0; i < segments; ++i)
    {
        const Vec3 p0 = point(i - 1);
        const Vec3 p1 = point(i);
        const Vec3 p2 = point(i + 1);
        const Vec3 p3 = point(i + 2);

        // centripetal parametrisation: no loops or cusps on uneven spacing
        auto knot = [](const Vec3 &a, const Vec3 &b)
        {
            return std::max(std::sqrt((b - a).length()), 1.0e-6);
        };

        const double t1 = knot(p0, p1);
        const double t2 = t1 + knot(p1, p2);
        const double t3 = t2 + knot(p2, p3);

        for (int k = 0; k < factor; ++k)
        {
            const double t =
                t1 + (t2 - t1) * static_cast<double>(k) / static_cast<double>(factor);

            const Vec3 a1 = p0 * ((t1 - t) / t1) + p1 * (t / t1);
            const Vec3 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
            const Vec3 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));

            const Vec3 b1 = a1 * ((t2 - t) / t2) + a2 * (t / t2);
            const Vec3 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));

            refined.push_back(b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1)));
        }
    }

    if (!closed)
        refined.push_back(curve.last());

    return refined;
}

QVector<float> refineValues(
    const QVector<float> &values,
    int factor,
    bool closed)
{
    const int n = values.size();

    if (factor <= 1 || n < 2)
        return values;

    const int segments = closed ? n : n - 1;

    QVector<float> refined;
    refined.reserve(segments * factor + 1);

    for (int i = 0; i < segments; ++i)
    {
        const float a = values.at(i);
        const float b = values.at((i + 1) % n);

        for (int k = 0; k < factor; ++k)
        {
            const float x =
                static_cast<float>(k) / static_cast<float>(factor);

            refined.push_back(a + (b - a) * x);
        }
    }

    if (!closed)
        refined.push_back(values.last());

    return refined;
}

int refineFactorForTube(
    const QVector<Vec3> &curve,
    double radius,
    bool closed)
{
    const int n = curve.size();

    if (n < 3 || radius <= 0.0)
        return 1;

    double length = 0.0;

    for (int i = 0; i + 1 < n; ++i)
        length += (curve.at(i + 1) - curve.at(i)).length();

    if (closed)
        length += (curve.first() - curve.last()).length();

    const double segment = length / static_cast<double>(closed ? n : n - 1);

    const int wanted =
        static_cast<int>(std::ceil(segment / (0.6 * radius)));

    const int budget = std::max(1, 2400 / n);

    return std::clamp(wanted, 1, std::min(8, budget));
}

namespace
{

// (p,q) curve on the standard torus, turned by `tubeOffset` around the tube.
QVector<Vec3> torusCurve(int p, int q, int samples, double tubeOffset)
{
    p = std::max(1, p);
    q = std::max(1, q);

    const double majorRadius = 2.0;
    const double minorRadius = 0.72;

    const double dp = static_cast<double>(p);
    const double dq = static_cast<double>(q);

    return sampleParametricCurve(
        [=](double t)
        {
            return (majorRadius + minorRadius * std::cos(dq * t + tubeOffset)) *
                   std::cos(dp * t);
        },
        [=](double t)
        {
            return (majorRadius + minorRadius * std::cos(dq * t + tubeOffset)) *
                   std::sin(dp * t);
        },
        [=](double t)
        {
            return minorRadius * std::sin(dq * t + tubeOffset);
        },
        samples);
}

} // namespace

QVector<Vec3> torusKnotCurve(int p, int q, int samples)
{
    return torusCurve(p, q, samples, 0.0);
}

QVector<Vec3> unknotCurve(int samples)
{
    return sampleParametricCurve(
        [](double t) { return 2.0 * std::cos(t); },
        [](double t) { return 2.0 * std::sin(t); },
        [](double) { return 0.0; },
        samples);
}

QVector<Vec3> trefoilCurve(int samples)
{
    return torusKnotCurve(3, 2, samples);
}

QVector<Vec3> figureEightCurve(int samples)
{
    return sampleParametricCurve(
        [](double t)
        {
            return (2.0 + std::cos(2.0 * t)) * std::cos(3.0 * t);
        },
        [](double t)
        {
            return (2.0 + std::cos(2.0 * t)) * std::sin(3.0 * t);
        },
        [](double t) { return std::sin(4.0 * t); },
        samples);
}

QVector<Vec3> cinquefoilCurve(int samples)
{
    return torusKnotCurve(5, 2, samples);
}

QVector<Vec3> stevedoreCurve(int samples)
{
    // 4-braid s1 s1 s2 s1^-1 s3^-1 s2 s3^-1  (closure = stevedore knot 6_1)
    static const std::vector<int> word = {1, 1, 2, -1, -3, 2, -3};

    // Odd number of samples per generator: avoids sample points of different
    // strands coinciding exactly in the projection at the crossing moment.
    int perGenerator = std::max(33, samples / (4 * 7));

    if (perGenerator % 2 == 0)
        ++perGenerator;

    // Every swap of two strands is squeezed into the middle of its section,
    // which bends the inner strands sharply. A wide core, slightly wider
    // spacing and light smoothing keep the bends gentle and the strands at
    // least 0.43 apart; the knot is unchanged.
    return smoothClosedCurve(
        braidClosureCurve(word, 4, perGenerator, 2.08, 0.56),
        0.15 * perGenerator);
}

CurveData buildDNAStructure(int samples)
{
    CurveData result;

    result.multiStrand = true;
    result.closed = true;
    result.name = knotTr("DNA Double Helix Structure");
    result.classification = knotTr("DNA Double Helix with base pairs");
    result.polynomial = QStringLiteral("Complex biological structure");
    result.exactBuiltIn = true;

    /*
     * Closed circular DNA: two strands wind around a circular core
     * (radius coreRadius) and make `twists` full turns around each other,
     * so their Gauss linking number is |Lk| = twists.
     */
    constexpr int twists = 10;
    constexpr double coreRadius = 2.2;
    constexpr double helixRadius = 0.5;

    const int strandSamples = std::max(samples, 160);

    QVector<Vec3> strands[2];

    for (int k = 0; k < 2; ++k)
    {
        strands[k].reserve(strandSamples);

        for (int i = 0; i < strandSamples; ++i)
        {
            const double theta =
                2.0 * Pi * static_cast<double>(i) /
                static_cast<double>(strandSamples);

            const double a =
                -static_cast<double>(twists) * theta +
                static_cast<double>(k) * Pi;

            const Vec3 radial(std::cos(theta), std::sin(theta), 0.0);
            const Vec3 up(0.0, 0.0, 1.0);

            strands[k].push_back(
                radial * coreRadius +
                radial * (helixRadius * std::cos(a)) +
                up * (helixRadius * std::sin(a)));
        }

        result.separateStrands.push_back(strands[k]);
        result.strandClosed.push_back(true);
        result.strandRadiusScale.push_back(1.0);
    }

    // base-pair rungs
    const int rungStep = std::max(2, strandSamples / (twists * 5));

    for (int i = 0; i < strandSamples; i += rungStep)
    {
        QVector<Vec3> rung;

        rung.push_back(strands[0].at(i));
        rung.push_back((strands[0].at(i) + strands[1].at(i)) * 0.5);
        rung.push_back(strands[1].at(i));

        result.separateStrands.push_back(rung);
        result.strandClosed.push_back(false);
        result.strandRadiusScale.push_back(0.4);
    }

    result.points = strands[0];

    return result;
}

CurveData buildArmorStructure(int samples)
{
    CurveData result;

    result.multiStrand = true;
    result.closed = true;
    result.name = knotTr("Celtic Armor Structure");
    result.classification = knotTr("Interwoven Celtic armor pattern");
    result.polynomial = QStringLiteral("Traditional protective knotwork");
    result.exactBuiltIn = true;

    /*
     * Closed chain of four rings ("chain mail"). Even rings lie flat,
     * odd rings stand upright; neighbouring rings pass through each
     * other (linking number +-1), opposite rings are unlinked.
     *
     * Linking the neighbours needs ringRadius > chainRadius, so two flat
     * rings in the same plane would cut through each other. The flat
     * rings are therefore lifted to +evenLift / -evenLift. These values
     * (found numerically) keep the centre lines at least 0.54 apart, so
     * tubes up to radius 0.27 do not touch.
     */
    constexpr int ringCount = 4;
    constexpr double chainRadius = 0.91;
    constexpr double ringRadius = 1.52;
    constexpr double evenLift = 0.91;

    const int ringSamples = std::max(64, samples / 2);

    for (int k = 0; k < ringCount; ++k)
    {
        const double phi =
            2.0 * Pi * static_cast<double>(k) /
            static_cast<double>(ringCount);

        const Vec3 radial(std::cos(phi), std::sin(phi), 0.0);
        const Vec3 tangent(-std::sin(phi), std::cos(phi), 0.0);
        const Vec3 up(0.0, 0.0, 1.0);

        const double lift =
            (k % 2 != 0) ? 0.0 : (k == 0 ? evenLift : -evenLift);

        const Vec3 center = radial * chainRadius + up * lift;

        result.separateStrands.push_back(circlePoints(
            center,
            tangent,
            (k % 2 == 0) ? radial : up,
            ringRadius,
            ringSamples));

        result.strandClosed.push_back(true);
        result.strandRadiusScale.push_back(1.0);
    }

    result.points = result.separateStrands.first();

    return result;
}

/*
 * =============================================================
 * PRESETS
 * =============================================================
 */

int knownCrossingNumber(Preset preset, int torusP, int torusQ)
{
    switch (preset)
    {
    case Preset::Unknot:
        return 0;

    case Preset::Trefoil:
        return 3;

    case Preset::FigureEight:
        return 4;

    case Preset::Cinquefoil:
        return 5;

    case Preset::Stevedore:
        return 6;

    case Preset::CustomTorus:
        if (torusP >= 1 && torusQ >= 1 && std::gcd(torusP, torusQ) == 1)
        {
            if (torusP == 1 || torusQ == 1)
                return 0;

            return std::min(torusP * (torusQ - 1), torusQ * (torusP - 1));
        }

        return -1;

    default:
        break;
    }

    return -1;
}

CurveData buildPreset(
    Preset preset,
    int samples,
    int torusP,
    int torusQ,
    const QString &customText)
{
    CurveData result;

    samples = std::max(32, samples);

    // Curves are generated finely and then resampled at uniform arc length,
    // which gives evenly spaced tube rings (and consistent crossing data).
    const int fine = std::max(samples * 2, 600);

    switch (preset)
    {
    case Preset::Unknot:
        result.points = unknotCurve(fine);
        result.name = knotTr("Unknot");
        result.classification = knotTr("Unknot / trivial knot (0_1)");
        result.polynomial = QStringLiteral("1");
        result.exactBuiltIn = true;
        break;

    case Preset::Trefoil:
        result.points = trefoilCurve(fine);
        result.name = knotTr("Trefoil Knot");
        result.classification = knotTr("Trefoil Knot (3_1) / T(3,2)");
        result.polynomial = alexanderPolynomialForTorusKnot(3, 2);
        result.exactBuiltIn = true;
        break;

    case Preset::FigureEight:
        result.points = figureEightCurve(fine);
        result.name = knotTr("Figure-Eight Knot");
        result.classification = knotTr("Figure-Eight Knot (4_1)");
        result.polynomial = QStringLiteral("−t + 3 − t⁻¹");
        result.exactBuiltIn = true;
        break;

    case Preset::Cinquefoil:
        result.points = cinquefoilCurve(fine);
        result.name = knotTr("Cinquefoil / Solomon's Seal");
        result.classification = knotTr("Cinquefoil Knot (5_1) / T(5,2)");
        result.polynomial = alexanderPolynomialForTorusKnot(5, 2);
        result.exactBuiltIn = true;
        break;

    case Preset::Stevedore:
        result.points = stevedoreCurve(fine);
        result.name = knotTr("Stevedore Knot");
        result.classification = knotTr("Stevedore Knot (6_1)");
        result.polynomial = QStringLiteral("−2t + 5 − 2t⁻¹");
        result.exactBuiltIn = true;
        break;

    case Preset::DNAHelix:
        result = buildDNAStructure(samples);
        result.knownCrossingNumber = -1;
        return result;

    case Preset::CelticKnot:
        result = buildArmorStructure(samples);
        result.knownCrossingNumber = -1;
        return result;

    case Preset::CustomTorus:
    {
        torusP = std::max(1, torusP);
        torusQ = std::max(1, torusQ);

        result.name = knotTr("Custom Torus Knot");

        const int common = std::gcd(torusP, torusQ);

        if (common == 1)
        {
            result.points = torusKnotCurve(torusP, torusQ, fine);

            result.classification =
                knotTr("Torus knot T(%1,%2)").arg(torusP).arg(torusQ);
        }
        else
        {
            /*
             * T(p,q) with d = gcd(p,q) > 1 is a link of d parallel copies of
             * T(p/d, q/d). (A single parametrisation would run d times over
             * the same curve.) The copies are spread evenly around the tube;
             * every pair has linking number (p/d)(q/d).
             */
            const int reducedP = torusP / common;
            const int reducedQ = torusQ / common;

            result.multiStrand = true;
            result.name = knotTr("Custom Torus Link");

            for (int k = 0; k < common; ++k)
            {
                const double offset =
                    2.0 * Pi * static_cast<double>(k) /
                    static_cast<double>(common * reducedP);

                result.separateStrands.push_back(resampleByArcLength(
                    torusCurve(reducedP, reducedQ, fine, offset),
                    samples,
                    true));

                result.strandClosed.push_back(true);
                result.strandRadiusScale.push_back(1.0);
            }

            result.points = result.separateStrands.first();

            result.classification =
                knotTr("Torus link T(%1,%2) (%3 components)")
                    .arg(torusP)
                    .arg(torusQ)
                    .arg(common);
        }

        result.polynomial =
            common == 1
                ? alexanderPolynomialForTorusKnot(torusP, torusQ)
                : knotTr("Multicomponent torus link");

        result.exactBuiltIn = true;
        break;
    }

    case Preset::CustomCoordinates:
    {
        QString error;
        QVector<Vec3> coordinates;

        // A text containing '=' is a formula; anything else is a list of
        // coordinates. (This way the error message matches the input type.)
        const bool isFormula = customText.contains('=');

        if (!isFormula)
        {
            if (!parseCoordinateSequence(customText, coordinates, error))
                throw std::runtime_error(error.toUtf8().constData());

            result.points = resampleByArcLength(coordinates, fine, true);
            result.name = knotTr("Custom Coordinate Sequence");
        }
        else
        {
            ScalarFunction fx;
            ScalarFunction fy;
            ScalarFunction fz;

            if (!parseFormula(customText, fx, fy, fz, error))
                throw std::runtime_error(error.toUtf8().constData());

            result.points = sampleParametricCurve(fx, fy, fz, fine);
            result.name = knotTr("Custom Parametric Curve");

            for (const Vec3 &point : result.points)
            {
                if (!finiteValue(point.x) || !finiteValue(point.y) ||
                    !finiteValue(point.z))
                {
                    throw std::runtime_error(
                        knotTr("The formula is not defined for the whole "
                               "parameter range 0 <= t <= 2*pi.")
                            .toUtf8()
                            .constData());
                }
            }
        }

        result.classification = knotTr("Numerical custom geometry");
        result.polynomial = knotTr("Unavailable for arbitrary input");
        result.exactBuiltIn = false;
        break;
    }

    case Preset::KnotTable:
    {
        // customText is the KnotInfo name, e.g. "8_19" or "11n_34"
        const KnotMath::KnotTable &table = KnotMath::KnotTable::instance();

        if (!table.isLoaded())
            throw std::runtime_error(table.error().toUtf8().constData());

        const int index = table.indexOf(customText);

        if (index < 0)
        {
            throw std::runtime_error(
                knotTr("Unknown knot \"%1\".")
                    .arg(customText.trimmed())
                    .toUtf8()
                    .constData());
        }

        const KnotTableRecord &record = table.at(index);

        // KnotInfo's curve (about 130 points), made smooth before it is
        // resampled
        result.points = resampleByArcLength(
            refineCurve(table.curve(index), 4, true),
            fine,
            true);

        result.name = knotTr("Knot %1").arg(KnotTable::displayName(record.name));
        result.classification = result.name;
        result.polynomial = KnotTable::alexanderText(record);
        result.exactBuiltIn = true;
        result.tableIndex = index;

        result.points = resampleByArcLength(result.points, samples, true);
        result.knownCrossingNumber = record.crossings;

        return result;
    }
    }

    result.points = resampleByArcLength(result.points, samples, true);
    result.knownCrossingNumber = knownCrossingNumber(preset, torusP, torusQ);

    return result;
}

bool parseCoordinateSequence(
    const QString &text,
    QVector<Vec3> &points,
    QString &errorMessage)
{
    points.clear();

    QString source = text.trimmed();

    if (source.isEmpty())
    {
        errorMessage = knotTr("No custom input was provided.");
        return false;
    }

    if (source.contains('='))
    {
        errorMessage = knotTr("Input appears to be a formula expression.");
        return false;
    }

    source.replace(';', '\n');

    const QStringList lines = source.split(
        QRegularExpression(QStringLiteral("[\\r\\n]+")),
        Qt::SkipEmptyParts);

    for (const QString &line : lines)
    {
        QString cleaned = line.trimmed();

        cleaned.remove('(');
        cleaned.remove(')');
        cleaned.remove('[');
        cleaned.remove(']');

        if (cleaned.isEmpty())
            continue;

        QStringList values;

        if (cleaned.contains(','))
        {
            values = cleaned.split(',', Qt::SkipEmptyParts);
        }
        else
        {
            values = cleaned.split(
                QRegularExpression(QStringLiteral("\\s+")),
                Qt::SkipEmptyParts);
        }

        if (values.size() != 3)
        {
            errorMessage =
                knotTr("Each coordinate must contain exactly three numbers.");

            points.clear();
            return false;
        }

        bool okX = false;
        bool okY = false;
        bool okZ = false;

        const double x = values.at(0).trimmed().toDouble(&okX);
        const double y = values.at(1).trimmed().toDouble(&okY);
        const double z = values.at(2).trimmed().toDouble(&okZ);

        if (!okX || !okY || !okZ ||
            !finiteValue(x) || !finiteValue(y) || !finiteValue(z))
        {
            errorMessage = knotTr("Invalid numeric coordinate detected.");

            points.clear();
            return false;
        }

        points.push_back(Vec3(x, y, z));
    }

    if (points.size() < 3)
    {
        errorMessage = knotTr("At least three coordinate points are required.");

        points.clear();
        return false;
    }

    return true;
}

bool parseFormula(
    const QString &text,
    ScalarFunction &fx,
    ScalarFunction &fy,
    ScalarFunction &fz,
    QString &errorMessage)
{
    const QString source = text.trimmed();

    if (source.isEmpty() || !source.contains('='))
    {
        errorMessage = knotTr("Expected formula format: x=...; y=...; z=...");
        return false;
    }

    const QStringList assignments = source.split(
        QRegularExpression(QStringLiteral("[;\\r\\n]+")),
        Qt::SkipEmptyParts);

    QString xExpression;
    QString yExpression;
    QString zExpression;

    for (QString assignment : assignments)
    {
        assignment = assignment.trimmed();

        const int equalsPosition = assignment.indexOf('=');

        if (equalsPosition <= 0)
            continue;

        const QString variable =
            assignment.left(equalsPosition).trimmed().toLower();

        const QString expression =
            normalizeFormula(assignment.mid(equalsPosition + 1));

        if (variable == QStringLiteral("x"))
            xExpression = expression;
        else if (variable == QStringLiteral("y"))
            yExpression = expression;
        else if (variable == QStringLiteral("z"))
            zExpression = expression;
    }

    if (xExpression.isEmpty() || yExpression.isEmpty() || zExpression.isEmpty())
    {
        errorMessage = knotTr("Formula input must define x, y and z.");
        return false;
    }

    auto testExpression = [](const QString &expression)
    {
        bool ok = false;

        evaluateExpression(expression, 0.37, ok);

        return ok;
    };

    if (!testExpression(xExpression) ||
        !testExpression(yExpression) ||
        !testExpression(zExpression))
    {
        errorMessage = knotTr("One or more formulas could not be evaluated.");
        return false;
    }

    // At run time a failed evaluation yields NaN, which buildPreset reports
    // as an error instead of silently plotting 0.
    auto makeFunction = [](const QString &expression) -> ScalarFunction
    {
        return [expression](double t)
        {
            bool ok = false;

            const double result = evaluateExpression(expression, t, ok);

            return ok ? result
                      : std::numeric_limits<double>::quiet_NaN();
        };
    };

    fx = makeFunction(xExpression);
    fy = makeFunction(yExpression);
    fz = makeFunction(zExpression);

    return true;
}

/*
 * =============================================================
 * CROSSINGS, COLOURS, TUBE MESH
 * =============================================================
 */

QVector<CrossingInfo> detectProjectionCrossings(
    const QVector<Vec3> &curve,
    int projectionAxis,
    int maxSamples)
{
    return detectProjectionCrossingsInternal(
        curve,
        projectionAxis,
        maxSamples);
}

QColor crossingColor(int crossingIndex)
{
    static const QVector<QColor> palette = {
        QColor(59, 130, 246),
        QColor(45, 200, 255),
        QColor(37, 99, 235),
        QColor(16, 185, 129),
        QColor(75, 85, 99),
        QColor(100, 116, 139),
        QColor(245, 158, 11),
        QColor(236, 72, 153),
    };

    const int count = palette.size();

    return palette.at(((crossingIndex % count) + count) % count);
}

QColor strandColor(int strandIndex)
{
    static const QVector<QColor> palette = {
        QColor(37, 99, 235),
        QColor(59, 130, 246),
        QColor(45, 200, 255),
        QColor(16, 185, 129),
        QColor(245, 158, 11),
        QColor(236, 72, 153),
        QColor(100, 116, 139),
        QColor(168, 85, 247),
    };

    const int count = palette.size();

    return palette.at(((strandIndex % count) + count) % count);
}

QColor componentColor(int componentIndex)
{
    static const QVector<QColor> palette = {
        QColor(187, 134, 252),
        QColor(3, 218, 198),
        QColor(255, 215, 0),
        QColor(255, 107, 107),
        QColor(79, 195, 247),
    };

    const int count = palette.size();

    return palette.at(((componentIndex % count) + count) % count);
}

Mesh buildTubeMeshWithCrossings(
    const QVector<Vec3> &curve,
    double radius,
    int ringSegments,
    bool closed,
    ColorMode colorMode,
    const QVector<CrossingInfo> &crossings,
    TubeFrameHint *frameHint)
{
    Mesh mesh;

    if (curve.size() < 3)
        return mesh;

    radius = std::max(0.005, radius);
    ringSegments = std::max(6, ringSegments);

    const int n = curve.size();

    QVector<Vec3> tangents(n);
    QVector<Vec3> normals(n);
    QVector<Vec3> binormals(n);

    const Vec3 initialTangent = safeNormalize(
        derivativeAt(curve, 0, closed),
        Vec3(1.0, 0.0, 0.0));

    const Vec3 reference =
        std::abs(Vec3::dot(initialTangent, Vec3(0.0, 0.0, 1.0))) < 0.85
            ? Vec3(0.0, 0.0, 1.0)
            : Vec3(0.0, 1.0, 0.0);

    const Vec3 initialNormal = safeNormalize(
        reference - initialTangent * Vec3::dot(reference, initialTangent),
        Vec3(1.0, 0.0, 0.0));

    const Vec3 initialBinormal = safeNormalize(
        Vec3::cross(initialTangent, initialNormal),
        Vec3(0.0, 1.0, 0.0));

    for (int i = 0; i < n; ++i)
    {
        tangents[i] = safeNormalize(
            derivativeAt(curve, i, closed),
            initialTangent);
    }

    // Animation frame: every ring keeps its normal from the previous frame.
    const bool followPrevious =
        frameHint != nullptr && frameHint->ringNormals.size() == n;

    normals[0] = initialNormal;
    binormals[0] = initialBinormal;

    if (followPrevious)
    {
        for (int i = 0; i < n; ++i)
        {
            const Vec3 tangent = tangents.at(i);
            const Vec3 previous = frameHint->ringNormals.at(i);

            Vec3 projected = previous - tangent * Vec3::dot(previous, tangent);

            // The tangent turned (almost) onto the old normal: continue from
            // the neighbouring ring instead.
            if (projected.length() < 0.1 && i > 0)
            {
                const Vec3 neighbour = normals.at(i - 1);
                projected = neighbour - tangent * Vec3::dot(neighbour, tangent);
            }

            normals[i] = safeNormalize(projected, initialNormal);

            binormals[i] = safeNormalize(
                Vec3::cross(tangent, normals.at(i)),
                initialBinormal);
        }
    }

    // parallel transport of the normal along the curve
    for (int i = 1; i < n && !followPrevious; ++i)
    {
        const Vec3 previousNormal = normals.at(i - 1);
        const Vec3 tangent = tangents.at(i);

        const Vec3 projected =
            previousNormal - tangent * Vec3::dot(previousNormal, tangent);

        const Vec3 normal = safeNormalize(
            projected,
            Vec3::cross(binormals.at(i - 1), tangent));

        normals[i] = normal;

        binormals[i] = safeNormalize(
            Vec3::cross(tangent, normal),
            binormals.at(i - 1));
    }

    /*
     * Closed tubes: after one full loop the transported frame generally
     * does not coincide with the starting frame (holonomy). Without a
     * correction the last ring is joined to the first one with a visible
     * twist. The remaining rotation angle is distributed evenly over all
     * rings.
     */
    if (closed && !followPrevious)
    {
        const Vec3 t0 = tangents.at(0);
        const Vec3 last = normals.at(n - 1);

        const Vec3 transported = safeNormalize(
            last - t0 * Vec3::dot(last, t0),
            normals.at(0));

        const double angle = std::atan2(
            Vec3::dot(Vec3::cross(transported, normals.at(0)), t0),
            Vec3::dot(transported, normals.at(0)));

        for (int i = 1; i < n; ++i)
        {
            const double phi =
                angle * static_cast<double>(i) / static_cast<double>(n);

            const double c = std::cos(phi);
            const double s = std::sin(phi);

            const Vec3 normal = normals.at(i);
            const Vec3 binormal = binormals.at(i);

            normals[i] = normal * c + binormal * s;
            binormals[i] = binormal * c - normal * s;
        }
    }

    if (frameHint != nullptr)
        frameHint->ringNormals = normals;

    mesh.ringCount = n;
    mesh.ringSegments = ringSegments;

    const int ringVertexCount = n * ringSegments;
    const int capVertexCount = closed ? 0 : 2 * (ringSegments + 1);

    mesh.indices.reserve(n * ringSegments * 6 + (closed ? 0 : ringSegments * 6));

    // Arc length at every ring. The twist phase of surface patterns makes a
    // whole number of turns on a closed tube, so the pattern closes up.
    QVector<double> arc(n, 0.0);

    for (int i = 1; i < n; ++i)
        arc[i] = arc.at(i - 1) + (curve.at(i) - curve.at(i - 1)).length();

    const double length =
        arc.at(n - 1) + (closed ? (curve.at(0) - curve.at(n - 1)).length() : 0.0);

    const double layLength = 7.0 * radius;

    const double turns =
        closed ? std::max(1.0, std::round(length / layLength))
               : length / layLength;

    const double phaseScale =
        length > Epsilon ? 2.0 * Pi * turns / length : 0.0;

    for (int i = 0; i < n; ++i)
    {
        mesh.centerline.push_back(static_cast<float>(curve.at(i).x));
        mesh.centerline.push_back(static_cast<float>(curve.at(i).y));
        mesh.centerline.push_back(static_cast<float>(curve.at(i).z));
        mesh.centerline.push_back(static_cast<float>(radius));

        mesh.centerlineStrand.push_back(0);
        mesh.centerlineArc.push_back(static_cast<float>(arc.at(i)));
    }

    mesh.strandLength.push_back(static_cast<float>(length));
    mesh.strandClosed.push_back(closed);

    // angles around the ring and twist phase per ring, computed once
    QVector<double> cosTheta(ringSegments);
    QVector<double> sinTheta(ringSegments);

    for (int j = 0; j < ringSegments; ++j)
    {
        const double theta =
            2.0 * Pi * static_cast<double>(j) /
            static_cast<double>(ringSegments);

        cosTheta[j] = std::cos(theta);
        sinTheta[j] = std::sin(theta);
    }

    QVector<float> cosPhase(n);
    QVector<float> sinPhase(n);

    for (int i = 0; i < n; ++i)
    {
        cosPhase[i] = static_cast<float>(std::cos(arc.at(i) * phaseScale));
        sinPhase[i] = static_cast<float>(std::sin(arc.at(i) * phaseScale));
    }

    // All vertex arrays are allocated once and written through pointers
    // (push_back is slow in debug builds, and this runs every animation
    // frame).
    const int totalVertexCount = ringVertexCount + capVertexCount;

    mesh.positions.resize(totalVertexCount * 3);
    mesh.normals.resize(totalVertexCount * 3);
    mesh.texcoords.resize(totalVertexCount * 4);
    mesh.vertexSample.resize(totalVertexCount);

    float *positionData = mesh.positions.data();
    float *normalData = mesh.normals.data();
    float *texcoordData = mesh.texcoords.data();
    int *sampleData = mesh.vertexSample.data();

    int written = 0;

    // segment -1: the centre of an end cap
    auto pushVertex = [&](const Vec3 &position, const Vec3 &normal, int segment, int ring)
    {
        float *p = positionData + 3 * written;
        float *q = normalData + 3 * written;
        float *t = texcoordData + 4 * written;

        p[0] = static_cast<float>(position.x);
        p[1] = static_cast<float>(position.y);
        p[2] = static_cast<float>(position.z);

        q[0] = static_cast<float>(normal.x);
        q[1] = static_cast<float>(normal.y);
        q[2] = static_cast<float>(normal.z);

        t[0] = segment >= 0 ? static_cast<float>(cosTheta.at(segment)) : 1.0f;
        t[1] = segment >= 0 ? static_cast<float>(sinTheta.at(segment)) : 0.0f;
        t[2] = cosPhase.at(ring);
        t[3] = sinPhase.at(ring);

        sampleData[written] = ring;

        ++written;
    };

    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < ringSegments; ++j)
        {
            const Vec3 radial =
                normals.at(i) * cosTheta.at(j) +
                binormals.at(i) * sinTheta.at(j);

            pushVertex(curve.at(i) + radial * radius, radial, j, i);
        }
    }

    const int ringLinks = closed ? n : n - 1;

    mesh.indices.resize(
        ringLinks * ringSegments * 6 + (closed ? 0 : 2 * ringSegments * 3));

    unsigned int *indexData = mesh.indices.data();

    for (int i = 0; i < ringLinks; ++i)
    {
        const int nextRing = closed ? (i + 1) % n : i + 1;

        for (int j = 0; j < ringSegments; ++j)
        {
            const int nextSegment = (j + 1) % ringSegments;

            const unsigned int a =
                static_cast<unsigned int>(i * ringSegments + j);

            const unsigned int b =
                static_cast<unsigned int>(nextRing * ringSegments + j);

            const unsigned int c =
                static_cast<unsigned int>(nextRing * ringSegments + nextSegment);

            const unsigned int d =
                static_cast<unsigned int>(i * ringSegments + nextSegment);

            // counter-clockwise seen from outside (geometric normal = radial)
            *indexData++ = a;
            *indexData++ = c;
            *indexData++ = b;

            *indexData++ = a;
            *indexData++ = d;
            *indexData++ = c;
        }
    }

    // end caps for open tubes
    if (!closed)
    {
        for (int cap = 0; cap < 2; ++cap)
        {
            const int ring = cap == 0 ? 0 : n - 1;
            const Vec3 axis = cap == 0 ? tangents.at(ring) * -1.0
                                       : tangents.at(ring);

            const unsigned int center =
                static_cast<unsigned int>(written);

            pushVertex(curve.at(ring), axis, -1, ring);

            for (int j = 0; j < ringSegments; ++j)
            {
                const Vec3 radial =
                    normals.at(ring) * cosTheta.at(j) +
                    binormals.at(ring) * sinTheta.at(j);

                pushVertex(curve.at(ring) + radial * radius, axis, j, ring);
            }

            for (int j = 0; j < ringSegments; ++j)
            {
                const unsigned int v0 =
                    center + 1 + static_cast<unsigned int>(j);

                const unsigned int v1 =
                    center + 1 +
                    static_cast<unsigned int>((j + 1) % ringSegments);

                *indexData++ = center;

                if (cap == 0)
                {
                    *indexData++ = v1;
                    *indexData++ = v0;
                }
                else
                {
                    *indexData++ = v0;
                    *indexData++ = v1;
                }
            }
        }
    }

    if (colorMode == ColorMode::SingleColor)
        return mesh;

    const int totalVertices = mesh.positions.size() / 3;

    mesh.colors.fill(1.0f, totalVertices * 3);

    auto paintRing = [&](int ringIndex, const QColor &color)
    {
        const int ring = ((ringIndex % n) + n) % n;

        for (int segment = 0; segment < ringSegments; ++segment)
        {
            const int colorIndex = (ring * ringSegments + segment) * 3;

            mesh.colors[colorIndex] = static_cast<float>(color.redF());
            mesh.colors[colorIndex + 1] = static_cast<float>(color.greenF());
            mesh.colors[colorIndex + 2] = static_cast<float>(color.blueF());
        }
    };

    if (colorMode == ColorMode::CrossingColors)
    {
        const int neighborhood = std::max(2, n / 80);

        for (int crossingIndex = 0; crossingIndex < crossings.size();
             ++crossingIndex)
        {
            const CrossingInfo &crossing = crossings.at(crossingIndex);

            const QColor color = crossingColor(crossingIndex);

            const int ringA = static_cast<int>(
                std::lround(crossing.fractionA * static_cast<double>(n)));

            const int ringB = static_cast<int>(
                std::lround(crossing.fractionB * static_cast<double>(n)));

            for (int offset = -neighborhood; offset <= neighborhood; ++offset)
            {
                paintRing(ringA + offset, color);
                paintRing(ringB + offset, color);
            }
        }
    }
    else if (colorMode == ColorMode::StrandColors)
    {
        /*
         * Arcs of a knot diagram: the curve is cut at every UNDER-crossing.
         * Each arc (running from one under-pass to the next) gets its own
         * colour. A diagram with k crossings has k arcs.
         */
        QVector<double> underFractions;
        underFractions.reserve(crossings.size());

        for (const CrossingInfo &crossing : crossings)
        {
            underFractions.push_back(
                crossing.aIsOver ? crossing.fractionB : crossing.fractionA);
        }

        std::sort(underFractions.begin(), underFractions.end());

        const int arcCount = underFractions.size();

        for (int ring = 0; ring < n; ++ring)
        {
            int arcIndex = 0;

            if (arcCount > 0)
            {
                const double fraction =
                    static_cast<double>(ring) / static_cast<double>(n);

                const int passed = static_cast<int>(
                    std::upper_bound(
                        underFractions.constBegin(),
                        underFractions.constEnd(),
                        fraction) -
                    underFractions.constBegin());

                arcIndex = (passed - 1 + arcCount) % arcCount;
            }

            paintRing(ring, strandColor(arcIndex));
        }
    }

    return mesh;
}

Mesh buildTubeMesh(
    const QVector<Vec3> &curve,
    double radius,
    int ringSegments,
    bool closed,
    ColorMode colorMode,
    int projectionAxis,
    TubeFrameHint *frameHint)
{
    QVector<CrossingInfo> crossings;

    if (colorMode != ColorMode::SingleColor && closed)
        crossings = detectProjectionCrossings(curve, projectionAxis, 700);

    return buildTubeMeshWithCrossings(
        curve,
        radius,
        ringSegments,
        closed,
        colorMode,
        crossings,
        frameHint);
}

/*
 * The tube is treated as a chain of spheres of the tube radius, about one
 * radius apart (each standing for `spacing` of tube). A sphere of radius r
 * at distance d, seen at angle theta from the normal, darkens a surface
 * point by about cos(theta) (r/d)²: its solid angle, cosine weighted.
 *
 * To be fast enough for every animation frame this is summed once per ring
 * as a vector (direction to the sphere times its weight, with d measured
 * from the ring's surface); a vertex then takes the part along its normal.
 * On the side facing a close strand, where the darkening matters, this is
 * the same as the sum per vertex. For two parallel tubes that touch, the
 * facing sides add up to about 1; with a gap of one diameter to about 0.3.
 */
void computeAmbientOcclusion(Mesh &mesh, double strength)
{
    const int samples = mesh.centerline.size() / 4;
    const int vertexCount = mesh.positions.size() / 3;

    if (samples < 2 ||
        mesh.vertexSample.size() != vertexCount ||
        mesh.centerlineStrand.size() != samples ||
        mesh.centerlineArc.size() != samples)
    {
        mesh.occlusion.clear();
        return;
    }

    auto centre = [&mesh](int s)
    {
        return Vec3(mesh.centerline.at(4 * s),
                    mesh.centerline.at(4 * s + 1),
                    mesh.centerline.at(4 * s + 2));
    };

    auto radiusOf = [&mesh](int s)
    {
        return static_cast<double>(mesh.centerline.at(4 * s + 3));
    };

    // spheres further than this (in radii) hardly matter
    constexpr double reachInRadii = 7.0;

    // parts of the same strand closer than this along it are the tube itself
    constexpr double ownInRadii = 3.0;

    double maxRadius = 0.0;

    for (int s = 0; s < samples; ++s)
        maxRadius = std::max(maxRadius, radiusOf(s));

    if (maxRadius <= 0.0)
    {
        mesh.occlusion.clear();
        return;
    }

    // Occluding spheres: about one per tube radius along every strand; each
    // stands for the length of tube up to the next one.
    QVector<int> casters;
    QVector<double> casterSpacing;

    for (int s = 0; s < samples; ++s)
    {
        const bool strandStart =
            s == 0 ||
            mesh.centerlineStrand.at(s) != mesh.centerlineStrand.at(s - 1);

        if (strandStart ||
            static_cast<double>(mesh.centerlineArc.at(s)) -
                    static_cast<double>(mesh.centerlineArc.at(casters.last())) >=
                radiusOf(s))
        {
            casters.push_back(s);
        }
    }

    casterSpacing.resize(casters.size());

    for (int k = 0; k < casters.size(); ++k)
    {
        const int s = casters.at(k);
        const bool hasNext =
            k + 1 < casters.size() &&
            mesh.centerlineStrand.at(casters.at(k + 1)) == mesh.centerlineStrand.at(s);

        casterSpacing[k] =
            hasNext ? static_cast<double>(mesh.centerlineArc.at(casters.at(k + 1)) -
                                          mesh.centerlineArc.at(s))
                    : radiusOf(s);
    }

    // spatial grid of the occluding spheres
    const double reach = reachInRadii * maxRadius;

    auto cellOf = [reach](double value)
    {
        return static_cast<long long>(std::floor(value / reach));
    };

    auto key = [](long long x, long long y, long long z)
    {
        constexpr long long offset = 1LL << 20;

        return ((x + offset) << 42) ^ ((y + offset) << 21) ^ (z + offset);
    };

    std::unordered_map<long long, std::vector<int>> grid;

    for (int k = 0; k < casters.size(); ++k)
    {
        const Vec3 c = centre(casters.at(k));

        grid[key(cellOf(c.x), cellOf(c.y), cellOf(c.z))].push_back(k);
    }

    // occlusion vector of every ring
    QVector<Vec3> ringVector(samples);

    for (int s = 0; s < samples; ++s)
    {
        const Vec3 c = centre(s);
        const double rs = radiusOf(s);
        const int strand = mesh.centerlineStrand.at(s);

        const long long cx = cellOf(c.x);
        const long long cy = cellOf(c.y);
        const long long cz = cellOf(c.z);

        Vec3 sum;

        for (long long dx = -1; dx <= 1; ++dx)
        for (long long dy = -1; dy <= 1; ++dy)
        for (long long dz = -1; dz <= 1; ++dz)
        {
            const auto it = grid.find(key(cx + dx, cy + dy, cz + dz));

            if (it == grid.end())
                continue;

            for (const int k : it->second)
            {
                const int t = casters.at(k);

                if (mesh.centerlineStrand.at(t) == strand)
                {
                    double along = std::abs(
                        static_cast<double>(mesh.centerlineArc.at(t)) -
                        static_cast<double>(mesh.centerlineArc.at(s)));

                    if (strand < mesh.strandLength.size() &&
                        strand < mesh.strandClosed.size() &&
                        mesh.strandClosed.at(strand))
                    {
                        along = std::min(
                            along,
                            static_cast<double>(mesh.strandLength.at(strand)) - along);
                    }

                    if (along < ownInRadii * rs)
                        continue;
                }

                const Vec3 toSphere = centre(t) - c;
                const double distance = toSphere.length();

                if (distance >= reach || distance < 1.0e-9)
                    continue;

                const double rt = radiusOf(t);

                // distance from the ring's surface on the facing side
                const double surface = std::max(distance - rs, 0.5 * rs);

                // fades out towards `reach` instead of stopping abruptly
                const double x = std::clamp(
                    (distance - 0.7 * reach) / (0.3 * reach), 0.0, 1.0);

                const double fade = 1.0 - x * x * (3.0 - 2.0 * x);

                const double weight =
                    (rt * rt) / (surface * surface) *
                    std::min(1.0, casterSpacing.at(k) / (2.0 * rt)) *
                    fade;

                sum += toSphere * (weight / distance);
            }
        }

        ringVector[s] = sum;
    }

    mesh.occlusion.resize(vertexCount);

    for (int v = 0; v < vertexCount; ++v)
    {
        const Vec3 normal(mesh.normals.at(3 * v),
                          mesh.normals.at(3 * v + 1),
                          mesh.normals.at(3 * v + 2));

        const double occlusion = std::max(
            0.0,
            Vec3::dot(normal, ringVector.at(mesh.vertexSample.at(v))));

        mesh.occlusion[v] = static_cast<float>(
            std::clamp(1.0 - strength * occlusion, 0.2, 1.0));
    }
}

/*
 * =============================================================
 * INVARIANTS
 * =============================================================
 */

double computeWrithe(const QVector<Vec3> &curve, int maxSamples)
{
    if (curve.size() < 4)
        return 0.0;

    QVector<Vec3> sampled = curve;

    if (maxSamples > 0 && curve.size() > maxSamples)
        sampled = resampleByArcLength(curve, std::max(24, maxSamples), true);

    if (sampled.size() > 3 &&
        (sampled.first() - sampled.last()).length() < 1.0e-9)
    {
        sampled.removeLast();
    }

    const int n = sampled.size();

    if (n < 4)
        return 0.0;

    double sum = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const int iNext = (i + 1) % n;

        for (int j = i + 2; j < n; ++j)
        {
            if (i == 0 && j == n - 1)
                continue;

            sum += segmentPairSolidAngle(
                sampled.at(i),
                sampled.at(iNext),
                sampled.at(j),
                sampled.at((j + 1) % n));
        }
    }

    const double writhe = sum / (2.0 * Pi);

    return finiteValue(writhe) ? writhe : 0.0;
}

double computeLinkingNumber(
    const QVector<Vec3> &curveA,
    const QVector<Vec3> &curveB,
    int maxSamples)
{
    if (curveA.size() < 3 || curveB.size() < 3)
        return 0.0;

    QVector<Vec3> a = curveA;
    QVector<Vec3> b = curveB;

    if (maxSamples > 0 && a.size() > maxSamples)
        a = resampleByArcLength(a, maxSamples, true);

    if (maxSamples > 0 && b.size() > maxSamples)
        b = resampleByArcLength(b, maxSamples, true);

    const int na = a.size();
    const int nb = b.size();

    double sum = 0.0;

    for (int i = 0; i < na; ++i)
    {
        const int iNext = (i + 1) % na;

        for (int j = 0; j < nb; ++j)
        {
            sum += segmentPairSolidAngle(
                a.at(i),
                a.at(iNext),
                b.at(j),
                b.at((j + 1) % nb));
        }
    }

    const double linking = sum / (4.0 * Pi);

    return finiteValue(linking) ? linking : 0.0;
}

int estimateCrossingNumber(
    const QVector<Vec3> &curve,
    int projectionAxis,
    int maxSamples)
{
    if (curve.size() < 4)
        return 0;

    return detectProjectionCrossings(curve, projectionAxis, maxSamples).size();
}

QString formatPolynomial(const std::map<int, long long> &coefficients)
{
    return formatLaurentPolynomial(coefficients);
}

QString alexanderPolynomialForTorusKnot(int p, int q)
{
    p = std::max(1, p);
    q = std::max(1, q);

    if (std::gcd(p, q) != 1)
    {
        return knotTr("Undefined as a single-knot Alexander polynomial");
    }

    /*
     * Delta(t) = (t^(pq) - 1)(t - 1) / ((t^p - 1)(t^q - 1))
     *
     * numerator   = t^(pq+1) - t^(pq) - t + 1
     * denominator = t^(p+q)  - t^p    - t^q + 1      (monic)
     */
    const int pq = p * q;

    std::vector<long long> numerator(static_cast<size_t>(pq + 2), 0);

    numerator[static_cast<size_t>(pq + 1)] += 1;
    numerator[static_cast<size_t>(pq)] -= 1;
    numerator[1] -= 1;
    numerator[0] += 1;

    std::vector<long long> denominator(static_cast<size_t>(p + q + 1), 0);

    denominator[static_cast<size_t>(p + q)] += 1;
    denominator[static_cast<size_t>(p)] -= 1;
    denominator[static_cast<size_t>(q)] -= 1;
    denominator[0] += 1;

    const std::vector<long long> quotient =
        polynomialQuotient(numerator, denominator);

    // symmetrise: Delta(t) = Delta(1/t) up to a power of t
    const int degree = static_cast<int>(quotient.size()) - 1;
    const int shift = -(degree / 2);

    std::map<int, long long> normalized;

    for (int power = 0; power <= degree; ++power)
    {
        const long long coefficient = quotient[static_cast<size_t>(power)];

        if (coefficient != 0)
            normalized[power + shift] = coefficient;
    }

    if (normalized.empty())
        return QStringLiteral("1");

    return formatLaurentPolynomial(normalized);
}

QString normalizedAlexanderPolynomialForPreset(Preset preset)
{
    switch (preset)
    {
    case Preset::Unknot:
        return QStringLiteral("1");

    case Preset::Trefoil:
        return alexanderPolynomialForTorusKnot(3, 2);

    case Preset::FigureEight:
        return QStringLiteral("−t + 3 − t⁻¹");

    case Preset::Cinquefoil:
        return alexanderPolynomialForTorusKnot(5, 2);

    case Preset::Stevedore:
        return QStringLiteral("−2t + 5 − 2t⁻¹");

    case Preset::DNAHelix:
    case Preset::CelticKnot:
        return knotTr("Multi-component link (not computed)");

    case Preset::CustomTorus:
        return knotTr("Computed from T(p,q)");

    case Preset::CustomCoordinates:
        return knotTr("Unavailable for arbitrary input");

    case Preset::KnotTable:
        return knotTr("From the knot table");
    }

    return knotTr("Unknown");
}

Invariants computeInvariantsWithCrossings(
    Preset preset,
    const QVector<Vec3> &curve,
    const QVector<CrossingInfo> &crossings,
    bool closed,
    int torusP,
    int torusQ,
    int tableIndex)
{
    Invariants result;

    result.writhe = computeWrithe(curve);
    result.crossingNumber = crossings.size();

    int signedSum = 0;

    for (const CrossingInfo &crossing : crossings)
        signedSum += crossing.sign;

    result.signedCrossingSum = signedSum;
    result.knownCrossingNumber = knownCrossingNumber(preset, torusP, torusQ);

    if (preset == Preset::CustomTorus)
    {
        result.alexanderPolynomial =
            alexanderPolynomialForTorusKnot(torusP, torusQ);
    }
    else
    {
        result.alexanderPolynomial =
            normalizedAlexanderPolynomialForPreset(preset);
    }

    // Jones polynomial of the displayed geometry (it tells mirror images
    // apart, so it also gives the handedness).
    JonesPolynomial jones;

    if (closed)
        jones = computeJonesPolynomial(curve);

    // Torus knots too large for the state sum: exact formula. A diagram whose
    // crossings are all positive (negative) shows the right-handed
    // (left-handed) torus knot.
    if (!jones.valid &&
        preset == Preset::CustomTorus &&
        std::gcd(torusP, torusQ) == 1 &&
        !crossings.isEmpty())
    {
        const bool allPositive = std::all_of(
            crossings.begin(),
            crossings.end(),
            [](const CrossingInfo &c) { return c.sign > 0; });

        const bool allNegative = std::all_of(
            crossings.begin(),
            crossings.end(),
            [](const CrossingInfo &c) { return c.sign < 0; });

        if (allPositive)
            jones = torusKnotJonesPolynomial(torusP, torusQ);
        else if (allNegative)
            jones = torusKnotJonesPolynomial(torusP, torusQ).mirrored();
    }

    if (jones.valid)
    {
        result.jonesPolynomial = jones.text;
    }
    else if (!jones.error.isEmpty())
    {
        result.jonesPolynomial = knotTr("Not computed: %1").arg(jones.error);
    }
    else
    {
        result.jonesPolynomial = knotTr("Not computed");
    }

    // "left-handed" / "right-handed" for a chiral torus knot T(p,q).
    auto torusHandedness = [&jones](int p, int q)
    {
        const JonesPolynomial right = torusKnotJonesPolynomial(p, q);

        if (!jones.valid || !right.valid || right.isSymmetric())
            return QString();

        if (jones == right)
            return QStringLiteral(", ") + knotTr("right-handed");

        if (jones == right.mirrored())
            return QStringLiteral(", ") + knotTr("left-handed");

        return QString();
    };

    const QString builtInMethod =
        knotTr("Exact values for this knot type. The writhe and the crossings "
               "are measured on the 3D curve on screen.");

    switch (preset)
    {
    case Preset::Unknot:
        result.classification = knotTr("Unknot / trivial knot (0_1)");
        result.methodology = builtInMethod;
        break;

    case Preset::Trefoil:
        result.classification =
            knotTr("Trefoil Knot (3_1)") + torusHandedness(2, 3);
        result.methodology = builtInMethod;
        break;

    case Preset::FigureEight:
        result.classification = knotTr("Figure-Eight Knot (4_1)");
        result.methodology = builtInMethod;
        break;

    case Preset::Cinquefoil:
        result.classification =
            knotTr("Cinquefoil Knot (5_1)") + torusHandedness(2, 5);
        result.methodology = builtInMethod;
        break;

    case Preset::Stevedore:
        result.classification = knotTr("Stevedore Knot (6_1)");
        result.methodology = builtInMethod;
        break;

    case Preset::CustomTorus:
        result.classification =
            knotTr("Torus knot T(%1, %2)").arg(torusP).arg(torusQ) +
            torusHandedness(torusP, torusQ);
        result.methodology = knotTr(
            "The writhe and the crossings are measured on the 3D curve on screen.");
        break;

    case Preset::KnotTable:
    {
        const KnotMath::KnotTable &table = KnotMath::KnotTable::instance();

        if (tableIndex < 0 || tableIndex >= table.size())
            break;

        const KnotTableRecord &record = table.at(tableIndex);

        const QString described = KnotTable::descriptiveName(record);

        result.classification =
            described.isEmpty()
                ? knotTr("Knot %1").arg(KnotTable::displayName(record.name))
                : QStringLiteral("%1 (%2)").arg(described, KnotTable::displayName(record.name));

        result.knownCrossingNumber = record.crossings;
        result.alexanderPolynomial = KnotTable::alexanderText(record);

        // the curve's own Jones polynomial shows which mirror image is on
        // screen; KnotInfo's value where the diagram is too large for it
        if (!jones.valid)
            result.jonesPolynomial = KnotTable::jonesText(record);

        // (the data source is credited in the Knot Browser and About)
        result.methodology =
            knotTr("From the knot table; writhe and projection crossings are "
                   "computed from the 3D curve.");
        break;
    }

    default:
        result.classification =
            closed ? knotTr("Custom knot")
                   : knotTr("Open curve");

        result.methodology = knotTr(
            "The knot type cannot be read off the shape alone. The values are "
            "measured on the 3D curve on screen.");

        if (preset == Preset::CustomCoordinates && closed)
        {
            const AlexanderPolynomial alexander =
                computeAlexanderPolynomial(curve);

            if (alexander.valid)
            {
                result.alexanderPolynomial = alexander.text;
                result.classification = identifyKnot(alexander, jones);

                result.methodology =
                    (alexander.confirmed
                         ? knotTr("Alexander polynomial computed exactly (Fox calculus) "
                                  "from a projection with %1 crossings and confirmed "
                                  "on a second projection.")
                         : knotTr("Alexander polynomial computed exactly (Fox calculus) "
                                  "from a projection with %1 crossings."))
                        .arg(alexander.diagramCrossings) +
                    QLatin1Char(' ') +
                    (jones.valid
                         ? knotTr("The knot type is looked up among all prime knots "
                                  "with up to 13 crossings and the composite knots "
                                  "with up to 8; the Jones polynomial (Kauffman "
                                  "bracket) rules out candidates and gives the "
                                  "handedness of torus knots. A knot with more "
                                  "crossings can share both polynomials.")
                         : knotTr("The knot type is looked up from the polynomial among "
                                  "all prime knots with up to 13 crossings and the "
                                  "composite knots with up to 8; mirror images are "
                                  "not told apart, and a knot with more crossings can "
                                  "share the polynomial.")) +
                    QLatin1Char(' ') +
                    knotTr("Writhe and crossing count are numerical.");
            }
            else
            {
                result.alexanderPolynomial = knotTr("Not computed");

                result.methodology =
                    knotTr("Alexander polynomial: %1").arg(alexander.error) +
                    QLatin1Char(' ') + result.methodology;
            }
        }
        break;
    }

    // knot names such as 3_1 written as in the knot list: 3₁
    static const QRegularExpression tableName(QStringLiteral("\\b\\d+_\\d+\\b"));

    QRegularExpressionMatch match;

    while ((match = tableName.match(result.classification)).hasMatch())
    {
        result.classification.replace(
            match.capturedStart(),
            match.capturedLength(),
            KnotTable::displayName(match.captured()));
    }

    return result;
}

Invariants computeInvariants(
    Preset preset,
    const QVector<Vec3> &curve,
    bool closed)
{
    const QVector<CrossingInfo> crossings =
        detectProjectionCrossings(curve, 2, 700);

    return computeInvariantsWithCrossings(
        preset,
        curve,
        crossings,
        closed);
}

Invariants computeMultiStrandInvariants(
    Preset preset,
    const CurveData &data)
{
    Invariants result;

    result.multiStrand = true;
    result.crossingNumber = -1;
    result.knownCrossingNumber = -1;
    result.classification = data.classification;
    result.alexanderPolynomial =
        preset == Preset::CustomTorus
            ? data.polynomial
            : normalizedAlexanderPolynomialForPreset(preset);
    result.jonesPolynomial = knotTr("Multi-component link (not computed)");

    // total self-writhe of the closed components
    double writheSum = 0.0;

    for (int i = 0; i < data.separateStrands.size(); ++i)
    {
        if (i < data.strandClosed.size() && data.strandClosed.at(i))
            writheSum += computeWrithe(data.separateStrands.at(i), 200);
    }

    result.writhe = writheSum;

    // pairwise Gauss linking numbers of the closed components
    QStringList parts;

    for (int i = 0; i < data.separateStrands.size(); ++i)
    {
        if (i >= data.strandClosed.size() || !data.strandClosed.at(i))
            continue;

        for (int j = i + 1; j < data.separateStrands.size(); ++j)
        {
            if (j >= data.strandClosed.size() || !data.strandClosed.at(j))
                continue;

            const int linking = static_cast<int>(std::lround(
                computeLinkingNumber(
                    data.separateStrands.at(i),
                    data.separateStrands.at(j),
                    160)));

            if (linking != 0)
            {
                parts << QStringLiteral("Lk(%1,%2) = %3")
                             .arg(i + 1)
                             .arg(j + 1)
                             .arg(signedInteger(linking));
            }
        }
    }

    const QString linkText =
        parts.isEmpty() ? knotTr("no linked pairs")
                        : parts.join(QStringLiteral(", "));

    result.methodology =
        knotTr("Gauss linking numbers of the closed components: %1.")
            .arg(linkText);

    return result;
}

QString presetDisplayName(Preset preset)
{
    switch (preset)
    {
    case Preset::Unknot:
        return knotTr("Unknot");

    case Preset::Trefoil:
        return knotTr("Trefoil Knot (3₁)");

    case Preset::FigureEight:
        return knotTr("Figure-Eight Knot (4₁)");

    case Preset::Cinquefoil:
        return knotTr("Cinquefoil / Solomon's Seal (5₁)");

    case Preset::Stevedore:
        return knotTr("Stevedore Knot (6₁)");

    case Preset::DNAHelix:
        return knotTr("DNA Double Helix (Multi-strand)");

    case Preset::CelticKnot:
        return knotTr("Celtic Armor (Multi-strand)");

    case Preset::CustomTorus:
        return knotTr("Custom Torus Knot T(p,q)");

    case Preset::CustomCoordinates:
        return knotTr("Custom Coordinates / Formula");

    case Preset::KnotTable:
        return knotTr("Knot Table");
    }

    return knotTr("Unknown");
}

QString mathematicalDescription(Preset preset)
{
    switch (preset)
    {
    case Preset::Unknot:
        return knotTr("A simple closed loop representing the trivial knot.");

    case Preset::Trefoil:
        return knotTr("The simplest nontrivial knot, represented here as the torus knot T(3,2).");

    case Preset::FigureEight:
        return knotTr("The simplest nontrivial knot that is amphicheiral, with crossing number four.");

    case Preset::Cinquefoil:
        return knotTr("The torus knot T(5,2), with five minimal crossings.");

    case Preset::Stevedore:
        return knotTr("The six-crossing stevedore knot, commonly denoted 6_1.");

    case Preset::DNAHelix:
        return knotTr("A closed circular double helix with base pair rungs; the two strands are linked 10 times.");

    case Preset::CelticKnot:
        return knotTr("A closed chain of four interlocked rings, like chain mail; neighbouring rings are linked once.");

    case Preset::CustomTorus:
        return knotTr("A family of torus knots/links specified by coprime or non-coprime integers p and q.");

    case Preset::CustomCoordinates:
        return knotTr("User-defined closed geometry from explicit 3D points or x(t), y(t), z(t).");

    case Preset::KnotTable:
        return knotTr("One of the 12,965 prime knots with up to 13 crossings, "
                      "with its 3D shape and invariants.");
    }

    return QString();
}

/*
 * =============================================================
 * UNKNOTTING ANIMATION
 * =============================================================
 */

namespace
{

double smoothStep01(double x)
{
    x = std::clamp(x, 0.0, 1.0);

    return x * x * (3.0 - 2.0 * x);
}

double smootherStep01(double x)
{
    x = std::clamp(x, 0.0, 1.0);

    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

Vec3 rotateAboutAxis(const Vec3 &v, const Vec3 &axis, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    return v * c +
           Vec3::cross(axis, v) * s +
           axis * (Vec3::dot(axis, v) * (1.0 - c));
}

struct AlignRotation
{
    Vec3 axis = Vec3(1.0, 0.0, 0.0);
    double angle = 0.0;
};

// Rotation that takes `direction` to +z.
AlignRotation rotationToZ(const Vec3 &direction)
{
    const Vec3 d = safeNormalize(direction, Vec3(0.0, 0.0, 1.0));
    const Vec3 z(0.0, 0.0, 1.0);

    const Vec3 a = Vec3::cross(d, z);

    const double s = a.length();
    const double c = Vec3::dot(d, z);

    AlignRotation result;

    if (s < 1.0e-9)
    {
        result.axis = Vec3(1.0, 0.0, 0.0);
        result.angle = c > 0.0 ? 0.0 : Pi;

        return result;
    }

    result.axis = a / s;
    result.angle = std::atan2(s, c);

    return result;
}

// Distance of two fractions of a closed curve (0..0.5).
double circularDistance(double a, double b)
{
    double d = std::abs(a - b);

    d -= std::floor(d);

    return std::min(d, 1.0 - d);
}

double pointSegmentDistance2D(
    double px, double py,
    double ax, double ay,
    double bx, double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;

    const double length2 = dx * dx + dy * dy;

    double t = 0.0;

    if (length2 > 1.0e-18)
        t = ((px - ax) * dx + (py - ay) * dy) / length2;

    t = std::clamp(t, 0.0, 1.0);

    const double cx = ax + dx * t - px;
    const double cy = ay + dy * t - py;

    return std::sqrt(cx * cx + cy * cy);
}

bool segmentsCross2D(
    double ax, double ay, double bx, double by,
    double cx, double cy, double dx, double dy)
{
    auto orient = [](double x1, double y1, double x2, double y2,
                     double x3, double y3)
    {
        return (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1);
    };

    const double o1 = orient(ax, ay, bx, by, cx, cy);
    const double o2 = orient(ax, ay, bx, by, dx, dy);
    const double o3 = orient(cx, cy, dx, dy, ax, ay);
    const double o4 = orient(cx, cy, dx, dy, bx, by);

    return ((o1 > 0.0) != (o2 > 0.0)) &&
           ((o3 > 0.0) != (o4 > 0.0));
}

double segmentDistance2D(
    double ax, double ay, double bx, double by,
    double cx, double cy, double dx, double dy)
{
    if (segmentsCross2D(ax, ay, bx, by, cx, cy, dx, dy))
        return 0.0;

    return std::min(
        std::min(
            pointSegmentDistance2D(ax, ay, cx, cy, dx, dy),
            pointSegmentDistance2D(bx, by, cx, cy, dx, dy)),
        std::min(
            pointSegmentDistance2D(cx, cy, ax, ay, bx, by),
            pointSegmentDistance2D(dx, dy, ax, ay, bx, by)));
}

double rayPointDistance2D(
    double ox, double oy, double ux, double uy,
    double px, double py)
{
    const double t = (px - ox) * ux + (py - oy) * uy;

    if (t <= 0.0)
        return std::hypot(px - ox, py - oy);

    return std::hypot(px - (ox + ux * t), py - (oy + uy * t));
}

double raySegmentDistance2D(
    double ox, double oy, double ux, double uy,
    double ax, double ay, double bx, double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;

    const double denominator = ux * dy - uy * dx;

    if (std::abs(denominator) > 1.0e-14)
    {
        const double t =
            ((ax - ox) * dy - (ay - oy) * dx) / denominator;

        const double u =
            ((ax - ox) * uy - (ay - oy) * ux) / denominator;

        if (t >= 0.0 && u >= 0.0 && u <= 1.0)
            return 0.0;
    }

    return std::min(
        pointSegmentDistance2D(ox, oy, ax, ay, bx, by),
        std::min(
            rayPointDistance2D(ox, oy, ux, uy, ax, ay),
            rayPointDistance2D(ox, oy, ux, uy, bx, by)));
}

double segmentDistance3D(
    const Vec3 &p1, const Vec3 &q1,
    const Vec3 &p2, const Vec3 &q2)
{
    const Vec3 d1 = q1 - p1;
    const Vec3 d2 = q2 - p2;
    const Vec3 r = p1 - p2;

    const double a = Vec3::dot(d1, d1);
    const double e = Vec3::dot(d2, d2);
    const double f = Vec3::dot(d2, r);

    constexpr double eps = 1.0e-12;

    double s = 0.0;
    double t = 0.0;

    if (a <= eps && e <= eps)
        return r.length();

    if (a <= eps)
    {
        t = std::clamp(f / e, 0.0, 1.0);
    }
    else
    {
        const double c = Vec3::dot(d1, r);

        if (e <= eps)
        {
            s = std::clamp(-c / a, 0.0, 1.0);
        }
        else
        {
            const double b = Vec3::dot(d1, d2);
            const double denominator = a * e - b * b;

            if (denominator > eps)
                s = std::clamp((b * f - c * e) / denominator, 0.0, 1.0);

            t = (b * s + f) / e;

            if (t < 0.0)
            {
                t = 0.0;
                s = std::clamp(-c / a, 0.0, 1.0);
            }
            else if (t > 1.0)
            {
                t = 1.0;
                s = std::clamp((b - c) / a, 0.0, 1.0);
            }
        }
    }

    return ((p1 + d1 * s) - (p2 + d2 * t)).length();
}

// Smallest distance of two strands in the projection (x,y). Pairs of
// segments that belong to the same crossing and neighbours along the curve
// are not counted.
double projectionClearance(
    const QVector<Vec3> &points,
    const QVector<CrossingInfo> &crossings,
    int skip)
{
    const int n = points.size();

    std::vector<char> excluded(
        static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0);

    for (const CrossingInfo &crossing : crossings)
    {
        for (int da = -2; da <= 2; ++da)
        {
            for (int db = -2; db <= 2; ++db)
            {
                const int a = ((crossing.segmentA + da) % n + n) % n;
                const int b = ((crossing.segmentB + db) % n + n) % n;

                const int lo = std::min(a, b);
                const int hi = std::max(a, b);

                excluded[static_cast<std::size_t>(lo) *
                             static_cast<std::size_t>(n) +
                         static_cast<std::size_t>(hi)] = 1;
            }
        }
    }

    // bounding box of every segment (the box gap never exceeds the distance)
    std::vector<double> minX(static_cast<std::size_t>(n));
    std::vector<double> maxX(static_cast<std::size_t>(n));
    std::vector<double> minY(static_cast<std::size_t>(n));
    std::vector<double> maxY(static_cast<std::size_t>(n));

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &p = points.at(i);
        const Vec3 &q = points.at((i + 1) % n);
        const std::size_t k = static_cast<std::size_t>(i);

        minX[k] = std::min(p.x, q.x);
        maxX[k] = std::max(p.x, q.x);
        minY[k] = std::min(p.y, q.y);
        maxY[k] = std::max(p.y, q.y);
    }

    double best = std::numeric_limits<double>::max();

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &a0 = points.at(i);
        const Vec3 &a1 = points.at((i + 1) % n);
        const std::size_t ki = static_cast<std::size_t>(i);

        for (int j = i + 1; j < n; ++j)
        {
            const int gap = std::min(j - i, n - (j - i));

            if (gap <= skip)
                continue;

            if (excluded[static_cast<std::size_t>(i) *
                             static_cast<std::size_t>(n) +
                         static_cast<std::size_t>(j)] != 0)
            {
                continue;
            }

            const std::size_t kj = static_cast<std::size_t>(j);

            const double gapX = std::max({0.0, minX[kj] - maxX[ki], minX[ki] - maxX[kj]});
            const double gapY = std::max({0.0, minY[kj] - maxY[ki], minY[ki] - maxY[kj]});

            if (gapX * gapX + gapY * gapY >= best * best)
                continue;

            const Vec3 &b0 = points.at(j);
            const Vec3 &b1 = points.at((j + 1) % n);

            best = std::min(
                best,
                segmentDistance2D(
                    a0.x, a0.y, a1.x, a1.y,
                    b0.x, b0.y, b1.x, b1.y));
        }
    }

    return best;
}

// The return strand (the last `ret` samples before the base point) swings
// out along `u`. That is only collision free if the rays from its points in
// direction `u` do not meet the rest of the curve.
bool swingRayClear(
    const QVector<Vec3> &points,
    int base,
    int sign,
    int ret,
    double ux,
    double uy,
    double clearance)
{
    const int n = points.size();

    const int nearRange = 2 * ret + 2;

    for (int k = 0; k < ret; ++k)
    {
        const int origin = (((base - sign * k) % n) + n) % n;

        const double ox = points.at(origin).x;
        const double oy = points.at(origin).y;

        for (int j = 0; j < n; ++j)
        {
            int distanceToOrigin = std::abs(j - origin);
            distanceToOrigin = std::min(distanceToOrigin, n - distanceToOrigin);

            if (distanceToOrigin <= 2)
                continue;

            int distanceToBase = std::abs(j - base);
            distanceToBase = std::min(distanceToBase, n - distanceToBase);

            const double threshold =
                distanceToBase <= nearRange ? 1.0e-9 : clearance;

            const Vec3 &a = points.at(j);
            const Vec3 &b = points.at((j + 1) % n);

            if (raySegmentDistance2D(ox, oy, ux, uy, a.x, a.y, b.x, b.y) <=
                threshold)
            {
                return false;
            }
        }
    }

    return true;
}

/*
 * Viewing directions to try. The exact axis directions are avoided on
 * purpose: symmetric curves (like the figure-eight) have several strands
 * passing through the very same point of the projection there, which makes
 * crossings ambiguous. Every direction is turned by a small, fixed,
 * "generic" rotation.
 */
QVector<Vec3> candidateViewDirections()
{
    QVector<Vec3> directions;

    directions.push_back(Vec3(0.0, 0.0, 1.0)); // the current view first
    directions.push_back(Vec3(0.0, 1.0, 0.0));
    directions.push_back(Vec3(1.0, 0.0, 0.0));

    constexpr int count = 40;

    const double golden = Pi * (3.0 - std::sqrt(5.0));

    for (int i = 0; i < count; ++i)
    {
        const double z = (static_cast<double>(i) + 0.5) /
                         static_cast<double>(count);

        const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double phi = golden * static_cast<double>(i);

        directions.push_back(
            Vec3(r * std::cos(phi), r * std::sin(phi), z));
    }

    const Vec3 genericAxis = safeNormalize(
        Vec3(0.3181, 0.5207, 0.7923),
        Vec3(0.0, 0.0, 1.0));

    for (Vec3 &direction : directions)
        direction = rotateAboutAxis(direction, genericAxis, 0.0517);

    return directions;
}

// A diagram is only used if no crossing is (nearly) ambiguous: a crossing
// at a vertex of the polyline, two strands with (almost) the same depth, or
// three strands through one point.
bool diagramIsRobust(
    const QVector<CrossingInfo> &crossings,
    double extent)
{
    constexpr double margin = 1.0e-3;

    for (const CrossingInfo &crossing : crossings)
    {
        if (crossing.parameterA < margin ||
            crossing.parameterA > 1.0 - margin ||
            crossing.parameterB < margin ||
            crossing.parameterB > 1.0 - margin)
        {
            return false;
        }

        if (std::abs(crossing.depthA - crossing.depthB) < 2.0e-3 * extent)
            return false;
    }

    const double pointTolerance = 2.0e-3 * extent;

    for (int a = 0; a < crossings.size(); ++a)
    {
        for (int b = a + 1; b < crossings.size(); ++b)
        {
            const double dx =
                crossings.at(a).midpoint.x - crossings.at(b).midpoint.x;

            const double dy =
                crossings.at(a).midpoint.y - crossings.at(b).midpoint.y;

            if (dx * dx + dy * dy < pointTolerance * pointTolerance)
                return false;
        }
    }

    return true;
}

// Number of crossings that are first met as an UNDER crossing when walking
// from the base point (so that they would have to be changed).
int countFlips(
    const QVector<CrossingInfo> &crossings,
    double baseFraction,
    int directionSign)
{
    int flips = 0;

    for (const CrossingInfo &crossing : crossings)
    {
        double ua = directionSign > 0
                        ? crossing.fractionA - baseFraction
                        : baseFraction - crossing.fractionA;

        double ub = directionSign > 0
                        ? crossing.fractionB - baseFraction
                        : baseFraction - crossing.fractionB;

        ua -= std::floor(ua);
        ub -= std::floor(ub);

        const bool firstIsA = ua < ub;

        if (crossing.aIsOver != firstIsA)
            ++flips;
    }

    return flips;
}

struct BaseChoice
{
    bool found = false;
    bool windowFree = false;

    int flips = std::numeric_limits<int>::max();
    int base = 0;
    int directionSign = 1;

    // direction of the supporting line at the base point
    double supportAngle = 0.0;
};

/*
 * The base point is an extreme point of the planar curve (so that the
 * curve lies on one side of a line through it), no crossing may be close
 * to it, and the return strand must be able to swing out into the free half
 * plane. All three are needed to make the final isotopy collision free.
 */
BaseChoice chooseBase(
    const QVector<Vec3> &points,
    const QVector<CrossingInfo> &crossings,
    int windowSamples,
    double rayClearance)
{
    BaseChoice best;

    const int n = points.size();

    if (n < 8)
        return best;

    constexpr int angleCount = 36;

    QVector<int> tried;

    const double window =
        static_cast<double>(windowSamples + 1) / static_cast<double>(n);

    for (int k = 0; k < angleCount; ++k)
    {
        const double phi =
            2.0 * Pi * static_cast<double>(k) /
            static_cast<double>(angleCount);

        const double ux = std::cos(phi);
        const double uy = std::sin(phi);

        int base = 0;
        double bestDot = -std::numeric_limits<double>::max();

        for (int i = 0; i < n; ++i)
        {
            const double dot = points.at(i).x * ux + points.at(i).y * uy;

            if (dot > bestDot)
            {
                bestDot = dot;
                base = i;
            }
        }

        if (tried.contains(base))
            continue;

        tried.push_back(base);

        const double fraction =
            static_cast<double>(base) / static_cast<double>(n);

        bool free = true;

        for (const CrossingInfo &crossing : crossings)
        {
            if (circularDistance(crossing.fractionA, fraction) < window ||
                circularDistance(crossing.fractionB, fraction) < window)
            {
                free = false;
                break;
            }
        }

        for (int sign = 1; sign >= -1; sign -= 2)
        {
            const int flips = countFlips(crossings, fraction, sign);

            const bool better =
                !best.found ||
                (free && !best.windowFree) ||
                (free == best.windowFree && flips < best.flips);

            // The ray test is by far the most expensive check, so it is only
            // made for a base that would be chosen.
            if (better &&
                swingRayClear(
                    points, base, sign, windowSamples, ux, uy, rayClearance))
            {
                best.found = true;
                best.windowFree = free;
                best.flips = flips;
                best.base = base;
                best.directionSign = sign;
                best.supportAngle = phi;
            }
        }
    }

    return best;
}

} // namespace

UnknottingAnimation UnknottingAnimation::build(
    const QVector<Vec3> &curve,
    double tubeRadius,
    int samples)
{
    /*
     * A curve with many crossings that lie close together needs a finer
     * polyline: the heights of two neighbouring crossings can only be set
     * independently if there are enough samples between them. The animation
     * checks itself and is rebuilt with more samples if necessary.
     */
    int count = std::clamp(samples, 96, 400);

    UnknottingAnimation result;

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        result = buildOnce(curve, tubeRadius, count);

        if (!result.isValid() || result.isVerified())
            break;

        count = std::min(600, static_cast<int>(count * 1.5));
    }

    return result;
}

UnknottingAnimation UnknottingAnimation::buildOnce(
    const QVector<Vec3> &curve,
    double tubeRadius,
    int samples)
{
    UnknottingAnimation invalid;

    if (curve.size() < 8)
        return invalid;

    const int requested = std::clamp(samples, 96, 600);

    const QVector<Vec3> points = resampleByArcLength(curve, requested, true);

    const int n = points.size();

    if (n < 32)
        return invalid;

    Vec3 center;

    for (const Vec3 &point : points)
        center += point;

    center = center / static_cast<double>(n);

    QVector<Vec3> relative;
    relative.reserve(n);

    double extent = 0.0;

    for (const Vec3 &point : points)
    {
        relative.push_back(point - center);
        extent = std::max(extent, relative.last().length());
    }

    if (extent < 1.0e-9 || !finiteValue(extent))
        return invalid;

    const int windowSamples = std::max(
        4,
        static_cast<int>(std::lround(0.04 * static_cast<double>(n))));

    const double radius = std::max(0.02, tubeRadius);

    double curveLength = 0.0;

    for (int i = 0; i < n; ++i)
        curveLength += (relative.at((i + 1) % n) - relative.at(i)).length();

    // Parts of the curve closer than this (along the curve) are neighbours.
    const int skipSamples = std::clamp(
        static_cast<int>(std::ceil(
            3.0 * radius * static_cast<double>(n) /
            std::max(curveLength, 1.0e-9))),
        4,
        std::max(4, n / 10));

    /*
     * ---------------------------------------------------------
     * Search viewing directions / base points / walking
     * directions. Every usable combination becomes a candidate.
     * ---------------------------------------------------------
     */

    struct Candidate
    {
        AlignRotation rotation;
        QVector<Vec3> rotated;
        QVector<CrossingInfo> crossings;
        BaseChoice base;
        double clearance = 0.0;
    };

    QVector<Candidate> candidates;

    const QVector<Vec3> directions = candidateViewDirections();

    for (const Vec3 &direction : directions)
    {
        Candidate candidate;

        candidate.rotation = rotationToZ(direction);

        candidate.rotated.reserve(n);

        for (const Vec3 &point : relative)
        {
            candidate.rotated.push_back(
                rotateAboutAxis(
                    point,
                    candidate.rotation.axis,
                    candidate.rotation.angle));
        }

        candidate.crossings =
            detectProjectionCrossings(candidate.rotated, 2, n);

        if (candidate.crossings.size() > 600)
            continue;

        if (!diagramIsRobust(candidate.crossings, extent))
            continue;

        candidate.base = chooseBase(
            candidate.rotated,
            candidate.crossings,
            windowSamples,
            0.6 * radius);

        if (!candidate.base.found)
            continue;

        candidate.clearance = projectionClearance(
            candidate.rotated,
            candidate.crossings,
            skipSamples);

        candidates.push_back(candidate);
    }

    if (candidates.isEmpty())
        return invalid;

    /*
     * Fewest crossing changes first. Among equals: diagrams whose strands
     * keep enough room (so that the tubes do not touch), then fewer
     * crossings, then more room.
     */
    const double goodClearance = 2.4 * radius;

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [goodClearance](const Candidate &a, const Candidate &b)
        {
            if (a.base.windowFree != b.base.windowFree)
                return a.base.windowFree;

            if (a.base.flips != b.base.flips)
                return a.base.flips < b.base.flips;

            const bool goodA = a.clearance >= goodClearance;
            const bool goodB = b.clearance >= goodClearance;

            if (goodA != goodB)
                return goodA;

            if (goodA && a.crossings.size() != b.crossings.size())
                return a.crossings.size() < b.crossings.size();

            return a.clearance > b.clearance;
        });

    /*
     * ---------------------------------------------------------
     * Builds the complete plan for one candidate.
     * ---------------------------------------------------------
     */

    auto construct = [&](const Candidate &best) -> UnknottingAnimation
    {
        UnknottingAnimation built;

        const int base = best.base.base;
        const int sign = best.base.directionSign;

        auto sourceIndex = [&](int i)
        {
            return (((base + sign * i) % n) + n) % n;
        };

        built.n_ = n;
        built.center_ = center;
        built.axis_ = best.rotation.axis;
        built.angle_ = best.rotation.angle;
        built.requestedRadius_ = radius;

        built.originalRel_.reserve(n);
        built.rotatedRel_.reserve(n);

        for (int i = 0; i < n; ++i)
        {
            built.originalRel_.push_back(relative.at(sourceIndex(i)));
            built.rotatedRel_.push_back(best.rotated.at(sourceIndex(i)));
        }

        const double baseFraction =
            static_cast<double>(base) / static_cast<double>(n);

        /*
         * Crossings in walking order
         */

        const int crossingCount = best.crossings.size();

        built.diagramCrossings_ = crossingCount;

        QVector<bool> needsChange(crossingCount, false);
        QVector<bool> aOver(crossingCount, true);

        for (int c = 0; c < crossingCount; ++c)
        {
            const CrossingInfo &crossing = best.crossings.at(c);

            double ua = sign > 0 ? crossing.fractionA - baseFraction
                                 : baseFraction - crossing.fractionA;

            double ub = sign > 0 ? crossing.fractionB - baseFraction
                                 : baseFraction - crossing.fractionB;

            ua -= std::floor(ua);
            ub -= std::floor(ub);

            built.crossingU_[0].push_back(ua);
            built.crossingU_[1].push_back(ub);

            aOver[c] = crossing.aIsOver;

            const bool firstIsA = ua < ub;

            needsChange[c] = crossing.aIsOver != firstIsA;
        }

        /*
         * Dimensions
         */

        built.layerHalfGap_ = std::max(1.8 * radius, 0.10 * extent);

        double minimumSeparation = 1.0;

        for (int c = 0; c < crossingCount; ++c)
        {
            minimumSeparation = std::min(
                minimumSeparation,
                std::abs(built.crossingU_[0].at(c) -
                         built.crossingU_[1].at(c)));
        }

        minimumSeparation = std::max(minimumSeparation, 0.02);

        double heightRange = std::max(
            2.2 * built.layerHalfGap_,
            0.45 * extent);

        heightRange = std::max(heightRange, 1.5 * radius / minimumSeparation);
        heightRange = std::min(heightRange, 1.6 * extent + 4.0 * radius);

        /*
         * Descending target heights (a staircase going down along the
         * curve, then a short return strand going up again)
         */

        const int returnSamples = windowSamples;

        built.returnSamples_ = returnSamples;
        built.ringRadius_ = heightRange;

        built.targetHeight_.reserve(n);
        built.alpha_.reserve(n);
        built.alphaUniform_.reserve(n);

        for (int i = 0; i < n; ++i)
        {
            double z = 0.0;
            double alpha = 0.0;

            if (i <= n - returnSamples)
            {
                const double x =
                    static_cast<double>(i) /
                    static_cast<double>(n - returnSamples);

                z = heightRange * (1.0 - 2.0 * x);
                alpha = Pi * x;
            }
            else
            {
                const double x =
                    static_cast<double>(i - (n - returnSamples)) /
                    static_cast<double>(returnSamples);

                z = -heightRange + 2.0 * heightRange * smoothStep01(x);
                alpha = Pi + Pi * x;
            }

            built.targetHeight_.push_back(z);
            built.alpha_.push_back(alpha);

            built.alphaUniform_.push_back(
                2.0 * Pi * static_cast<double>(i) / static_cast<double>(n));
        }

        built.outwardX_ = std::cos(best.base.supportAngle);
        built.outwardY_ = std::sin(best.base.supportAngle);

        built.baseRel_ = built.rotatedRel_.at(0);

        /*
         * Height nodes (one pair per crossing)
         */

        for (int c = 0; c < crossingCount; ++c)
        {
            for (int which = 0; which < 2; ++which)
            {
                Node node;

                node.u = built.crossingU_[which].at(c);
                node.crossing = c;
                node.which = which;

                built.nodes_.push_back(node);
            }
        }

        std::sort(
            built.nodes_.begin(),
            built.nodes_.end(),
            [](const Node &a, const Node &b)
            {
                return a.u < b.u;
            });

        built.crossingNode_[0].fill(0, crossingCount);
        built.crossingNode_[1].fill(0, crossingCount);

        for (int k = 0; k < built.nodes_.size(); ++k)
        {
            const Node &node = built.nodes_.at(k);

            built.crossingNode_[node.which][node.crossing] = k;

            const bool over =
                node.which == 0 ? aOver.at(node.crossing)
                                : !aOver.at(node.crossing);

            built.nodeInit_.push_back(
                over ? built.layerHalfGap_ : -built.layerHalfGap_);
        }

        const int nodeCount = built.nodes_.size();

        built.sampleLow_.fill(0, n);
        built.sampleHigh_.fill(0, n);
        built.sampleWeight_.fill(0.0, n);

        if (nodeCount > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                const double u =
                    static_cast<double>(i) / static_cast<double>(n);

                int low = nodeCount - 1;

                for (int k = 0; k < nodeCount; ++k)
                {
                    if (built.nodes_.at(k).u <= u)
                        low = k;
                    else
                        break;
                }

                const int high = (low + 1) % nodeCount;

                double gap =
                    built.nodes_.at(high).u - built.nodes_.at(low).u;

                if (gap <= 0.0)
                    gap += 1.0;

                double distance = u - built.nodes_.at(low).u;

                if (distance < 0.0)
                    distance += 1.0;

                built.sampleLow_[i] = low;
                built.sampleHigh_[i] = high;
                built.sampleWeight_[i] = smoothStep01(distance / gap);
            }
        }

        /*
         * Order of the crossing changes (along the curve)
         */

        for (int c = 0; c < crossingCount; ++c)
        {
            if (needsChange.at(c))
                built.flipOrder_.push_back(c);
        }

        std::sort(
            built.flipOrder_.begin(),
            built.flipOrder_.end(),
            [&built](int a, int b)
            {
                const double ua = std::min(
                    built.crossingU_[0].at(a),
                    built.crossingU_[1].at(a));

                const double ub = std::min(
                    built.crossingU_[0].at(b),
                    built.crossingU_[1].at(b));

                return ua < ub;
            });

        built.changes_ = built.flipOrder_.size();

        /*
         * Timeline
         */

        const double alignDuration = built.angle_ > 0.05 ? 1.3 : 0.0;
        const double layerDuration = 1.0;

        built.slot_ = std::clamp(
            1.4 - 0.06 * static_cast<double>(built.changes_),
            0.45,
            1.4);

        const double changeDuration =
            built.slot_ * static_cast<double>(built.changes_);

        const double settleDuration = 1.1;
        const double shrinkDuration = 1.6;
        const double roundDuration = 0.9;
        const double flattenDuration = 1.3;

        built.alignEnd_ = alignDuration;
        built.layerEnd_ = built.alignEnd_ + layerDuration;
        built.changeEnd_ = built.layerEnd_ + changeDuration;
        built.settleEnd_ = built.changeEnd_ + settleDuration;
        built.shrinkEnd_ = built.settleEnd_ + shrinkDuration;
        built.untangleEnd_ = built.shrinkEnd_ + roundDuration;
        built.flattenEnd_ = built.untangleEnd_ + flattenDuration;

        const double total = built.flattenEnd_;

        for (int j = 0; j < built.changes_; ++j)
        {
            const int c = built.flipOrder_.at(j);

            const CrossingInfo &crossing = best.crossings.at(c);

            UnknottingEvent event;

            event.number = j + 1;

            event.time =
                (built.layerEnd_ +
                 built.slot_ * (static_cast<double>(j) + 0.5)) /
                total;

            event.position =
                center + Vec3(crossing.pointA.x, crossing.pointA.y, 0.0);

            built.events_.push_back(event);
        }

        built.valid_ = true;

        built.verified_ = built.verify();

        /*
         * Bounds of the whole animation
         */

        bool first = true;

        for (int step = 0; step <= 120; ++step)
        {
            const QVector<Vec3> frame =
                built.curveAt(static_cast<double>(step) / 120.0);

            for (const Vec3 &p : frame)
            {
                if (first)
                {
                    built.boundsMin_ = p;
                    built.boundsMax_ = p;
                    first = false;

                    continue;
                }

                built.boundsMin_.x = std::min(built.boundsMin_.x, p.x);
                built.boundsMin_.y = std::min(built.boundsMin_.y, p.y);
                built.boundsMin_.z = std::min(built.boundsMin_.z, p.z);

                built.boundsMax_.x = std::max(built.boundsMax_.x, p.x);
                built.boundsMax_.y = std::max(built.boundsMax_.y, p.y);
                built.boundsMax_.z = std::max(built.boundsMax_.z, p.z);
            }
        }

        return built;
    };

    /*
     * ---------------------------------------------------------
     * Try the candidates in order. A plan is accepted when it
     * passes its own checks (right crossing changes) and the
     * tubes keep their distance. If no plan has the full
     * clearance, the roomiest one is used with a thinner tube.
     * ---------------------------------------------------------
     */

    constexpr int maxAttempts = 18;

    const double acceptable = 1.05 * radius;

    // Below this the plan is not shown as "verified".
    const double floorClearance = std::max(0.004, 0.010 * extent);

    UnknottingAnimation fallback;

    double fallbackClearance = -1.0;

    int attempts = 0;

    for (const Candidate &candidate : candidates)
    {
        if (attempts++ >= maxAttempts)
            break;

        UnknottingAnimation built = construct(candidate);

        if (!built.valid_ || !built.verified_)
            continue;

        // Plans that cannot beat the best one so far are dropped early.
        const double stopBelow =
            std::max(fallbackClearance, 0.0);

        built.minClearance_ = built.measureClearance(150, stopBelow);

        if (built.minClearance_ < stopBelow)
            continue;

        if (built.minClearance_ >= acceptable)
            return built;

        if (built.minClearance_ > fallbackClearance)
        {
            fallbackClearance = built.minClearance_;
            fallback = built;
        }
    }

    if (fallback.valid_)
    {
        if (fallbackClearance < floorClearance)
            fallback.verified_ = false;

        return fallback;
    }

    // Nothing passed the checks. Hand back the first plan, flagged as not
    // verified, so that the caller can still show something.
    UnknottingAnimation unverified = construct(candidates.first());

    unverified.verified_ = false;
    unverified.minClearance_ = 0.0;

    return unverified;
}

bool UnknottingAnimation::isValid() const
{
    return valid_;
}

bool UnknottingAnimation::isVerified() const
{
    return verified_;
}

/*
 * Self check of the diagrams: the diagram after the "Layer" stage must
 * have the same crossings as the original diagram, `changes_` of them must
 * be wrong (under first), after the crossing changes none, and between
 * two pass-throughs nothing may change.
 */
bool UnknottingAnimation::verify() const
{
    if (!valid_ || flattenEnd_ <= 0.0)
        return false;

    auto inspect = [this](double seconds, int &count, int &wrong)
    {
        const QVector<Vec3> frame = curveAt(seconds / flattenEnd_);

        const QVector<CrossingInfo> crossings =
            detectProjectionCrossings(frame, 2, frame.size());

        count = crossings.size();
        wrong = 0;

        for (const CrossingInfo &crossing : crossings)
        {
            if (!crossing.aIsOver)
                ++wrong;
        }
    };

    int count = 0;
    int wrong = 0;

    inspect(layerEnd_ + 1.0e-6, count, wrong);

    if (count != diagramCrossings_ || wrong != changes_)
        return false;

    inspect(changeEnd_ + 1.0e-6, count, wrong);

    if (count != diagramCrossings_ || wrong != 0)
        return false;

    inspect(settleEnd_ - 1.0e-6, count, wrong);

    if (count != diagramCrossings_ || wrong != 0)
        return false;

    constexpr double locals[] = {0.04, 0.20, 0.36, 0.64, 0.80, 0.96};

    for (int j = 0; j < changes_; ++j)
    {
        for (const double local : locals)
        {
            inspect(
                layerEnd_ + slot_ * (static_cast<double>(j) + local),
                count,
                wrong);

            const int expected = changes_ - j - (local > 0.5 ? 1 : 0);

            if (count != diagramCrossings_ || wrong != expected)
                return false;
        }
    }

    return true;
}

double UnknottingAnimation::measureClearance(
    int frames,
    double stopBelow) const
{
    if (!valid_ || n_ < 8 || frames < 1)
        return 0.0;

    const double arcSkip = 4.0 * requestedRadius_;

    const int stride = n_ > 160 ? 2 : 1;

    double best = std::numeric_limits<double>::max();

    QVector<Vec3> sampled;
    QVector<int> index;
    QVector<double> arc;
    QVector<Vec3> boxLo;
    QVector<Vec3> boxHi;

    for (int f = 0; f <= frames; ++f)
    {
        const double t = static_cast<double>(f) / static_cast<double>(frames);
        const double tau = t * flattenEnd_;

        // While two strands pass through each other the pair that is
        // involved is not counted.
        int activeEvent = -1;

        if (changes_ > 0 && tau >= layerEnd_ && tau < changeEnd_)
        {
            const double progress = (tau - layerEnd_) / slot_;

            const int j = std::min(
                static_cast<int>(std::floor(progress)),
                changes_ - 1);

            const double local = progress - static_cast<double>(j);

            if (local > 0.08 && local < 0.92)
                activeEvent = j;
        }

        const QVector<Vec3> full = curveAt(t);

        sampled.clear();
        index.clear();

        for (int i = 0; i < n_; i += stride)
        {
            sampled.push_back(full.at(i));
            index.push_back(i);
        }

        const int m = sampled.size();

        arc.resize(m + 1);
        arc[0] = 0.0;

        for (int i = 0; i < m; ++i)
        {
            arc[i + 1] =
                arc.at(i) + (sampled.at((i + 1) % m) - sampled.at(i)).length();
        }

        const double total = arc.at(m);

        double eventU0 = -1.0;
        double eventU1 = -1.0;

        if (activeEvent >= 0)
        {
            const int c = flipOrder_.at(activeEvent);

            eventU0 = crossingU_[0].at(c);
            eventU1 = crossingU_[1].at(c);
        }

        /*
         * Segments are grouped into chunks with a bounding box. Two chunks
         * that are further apart than the best distance so far cannot lower
         * it, so their segment pairs are skipped (same result, much faster).
         */
        constexpr int chunk = 8;

        const int chunkCount = (m + chunk - 1) / chunk;

        boxLo.resize(chunkCount);
        boxHi.resize(chunkCount);

        for (int c = 0; c < chunkCount; ++c)
        {
            Vec3 lo = sampled.at(c * chunk);
            Vec3 hi = lo;

            for (int i = c * chunk; i < std::min(m, (c + 1) * chunk); ++i)
            {
                const Vec3 &p = sampled.at((i + 1) % m);

                lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
                hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
            }

            boxLo[c] = lo;
            boxHi[c] = hi;
        }

        // Distance of segments i < j; true once it is below `stopBelow`.
        auto testPair = [&](int i, int j)
        {
            // Gap along the curve between the two segments (from the end of
            // one to the start of the other, both ways round), so that
            // neighbouring segments, which share an end point (distance 0),
            // are always skipped, however long they are.
            const double gap = arc.at(j) - arc.at(i + 1);
            const double gapAround = total - arc.at(j + 1) + arc.at(i);

            if (std::min(gap, gapAround) < arcSkip)
                return false;

            if (activeEvent >= 0)
            {
                const double fi =
                    static_cast<double>(index.at(i)) /
                    static_cast<double>(n_);

                const double fj =
                    static_cast<double>(index.at(j)) /
                    static_cast<double>(n_);

                constexpr double near = 0.06;

                const bool pairA =
                    circularDistance(fi, eventU0) < near &&
                    circularDistance(fj, eventU1) < near;

                const bool pairB =
                    circularDistance(fi, eventU1) < near &&
                    circularDistance(fj, eventU0) < near;

                if (pairA || pairB)
                    return false;
            }

            const double d = segmentDistance3D(
                sampled.at(i),
                sampled.at((i + 1) % m),
                sampled.at(j),
                sampled.at((j + 1) % m));

            best = std::min(best, d);

            return best < stopBelow;
        };

        for (int ca = 0; ca < chunkCount; ++ca)
        {
            for (int cb = ca; cb < chunkCount; ++cb)
            {
                const Vec3 &loA = boxLo.at(ca);
                const Vec3 &hiA = boxHi.at(ca);
                const Vec3 &loB = boxLo.at(cb);
                const Vec3 &hiB = boxHi.at(cb);

                const double gapX = std::max({0.0, loB.x - hiA.x, loA.x - hiB.x});
                const double gapY = std::max({0.0, loB.y - hiA.y, loA.y - hiB.y});
                const double gapZ = std::max({0.0, loB.z - hiA.z, loA.z - hiB.z});

                if (gapX * gapX + gapY * gapY + gapZ * gapZ >= best * best)
                    continue;

                const int iEnd = std::min(m, (ca + 1) * chunk);
                const int jEnd = std::min(m, (cb + 1) * chunk);

                for (int i = ca * chunk; i < iEnd; ++i)
                {
                    const int jStart = (ca == cb) ? i + 1 : cb * chunk;

                    for (int j = jStart; j < jEnd; ++j)
                    {
                        if (testPair(i, j))
                            return best;
                    }
                }
            }
        }
    }

    return best;
}

int UnknottingAnimation::crossingChanges() const
{
    return changes_;
}

int UnknottingAnimation::diagramCrossings() const
{
    return diagramCrossings_;
}

double UnknottingAnimation::totalSeconds() const
{
    return flattenEnd_;
}

double UnknottingAnimation::minimumClearance() const
{
    return minClearance_;
}

double UnknottingAnimation::suggestedTubeRadius() const
{
    const double diagonal = (boundsMax_ - boundsMin_).length();

    const double floorRadius = std::max(0.004, 0.006 * diagonal);

    return std::clamp(
        0.46 * minClearance_,
        std::min(floorRadius, requestedRadius_),
        requestedRadius_);
}

const QVector<UnknottingEvent> &UnknottingAnimation::events() const
{
    return events_;
}

double UnknottingAnimation::finalRingRadius() const
{
    return ringRadius_;
}

Vec3 UnknottingAnimation::boundsMin() const
{
    return boundsMin_;
}

Vec3 UnknottingAnimation::boundsMax() const
{
    return boundsMax_;
}

/*
 * changeProgress: number of finished changes plus the progress of the
 * running one (0..1).
 */
QVector<double> UnknottingAnimation::nodeValuesAt(double changeProgress) const
{
    QVector<double> values = nodeInit_;

    for (int j = 0; j < flipOrder_.size(); ++j)
    {
        const double local =
            std::clamp(changeProgress - static_cast<double>(j), 0.0, 1.0);

        if (local <= 0.0)
            break;

        const double eased = smoothStep01((local - 0.12) / 0.76);

        const int c = flipOrder_.at(j);

        for (int which = 0; which < 2; ++which)
        {
            const int node = crossingNode_[which].at(c);

            values[node] = nodeInit_.at(node) * (1.0 - 2.0 * eased);
        }
    }

    return values;
}

QVector<double> UnknottingAnimation::layerHeights(
    const QVector<double> &nodeValues) const
{
    QVector<double> heights;

    heights.fill(0.0, n_);

    if (nodes_.isEmpty())
        return heights;

    for (int i = 0; i < n_; ++i)
    {
        const double w = sampleWeight_.at(i);

        heights[i] =
            (1.0 - w) * nodeValues.at(sampleLow_.at(i)) +
            w * nodeValues.at(sampleHigh_.at(i));
    }

    return heights;
}

QVector<Vec3> UnknottingAnimation::curveAt(
    double t,
    QVector<float> *highlight,
    Stage *stage,
    int *changeNumber) const
{
    QVector<Vec3> output;

    if (highlight != nullptr)
        highlight->fill(0.0f, n_);

    if (stage != nullptr)
        *stage = Stage::Done;

    if (changeNumber != nullptr)
        *changeNumber = 0;

    if (!valid_)
        return output;

    const double tau = std::clamp(t, 0.0, 1.0) * flattenEnd_;

    output.reserve(n_);

    auto lerp = [](double a, double b, double x)
    {
        return a + (b - a) * x;
    };

    // The final loop lives in the vertical plane through the base point
    // that contains the outward direction (u) and the height axis.
    const double ux = outwardX_;
    const double uy = outwardY_;

    const double vx = -outwardY_;
    const double vy = outwardX_;

    const double ax = baseRel_.x;
    const double ay = baseRel_.y;

    const double R = ringRadius_;

    const int bodyEnd = n_ - returnSamples_;

    if (tau < alignEnd_)
    {
        const double e = smootherStep01(tau / alignEnd_);

        for (int i = 0; i < n_; ++i)
        {
            output.push_back(
                center_ +
                rotateAboutAxis(originalRel_.at(i), axis_, angle_ * e));
        }

        if (stage != nullptr)
            *stage = Stage::Align;

        return output;
    }

    if (tau < layerEnd_)
    {
        const double e =
            smootherStep01((tau - alignEnd_) / (layerEnd_ - alignEnd_));

        const QVector<double> z = layerHeights(nodeValuesAt(0.0));

        for (int i = 0; i < n_; ++i)
        {
            const Vec3 &p = rotatedRel_.at(i);

            output.push_back(
                center_ + Vec3(p.x, p.y, lerp(p.z, z.at(i), e)));
        }

        if (stage != nullptr)
            *stage = Stage::Layer;

        return output;
    }

    if (tau < changeEnd_)
    {
        const double progress = (tau - layerEnd_) / slot_;

        const int current = std::min(
            static_cast<int>(std::floor(progress)),
            changes_ - 1);

        const double local = progress - static_cast<double>(current);

        const QVector<double> z = layerHeights(nodeValuesAt(progress));

        for (int i = 0; i < n_; ++i)
        {
            const Vec3 &p = rotatedRel_.at(i);

            output.push_back(center_ + Vec3(p.x, p.y, z.at(i)));
        }

        if (highlight != nullptr && current >= 0)
        {
            const int c = flipOrder_.at(current);

            const double activity =
                std::sin(Pi * std::clamp(local, 0.0, 1.0));

            constexpr double window = 0.05;

            for (int i = 0; i < n_; ++i)
            {
                const double u =
                    static_cast<double>(i) / static_cast<double>(n_);

                // signed: + on the strand that was on top, - on the other
                double weight = 0.0;

                for (int which = 0; which < 2; ++which)
                {
                    const double d =
                        circularDistance(u, crossingU_[which].at(c));

                    if (d < window)
                    {
                        const double w =
                            0.5 * (1.0 + std::cos(Pi * d / window));

                        const double side =
                            nodeInit_.at(crossingNode_[which].at(c)) > 0.0
                                ? 1.0
                                : -1.0;

                        if (w > std::abs(weight))
                            weight = side * w;
                    }
                }

                (*highlight)[i] = static_cast<float>(weight * activity);
            }
        }

        if (stage != nullptr)
            *stage = Stage::Change;

        if (changeNumber != nullptr)
            *changeNumber = current + 1;

        return output;
    }

    if (tau < settleEnd_)
    {
        const double e =
            smootherStep01((tau - changeEnd_) / (settleEnd_ - changeEnd_));

        const QVector<double> z =
            layerHeights(nodeValuesAt(static_cast<double>(changes_)));

        for (int i = 0; i < n_; ++i)
        {
            const Vec3 &p = rotatedRel_.at(i);

            output.push_back(
                center_ +
                Vec3(p.x, p.y, lerp(z.at(i), targetHeight_.at(i), e)));
        }

        if (stage != nullptr)
            *stage = Stage::Settle;

        return output;
    }

    if (tau < shrinkEnd_)
    {
        /*
         * The whole diagram is shrunk towards the base point (all
         * crossings stay as they are, the heights do not change) while
         * the return strand swings out along `u` into the free half plane.
         */

        const double e =
            smoothStep01((tau - settleEnd_) / (shrinkEnd_ - settleEnd_));

        for (int i = 0; i < n_; ++i)
        {
            const Vec3 &p = rotatedRel_.at(i);

            double x = ax + (1.0 - e) * (p.x - ax);
            double y = ay + (1.0 - e) * (p.y - ay);
            double z = targetHeight_.at(i);

            if (i > bodyEnd)
            {
                const double alpha = alpha_.at(i);

                const double lambda = -R * std::sin(alpha);

                x += e * ux * lambda;
                y += e * uy * lambda;

                z = lerp(z, R * std::cos(alpha), e);
            }

            output.push_back(center_ + Vec3(x, y, z));
        }

        if (stage != nullptr)
            *stage = Stage::Untangle;

        return output;
    }

    if (tau < untangleEnd_)
    {
        /*
         * "D" shape (vertical line + half circle) is rounded into a
         * circle: the line bulges out to the other side.
         */

        const double e =
            smoothStep01((tau - shrinkEnd_) / (untangleEnd_ - shrinkEnd_));

        for (int i = 0; i < n_; ++i)
        {
            const double alpha = alpha_.at(i);

            const double circleLambda = -R * std::sin(alpha);
            const double circleHeight = R * std::cos(alpha);

            double lambda = circleLambda;
            double z = circleHeight;

            if (i <= bodyEnd)
            {
                lambda = e * circleLambda;
                z = lerp(targetHeight_.at(i), circleHeight, e);
            }

            output.push_back(
                center_ + Vec3(ax + ux * lambda, ay + uy * lambda, z));
        }

        if (stage != nullptr)
            *stage = Stage::Untangle;

        return output;
    }

    /*
     * Flatten: the circle (in the vertical plane) is laid down into the
     * horizontal plane around the original centre, and the points slide to
     * an even spacing along it. This is a rigid motion of a plain circle.
     */

    const double e = tau < flattenEnd_
                         ? smootherStep01(
                               (tau - untangleEnd_) /
                               (flattenEnd_ - untangleEnd_))
                         : 1.0;

    const double slide = smoothStep01(e / 0.6);

    const double theta = e * 0.5 * Pi;

    const double sinTheta = e >= 1.0 ? 1.0 : std::sin(theta);
    const double cosTheta = e >= 1.0 ? 0.0 : std::cos(theta);

    const double cx = (1.0 - e) * ax;
    const double cy = (1.0 - e) * ay;

    for (int i = 0; i < n_; ++i)
    {
        const double alpha = lerp(alpha_.at(i), alphaUniform_.at(i), slide);

        const double lambda = -R * std::sin(alpha);
        const double height = R * std::cos(alpha);

        output.push_back(
            center_ +
            Vec3(
                cx + ux * lambda + vx * height * sinTheta,
                cy + uy * lambda + vy * height * sinTheta,
                height * cosTheta));
    }

    if (stage != nullptr)
        *stage = tau < flattenEnd_ ? Stage::Flatten : Stage::Done;

    return output;
}

void colorMeshByHighlight(
    Mesh &mesh,
    const QColor &base,
    const QColor &hot,
    const QVector<float> &weights)
{
    if (mesh.ringCount <= 0 || mesh.ringSegments <= 0)
        return;

    const int vertexCount = mesh.positions.size() / 3;

    mesh.colors.fill(1.0f, vertexCount * 3);

    for (int ring = 0; ring < mesh.ringCount; ++ring)
    {
        const float w =
            ring < weights.size()
                ? std::clamp(weights.at(ring), 0.0f, 1.0f)
                : 0.0f;

        const float red =
            static_cast<float>(base.redF()) * (1.0f - w) +
            static_cast<float>(hot.redF()) * w;

        const float green =
            static_cast<float>(base.greenF()) * (1.0f - w) +
            static_cast<float>(hot.greenF()) * w;

        const float blue =
            static_cast<float>(base.blueF()) * (1.0f - w) +
            static_cast<float>(hot.blueF()) * w;

        for (int segment = 0; segment < mesh.ringSegments; ++segment)
        {
            const int vertex = ring * mesh.ringSegments + segment;

            if (vertex * 3 + 2 >= mesh.colors.size())
                continue;

            mesh.colors[vertex * 3] = red;
            mesh.colors[vertex * 3 + 1] = green;
            mesh.colors[vertex * 3 + 2] = blue;
        }
    }
}

/*
 * =============================================================
 * TUBE CLEARANCE
 * =============================================================
 */

namespace
{

struct ClearanceStrand
{
    QVector<Vec3> points;

    // arc length at the start of every point, arc[n] = total length
    QVector<double> arc;

    bool closed = true;
};

// Bounding box of the segments first..last-1 of one strand.
struct ClearanceBox
{
    int strand = 0;
    int first = 0;
    int last = 0;

    Vec3 lo;
    Vec3 hi;
};

double boxDistance(const ClearanceBox &a, const ClearanceBox &b)
{
    const double dx = std::max({0.0, a.lo.x - b.hi.x, b.lo.x - a.hi.x});
    const double dy = std::max({0.0, a.lo.y - b.hi.y, b.lo.y - a.hi.y});
    const double dz = std::max({0.0, a.lo.z - b.hi.z, b.lo.z - a.hi.z});

    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

double safeTubeRadius(const CurveData &data, double requestedRadius)
{
    if (!(requestedRadius > 0.0))
        return requestedRadius;

    QVector<ClearanceStrand> strands;

    auto addStrand = [&strands](const QVector<Vec3> &points, bool closed)
    {
        const int n = points.size();

        if (n < 2)
            return;

        ClearanceStrand strand;

        strand.points = points;
        strand.closed = closed;
        strand.arc.fill(0.0, n + 1);

        for (int i = 0; i < n; ++i)
        {
            const bool hasSegment = closed || i + 1 < n;

            strand.arc[i + 1] =
                strand.arc.at(i) +
                (hasSegment
                     ? (points.at((i + 1) % n) - points.at(i)).length()
                     : 0.0);
        }

        strands.push_back(strand);
    };

    if (data.multiStrand)
    {
        for (int k = 0; k < data.separateStrands.size(); ++k)
        {
            const bool closed =
                k < data.strandClosed.size() ? data.strandClosed.at(k) : true;

            if (closed)
                addStrand(data.separateStrands.at(k), true);
        }
    }
    else
    {
        addStrand(data.points, data.closed);
    }

    constexpr double factor = 0.46;
    constexpr int chunkSize = 16;

    // Only distances that would force a thinner tube are resolved.
    const double limit = requestedRadius / factor;
    const double neighbourArc = 4.0 * requestedRadius;

    QVector<ClearanceBox> boxes;

    for (int s = 0; s < strands.size(); ++s)
    {
        const ClearanceStrand &strand = strands.at(s);

        const int n = strand.points.size();
        const int segments = strand.closed ? n : n - 1;

        for (int first = 0; first < segments; first += chunkSize)
        {
            ClearanceBox box;

            box.strand = s;
            box.first = first;
            box.last = std::min(first + chunkSize, segments);
            box.lo = strand.points.at(first);
            box.hi = box.lo;

            for (int i = first + 1; i <= box.last; ++i)
            {
                const Vec3 &p = strand.points.at(i % n);

                box.lo = Vec3(std::min(box.lo.x, p.x),
                              std::min(box.lo.y, p.y),
                              std::min(box.lo.z, p.z));

                box.hi = Vec3(std::max(box.hi.x, p.x),
                              std::max(box.hi.y, p.y),
                              std::max(box.hi.z, p.z));
            }

            boxes.push_back(box);
        }
    }

    double best = limit;

    for (int a = 0; a < boxes.size(); ++a)
    {
        const ClearanceBox &boxA = boxes.at(a);
        const ClearanceStrand &strandA = strands.at(boxA.strand);
        const int nA = strandA.points.size();

        for (int b = a; b < boxes.size(); ++b)
        {
            const ClearanceBox &boxB = boxes.at(b);

            if (boxDistance(boxA, boxB) >= best)
                continue;

            const ClearanceStrand &strandB = strands.at(boxB.strand);
            const int nB = strandB.points.size();
            const bool sameStrand = boxA.strand == boxB.strand;

            for (int i = boxA.first; i < boxA.last; ++i)
            {
                const int jStart = (a == b) ? i + 1 : boxB.first;

                for (int j = jStart; j < boxB.last; ++j)
                {
                    if (sameStrand)
                    {
                        // gap along the curve between the two segments
                        // (j > i on the same strand)
                        double along = strandA.arc.at(j) - strandA.arc.at(i + 1);

                        if (strandA.closed)
                        {
                            along = std::min(
                                along,
                                strandA.arc.at(nA) - strandA.arc.at(j + 1) +
                                    strandA.arc.at(i));
                        }

                        if (along < neighbourArc)
                            continue;
                    }

                    const double d = segmentDistance3D(
                        strandA.points.at(i),
                        strandA.points.at((i + 1) % nA),
                        strandB.points.at(j),
                        strandB.points.at((j + 1) % nB));

                    best = std::min(best, d);
                }
            }
        }
    }

    return std::min(requestedRadius, factor * best);
}

double strandDistance(
    const Vec3 &p1, const Vec3 &q1,
    const Vec3 &p2, const Vec3 &q2)
{
    return segmentDistance3D(p1, q1, p2, q2);
}

/*
 * =============================================================
 * ALEXANDER POLYNOMIAL
 * =============================================================
 *
 * Wirtinger presentation of the diagram: one generator per arc (an arc
 * runs from one under-pass to the next). At a crossing with over arc i,
 * incoming under arc j and outgoing under arc l the relation is
 *
 *   x_l = x_i x_j x_i^-1      (positive crossing)
 *   x_l = x_i^-1 x_j x_i      (negative crossing).
 *
 * The Fox derivatives, abelianised (every x -> t), give the rows
 *
 *   positive:  1 - t   at i,   t     at j,   -1 at l
 *   negative:  1 - 1/t at i,   1/t   at j,   -1 at l
 *              (multiplied by t: t - 1, 1, -t).
 *
 * Any (c-1)x(c-1) minor of this c x c matrix is Delta(t) up to +-t^k.
 */

namespace
{

using IntPolynomial = std::vector<long long>; // index = power of t

constexpr long long PolynomialLimit = 1LL << 62;

bool checkedMultiply(long long a, long long b, long long &result)
{
    if (a == 0 || b == 0)
    {
        result = 0;
        return true;
    }

    if (std::llabs(a) > PolynomialLimit / std::llabs(b))
        return false;

    result = a * b;
    return true;
}

// |a|, |b| <= 2^62, so the sum itself cannot overflow.
bool checkedAdd(long long a, long long b, long long &result)
{
    result = a + b;

    return std::llabs(result) <= PolynomialLimit;
}

void trimPolynomial(IntPolynomial &p)
{
    while (!p.empty() && p.back() == 0)
        p.pop_back();
}

// result = a * b - c * d
bool multiplySubtract(
    const IntPolynomial &a,
    const IntPolynomial &b,
    const IntPolynomial &c,
    const IntPolynomial &d,
    IntPolynomial &result)
{
    const size_t sizeAB =
        a.empty() || b.empty() ? 0 : a.size() + b.size() - 1;

    const size_t sizeCD =
        c.empty() || d.empty() ? 0 : c.size() + d.size() - 1;

    result.assign(std::max(sizeAB, sizeCD), 0);

    long long product = 0;

    for (size_t i = 0; i < a.size(); ++i)
    {
        for (size_t j = 0; j < b.size(); ++j)
        {
            if (!checkedMultiply(a[i], b[j], product) ||
                !checkedAdd(result[i + j], product, result[i + j]))
            {
                return false;
            }
        }
    }

    for (size_t i = 0; i < c.size(); ++i)
    {
        for (size_t j = 0; j < d.size(); ++j)
        {
            if (!checkedMultiply(c[i], d[j], product) ||
                !checkedAdd(result[i + j], -product, result[i + j]))
            {
                return false;
            }
        }
    }

    trimPolynomial(result);

    return true;
}

// Exact division; fails if the remainder is not zero.
bool divideExact(
    const IntPolynomial &numerator,
    const IntPolynomial &denominator,
    IntPolynomial &quotient)
{
    if (denominator.empty())
        return false;

    if (numerator.empty())
    {
        quotient.clear();
        return true;
    }

    if (numerator.size() < denominator.size())
        return false;

    IntPolynomial rest = numerator;

    const size_t degree = denominator.size() - 1;
    const long long lead = denominator.back();

    quotient.assign(numerator.size() - degree, 0);

    for (size_t k = quotient.size(); k-- > 0;)
    {
        const long long top = rest[k + degree];

        if (top % lead != 0)
            return false;

        const long long q = top / lead;

        quotient[k] = q;

        if (q == 0)
            continue;

        long long product = 0;

        for (size_t j = 0; j <= degree; ++j)
        {
            if (!checkedMultiply(q, denominator[j], product) ||
                !checkedAdd(rest[k + j], -product, rest[k + j]))
            {
                return false;
            }
        }
    }

    for (const long long r : rest)
    {
        if (r != 0)
            return false;
    }

    trimPolynomial(quotient);

    return true;
}

// Fraction-free Gaussian elimination (Bareiss) over Z[t].
bool polynomialDeterminant(
    std::vector<std::vector<IntPolynomial>> m,
    IntPolynomial &determinant)
{
    const int size = static_cast<int>(m.size());

    if (size == 0)
    {
        determinant = IntPolynomial{1};
        return true;
    }

    IntPolynomial previous{1};
    bool negative = false;

    for (int k = 0; k < size - 1; ++k)
    {
        if (m[k][k].empty())
        {
            int pivotRow = -1;

            for (int r = k + 1; r < size; ++r)
            {
                if (!m[r][k].empty())
                {
                    pivotRow = r;
                    break;
                }
            }

            if (pivotRow < 0)
            {
                determinant.clear();
                return true;
            }

            std::swap(m[k], m[pivotRow]);
            negative = !negative;
        }

        for (int i = k + 1; i < size; ++i)
        {
            for (int j = k + 1; j < size; ++j)
            {
                IntPolynomial value;

                if (!multiplySubtract(m[i][j], m[k][k], m[i][k], m[k][j], value) ||
                    !divideExact(value, previous, m[i][j]))
                {
                    return false;
                }
            }
        }

        previous = m[k][k];
    }

    determinant = m[size - 1][size - 1];

    if (negative)
    {
        for (long long &coefficient : determinant)
            coefficient = -coefficient;
    }

    return true;
}

// Unnormalised Alexander polynomial of one diagram (false on overflow).
bool alexanderFromCrossings(
    const QVector<CrossingInfo> &crossings,
    IntPolynomial &delta)
{
    const int c = crossings.size();

    if (c == 0)
    {
        delta = IntPolynomial{1};
        return true;
    }

    auto underFraction = [&crossings](int k)
    {
        const CrossingInfo &crossing = crossings.at(k);

        return crossing.aIsOver ? crossing.fractionB : crossing.fractionA;
    };

    // under-passes in walking order; arc m starts at under-pass m
    QVector<int> order(c);
    std::iota(order.begin(), order.end(), 0);

    std::sort(
        order.begin(),
        order.end(),
        [&underFraction](int a, int b)
        {
            return underFraction(a) < underFraction(b);
        });

    QVector<double> unders(c);
    QVector<int> rank(c);

    for (int m = 0; m < c; ++m)
    {
        rank[order.at(m)] = m;
        unders[m] = underFraction(order.at(m));
    }

    auto arcAt = [&unders, c](double fraction)
    {
        const int passed = static_cast<int>(
            std::upper_bound(unders.constBegin(), unders.constEnd(), fraction) -
            unders.constBegin());

        return (passed - 1 + c) % c;
    };

    std::vector<std::vector<IntPolynomial>> matrix(
        static_cast<size_t>(c),
        std::vector<IntPolynomial>(static_cast<size_t>(c)));

    auto add = [&matrix](int row, int column, long long c0, long long c1)
    {
        IntPolynomial &entry =
            matrix[static_cast<size_t>(row)][static_cast<size_t>(column)];

        if (entry.size() < 2)
            entry.resize(2, 0);

        entry[0] += c0;
        entry[1] += c1;

        trimPolynomial(entry);
    };

    for (int k = 0; k < c; ++k)
    {
        const CrossingInfo &crossing = crossings.at(k);

        const double overFraction =
            crossing.aIsOver ? crossing.fractionA : crossing.fractionB;

        const int over = arcAt(overFraction);
        const int out = rank.at(k);
        const int in = (out - 1 + c) % c;

        if (crossing.sign > 0)
        {
            add(k, over, 1, -1);
            add(k, in, 0, 1);
            add(k, out, -1, 0);
        }
        else
        {
            add(k, over, -1, 1);
            add(k, in, 1, 0);
            add(k, out, 0, -1);
        }
    }

    // remove the last row and column
    matrix.pop_back();

    for (std::vector<IntPolynomial> &row : matrix)
        row.pop_back();

    return polynomialDeterminant(matrix, delta);
}

// Divides out t^k and the sign; checks Delta(1) = 1 and the symmetry.
bool normalizeAlexander(IntPolynomial p, AlexanderPolynomial &result)
{
    trimPolynomial(p);

    if (p.empty())
        return false;

    size_t low = 0;

    while (p[low] == 0)
        ++low;

    IntPolynomial core(p.begin() + static_cast<std::ptrdiff_t>(low), p.end());

    const int span = static_cast<int>(core.size()) - 1;

    if (span % 2 != 0)
        return false;

    long long valueAtOne = 0;

    for (const long long coefficient : core)
        valueAtOne += coefficient;

    if (valueAtOne != 1 && valueAtOne != -1)
        return false;

    if (valueAtOne == -1)
    {
        for (long long &coefficient : core)
            coefficient = -coefficient;
    }

    for (int i = 0; i <= span; ++i)
    {
        if (core[static_cast<size_t>(i)] != core[static_cast<size_t>(span - i)])
            return false;
    }

    result.lowestPower = -span / 2;
    result.coefficients.clear();

    std::map<int, long long> terms;

    for (int i = 0; i <= span; ++i)
    {
        result.coefficients.push_back(core[static_cast<size_t>(i)]);

        if (core[static_cast<size_t>(i)] != 0)
            terms[result.lowestPower + i] = core[static_cast<size_t>(i)];
    }

    result.text = formatLaurentPolynomial(terms);

    return true;
}

// Clean (regular) projections of a closed curve from several viewing
// directions, fewest crossings first. Empty (with `error` set) if there is
// none. `projectedCurves` (if given) receives the curve of every diagram,
// centred and turned so that the viewer looks down the z axis.
QVector<QVector<CrossingInfo>> cleanDiagrams(
    const QVector<Vec3> &curve,
    QString &error,
    QVector<QVector<Vec3>> *projectedCurves = nullptr)
{
    QVector<QVector<CrossingInfo>> diagrams;

    QVector<Vec3> points = curve;

    if (points.size() > 3 && (points.first() - points.last()).length() < 1.0e-9)
        points.removeLast();

    if (points.size() > 400)
        points = resampleByArcLength(points, 400, true);

    const int n = points.size();

    if (n < 8)
    {
        error = knotTr("The curve has too few points.");
        return diagrams;
    }

    Vec3 center;

    for (const Vec3 &point : points)
        center += point;

    center = center / static_cast<double>(n);

    double extent = 0.0;

    for (Vec3 &point : points)
    {
        point -= center;
        extent = std::max(extent, point.length());
    }

    if (extent < 1.0e-9 || !finiteValue(extent))
    {
        error = knotTr("The curve is degenerate.");
        return diagrams;
    }

    QVector<QVector<Vec3>> curves;

    for (const Vec3 &direction : candidateViewDirections())
    {
        const AlignRotation rotation = rotationToZ(direction);

        QVector<Vec3> rotated;
        rotated.reserve(n);

        for (const Vec3 &point : points)
            rotated.push_back(rotateAboutAxis(point, rotation.axis, rotation.angle));

        QVector<CrossingInfo> crossings =
            detectProjectionCrossings(rotated, 2, n);

        if (diagramIsRobust(crossings, extent))
        {
            diagrams.push_back(crossings);
            curves.push_back(rotated);
        }
    }

    if (diagrams.isEmpty())
    {
        error =
            knotTr("No clean projection was found (strands too close together).");
        return diagrams;
    }

    QVector<int> order(diagrams.size());

    for (int i = 0; i < order.size(); ++i)
        order[i] = i;

    std::stable_sort(
        order.begin(),
        order.end(),
        [&diagrams](int a, int b)
        {
            return diagrams.at(a).size() < diagrams.at(b).size();
        });

    QVector<QVector<CrossingInfo>> sorted;

    sorted.reserve(order.size());

    for (int i : order)
        sorted.push_back(diagrams.at(i));

    if (projectedCurves != nullptr)
    {
        projectedCurves->clear();

        for (int i : order)
            projectedCurves->push_back(curves.at(i));
    }

    return sorted;
}

} // namespace

bool cleanestProjection(
    const QVector<Vec3> &curve,
    QVector<Vec3> &projected,
    QVector<CrossingInfo> &crossings,
    QString &error)
{
    QVector<QVector<Vec3>> curves;

    const QVector<QVector<CrossingInfo>> diagrams =
        cleanDiagrams(curve, error, &curves);

    if (diagrams.isEmpty())
        return false;

    projected = curves.first();
    crossings = diagrams.first();

    return true;
}

bool cleanProjections(
    const QVector<Vec3> &curve,
    QVector<QVector<Vec3>> &projected,
    QVector<QVector<CrossingInfo>> &crossings,
    QString &error)
{
    crossings = cleanDiagrams(curve, error, &projected);

    return !crossings.isEmpty();
}

AlexanderPolynomial computeAlexanderPolynomial(
    const QVector<Vec3> &curve,
    int maxCrossings)
{
    AlexanderPolynomial result;

    const QVector<QVector<CrossingInfo>> diagrams =
        cleanDiagrams(curve, result.error);

    if (diagrams.isEmpty())
        return result;

    result.diagramCrossings = diagrams.first().size();

    if (result.diagramCrossings > maxCrossings)
    {
        result.error =
            knotTr("The simplest projection found has %1 crossings (limit %2).")
                .arg(result.diagramCrossings)
                .arg(maxCrossings);
        return result;
    }

    IntPolynomial raw;

    if (!alexanderFromCrossings(diagrams.first(), raw))
    {
        result.error = knotTr("The coefficients are too large.");
        return result;
    }

    if (!normalizeAlexander(raw, result))
    {
        result.error =
            knotTr("The projection did not give a valid knot polynomial.");
        return result;
    }

    // An invariant must not depend on the projection: check a second one.
    if (diagrams.size() > 1 && diagrams.at(1).size() <= maxCrossings)
    {
        AlexanderPolynomial second;

        if (alexanderFromCrossings(diagrams.at(1), raw) &&
            normalizeAlexander(raw, second))
        {
            if (second.coefficients != result.coefficients)
            {
                result.error =
                    knotTr("Two projections gave different polynomials.");
                result.text.clear();
                return result;
            }

            result.confirmed = true;
        }
    }

    result.valid = true;

    return result;
}

/*
 * =============================================================
 * JONES POLYNOMIAL
 * =============================================================
 *
 * Kauffman bracket: the curve is cut at all 2c passes into 2c edges. At
 * every crossing the four edge ends are joined in pairs, either by the
 * oriented smoothing (in with out) or by the other one (in with in, out
 * with out). For a positive crossing the oriented smoothing is the
 * A-smoothing, for a negative one it is the B-smoothing. Then
 *
 *   <K> = sum over states  A^(#A - #B) d^(loops - 1),   d = -A² - A^-2
 *   V(t) = (-A³)^(-w) <K>   with A = t^(-1/4),  w = sum of the signs.
 */

namespace
{

JonesPolynomial makeJones(const std::map<int, long long> &terms)
{
    JonesPolynomial result;

    std::map<int, long long> nonzero;

    for (const auto &term : terms)
    {
        if (term.second != 0)
            nonzero[term.first] = term.second;
    }

    if (nonzero.empty())
        return result;

    result.lowestPower = nonzero.begin()->first;

    for (int power = result.lowestPower; power <= nonzero.rbegin()->first; ++power)
    {
        const auto it = nonzero.find(power);
        result.coefficients.push_back(it != nonzero.end() ? it->second : 0);
    }

    result.text = formatLaurentPolynomial(nonzero);
    result.valid = true;

    return result;
}

// V(1) = 1, V'(1) = 0 and V(e^(2 pi i/3)) = 1 hold for every knot.
bool jonesLooksValid(const JonesPolynomial &v)
{
    long long atOne = 0;
    long long derivative = 0;
    long long byResidue[3] = {0, 0, 0};

    for (int i = 0; i < v.coefficients.size(); ++i)
    {
        const long long c = v.coefficients.at(i);
        const int power = v.lowestPower + i;

        atOne += c;
        derivative += c * power;
        byResidue[((power % 3) + 3) % 3] += c;
    }

    // with w = e^(2 pi i/3): S0 + S1 w + S2 w² = (S0 - S2) + (S1 - S2) w
    return atOne == 1 &&
           derivative == 0 &&
           byResidue[1] == byResidue[2] &&
           byResidue[0] - byResidue[2] == 1;
}

int findRoot(int *parent, int x)
{
    while (parent[x] != x)
    {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }

    return x;
}

bool jonesFromCrossings(
    const QVector<CrossingInfo> &crossings,
    JonesPolynomial &result)
{
    const int c = crossings.size();

    if (c == 0)
    {
        result = makeJones({{0, 1}});
        return true;
    }

    // 2^c states, and the counts below must not overflow.
    if (c > 24)
        return false;

    // passes in walking order
    struct PassRef
    {
        double fraction;
        int crossing;
        bool over;
    };

    QVector<PassRef> passes;

    for (int k = 0; k < c; ++k)
    {
        const CrossingInfo &crossing = crossings.at(k);

        passes.push_back({crossing.fractionA, k, crossing.aIsOver});
        passes.push_back({crossing.fractionB, k, !crossing.aIsOver});
    }

    std::sort(
        passes.begin(),
        passes.end(),
        [](const PassRef &a, const PassRef &b)
        {
            return a.fraction < b.fraction;
        });

    const int m = 2 * c; // edges: edge e runs from pass e to pass e + 1

    QVector<int> overPos(c);
    QVector<int> underPos(c);

    for (int p = 0; p < m; ++p)
    {
        if (passes.at(p).over)
            overPos[passes.at(p).crossing] = p;
        else
            underPos[passes.at(p).crossing] = p;
    }

    // per crossing: the two edge pairs joined by the A- and B-smoothing
    QVector<std::array<int, 4>> smoothA(c);
    QVector<std::array<int, 4>> smoothB(c);

    int writhe = 0;

    for (int k = 0; k < c; ++k)
    {
        const int overIn = (overPos.at(k) - 1 + m) % m;
        const int overOut = overPos.at(k);
        const int underIn = (underPos.at(k) - 1 + m) % m;
        const int underOut = underPos.at(k);

        const std::array<int, 4> oriented = {overIn, underOut, underIn, overOut};
        const std::array<int, 4> other = {overIn, underIn, overOut, underOut};

        const bool positive = crossings.at(k).sign > 0;

        writhe += positive ? 1 : -1;

        smoothA[k] = positive ? oriented : other;
        smoothB[k] = positive ? other : oriented;
    }

    // counts[a][loops]: states with `a` A-smoothings and `loops` circles
    std::vector<std::vector<long long>> counts(
        static_cast<size_t>(c + 1),
        std::vector<long long>(static_cast<size_t>(m + 1), 0));

    int parent[48];

    const unsigned long long stateCount = 1ULL << c;

    for (unsigned long long state = 0; state < stateCount; ++state)
    {
        for (int e = 0; e < m; ++e)
            parent[e] = e;

        int loops = m;
        int a = 0;

        for (int k = 0; k < c; ++k)
        {
            const bool useA = ((state >> k) & 1ULL) != 0;

            if (useA)
                ++a;

            const std::array<int, 4> &pairs = useA ? smoothA.at(k) : smoothB.at(k);

            for (int j = 0; j < 4; j += 2)
            {
                const int x = findRoot(parent, pairs[j]);
                const int y = findRoot(parent, pairs[j + 1]);

                if (x != y)
                {
                    parent[x] = y;
                    --loops;
                }
            }
        }

        ++counts[static_cast<size_t>(a)][static_cast<size_t>(loops)];
    }

    // powers of d = -A² - A^-2 (exponent of A -> coefficient)
    std::vector<std::map<int, long long>> dPower(static_cast<size_t>(m + 1));
    dPower[0][0] = 1;

    for (int j = 1; j <= m; ++j)
    {
        for (const auto &term : dPower[static_cast<size_t>(j - 1)])
        {
            dPower[static_cast<size_t>(j)][term.first + 2] -= term.second;
            dPower[static_cast<size_t>(j)][term.first - 2] -= term.second;
        }
    }

    // (-A³)^(-w) <K>, as powers of A
    std::map<int, long long> inA;

    const long long writheSign = (writhe % 2 == 0) ? 1 : -1;

    for (int a = 0; a <= c; ++a)
    {
        for (int loops = 1; loops <= m; ++loops)
        {
            const long long count =
                counts[static_cast<size_t>(a)][static_cast<size_t>(loops)];

            if (count == 0)
                continue;

            for (const auto &term : dPower[static_cast<size_t>(loops - 1)])
            {
                const int exponent = 2 * a - c + term.first - 3 * writhe;

                inA[exponent] += writheSign * count * term.second;
            }
        }
    }

    // A = t^(-1/4)
    std::map<int, long long> inT;

    for (const auto &term : inA)
    {
        if (term.second == 0)
            continue;

        if (term.first % 4 != 0)
            return false;

        inT[-term.first / 4] += term.second;
    }

    result = makeJones(inT);

    return result.valid;
}

} // namespace

JonesPolynomial JonesPolynomial::mirrored() const
{
    std::map<int, long long> terms;

    for (int i = 0; i < coefficients.size(); ++i)
        terms[-(lowestPower + i)] = coefficients.at(i);

    JonesPolynomial result = makeJones(terms);

    result.valid = valid;
    result.diagramCrossings = diagramCrossings;
    result.confirmed = confirmed;
    result.error = error;

    return result;
}

bool JonesPolynomial::isSymmetric() const
{
    return valid && mirrored() == *this;
}

bool JonesPolynomial::operator==(const JonesPolynomial &other) const
{
    return lowestPower == other.lowestPower &&
           coefficients == other.coefficients;
}

JonesPolynomial computeJonesPolynomial(
    const QVector<Vec3> &curve,
    int maxCrossings)
{
    JonesPolynomial result;

    maxCrossings = std::min(maxCrossings, 24);

    const QVector<QVector<CrossingInfo>> diagrams =
        cleanDiagrams(curve, result.error);

    if (diagrams.isEmpty())
        return result;

    const int crossings = diagrams.first().size();

    if (crossings > maxCrossings)
    {
        result.diagramCrossings = crossings;
        result.error =
            knotTr("The simplest projection found has %1 crossings (limit %2).")
                .arg(crossings)
                .arg(maxCrossings);
        return result;
    }

    JonesPolynomial computed;

    if (!jonesFromCrossings(diagrams.first(), computed) ||
        !jonesLooksValid(computed))
    {
        result.diagramCrossings = crossings;
        result.error =
            knotTr("The projection did not give a valid knot polynomial.");
        return result;
    }

    result = computed;
    result.diagramCrossings = crossings;

    // An invariant must not depend on the projection: check a second one.
    if (diagrams.size() > 1 && diagrams.at(1).size() <= maxCrossings)
    {
        JonesPolynomial second;

        if (jonesFromCrossings(diagrams.at(1), second))
        {
            if (!(second == result))
            {
                JonesPolynomial failed;

                failed.diagramCrossings = crossings;
                failed.error =
                    knotTr("Two projections gave different polynomials.");

                return failed;
            }

            result.confirmed = true;
        }
    }

    return result;
}

JonesPolynomial torusKnotJonesPolynomial(int p, int q)
{
    JonesPolynomial result;

    if (p < 1 || q < 1 || std::gcd(p, q) != 1)
        return result;

    if (p == 1 || q == 1)
        return makeJones({{0, 1}});

    // 1 - t^(p+1) - t^(q+1) + t^(p+q), divided exactly by 1 - t²
    IntPolynomial numerator(static_cast<size_t>(p + q + 1), 0);

    numerator[0] += 1;
    numerator[static_cast<size_t>(p + 1)] -= 1;
    numerator[static_cast<size_t>(q + 1)] -= 1;
    numerator[static_cast<size_t>(p + q)] += 1;

    trimPolynomial(numerator);

    IntPolynomial quotient;

    if (!divideExact(numerator, IntPolynomial{1, 0, -1}, quotient))
        return result;

    const int shift = (p - 1) * (q - 1) / 2;

    std::map<int, long long> terms;

    for (size_t i = 0; i < quotient.size(); ++i)
        terms[shift + static_cast<int>(i)] = quotient[i];

    return makeJones(terms);
}

/*
 * =============================================================
 * KNOT IDENTIFICATION (knot table, up to 13 crossings)
 * =============================================================
 *
 * The polynomials of a curve are looked up in the knot table (knottable.h:
 * every prime knot with up to 13 crossings, with its Alexander and Jones
 * polynomials) and among the composite knots with at most 8 crossings.
 * computeAlexanderPolynomial() and computeJonesPolynomial() give exactly
 * the table's values for the 3D curves of all 12,965 table knots (the Jones
 * polynomial where the diagram is small enough for it), in the same
 * convention, so the values can be compared directly.
 *
 * Alexander polynomials are compared as the symmetric coefficients
 * a0, a1, a2, ... of
 *   Delta(t) = a0 + a1 (t + 1/t) + a2 (t^2 + 1/t^2) + ...
 * normalised so that Delta(1) = 1.
 */

namespace
{

// Symmetric coefficients a0, a1, ... of a normalised polynomial.
std::vector<long long> symmetricCoefficients(const AlexanderPolynomial &polynomial)
{
    const int size = polynomial.coefficients.size();
    const int middle = size / 2;

    std::vector<long long> half;

    for (int i = middle; i < size; ++i)
        half.push_back(polynomial.coefficients.at(i));

    return half;
}

// The same for a normalised polynomial given as power -> coefficient.
std::vector<long long> symmetricCoefficients(const std::map<int, long long> &terms)
{
    std::vector<long long> half;

    if (terms.empty())
        return half;

    for (int power = 0; power <= terms.rbegin()->first; ++power)
    {
        const auto it = terms.find(power);
        half.push_back(it != terms.end() ? it->second : 0);
    }

    return half;
}

std::map<int, long long> multiplyTerms(
    const std::map<int, long long> &a,
    const std::map<int, long long> &b)
{
    std::map<int, long long> product;

    for (const auto &x : a)
    {
        for (const auto &y : b)
            product[x.first + y.first] += x.second * y.second;
    }

    return product;
}

// Table knots by Alexander polynomial (symmetric coefficients).
const std::map<std::vector<long long>, std::vector<int>> &alexanderIndex()
{
    static const std::map<std::vector<long long>, std::vector<int>> index = []()
    {
        std::map<std::vector<long long>, std::vector<int>> result;

        const KnotTable &table = KnotTable::instance();

        for (int i = 0; i < table.size(); ++i)
        {
            result[symmetricCoefficients(
                       KnotTable::normalisedAlexander(table.at(i)))]
                .push_back(i);
        }

        return result;
    }();

    return index;
}

/*
 * The composite knots with at most 8 crossings (KnotInfo names of the
 * summands). Two of them share their Alexander polynomial with prime knots
 * (3_1 # 3_1 with 8_20, 3_1 # 4_1 with 8_21).
 */
struct CompositeKnot
{
    const char *first;
    const char *second;
    int crossings;
    const char *name;
};

const CompositeKnot compositeKnots[] = {
    {"3_1", "3_1", 6, QT_TRANSLATE_NOOP("KnotMath", "granny or square knot")},
    {"3_1", "4_1", 7, ""},
    {"4_1", "4_1", 8, ""},
    {"3_1", "5_1", 8, ""},
    {"3_1", "5_2", 8, ""},
};

// In-sentence name ("trefoil knot") of the knots that have one, otherwise
// empty.
QString knotName(const KnotTableRecord &record)
{
    struct Name
    {
        const char *knot;
        const char *name;
    };

    static const Name names[] = {
        {"3_1", QT_TRANSLATE_NOOP("KnotMath", "trefoil knot")},
        {"4_1", QT_TRANSLATE_NOOP("KnotMath", "figure-eight knot")},
        {"5_1", QT_TRANSLATE_NOOP("KnotMath", "cinquefoil knot")},
        {"5_2", QT_TRANSLATE_NOOP("KnotMath", "three-twist knot")},
        {"6_1", QT_TRANSLATE_NOOP("KnotMath", "stevedore knot")},
        {"6_2", QT_TRANSLATE_NOOP("KnotMath", "Miller Institute knot")},
        {"7_1", QT_TRANSLATE_NOOP("KnotMath", "septafoil knot")},
        {"7_4", QT_TRANSLATE_NOOP("KnotMath", "endless knot")},
        {"8_18", QT_TRANSLATE_NOOP("KnotMath", "carrick mat")},
        {"10_161", QT_TRANSLATE_NOOP("KnotMath", "Perko pair knot")},
        {"11n_34", QT_TRANSLATE_NOOP("KnotMath", "Conway knot")},
        {"11n_42", QT_TRANSLATE_NOOP("KnotMath", "Kinoshita-Terasaka knot")},
        {"12n_242", QT_TRANSLATE_NOOP("KnotMath", "(−2,3,7) pretzel knot")},
    };

    for (const Name &entry : names)
    {
        if (record.name == QLatin1String(entry.knot))
            return knotTr(entry.name);
    }

    // KnotInfo's geometric type "torus knot T(2,9)"
    const QString geometric = record.geometricType.trimmed();

    if (geometric.startsWith(QLatin1String("torus knot"), Qt::CaseInsensitive))
        return knotTr("torus knot %1").arg(geometric.mid(11).trimmed());

    return QString();
}

KnotTableEntry primeEntry(int index)
{
    const KnotTableRecord &record = KnotTable::instance().at(index);

    KnotTableEntry entry;

    entry.notation = KnotTable::displayName(record.name);
    entry.name = knotName(record);
    entry.crossingNumber = record.crossings;
    entry.tableIndex = index;

    return entry;
}

JonesPolynomial multiplyJones(const JonesPolynomial &a, const JonesPolynomial &b)
{
    std::map<int, long long> terms;

    for (int i = 0; i < a.coefficients.size(); ++i)
    {
        for (int j = 0; j < b.coefficients.size(); ++j)
        {
            terms[a.lowestPower + i + b.lowestPower + j] +=
                a.coefficients.at(i) * b.coefficients.at(j);
        }
    }

    return makeJones(terms);
}

// How a table entry fits a computed Jones polynomial.
struct JonesFit
{
    bool fits = true;

    // "right-handed", "granny knot", ... (empty if nothing to add)
    QString variant;
};

JonesFit fitJones(const KnotTableEntry &entry, const JonesPolynomial &v)
{
    JonesFit fit;

    if (!v.valid)
        return fit;

    const KnotTable &table = KnotTable::instance();

    if (entry.tableIndex >= 0)
    {
        const KnotTableRecord &record = table.at(entry.tableIndex);
        const JonesPolynomial reference = makeJones(record.jones);

        if (!reference.valid)
            return fit;

        if (!(v == reference) && !(v == reference.mirrored()))
        {
            fit.fits = false;
            return fit;
        }

        // the Jones polynomial of a positive (right-handed) torus knot has
        // only positive powers
        if (record.geometricType.trimmed().startsWith(QLatin1String("torus knot"),
                                                      Qt::CaseInsensitive))
        {
            fit.variant = v.lowestPower > 0 ? knotTr("right-handed")
                                            : knotTr("left-handed");
        }

        return fit;
    }

    if (entry.summands.size() != 2)
        return fit;

    const JonesPolynomial a = makeJones(table.at(entry.summands.at(0)).jones);
    const JonesPolynomial b = makeJones(table.at(entry.summands.at(1)).jones);

    if (!a.valid || !b.valid)
        return fit;

    // the summands with the same or with opposite handedness
    const JonesPolynomial same = multiplyJones(a, b);
    const JonesPolynomial opposite = multiplyJones(a, b.mirrored());

    const bool twins = entry.summands.at(0) == entry.summands.at(1);

    if (v == same || v == same.mirrored())
        fit.variant = twins && !a.isSymmetric() ? knotTr("granny knot") : QString();
    else if (v == opposite || v == opposite.mirrored())
        fit.variant = twins && !a.isSymmetric() ? knotTr("square knot") : QString();
    else
        fit.fits = false;

    return fit;
}

} // namespace

QVector<KnotTableEntry> knotsWithAlexanderPolynomial(
    const AlexanderPolynomial &polynomial)
{
    QVector<KnotTableEntry> matches;

    if (!polynomial.valid || polynomial.coefficients.isEmpty())
        return matches;

    const std::vector<long long> half = symmetricCoefficients(polynomial);

    const auto found = alexanderIndex().find(half);

    if (found != alexanderIndex().end())
    {
        for (const int index : found->second)
            matches.push_back(primeEntry(index));
    }

    const KnotTable &table = KnotTable::instance();

    for (const CompositeKnot &composite : compositeKnots)
    {
        const int first = table.indexOf(QString::fromLatin1(composite.first));
        const int second = table.indexOf(QString::fromLatin1(composite.second));

        if (first < 0 || second < 0)
            continue;

        const std::map<int, long long> product = multiplyTerms(
            KnotTable::normalisedAlexander(table.at(first)),
            KnotTable::normalisedAlexander(table.at(second)));

        if (symmetricCoefficients(product) != half)
            continue;

        KnotTableEntry entry;

        entry.notation = QStringLiteral("%1 # %2").arg(
            KnotTable::displayName(table.at(first).name),
            KnotTable::displayName(table.at(second).name));
        entry.name = composite.name[0] != '\0' ? knotTr(composite.name) : QString();
        entry.crossingNumber = composite.crossings;
        entry.summands = {first, second};

        matches.push_back(entry);
    }

    std::stable_sort(
        matches.begin(), matches.end(),
        [](const KnotTableEntry &a, const KnotTableEntry &b)
        {
            return a.crossingNumber < b.crossingNumber;
        });

    return matches;
}

QString identifyKnot(
    const AlexanderPolynomial &polynomial,
    const JonesPolynomial &jones)
{
    if (!polynomial.valid)
        return QString();

    const KnotTable &table = KnotTable::instance();

    if (!table.isLoaded())
        return table.error();

    // A diagram with c crossings shows a knot with at most c crossings.
    int diagramCrossings = -1;

    for (const int c : {polynomial.diagramCrossings,
                        jones.valid ? jones.diagramCrossings : -1})
    {
        if (c >= 0 && (diagramCrossings < 0 || c < diagramCrossings))
            diagramCrossings = c;
    }

    // Delta = 1: no non-trivial knot with fewer than 11 crossings has it.
    if (polynomial.coefficients.size() == 1)
    {
        const bool jonesIsOne =
            jones.valid && jones.lowestPower == 0 &&
            jones.coefficients == QVector<long long>{1};

        if (jonesIsOne)
            return knotTr("Probably the unknot (its Alexander and Jones polynomials are 1)");

        if (!jones.valid)
        {
            if (diagramCrossings >= 0 && diagramCrossings < 11)
                return knotTr("Probably the unknot (no knot with fewer than 11 crossings has Δ = 1)");

            return knotTr("Probably the unknot, or a knot with Δ = 1 such as the "
                          "Conway knot (11n₃₄)");
        }

        // knotted: look for it among the knots with Delta = 1
    }

    auto describe = [](const KnotTableEntry &entry, const QString &variant)
    {
        const bool composite = !entry.summands.isEmpty();

        if (composite && !variant.isEmpty())
            return QStringLiteral("%1 (%2)").arg(variant, entry.notation);

        const QString knot =
            entry.name.isEmpty()
                ? knotTr("knot %1").arg(entry.notation)
                : QStringLiteral("%1 (%2)").arg(entry.name, entry.notation);

        if (variant.isEmpty())
            return knot;

        // translatable, so that a language can put the variant after the knot
        //: %1 is a variant such as "right-handed", %2 the knot
        return knotTr("%1 %2").arg(variant, knot);
    };

    QStringList parts;

    for (const KnotTableEntry &entry : knotsWithAlexanderPolynomial(polynomial))
    {
        if (diagramCrossings >= 0 && entry.crossingNumber > diagramCrossings)
            continue;

        const JonesFit fit = fitJones(entry, jones);

        if (fit.fits)
            parts << describe(entry, fit.variant);
    }

    if (parts.isEmpty())
    {
        return knotTr("Not a prime knot with up to 13 crossings (no knot in the "
                      "table matches its polynomials)");
    }

    if (parts.size() == 1)
        return knotTr("Probably the %1").arg(parts.first());

    if (parts.size() <= 3)
        return knotTr("Probably one of: %1").arg(parts.join(knotTr(" or ")));

    // sorted by crossing number, so the first ones are the simplest
    //: %1 is the number of candidates, %2 the first three of them
    return knotTr("Probably one of %1 knots, the simplest: %2")
        .arg(parts.size())
        .arg(parts.mid(0, 3).join(QStringLiteral(", ")));
}

void colorMeshByStrandHighlight(
    Mesh &mesh,
    const QColor &base,
    const QColor &over,
    const QColor &under,
    const QVector<float> &weights)
{
    if (mesh.ringCount <= 0 || mesh.ringSegments <= 0)
        return;

    const int vertexCount = mesh.positions.size() / 3;

    mesh.colors.fill(1.0f, vertexCount * 3);

    for (int ring = 0; ring < mesh.ringCount; ++ring)
    {
        const float signedWeight =
            ring < weights.size()
                ? std::clamp(weights.at(ring), -1.0f, 1.0f)
                : 0.0f;

        const QColor &hot = signedWeight >= 0.0f ? over : under;
        const float w = std::abs(signedWeight);

        const float red =
            static_cast<float>(base.redF()) * (1.0f - w) +
            static_cast<float>(hot.redF()) * w;

        const float green =
            static_cast<float>(base.greenF()) * (1.0f - w) +
            static_cast<float>(hot.greenF()) * w;

        const float blue =
            static_cast<float>(base.blueF()) * (1.0f - w) +
            static_cast<float>(hot.blueF()) * w;

        for (int segment = 0; segment < mesh.ringSegments; ++segment)
        {
            const int vertex = ring * mesh.ringSegments + segment;

            if (vertex * 3 + 2 >= mesh.colors.size())
                continue;

            mesh.colors[vertex * 3] = red;
            mesh.colors[vertex * 3 + 1] = green;
            mesh.colors[vertex * 3 + 2] = blue;
        }
    }
}

QVector<UnknottingAnimation::TimelineMark> UnknottingAnimation::timeline() const
{
    QVector<TimelineMark> marks;

    if (!valid_ || flattenEnd_ <= 0.0)
        return marks;

    auto add = [&](double seconds, Stage stage, int change)
    {
        TimelineMark mark;

        mark.time = std::clamp(seconds / flattenEnd_, 0.0, 1.0);
        mark.stage = stage;
        mark.change = change;

        marks.push_back(mark);
    };

    if (alignEnd_ > 0.0)
        add(0.0, Stage::Align, 0);

    add(alignEnd_, Stage::Layer, 0);

    for (int j = 0; j < changes_; ++j)
        add(layerEnd_ + slot_ * static_cast<double>(j), Stage::Change, j + 1);

    add(changeEnd_, Stage::Settle, 0);
    add(settleEnd_, Stage::Untangle, 0);
    add(untangleEnd_, Stage::Flatten, 0);
    add(flattenEnd_, Stage::Done, 0);

    return marks;
}

UnknottingAnimation::Diagram UnknottingAnimation::diagram() const
{
    Diagram result;

    if (!valid_)
        return result;

    result.points.reserve(n_);

    for (const Vec3 &p : rotatedRel_)
        result.points.push_back(QPointF(p.x, p.y));

    const int crossingCount = crossingU_[0].size();

    for (int c = 0; c < crossingCount; ++c)
    {
        DiagramCrossing crossing;

        crossing.u[0] = crossingU_[0].at(c);
        crossing.u[1] = crossingU_[1].at(c);

        crossing.overAtStart =
            nodeInit_.at(crossingNode_[0].at(c)) > 0.0 ? 0 : 1;

        // position: the first pass, interpolated along the polyline
        const double x = crossing.u[0] * static_cast<double>(n_);
        const int i0 = static_cast<int>(std::floor(x)) % n_;
        const int i1 = (i0 + 1) % n_;
        const double f = x - std::floor(x);

        crossing.position =
            result.points.at(i0) * (1.0 - f) + result.points.at(i1) * f;

        crossing.change = static_cast<int>(flipOrder_.indexOf(c)) + 1;

        result.crossings.push_back(crossing);
    }

    return result;
}

int knownUnknottingNumber(Preset preset, int torusP, int torusQ)
{
    switch (preset)
    {
    case Preset::Unknot:
        return 0;

    case Preset::Trefoil:
        return 1;

    case Preset::FigureEight:
        return 1;

    case Preset::Cinquefoil:
        return 2;

    case Preset::Stevedore:
        return 1;

    case Preset::CustomTorus:
        // Milnor's conjecture (Kronheimer-Mrowka): (p-1)(q-1)/2
        if (torusP >= 1 && torusQ >= 1 && std::gcd(torusP, torusQ) == 1)
            return (torusP - 1) * (torusQ - 1) / 2;

        return -1;

    default:
        break;
    }

    return -1;
}

} // namespace KnotMath
