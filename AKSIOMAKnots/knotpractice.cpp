#include "knotpractice.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace KnotMath
{

namespace
{

int mod3(int v)
{
    return ((v % 3) + 3) % 3;
}

// Solutions of A x = b over the integers mod 3: one solution and a basis of
// the solutions of A x = 0. false if there is no solution.
bool solveMod3(
    QVector<QVector<int>> a,
    QVector<int> b,
    int unknowns,
    QVector<int> &particular,
    QVector<QVector<int>> &kernel)
{
    const int rows = a.size();

    QVector<int> pivotColumn;

    int row = 0;

    for (int col = 0; col < unknowns && row < rows; ++col)
    {
        int pivot = -1;

        for (int r = row; r < rows; ++r)
        {
            if (a[r][col] != 0)
            {
                pivot = r;
                break;
            }
        }

        if (pivot < 0)
            continue;

        std::swap(a[row], a[pivot]);
        std::swap(b[row], b[pivot]);

        // 1 and 2 are their own inverses mod 3
        const int inverse = a[row][col];

        for (int c = 0; c < unknowns; ++c)
            a[row][c] = mod3(a[row][c] * inverse);

        b[row] = mod3(b[row] * inverse);

        for (int r = 0; r < rows; ++r)
        {
            if (r == row || a[r][col] == 0)
                continue;

            const int factor = a[r][col];

            for (int c = 0; c < unknowns; ++c)
                a[r][c] = mod3(a[r][c] - factor * a[row][c]);

            b[r] = mod3(b[r] - factor * b[row]);
        }

        pivotColumn.push_back(col);
        ++row;
    }

    for (int r = row; r < rows; ++r)
    {
        if (b[r] != 0)
            return false;
    }

    QVector<bool> isPivot(unknowns, false);

    for (int col : pivotColumn)
        isPivot[col] = true;

    particular = QVector<int>(unknowns, 0);

    for (int r = 0; r < pivotColumn.size(); ++r)
        particular[pivotColumn[r]] = b[r];

    kernel.clear();

    for (int free = 0; free < unknowns; ++free)
    {
        if (isPivot[free])
            continue;

        QVector<int> v(unknowns, 0);

        v[free] = 1;

        for (int r = 0; r < pivotColumn.size(); ++r)
            v[pivotColumn[r]] = mod3(-a[r][free]);

        kernel.push_back(v);
    }

    return true;
}

// Row per crossing: 2 for the over arc, -1 for each under arc.
QVector<QVector<int>> coloringMatrix(const DiagramArcs &arcs)
{
    QVector<QVector<int>> m;

    for (int k = 0; k < arcs.overArc.size(); ++k)
    {
        QVector<int> row(arcs.arcCount, 0);

        row[arcs.overArc[k]] += 2;
        row[arcs.inArc[k]] -= 1;
        row[arcs.outArc[k]] -= 1;

        m.push_back(row);
    }

    return m;
}

bool isConstant(const QVector<int> &v)
{
    for (int x : v)
    {
        if (x != v.first())
            return false;
    }

    return true;
}

Vec3 segmentDirection(const KnotDiagram &diagram, int segment)
{
    const int n = diagram.points.size();

    const Vec3 &a = diagram.points[segment];
    const Vec3 &b = diagram.points[(segment + 1) % n];

    return b - a;
}

} // namespace

DiagramArcs diagramArcs(const KnotDiagram &diagram)
{
    DiagramArcs arcs;

    arcs.pointCount = diagram.points.size();

    const int c = diagram.crossings.size();

    for (const DiagramCrossing &crossing : diagram.crossings)
    {
        const double sA = crossing.segA + crossing.tA;
        const double sB = crossing.segB + crossing.tB;

        arcs.overS.push_back(crossing.aOver ? sA : sB);
        arcs.underPassS.push_back(crossing.aOver ? sB : sA);

        const Vec3 over = segmentDirection(
            diagram, crossing.aOver ? crossing.segA : crossing.segB);

        const Vec3 under = segmentDirection(
            diagram, crossing.aOver ? crossing.segB : crossing.segA);

        arcs.sign.push_back(over.x * under.y - over.y * under.x >= 0.0 ? 1 : -1);
    }

    arcs.underS = arcs.underPassS;

    std::sort(arcs.underS.begin(), arcs.underS.end());

    arcs.arcCount = std::max(1, c);

    for (int k = 0; k < c; ++k)
    {
        const int out = static_cast<int>(
            std::lower_bound(arcs.underS.begin(), arcs.underS.end(),
                             arcs.underPassS[k]) -
            arcs.underS.begin());

        arcs.outArc.push_back(out);
        arcs.inArc.push_back((out + c - 1) % c);
        arcs.overArc.push_back(arcAt(arcs, arcs.overS[k]));
    }

    return arcs;
}

int arcAt(const DiagramArcs &arcs, double s)
{
    const int c = arcs.underS.size();

    if (c == 0)
        return 0;

    // the last under-pass at or before s; before the first one the curve
    // is still on the last arc
    const int k = static_cast<int>(
        std::upper_bound(arcs.underS.begin(), arcs.underS.end(), s) -
        arcs.underS.begin()) - 1;

    return k < 0 ? c - 1 : k;
}

bool crossingColoringOk(
    const DiagramArcs &arcs,
    int crossing,
    const QVector<int> &colors)
{
    const int o = colors.value(arcs.overArc[crossing], -1);
    const int i = colors.value(arcs.inArc[crossing], -1);
    const int u = colors.value(arcs.outArc[crossing], -1);

    if (o < 0 || i < 0 || u < 0)
        return true;

    // all equal or all different: exactly when the sum is 0 mod 3
    return (o + i + u) % 3 == 0;
}

int coloringDimension(const DiagramArcs &arcs)
{
    QVector<int> particular;
    QVector<QVector<int>> kernel;

    solveMod3(coloringMatrix(arcs),
              QVector<int>(arcs.overArc.size(), 0),
              arcs.arcCount, particular, kernel);

    return kernel.size();
}

QVector<int> completeTricoloring(
    const DiagramArcs &arcs,
    const QVector<int> &fixed)
{
    QVector<QVector<int>> a = coloringMatrix(arcs);
    QVector<int> b(a.size(), 0);

    for (int arc = 0; arc < arcs.arcCount; ++arc)
    {
        const int color = fixed.value(arc, -1);

        if (color < 0)
            continue;

        QVector<int> row(arcs.arcCount, 0);

        row[arc] = 1;

        a.push_back(row);
        b.push_back(color);
    }

    QVector<int> particular;
    QVector<QVector<int>> kernel;

    if (!solveMod3(a, b, arcs.arcCount, particular, kernel))
        return {};

    if (!isConstant(particular))
        return particular;

    // a constant solution plus any non-constant direction is non-constant
    for (const QVector<int> &v : kernel)
    {
        if (isConstant(v))
            continue;

        QVector<int> result = particular;

        for (int i = 0; i < result.size(); ++i)
            result[i] = mod3(result[i] + v[i]);

        return result;
    }

    return {};
}

long long diagramDeterminant(const DiagramArcs &arcs)
{
    const int c = arcs.overArc.size();

    if (c <= 1)
        return 1;

    // Bareiss elimination: every intermediate value is a minor of the
    // matrix, and rows have length sqrt(6), so a k x k minor is at most
    // 6^(k/2) and the product of two of them fits in 64 bits up to k = 23.
    if (c > 24)
        return -1;

    const QVector<QVector<int>> full = coloringMatrix(arcs);

    const int n = c - 1;

    QVector<QVector<long long>> m(n, QVector<long long>(n, 0));

    for (int r = 0; r < n; ++r)
    {
        for (int col = 0; col < n; ++col)
            m[r][col] = full[r][col];
    }

    long long previous = 1;

    for (int k = 0; k < n; ++k)
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

            // only the absolute value is needed
            std::swap(m[k], m[swapRow]);
        }

        for (int r = k + 1; r < n; ++r)
        {
            for (int col = k + 1; col < n; ++col)
            {
                m[r][col] =
                    (m[r][col] * m[k][k] - m[r][k] * m[k][col]) / previous;
            }

            m[r][k] = 0;
        }

        previous = m[k][k];
    }

    return std::llabs(m[n - 1][n - 1]);
}

} // namespace KnotMath
