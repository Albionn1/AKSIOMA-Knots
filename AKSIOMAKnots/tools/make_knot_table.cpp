/*
 * Builds data/knottable.dat (the knot table of the Knot Browser) from the
 * KnotInfo database: C. Livingston and A. H. Moore, KnotInfo: Table of Knot
 * Invariants, knotinfo.org.
 *
 * Input (from knotinfo.org -> Database Download):
 *   3d-coordinates.csv          (from knotinfo_3d-coordinates.csv.tar.gz)
 *   knotinfo_data_complete.csv  (knotinfo_data_complete.xls saved as CSV UTF-8)
 *
 * Build and run (Qt 6 Core only, not part of the application build):
 *   g++ -std=c++17 -O2 make_knot_table.cpp -I<Qt>/include -I<Qt>/include/QtCore
 *       -L<Qt>/lib -lQt6Core -o make_knot_table
 *   make_knot_table 3d-coordinates.csv knotinfo_data_complete.csv knottable.dat
 *
 * Output format (QDataStream, Qt 6.0, the whole stream compressed with
 * qCompress): "AKSK", version, count, quantisation levels, then per knot
 *   name, conway name, crossing number, alternating, fibered,
 *   unknotting number (text), genus, braid index, braid notation, signature,
 *   determinant, volume, symmetry type, Conway / two-bridge / pretzel /
 *   Montesinos notation and geometric type (torus, satellite, hyperbolic),
 *   Alexander and Jones polynomials as (power, coefficient) lists,
 *   bounding box and quantised 3D coordinates of the curve (0..levels in
 *   the box, each stored as the difference to the previous point, modulo
 *   2^16).
 */

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{

// One CSV line (RFC 4180: quoted fields, "" inside quotes).
QStringList splitCsv(const QString &line)
{
    QStringList fields;
    QString field;
    bool quoted = false;

    for (int i = 0; i < line.size(); ++i)
    {
        const QChar c = line.at(i);

        if (quoted)
        {
            if (c == QLatin1Char('"'))
            {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"'))
                {
                    field += QLatin1Char('"');
                    ++i;
                }
                else
                {
                    quoted = false;
                }
            }
            else
            {
                field += c;
            }
        }
        else if (c == QLatin1Char('"'))
        {
            quoted = true;
        }
        else if (c == QLatin1Char(','))
        {
            fields << field;
            field.clear();
        }
        else
        {
            field += c;
        }
    }

    fields << field;

    return fields;
}

// "t^(-2)-t^(-1)+ 1-t+ t^2", "-7+ 34*t-53*t^2", "3/t^8 - 17/t"
// -> power -> coefficient. Returns false if the text is not a Laurent
// polynomial in t.
bool parsePolynomial(QString text, QMap<int, qint64> &terms)
{
    terms.clear();
    text.remove(QLatin1Char(' '));

    if (text.isEmpty())
        return false;

    // split into signed terms at + and - that are not inside parentheses
    QStringList pieces;
    QString current;
    int depth = 0;

    for (int i = 0; i < text.size(); ++i)
    {
        const QChar c = text.at(i);

        if (c == QLatin1Char('('))
            ++depth;
        else if (c == QLatin1Char(')'))
            --depth;

        if ((c == QLatin1Char('+') || c == QLatin1Char('-')) && depth == 0 && i > 0 &&
            text.at(i - 1) != QLatin1Char('^'))
        {
            pieces << current;
            current.clear();
        }

        current += c;
    }

    pieces << current;

    // c*t^k, c*t^(-k), c*t, c, and c/t^k, c/t (negative powers)
    static const QRegularExpression term(
        QStringLiteral("^([+-]?)(\\d*)(?:\\*?t(?:\\^\\(?(-?\\d+)\\)?)?|(/t)(?:\\^(\\d+))?)?$"));

    for (const QString &piece : pieces)
    {
        if (piece.isEmpty())
            continue;

        const QRegularExpressionMatch m = term.match(piece);

        const bool hasT =
            piece.contains(QLatin1Char('t'));

        if (!m.hasMatch() || (m.captured(2).isEmpty() && !hasT))
            return false;

        qint64 coefficient = m.captured(2).isEmpty() ? 1 : m.captured(2).toLongLong();

        if (m.captured(1) == QLatin1String("-"))
            coefficient = -coefficient;

        int power = 0;

        if (!m.captured(4).isEmpty())
            power = m.captured(5).isEmpty() ? -1 : -m.captured(5).toInt();
        else if (hasT)
            power = m.captured(3).isEmpty() ? 1 : m.captured(3).toInt();

        terms[power] += coefficient;
    }

    for (auto it = terms.begin(); it != terms.end();)
    {
        if (it.value() == 0)
            it = terms.erase(it);
        else
            ++it;
    }

    return !terms.isEmpty();
}

struct Record
{
    QString name;
    QString conwayName;
    qint8 crossings = 0;
    bool alternating = false;
    bool fibered = false;
    QString unknotting;
    qint8 genus = -1;
    qint8 braidIndex = -1;
    QString braidNotation;
    qint16 signature = 0;
    qint32 determinant = 0;
    float volume = 0.0f;
    QString symmetry;

    // knot family (version 2)
    QString conwayNotation;
    QString twoBridge;
    QString pretzel;
    QString montesinos;
    QString geometricType;

    QMap<int, qint64> alexander;
    QMap<int, qint64> jones;
    QVector<float> points; // x, y, z
};

int intOr(const QString &text, int fallback)
{
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);

    return ok ? value : fallback;
}

