#include "knotseifert.h"

#include "knotpractice.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace KnotMath
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

QString seifertTr(const char *text)
{
    return QCoreApplication::translate("KnotMath", text);
}

// Seifert matrices larger than this are not computed (the linking numbers
// take long, and 64-bit determinants could overflow).
constexpr int MaxMatrixSize = 16;

// How far a highlighted loop is lifted off the surface (times the extent).
// Lifted farther, it can pass through another part of the surface or the
// other loop and change the linking number (it does from about 0.025 on).
constexpr double SeifertLoopLift = 0.015;

// How far inside its circle a loop crosses a disk (times the extent; less
// in thin circles).
constexpr double LoopInset = 0.025;

/*
 * Arc length along the closed diagram polyline.
 */
struct ArcLength
{
    const QVector<Vec3> *points = nullptr;
    QVector<double> cum;

    explicit ArcLength(const QVector<Vec3> &p)
        : points(&p)
    {
        const int n = p.size();

        cum = QVector<double>(n + 1, 0.0);

        for (int i = 0; i < n; ++i)
        {
            const Vec3 &a = p[i];
            const Vec3 &b = p[(i + 1) % n];

            cum[i + 1] = cum[i] + std::hypot(b.x - a.x, b.y - a.y);
        }
    }

    int size() const { return points->size(); }

    double total() const { return cum.last(); }

    // sample index + parameter -> length
    double lengthAt(double s) const
    {
        const int n = size();
        const double wraps = std::floor(s / n);

        s -= wraps * n;

        const int i = std::clamp(static_cast<int>(std::floor(s)), 0, n - 1);

        return cum[i] + (s - i) * (cum[i + 1] - cum[i]) + wraps * total();
    }

    Vec3 pointAtLength(double length) const
    {
        const int n = size();
        const double wraps = std::floor(length / total());

        length -= wraps * total();

        const int i = std::clamp(
            static_cast<int>(std::upper_bound(cum.begin(), cum.end(), length) - cum.begin()) - 1,
            0, n - 1);

        const double segment = cum[i + 1] - cum[i];
        const double f = segment > 1e-15 ? (length - cum[i]) / segment : 0.0;

        const Vec3 &a = (*points)[i];
        const Vec3 &b = (*points)[(i + 1) % n];

        return Vec3(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, 0.0);
    }
};

double signedArea(const QVector<Vec3> &polygon)
{
    double area = 0.0;

    const int n = polygon.size();

    for (int i = 0; i < n; ++i)
    {
        const Vec3 &a = polygon[i];
        const Vec3 &b = polygon[(i + 1) % n];

        area += a.x * b.y - b.x * a.y;
    }

    return 0.5 * area;
}

bool insidePolygon(const QVector<Vec3> &polygon, double x, double y)
{
    bool inside = false;

    const int n = polygon.size();

    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        const Vec3 &a = polygon[i];
        const Vec3 &b = polygon[j];

        if ((a.y > y) != (b.y > y) &&
            x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
        {
            inside = !inside;
        }
    }

    return inside;
}

double smoothTwist(double u)
{
    const double s = std::sin(Pi * u);

    return s * s;
}

/*
 * Ear clipping of a simple polygon (2D, either orientation). Returns
 * triangles as index triples; empty if the polygon is not simple enough.
 */
QVector<int> triangulate(const QVector<Vec3> &polygon)
{
    const int n = polygon.size();

    QVector<int> triangles;

    if (n < 3)
        return triangles;

    const double orientation = signedArea(polygon) >= 0.0 ? 1.0 : -1.0;

    QVector<int> remaining(n);

    for (int i = 0; i < n; ++i)
        remaining[i] = i;

    auto cross = [&](int a, int b, int c)
    {
        const Vec3 &p = polygon[a];
        const Vec3 &q = polygon[b];
        const Vec3 &r = polygon[c];

        return ((q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x)) * orientation;
    };

    auto insideTriangle = [&](int a, int b, int c, int p)
    {
        return cross(a, b, p) > 0.0 && cross(b, c, p) > 0.0 && cross(c, a, p) > 0.0;
    };

    int guard = 0;
    int i = 0;

    while (remaining.size() > 3 && guard < 4 * n * n)
    {
        ++guard;

        const int m = remaining.size();

        const int a = remaining[(i + m - 1) % m];
        const int b = remaining[i % m];
        const int c = remaining[(i + 1) % m];

        bool ear = cross(a, b, c) > 1e-14;

        if (ear)
        {
            for (int k = 0; k < m && ear; ++k)
            {
                const int p = remaining[k];

                if (p == a || p == b || p == c)
                    continue;

                if (insideTriangle(a, b, c, p))
                    ear = false;
            }
        }

        if (ear)
        {
            triangles << a << b << c;
            remaining.removeAt(i % m);
            i = std::max(0, i - 1);
            guard = 0;
        }
        else
        {
            i = (i + 1) % m;

            // a whole round without an ear: drop a degenerate vertex
            if (guard > m && cross(a, b, c) <= 1e-14 && std::abs(cross(a, b, c)) <= 1e-12)
            {
                remaining.removeAt(i % m == 0 ? m - 1 : (i - 1) % m);
                guard = 0;
            }
        }
    }

    if (remaining.size() == 3)
        triangles << remaining[0] << remaining[1] << remaining[2];

    return triangles;
}

/*
 * Exact integer and polynomial determinants (fraction-free elimination).
 */
long long integerDeterminant(QVector<QVector<long long>> m)
{
    const int n = m.size();

    if (n == 0)
        return 1;

    long long previous = 1;
    int sign = 1;

    for (int k = 0; k < n - 1; ++k)
    {
        if (m[k][k] == 0)
        {
            int swapRow = -1;

            for (int r = k + 1; r < n; ++r)
            {
                if (m[r][k] != 0)
                {
                    swapRow = r;
                    break;
                }
            }

            if (swapRow < 0)
                return 0;

            std::swap(m[k], m[swapRow]);
            sign = -sign;
        }

        for (int r = k + 1; r < n; ++r)
        {
            for (int c = k + 1; c < n; ++c)
                m[r][c] = (m[r][c] * m[k][k] - m[r][k] * m[k][c]) / previous;

            m[r][k] = 0;
        }

        previous = m[k][k];
    }

    return sign * m[n - 1][n - 1];
}

// Integer polynomials, coefficient of t^i at index i.
using Poly = QVector<long long>;

void trim(Poly &p)
{
    while (!p.isEmpty() && p.last() == 0)
        p.removeLast();
}

Poly multiply(const Poly &a, const Poly &b)
{
    if (a.isEmpty() || b.isEmpty())
        return {};

    Poly r(a.size() + b.size() - 1, 0);

    for (int i = 0; i < a.size(); ++i)
    {
        for (int j = 0; j < b.size(); ++j)
            r[i + j] += a[i] * b[j];
    }

    trim(r);

    return r;
}

Poly subtract(const Poly &a, const Poly &b)
{
    Poly r(std::max(a.size(), b.size()), 0);

    for (int i = 0; i < a.size(); ++i)
        r[i] += a[i];

    for (int i = 0; i < b.size(); ++i)
        r[i] -= b[i];

    trim(r);

    return r;
}

// a / b, known to divide exactly
Poly divideExact(Poly a, const Poly &b)
{
    trim(a);

    if (a.isEmpty())
        return {};

    const int db = b.size() - 1;

    Poly q(std::max(1, static_cast<int>(a.size()) - db), 0);

    while (a.size() - 1 >= db && !a.isEmpty())
    {
        const int shift = a.size() - 1 - db;
        const long long factor = a.last() / b.last();

        q[shift] = factor;

        for (int i = 0; i < b.size(); ++i)
            a[i + shift] -= factor * b[i];

        trim(a);
    }

    trim(q);

    return q;
}

Poly polynomialDeterminant(QVector<QVector<Poly>> m)
{
    const int n = m.size();

    if (n == 0)
        return Poly{1};

    Poly previous{1};
    int sign = 1;

    for (int k = 0; k < n - 1; ++k)
    {
        if (m[k][k].isEmpty())
        {
            int swapRow = -1;

            for (int r = k + 1; r < n; ++r)
            {
                if (!m[r][k].isEmpty())
                {
                    swapRow = r;
                    break;
                }
            }

            if (swapRow < 0)
                return {};

            std::swap(m[k], m[swapRow]);
            sign = -sign;
        }

        for (int r = k + 1; r < n; ++r)
        {
            for (int c = k + 1; c < n; ++c)
            {
                m[r][c] = divideExact(
                    subtract(multiply(m[r][c], m[k][k]), multiply(m[r][k], m[k][c])),
                    previous);
            }

            m[r][k].clear();
        }

        previous = m[k][k];
    }

    Poly result = m[n - 1][n - 1];

    if (sign < 0)
    {
        for (long long &c : result)
            c = -c;
    }

    return result;
}

// Number of positive minus negative eigenvalues (Jacobi rotations).
int symmetricSignature(const QVector<QVector<int>> &matrix)
{
    const int n = matrix.size();

    QVector<QVector<double>> a(n, QVector<double>(n, 0.0));

    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < n; ++j)
            a[i][j] = matrix[i][j];
    }

    for (int sweep = 0; sweep < 100; ++sweep)
    {
        double off = 0.0;

        for (int i = 0; i < n; ++i)
        {
            for (int j = i + 1; j < n; ++j)
                off += a[i][j] * a[i][j];
        }

        if (off < 1e-22)
            break;

        for (int p = 0; p < n; ++p)
        {
            for (int q = p + 1; q < n; ++q)
            {
                if (std::abs(a[p][q]) < 1e-300)
                    continue;

                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) /
                                 (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;

                for (int k = 0; k < n; ++k)
                {
                    const double akp = a[k][p];
                    const double akq = a[k][q];

                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }

                for (int k = 0; k < n; ++k)
                {
                    const double apk = a[p][k];
                    const double aqk = a[q][k];

                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
            }
        }
    }

    int signature = 0;

    for (int i = 0; i < n; ++i)
        signature += a[i][i] > 0.0 ? 1 : -1;

    return signature;
}