void writePolynomial(QDataStream &out, const QMap<int, qint64> &terms)
{
    out << static_cast<qint32>(terms.size());

    for (auto it = terms.begin(); it != terms.end(); ++it)
        out << static_cast<qint32>(it.key()) << static_cast<qint64>(it.value());
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr,
                     "usage: make_knot_table 3d-coordinates.csv "
                     "knotinfo_data_complete.csv knottable.dat\n");
        return 1;
    }

    /*
     * 3D coordinates: name,crossings,index,[[x;y;z];[x;y;z];...]
     */
    QHash<QString, QVector<float>> coordinates;

    {
        QFile file(QString::fromLocal8Bit(argv[1]));

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            std::fprintf(stderr, "cannot read %s\n", argv[1]);
            return 1;
        }

        QTextStream in(&file);

        while (!in.atEnd())
        {
            const QString line = in.readLine().trimmed();
            const int bracket = line.indexOf(QLatin1Char('['));

            if (bracket < 0)
                continue;

            const QString name = line.left(line.indexOf(QLatin1Char(',')));

            QString list = line.mid(bracket);
            list.remove(QLatin1Char('['));
            list.remove(QLatin1Char(']'));

            const QStringList numbers = list.split(QLatin1Char(';'), Qt::SkipEmptyParts);

            if (numbers.size() < 9 || numbers.size() % 3 != 0)
            {
                std::fprintf(stderr, "bad coordinates for %s\n", qPrintable(name));
                continue;
            }

            QVector<float> points;
            points.reserve(numbers.size());

            for (const QString &number : numbers)
                points << number.toFloat();

            coordinates.insert(name, points);
        }
    }

    /*
     * Invariants: header row with column names, a second row with
     * descriptions, then one row per knot.
     */
    QVector<Record> records;

    int badAlexander = 0;
    int badJones = 0;

    {
        QFile file(QString::fromLocal8Bit(argv[2]));

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            std::fprintf(stderr, "cannot read %s\n", argv[2]);
            return 1;
        }

        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);

        QStringList header = splitCsv(in.readLine());

        if (!header.isEmpty())
            header[0].remove(QChar(0xFEFF));

        QHash<QString, int> column;

        for (int i = 0; i < header.size(); ++i)
            column.insert(header.at(i).trimmed(), i);

        in.readLine(); // descriptions

        auto value = [&column](const QStringList &row, const char *name)
        {
            const int i = column.value(QString::fromLatin1(name), -1);

            return i >= 0 && i < row.size() ? row.at(i).trimmed() : QString();
        };

        while (!in.atEnd())
        {
            const QStringList row = splitCsv(in.readLine());

            Record r;

            r.name = value(row, "name");

            // the unknot has no 3D curve and is a preset already
            if (r.name.isEmpty() || r.name == QLatin1String("0_1"))
                continue;

            r.conwayName = value(row, "classical_conway_name");
            r.crossings = static_cast<qint8>(intOr(value(row, "crossing_number"), 0));
            r.alternating = value(row, "alternating") == QLatin1String("Y");
            r.fibered = value(row, "fibered") == QLatin1String("Y");
            r.unknotting = value(row, "unknotting_number");
            r.genus = static_cast<qint8>(intOr(value(row, "three_genus"), -1));
            r.braidIndex = static_cast<qint8>(intOr(value(row, "braid_index"), -1));
            r.braidNotation = value(row, "braid_notation");
            r.signature = static_cast<qint16>(intOr(value(row, "signature"), 0));
            r.determinant = intOr(value(row, "determinant"), 0);
            r.volume = value(row, "volume").toFloat();
            r.symmetry = value(row, "symmetry_type");

            r.conwayNotation = value(row, "conway_notation");
            r.twoBridge = value(row, "two_bridge_notation");
            r.pretzel = value(row, "pretzel_notation");
            r.montesinos = value(row, "montesinos_notation");
            r.geometricType = value(row, "geometric_type");

            if (!parsePolynomial(value(row, "alexander_polynomial"), r.alexander))
                ++badAlexander;

            if (!parsePolynomial(value(row, "jones_polynomial"), r.jones))
                ++badJones;

            r.points = coordinates.value(r.name);

            if (r.points.isEmpty())
            {
                std::fprintf(stderr, "no coordinates for %s\n", qPrintable(r.name));
                continue;
            }

            records << r;
        }
    }

    /*
     * Write
     */
    QByteArray raw;

    {
        QDataStream out(&raw, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);

        // 12 bits per coordinate: 1/4095 of the knot's size, far below
        // what can be seen, and the file is about half as large
        const quint16 levels = 4095;

        out.writeRawData("AKSK", 4);
        out << static_cast<qint32>(2) << static_cast<qint32>(records.size()) << levels;

        for (const Record &r : records)
        {
            out << r.name << r.conwayName << r.crossings << r.alternating << r.fibered
                << r.unknotting << r.genus << r.braidIndex << r.braidNotation
                << r.signature << r.determinant << r.volume << r.symmetry
                << r.conwayNotation << r.twoBridge << r.pretzel << r.montesinos
                << r.geometricType;

            writePolynomial(out, r.alexander);
            writePolynomial(out, r.jones);

            // 16-bit coordinates inside the bounding box
            float lo[3] = {1e30f, 1e30f, 1e30f};
            float hi[3] = {-1e30f, -1e30f, -1e30f};

            for (int i = 0; i < r.points.size(); ++i)
            {
                lo[i % 3] = std::min(lo[i % 3], r.points.at(i));
                hi[i % 3] = std::max(hi[i % 3], r.points.at(i));
            }

            out << static_cast<quint16>(r.points.size() / 3);

            for (int k = 0; k < 3; ++k)
                out << lo[k] << hi[k];

            // stored as differences to the previous point (mod 2^16): the
            // numbers are small and compress much better
            quint16 previous[3] = {0, 0, 0};

            for (int i = 0; i < r.points.size(); ++i)
            {
                const float range = std::max(hi[i % 3] - lo[i % 3], 1e-12f);
                const float x = (r.points.at(i) - lo[i % 3]) / range;

                const quint16 q =
                    static_cast<quint16>(std::lround(std::clamp(x, 0.0f, 1.0f) * levels));

                out << static_cast<quint16>(q - previous[i % 3]);

                previous[i % 3] = q;
            }
        }
    }

    const QByteArray compressed = qCompress(raw, 9);

    QFile output(QString::fromLocal8Bit(argv[3]));

    if (!output.open(QIODevice::WriteOnly) || output.write(compressed) != compressed.size())
    {
        std::fprintf(stderr, "cannot write %s\n", argv[3]);
        return 1;
    }

    std::printf("%d knots, %d without Alexander, %d without Jones polynomial; "
                "%lld bytes (%lld uncompressed)\n",
                static_cast<int>(records.size()), badAlexander, badJones,
                static_cast<long long>(compressed.size()), static_cast<long long>(raw.size()));

    return 0;
}