/*
 * Mesh helpers.
 */

// Adds a two-sided patch: every triangle once facing `normals` (positive
// side, colour `front`) and once facing the other way (colour `back`).
void addTwoSided(
    Mesh &mesh,
    const QVector<Vec3> &positions,
    const QVector<Vec3> &normals,
    const QVector<int> &triangles,
    const QColor &front,
    const QColor &back)
{
    // rope material: the middle of a strand (no dark grooves)
    const float ropeX = static_cast<float>(std::cos(Pi / 3.0));
    const float ropeY = static_cast<float>(std::sin(Pi / 3.0));

    for (int side = 0; side < 2; ++side)
    {
        const unsigned int base = static_cast<unsigned int>(mesh.positions.size() / 3);
        const double sign = side == 0 ? 1.0 : -1.0;
        const QColor &color = side == 0 ? front : back;

        for (int i = 0; i < positions.size(); ++i)
        {
            const Vec3 &p = positions[i];
            const Vec3 n = normals[i] * sign;

            mesh.positions << static_cast<float>(p.x) << static_cast<float>(p.y) << static_cast<float>(p.z);
            mesh.normals << static_cast<float>(n.x) << static_cast<float>(n.y) << static_cast<float>(n.z);
            mesh.colors << static_cast<float>(color.redF()) << static_cast<float>(color.greenF())
                        << static_cast<float>(color.blueF());
            mesh.texcoords << ropeX << ropeY << 1.0f << 0.0f;

            // no centre line of its own: keeps the tubes' centre lines
            // (shadows, best view) when the meshes are joined
            mesh.vertexSample << 0;
        }

        for (int t = 0; t + 2 < triangles.size(); t += 3)
        {
            int a = triangles[t];
            int b = triangles[t + 1];
            int c = triangles[t + 2];

            // counter-clockwise seen from the side the copy faces
            const Vec3 geometric = Vec3::cross(positions[b] - positions[a], positions[c] - positions[a]);
            const Vec3 facing = (normals[a] + normals[b] + normals[c]) * sign;

            if (Vec3::dot(geometric, facing) < 0.0)
                std::swap(b, c);

            mesh.indices << base + a << base + b << base + c;
        }
    }
}

Mesh tube(const QVector<Vec3> &curve, double radius, const QColor &color)
{
    Mesh mesh = buildTubeMesh(curve, radius, 20, true, ColorMode::SingleColor, 2);

    if (mesh.isValid())
        mesh.fillColor(color);

    return mesh;
}

// Positive normal of a band (the derivative cross product, see below).
Vec3 bandNormal(const SeifertSurface::Band &band, double u, double v)
{
    constexpr double h = 1e-4;

    const double u0 = std::clamp(u - h, 0.0, 1.0);
    const double u1 = std::clamp(u + h, 0.0, 1.0);
    const double v0 = std::clamp(v - h, 0.0, 1.0);
    const double v1 = std::clamp(v + h, 0.0, 1.0);

    const Vec3 du = seifertBandPoint(band, u1, v) - seifertBandPoint(band, u0, v);
    const Vec3 dv = seifertBandPoint(band, u, v1) - seifertBandPoint(band, u, v0);

    return Vec3::cross(du, dv).normalized();
}

// The model is built with the diagram in the xy plane and the levels along
// z; shown, it lies on the floor like a map: (x, y, z) -> (x, z, -y), so
// the levels rise upwards and the diagram reads the right way round from
// the front.
void layFlat(Mesh &mesh)
{
    auto turn = [](QVector<float> &values, int stride)
    {
        for (int i = 0; i + 2 < values.size(); i += stride)
        {
            const float y = values[i + 1];

            values[i + 1] = values[i + 2];
            values[i + 2] = -y;
        }
    };

    turn(mesh.positions, 3);
    turn(mesh.normals, 3);
    turn(mesh.centerline, 4);
}

} // namespace

Vec3 seifertBandPoint(const SeifertSurface::Band &band, double u, double v)
{
    const Vec3 left = band.a0 + (band.a1 - band.a0) * v;
    const Vec3 right = band.b0 + (band.b1 - band.b0) * v;

    Vec3 p = left + (right - left) * u;

    p.z = band.zA + (band.zB - band.zA) * u + band.twist * smoothTwist(u) * (1.0 - 2.0 * v);

    return p;
}

SeifertSurface buildSeifertSurface(const QVector<Vec3> &curve, bool computeMatrix)
{
    double extent = 0.0;

    Vec3 centre;

    for (const Vec3 &p : curve)
        centre += p;

    if (!curve.isEmpty())
        centre = centre / static_cast<double>(curve.size());

    for (const Vec3 &p : curve)
        extent = std::max(extent, (p - centre).length());

    if (extent < 1e-9)
    {
        SeifertSurface surface;
        surface.error = seifertTr("The curve is degenerate.");
        return surface;
    }

    QString error;

    // The best diagram first. Where crossings are cramped, a loop on the
    // surface can come out wrong; the Seifert matrix is therefore checked
    // against the Alexander polynomial computed independently from the
    // curve (Fox calculus), and the next diagrams are tried if it fails.
    const QVector<KnotDiagram> diagrams = diagramsFromCurve(curve, extent, error, 8);

    if (diagrams.isEmpty())
    {
        SeifertSurface surface;
        surface.error = error;
        return surface;
    }

    std::map<int, long long> reference;

    if (computeMatrix)
    {
        const AlexanderPolynomial alexander = computeAlexanderPolynomial(curve);

        if (alexander.valid)
        {
            for (int i = 0; i < alexander.coefficients.size(); ++i)
            {
                if (alexander.coefficients[i] != 0)
                    reference[alexander.lowestPower + i] = alexander.coefficients[i];
            }
        }
    }

    SeifertSurface first;

    for (int i = 0; i < diagrams.size(); ++i)
    {
        SeifertSurface surface = buildSeifertSurface(diagrams[i], extent, computeMatrix);

        if (i == 0)
            first = surface;

        if (!computeMatrix || !surface.valid)
            return surface;

        if (surface.matrixValid && (reference.empty() || surface.alexander == reference))
            return surface;
    }

    // the surface is still right; only its matrix could not be trusted
    if (first.matrixValid)
    {
        first.matrixValid = false;
        first.seifertMatrix.clear();
        first.loops.clear();
        first.loopNormals.clear();
        first.pushedLoops.clear();
        first.matrixError =
            seifertTr("The Seifert matrix could not be determined reliably for this knot.");
    }

    return first;
}

SeifertSurface buildSeifertSurface(const KnotDiagram &diagram, double extent, bool computeMatrix)
{
    SeifertSurface surface;

    surface.extent = extent;

    if (!diagram.isValid())
    {
        surface.error = seifertTr("The knot diagram is empty.");
        return surface;
    }

    const QVector<Vec3> &points = diagram.points;
    const ArcLength arc(points);
    const double total = arc.total();
    const int c = diagram.crossings.size();

    surface.crossings = c;

    // sizes, relative to the model
    const double cornerCut = 0.14 * extent;
    const double levelGap = 0.22 * extent;

    /*
     * Passes (each crossing is passed twice), sorted along the curve.
     */
    struct Pass
    {
        double length = 0.0;
        int crossing = 0;
        bool isA = true;
        // length cut away on each side
        double cut = 0.0;
    };

    QVector<Pass> passes;

    for (int k = 0; k < c; ++k)
    {
        const DiagramCrossing &crossing = diagram.crossings[k];

        Pass a;
        a.length = arc.lengthAt(crossing.segA + crossing.tA);
        a.crossing = k;
        a.isA = true;

        Pass b;
        b.length = arc.lengthAt(crossing.segB + crossing.tB);
        b.crossing = k;
        b.isA = false;

        passes << a << b;
    }

    std::sort(passes.begin(), passes.end(),
              [](const Pass &x, const Pass &y) { return x.length < y.length; });

    const int passCount = passes.size();

    // index of the pass of every crossing on strand A and B
    QVector<int> passA(c, -1);
    QVector<int> passB(c, -1);

    for (int p = 0; p < passCount; ++p)
    {
        if (passes[p].isA)
            passA[passes[p].crossing] = p;
        else
            passB[passes[p].crossing] = p;

        const double previous = passes[(p + passCount - 1) % passCount].length;
        const double next = passes[(p + 1) % passCount].length;

        double gapBefore = passes[p].length - previous;
        double gapAfter = next - passes[p].length;

        if (gapBefore <= 0.0)
            gapBefore += total;

        if (gapAfter <= 0.0)
            gapAfter += total;

        passes[p].cut = std::min(cornerCut, 0.35 * std::min(gapBefore, gapAfter));
    }

    auto partner = [&](int p)
    {
        const int crossing = passes[p].crossing;

        return passes[p].isA ? passB[crossing] : passA[crossing];
    };

    /*
     * Pieces of the curve between the passes; piece k starts after pass k.
     */
    QVector<QVector<Vec3>> pieces;

    if (c == 0)
    {
        pieces.push_back(points);
    }
    else
    {
        for (int k = 0; k < passCount; ++k)
        {
            const double from = passes[k].length + passes[k].cut;

            double to = passes[(k + 1) % passCount].length - passes[(k + 1) % passCount].cut;

            if (to <= from)
                to += total;

            QVector<Vec3> piece;

            piece.push_back(arc.pointAtLength(from));

            // the polyline's own points in between
            const int n = points.size();

            for (int wrap = 0; wrap < 2; ++wrap)
            {
                for (int i = 0; i < n; ++i)
                {
                    const double length = arc.cum[i] + wrap * total;

                    if (length > from + 1e-9 && length < to - 1e-9)
                        piece.push_back(points[i]);
                }
            }

            piece.push_back(arc.pointAtLength(to));

            pieces.push_back(piece);
        }
    }

    /*
     * Seifert circles: at the end of a piece, the smoothing continues on the
     * piece that leaves the crossing on the other strand.
     */
    const int pieceCount = pieces.size();

    QVector<int> pieceCircle(pieceCount, -1);
    // first polygon index of every piece in its circle
    QVector<int> pieceStart(pieceCount, 0);

    for (int start = 0; start < pieceCount; ++start)
    {
        if (pieceCircle[start] >= 0)
            continue;

        const int circle = surface.circlePolygons.size();

        QVector<Vec3> polygon;

        int piece = start;

        do
        {
            pieceCircle[piece] = circle;
            pieceStart[piece] = polygon.size();

            polygon += pieces[piece];

            piece = c == 0 ? piece : partner((piece + 1) % passCount);
        }
        while (piece != start);

        surface.circlePolygons.push_back(polygon);
    }

    const int circleCount = surface.circlePolygons.size();

    surface.circles = circleCount;
    surface.genus = (c - circleCount + 1) / 2;

    /*
     * Orientation and nesting (heights).
     */
    for (int i = 0; i < circleCount; ++i)
    {
        const QVector<Vec3> &polygon = surface.circlePolygons[i];

        surface.circleCounterClockwise.push_back(signedArea(polygon) > 0.0);

        int depth = 0;

        for (int j = 0; j < circleCount; ++j)
        {
            if (j != i && insidePolygon(surface.circlePolygons[j], polygon[0].x, polygon[0].y))
                ++depth;
        }

        surface.circleDepth.push_back(depth);
    }

    for (int i = 0; i < circleCount; ++i)
    {
        for (Vec3 &p : surface.circlePolygons[i])
            p.z = surface.circleDepth[i] * levelGap;
    }

    /*
     * Bands.
     */
    const QVector<int> signs = diagramArcs(diagram).sign;

    // twist: the edges must clear each other at the crossing, but the band
    // must stay between the disks it joins
    const double twistHeight = std::min(0.4 * levelGap, std::max(0.6 * cornerCut, 0.03 * extent));

    for (int k = 0; k < c; ++k)
    {
        const int p1 = passA[k];
        const int p2 = passB[k];

        const int beforeP1 = (p1 + passCount - 1) % passCount;
        const int beforeP2 = (p2 + passCount - 1) % passCount;

        SeifertSurface::Band band;

        band.circleA = pieceCircle[beforeP1];
        band.circleB = pieceCircle[beforeP2];

        // circle A: ... piece before p1, then the piece after p2
        band.a0 = pieces[beforeP1].last();
        band.a1 = pieces[p2].first();

        // circle B: ... piece before p2, then the piece after p1
        band.b1 = pieces[beforeP2].last();
        band.b0 = pieces[p1].first();

        band.zA = surface.circleDepth[band.circleA] * levelGap;
        band.zB = surface.circleDepth[band.circleB] * levelGap;

        band.a0.z = band.a1.z = band.zA;
        band.b0.z = band.b1.z = band.zB;

        // the a0 -> b0 edge is strand A
        band.twist = diagram.crossings[k].aOver ? twistHeight : -twistHeight;
        band.sign = signs.value(k, 1);

        band.cornerA = pieceStart[beforeP1] + pieces[beforeP1].size() - 1;
        band.cornerB = pieceStart[beforeP2] + pieces[beforeP2].size() - 1;

        surface.bands.push_back(band);
    }

    /*
     * The knot as the boundary: pieces at their heights, band edges at the
     * crossings.
     */
    constexpr int edgeSteps = 10;

    auto buildBoundary = [&](bool flat)
    {
        QVector<Vec3> curve;

        for (int k = 0; k < pieceCount; ++k)
        {
            const double z = flat ? 0.0 : surface.circleDepth[pieceCircle[k]] * levelGap;

            for (const Vec3 &p : pieces[k])
                curve.push_back(Vec3(p.x, p.y, z));

            if (c == 0)
                break;

            // the pass at the end of this piece
            const int p = (k + 1) % passCount;

            SeifertSurface::Band band = surface.bands[passes[p].crossing];

            if (flat)
                band.zA = band.zB = 0.0;

            for (int step = 1; step < edgeSteps; ++step)
            {
                const double t = static_cast<double>(step) / edgeSteps;

                // strand A runs along v = 0 from circle A to B, strand B
                // along v = 1 from circle B to A
                curve.push_back(passes[p].isA ? seifertBandPoint(band, t, 0.0)
                                              : seifertBandPoint(band, 1.0 - t, 1.0));
            }
        }

        return curve;
    };

    surface.boundary = buildBoundary(false);
    surface.flatBoundary = buildBoundary(true);

    surface.valid = true;

    if (!computeMatrix)
        return surface;

    /*
     * Seifert matrix.
     */
    const int size = c - circleCount + 1;

    if (size == 0)
    {
        surface.matrixValid = true;
        surface.determinant = 1;
        surface.alexander[0] = 1;
        surface.alexanderText = QStringLiteral("1");
        return surface;
    }

    if (size > MaxMatrixSize)
    {
        surface.matrixError =
            seifertTr("The surface is too large for the Seifert matrix (genus %1).").arg(surface.genus);
        return surface;
    }

    // spanning tree of the Seifert graph (circles, bands)
    QVector<int> parent(circleCount, -1);
    QVector<int> parentBand(circleCount, -1);
    QVector<int> depthInTree(circleCount, -1);
    QVector<bool> treeBand(c, false);

    {
        QVector<int> queue{0};

        depthInTree[0] = 0;

        for (int head = 0; head < queue.size(); ++head)
        {
            const int circle = queue[head];

            for (int k = 0; k < c; ++k)
            {
                const SeifertSurface::Band &band = surface.bands[k];

                int other = -1;

                if (band.circleA == circle)
                    other = band.circleB;
                else if (band.circleB == circle)
                    other = band.circleA;

                if (other < 0 || depthInTree[other] >= 0)
                    continue;

                depthInTree[other] = depthInTree[circle] + 1;
                parent[other] = circle;
                parentBand[other] = k;
                treeBand[k] = true;

                queue.push_back(other);
            }
        }
    }

    for (int i = 0; i < circleCount; ++i)
    {
        if (depthInTree[i] < 0)
        {
            surface.matrixError = seifertTr("The Seifert circles are not connected.");
            return surface;
        }
    }

    // Paths across a disk follow its circle, offset slightly inwards; thin
    // circles (between two crossings of a twist) get a smaller offset.
    // Area / perimeter is about a third of the width of a thin circle.
    QVector<double> inset(circleCount, LoopInset * extent);

    for (int i = 0; i < circleCount; ++i)
    {
        const QVector<Vec3> &polygon = surface.circlePolygons[i];

        double perimeter = 0.0;

        for (int k = 0; k < polygon.size(); ++k)
            perimeter += (polygon[(k + 1) % polygon.size()] - polygon[k]).length();

        if (perimeter > 1e-12)
            inset[i] = std::min(inset[i], 0.5 * std::abs(signedArea(polygon)) / perimeter);
    }

    const double pushOff = 0.01 * extent;

    auto inward = [&](int circle, const Vec3 &tangent)
    {
        const double sign = surface.circleCounterClockwise[circle] ? 1.0 : -1.0;

        return Vec3(-tangent.y * sign, tangent.x * sign, 0.0).normalized();
    };

    auto diskNormal = [&](int circle)
    {
        return Vec3(0.0, 0.0, surface.circleCounterClockwise[circle] ? 1.0 : -1.0);
    };

    // corner of band k on circle `circle`
    auto cornerOf = [&](int k, int circle)
    {
        const SeifertSurface::Band &band = surface.bands[k];

        return band.circleA == circle ? band.cornerA : band.cornerB;
    };

    auto cornerPoint = [&](int circle, int corner)
    {
        const QVector<Vec3> &polygon = surface.circlePolygons[circle];
        const int m = polygon.size();

        const Vec3 &a = polygon[corner % m];
        const Vec3 &b = polygon[(corner + 1) % m];

        const Vec3 tangent = (b - a).normalized();

        return (a + b) * 0.5 + inward(circle, tangent) * inset[circle];
    };

    auto insetVertex = [&](int circle, int i)
    {
        const QVector<Vec3> &polygon = surface.circlePolygons[circle];
        const int m = polygon.size();

        const Vec3 tangent = (polygon[(i + 1) % m] - polygon[(i + m - 1) % m]).normalized();

        return polygon[i % m] + inward(circle, tangent) * inset[circle];
    };

    struct Step
    {
        int band = 0;
        int from = 0;
        int to = 0;
    };

    // a loop: band steps from circle to circle, back to the start
    QVector<QVector<Step>> loops;

    for (int k = 0; k < c; ++k)
    {
        if (treeBand[k])
            continue;

        const SeifertSurface::Band &band = surface.bands[k];

        // A -> B over band k, then back to A in the tree
        QVector<Step> loop;

        loop.push_back({k, band.circleA, band.circleB});

        int x = band.circleB;
        int y = band.circleA;

        QVector<Step> up;
        QVector<Step> down;

        while (x != y)
        {
            if (depthInTree[x] >= depthInTree[y])
            {
                up.push_back({parentBand[x], x, parent[x]});
                x = parent[x];
            }
            else
            {
                down.push_front({parentBand[y], parent[y], y});
                y = parent[y];
            }
        }

        loop += up;
        loop += down;

        loops.push_back(loop);
    }

    // the loops as curves on the surface, and pushed off it
    QVector<QVector<Vec3>> onSurface;
    QVector<QVector<Vec3>> pushed;
    QVector<QVector<Vec3>> normalsAlong;

    constexpr int bandSteps = 12;

    for (const QVector<Step> &loop : loops)
    {
        QVector<Vec3> curve;
        QVector<Vec3> off;
        QVector<Vec3> normals;

        for (int i = 0; i < loop.size(); ++i)
        {
            const Step &step = loop[i];
            const SeifertSurface::Band &band = surface.bands[step.band];

            // across the band, along its middle
            const bool forward = step.from == band.circleA;

            for (int j = 0; j <= bandSteps; ++j)
            {
                const double t = static_cast<double>(j) / bandSteps;
                const double u = forward ? t : 1.0 - t;

                const Vec3 p = seifertBandPoint(band, u, 0.5);

                Vec3 normal = bandNormal(band, u, 0.5);

                // Where the band meets a disk the surface can be folded
                // (a band rising from a disk to a circle inside it): the
                // positive side is the wedge between them, along the
                // bisector of the two normals.
                if (j == 0)
                    normal = (normal + diskNormal(step.from)).normalized();
                else if (j == bandSteps)
                    normal = (normal + diskNormal(step.to)).normalized();

                curve.push_back(p);
                off.push_back(p + normal * pushOff);
                normals.push_back(normal);
            }

            // across the disk of the next circle, to the next band
            const int circle = step.to;
            const Step &next = loop[(i + 1) % loop.size()];

            const int entry = cornerOf(step.band, circle);
            const int exit = cornerOf(next.band, circle);

            const int m = surface.circlePolygons[circle].size();

            const int forwardCount = ((exit - entry) % m + m) % m;
            const int backwardCount = ((entry - exit) % m + m) % m;

            QVector<Vec3> path;

            path.push_back(cornerPoint(circle, entry));

            if (forwardCount <= backwardCount)
            {
                for (int j = 1; j <= forwardCount; ++j)
                    path.push_back(insetVertex(circle, entry + j));
            }
            else
            {
                for (int j = 0; j < backwardCount; ++j)
                    path.push_back(insetVertex(circle, entry - j + m));
            }

            path.push_back(cornerPoint(circle, exit));

            const Vec3 normal = diskNormal(circle);

            for (const Vec3 &p : path)
            {
                curve.push_back(p);
                off.push_back(p + normal * pushOff);
                normals.push_back(normal);
            }
        }

        onSurface.push_back(curve);
        pushed.push_back(off);
        normalsAlong.push_back(normals);
    }

    QVector<QVector<int>> matrix(size, QVector<int>(size, 0));

    for (int i = 0; i < size; ++i)
    {
        for (int j = 0; j < size; ++j)
        {
            const double lk = computeLinkingNumber(onSurface[i], pushed[j], 0);

            const double rounded = std::round(lk);

            if (std::abs(lk - rounded) > 0.2)
            {
                surface.matrixError =
                    seifertTr("A linking number on the surface could not be determined.");
                return surface;
            }

            matrix[i][j] = static_cast<int>(rounded);
        }
    }

    // check: det(V - V^T) = +-1 for a knot
    QVector<QVector<long long>> antisymmetric(size, QVector<long long>(size, 0));
    QVector<QVector<long long>> symmetric(size, QVector<long long>(size, 0));
    QVector<QVector<int>> symmetricInt(size, QVector<int>(size, 0));

    for (int i = 0; i < size; ++i)
    {
        for (int j = 0; j < size; ++j)
        {
            antisymmetric[i][j] = matrix[i][j] - matrix[j][i];
            symmetric[i][j] = matrix[i][j] + matrix[j][i];
            symmetricInt[i][j] = matrix[i][j] + matrix[j][i];
        }
    }

    if (std::llabs(integerDeterminant(antisymmetric)) != 1)
    {
        surface.matrixError = seifertTr("The Seifert matrix failed its check (det(V - V^T) = 1).");
        return surface;
    }

    surface.seifertMatrix = matrix;
    surface.determinant = std::llabs(integerDeterminant(symmetric));
    surface.signature = symmetricSignature(symmetricInt);

    // Alexander polynomial det(V - t V^T)
    QVector<QVector<Poly>> polynomial(size, QVector<Poly>(size));

    for (int i = 0; i < size; ++i)
    {
        for (int j = 0; j < size; ++j)
        {
            Poly entry{static_cast<long long>(matrix[i][j]), static_cast<long long>(-matrix[j][i])};

            trim(entry);

            polynomial[i][j] = entry;
        }
    }

    Poly delta = polynomialDeterminant(polynomial);

    int low = 0;

    while (low < delta.size() && delta[low] == 0)
        ++low;

    if (low >= delta.size())
    {
        surface.matrixError = seifertTr("The Seifert matrix failed its check (det(V - V^T) = 1).");
        return surface;
    }

    const int high = delta.size() - 1;

    // symmetric around t^0, and Delta(1) = 1
    const int shift = (low + high) / 2;

    long long valueAtOne = 0;

    for (long long coefficient : delta)
        valueAtOne += coefficient;

    const long long sign = valueAtOne < 0 ? -1 : 1;

    for (int i = low; i <= high; ++i)
    {
        if (delta[i] != 0)
            surface.alexander[i - shift] = sign * delta[i];
    }

    surface.alexanderText = formatPolynomial(surface.alexander);
    surface.matrixValid = true;

    // the loops behind the matrix, to be shown on the surface
    surface.loops = onSurface;
    surface.loopNormals = normalsAlong;
    surface.pushedLoops = pushed;

    return surface;
}

QVector<Vec3> seifertLiftedLoop(const SeifertSurface &surface, int index)
{
    QVector<Vec3> curve;

    if (index < 0 || index >= surface.loops.size())
        return curve;

    const QVector<Vec3> &loop = surface.loops[index];
    const QVector<Vec3> &normals = surface.loopNormals[index];
    const double lift = SeifertLoopLift * surface.extent;

    for (int i = 0; i < loop.size(); ++i)
        curve.push_back(loop[i] + normals[i] * lift);

    return curve;
}

double seifertTubeRadius(const SeifertSurface &surface, double requestedRadius)
{
    return std::min(requestedRadius, 0.022 * surface.extent);
}

Mesh seifertMesh(
    const SeifertSurface &surface,
    int step,
    double tubeRadius,
    const QColor &knotColor,
    const QColor &front,
    const QColor &back,
    const SeifertHighlight &highlight)
{
    Mesh mesh;

    if (!surface.valid)
        return mesh;

    if (step <= 0)
    {
        mesh.append(tube(surface.flatBoundary, tubeRadius, knotColor));
        layFlat(mesh);
        return mesh;
    }

    // circles: their own colours while they are the subject, then the
    // colour of the knot
    if (step <= 2)
    {
        for (int i = 0; i < surface.circles; ++i)
        {
            mesh.append(tube(surface.circlePolygons[i],
                             step == 1 ? tubeRadius : 0.8 * tubeRadius,
                             step == 1 ? componentColor(i) : knotColor));
        }
    }

    // the surface, joined to the tubes at the end
    Mesh surfaceMesh;

    if (step >= 2)
    {
        // disks
        for (int i = 0; i < surface.circles; ++i)
        {
            const QVector<Vec3> &polygon = surface.circlePolygons[i];

            const QVector<int> triangles = triangulate(polygon);

            const Vec3 normal(0.0, 0.0, surface.circleCounterClockwise[i] ? 1.0 : -1.0);

            addTwoSided(surfaceMesh, polygon, QVector<Vec3>(polygon.size(), normal), triangles, front, back);
        }
    }

    if (step >= 3)
    {
        // bands
        constexpr int nu = 20;
        constexpr int nv = 6;

        for (const SeifertSurface::Band &band : surface.bands)
        {
            QVector<Vec3> positions;
            QVector<Vec3> normals;
            QVector<int> triangles;

            for (int i = 0; i <= nu; ++i)
            {
                for (int j = 0; j <= nv; ++j)
                {
                    const double u = static_cast<double>(i) / nu;
                    const double v = static_cast<double>(j) / nv;

                    positions.push_back(seifertBandPoint(band, u, v));
                    normals.push_back(bandNormal(band, u, v));
                }
            }

            for (int i = 0; i < nu; ++i)
            {
                for (int j = 0; j < nv; ++j)
                {
                    const int a = i * (nv + 1) + j;
                    const int b = a + nv + 1;

                    triangles << a << b << b + 1;
                    triangles << a << b + 1 << a + 1;
                }
            }

            addTwoSided(surfaceMesh, positions, normals, triangles, front, back);
        }

        // Two loops of the Seifert matrix: thin enough that the lifted
        // one, just off the surface, does not touch the other.
        const int count = surface.loops.size();

        const bool showLoops =
            surface.matrixValid && highlight.loop >= 0 && highlight.loop < count &&
            highlight.lifted >= 0 && highlight.lifted < count;

        // On the disks the loops run close to the knot: a thinner knot
        // leaves them in sight.
        mesh.append(tube(surface.boundary,
                         showLoops ? std::min(tubeRadius, 0.45 * LoopInset * surface.extent) : tubeRadius,
                         knotColor));

        if (showLoops)
        {
            const double loopRadius =
                std::min(0.35 * tubeRadius, 0.4 * SeifertLoopLift * surface.extent);

            // without repeated points (where a band meets a disk)
            auto cleaned = [](const QVector<Vec3> &curve)
            {
                QVector<Vec3> result;

                for (const Vec3 &p : curve)
                {
                    if (result.isEmpty() || (p - result.last()).length() > 1e-9)
                        result.push_back(p);
                }

                if (result.size() > 1 && (result.first() - result.last()).length() <= 1e-9)
                    result.removeLast();

                return result;
            };

            mesh.append(tube(cleaned(surface.loops[highlight.loop]), loopRadius, highlight.loopColor));
            mesh.append(tube(cleaned(seifertLiftedLoop(surface, highlight.lifted)),
                             loopRadius,
                             highlight.liftedColor));
        }
    }

    mesh.append(surfaceMesh);

    layFlat(mesh);

    return mesh;
}

} // namespace KnotMath
