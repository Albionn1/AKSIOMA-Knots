#include "knottable.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace KnotMath
{

namespace
{

// For the names marked with QT_TRANSLATE_NOOP below; texts written in place
// call QCoreApplication::translate() directly, so that lupdate finds them
// with this context.
QString tableTr(const char *text)
{
    return QCoreApplication::translate("KnotTable", text);
}

std::map<int, long long> readPolynomial(QDataStream &in)
{
    std::map<int, long long> terms;

    qint32 count = 0;
    in >> count;

    for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i)
    {
        qint32 power = 0;
        qint64 coefficient = 0;

        in >> power >> coefficient;

        terms[power] = coefficient;
    }

    return terms;
}

// name -> index, built together with the table
QHash<QString, int> &nameIndex()
{
    static QHash<QString, int> index;
    return index;
}

} // namespace

const KnotTable &KnotTable::instance()
{
    static const KnotTable table;
    return table;
}

KnotTable::KnotTable()
{
    QFile file(QStringLiteral(":/data/knottable.dat"));

    if (!file.open(QIODevice::ReadOnly))
    {
        error_ = QCoreApplication::translate("KnotTable", "The knot table is missing from this build.");
        return;
    }

    const QByteArray raw = qUncompress(file.readAll());

    QDataStream in(raw);
    in.setVersion(QDataStream::Qt_6_0);

    char magic[4] = {0, 0, 0, 0};

    qint32 version = 0;
    qint32 count = 0;
    quint16 levels = 0;

    if (raw.isEmpty() ||
        in.readRawData(magic, 4) != 4 ||
        std::memcmp(magic, "AKSK", 4) != 0)
    {
        error_ = QCoreApplication::translate("KnotTable", "The knot table is damaged.");
        return;
    }

    in >> version >> count >> levels;

    if (version != 2 || count <= 0 || levels == 0)
    {
        error_ = QCoreApplication::translate("KnotTable", "The knot table has an unknown format.");
        return;
    }

    levels_ = levels;
    records_.reserve(count);

    for (qint32 i = 0; i < count; ++i)
    {
        KnotTableRecord r;

        qint8 crossings = 0;
        qint8 genus = -1;
        qint8 braidIndex = -1;
        qint16 signature = 0;
        qint32 determinant = 0;
        float volume = 0.0f;

        in >> r.name >> r.conwayName >> crossings >> r.alternating >> r.fibered
           >> r.unknotting >> genus >> braidIndex >> r.braidNotation
           >> signature >> determinant >> volume >> r.symmetry
           >> r.conwayNotation >> r.twoBridge >> r.pretzel >> r.montesinos
           >> r.geometricType;

        r.crossings = crossings;
        r.genus = genus;
        r.braidIndex = braidIndex;
        r.signature = signature;
        r.determinant = determinant;
        r.volume = volume;

        r.alexander = readPolynomial(in);
        r.jones = readPolynomial(in);

        quint16 pointCount = 0;
        in >> pointCount;

        for (int k = 0; k < 3; ++k)
            in >> r.boxLow[k] >> r.boxHigh[k];

        // coordinates are stored as differences to the previous point
        r.quantised.resize(pointCount * 3);

        quint16 previous[3] = {0, 0, 0};

        for (int j = 0; j < r.quantised.size(); ++j)
        {
            quint16 delta = 0;
            in >> delta;

            previous[j % 3] = static_cast<quint16>(previous[j % 3] + delta);
            r.quantised[j] = previous[j % 3];
        }

        if (in.status() != QDataStream::Ok)
        {
            error_ = QCoreApplication::translate("KnotTable", "The knot table is damaged.");
            records_.clear();
            nameIndex().clear();
            return;
        }

        nameIndex().insert(r.name, records_.size());
        records_.push_back(r);
    }

    loaded_ = true;
}

bool KnotTable::isLoaded() const
{
    return loaded_;
}

QString KnotTable::error() const
{
    return error_;
}

int KnotTable::size() const
{
    return records_.size();
}

const KnotTableRecord &KnotTable::at(int index) const
{
    return records_.at(index);
}

int KnotTable::indexOf(const QString &name) const
{
    return nameIndex().value(name.trimmed(), -1);
}

QVector<Vec3> KnotTable::curve(int index) const
{
    QVector<Vec3> points;

    if (index < 0 || index >= records_.size())
        return points;

    const KnotTableRecord &r = records_.at(index);
    const int count = r.quantised.size() / 3;

    points.reserve(count);

    Vec3 centre;

    for (int i = 0; i < count; ++i)
    {
        double xyz[3];

        for (int k = 0; k < 3; ++k)
        {
            const double unit =
                static_cast<double>(r.quantised.at(3 * i + k)) / levels_;

            xyz[k] = r.boxLow[k] + unit * (r.boxHigh[k] - r.boxLow[k]);
        }

        points.push_back(Vec3(xyz[0], xyz[1], xyz[2]));
        centre += points.last();
    }

    if (count == 0)
        return points;

    centre = centre / static_cast<double>(count);

    // same size as the built-in presets (outer radius about 2.7)
    double extent = 0.0;

    for (const Vec3 &p : points)
        extent = std::max(extent, (p - centre).length());

    const double scale = extent > 1.0e-12 ? 2.7 / extent : 1.0;

    for (Vec3 &p : points)
        p = (p - centre) * scale;

    return points;
}

QString KnotTable::displayName(const QString &name)
{
    static const QString subscripts = QStringLiteral("₀₁₂₃₄₅₆₇₈₉");

    const int underscore = name.indexOf(QLatin1Char('_'));

    if (underscore < 0)
        return name;

    QString result = name.left(underscore);

    for (const QChar c : name.mid(underscore + 1))
        result += c.isDigit() ? subscripts.at(c.digitValue()) : c;

    return result;
}

QString KnotTable::commonName(const QString &name)
{
    struct Name
    {
        const char *knot;
        const char *name;
    };

    // The established names (as in Wikipedia's list of prime knots and its
    // knot articles).
    static const Name names[] = {
        {"3_1", QT_TRANSLATE_NOOP("KnotTable", "Trefoil knot")},
        {"4_1", QT_TRANSLATE_NOOP("KnotTable", "Figure-eight knot")},
        {"5_1", QT_TRANSLATE_NOOP("KnotTable", "Cinquefoil knot")},
        {"5_2", QT_TRANSLATE_NOOP("KnotTable", "Three-twist knot")},
        {"6_1", QT_TRANSLATE_NOOP("KnotTable", "Stevedore knot")},
        {"6_2", QT_TRANSLATE_NOOP("KnotTable", "Miller Institute knot")},
        {"7_1", QT_TRANSLATE_NOOP("KnotTable", "Septafoil knot")},
        {"7_4", QT_TRANSLATE_NOOP("KnotTable", "Endless knot")},
        {"8_18", QT_TRANSLATE_NOOP("KnotTable", "Carrick mat")},
        {"10_161", QT_TRANSLATE_NOOP("KnotTable", "Perko pair knot")},
        {"11n_34", QT_TRANSLATE_NOOP("KnotTable", "Conway knot")},
        {"11n_42", QT_TRANSLATE_NOOP("KnotTable", "Kinoshita-Terasaka knot")},
        {"12n_242", QT_TRANSLATE_NOOP("KnotTable", "(−2,3,7) pretzel knot")},
    };

    for (const Name &entry : names)
    {
        if (name == QLatin1String(entry.knot))
            return tableTr(entry.name);
    }

    return QString();
}

QString KnotTable::familyName(const KnotTableRecord &record)
{
    // proper minus signs
    auto pretty = [](QString text)
    {
        return text.replace(QLatin1Char('-'), QChar(0x2212));
    };

    // geometric type: "torus knot T(2,5)", "satellite T(2,3)_(2,7)"
    const QString geometric = record.geometricType.trimmed();

    if (geometric.startsWith(QLatin1String("torus knot"), Qt::CaseInsensitive))
        return QCoreApplication::translate("KnotTable", "Torus knot %1").arg(geometric.mid(11).trimmed());

    if (geometric.startsWith(QLatin1String("satel"), Qt::CaseInsensitive))
    {
        // both satellites up to 13 crossings are cables of the trefoil
        return geometric.contains(QLatin1String("T(2,3)_"))
                   ? QCoreApplication::translate("KnotTable", "Cable of the trefoil (satellite knot)")
                   : QCoreApplication::translate("KnotTable", "Satellite knot");
    }

    // two-bridge knot [p,q]
    static const QRegularExpression fraction(
        QStringLiteral("^\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\]$"));

    const QRegularExpressionMatch m = fraction.match(record.twoBridge.trimmed());

    if (m.hasMatch())
    {
        const long long p = m.captured(1).toLongLong();
        const long long q = m.captured(2).toLongLong();

        // Twist knot with n half-twists: p = 2n + 1 and q = +-n or +-n^-1
        // (mod p); n^-1 = -2 (mod p), so q is n, n + 1, 2 or p - 2.
        if (p % 2 == 1 && p >= 5)
        {
            const long long n = (p - 1) / 2;

            if (q == n || q == n + 1 || q == 2 || q == p - 2)
                return QCoreApplication::translate("KnotTable", "Twist knot (%1 half-twists)").arg(n);
        }

        return QCoreApplication::translate("KnotTable", "Two-bridge knot %1/%2").arg(p).arg(q);
    }

    // KnotInfo writes "Non-pretzel" where it is known that there is none
    const QString pretzel = record.pretzel.trimmed();

    if (pretzel.startsWith(QLatin1String("P(")))
        return QCoreApplication::translate("KnotTable", "Pretzel knot %1").arg(pretty(pretzel));

    const QString montesinos = record.montesinos.trimmed();

    if (!montesinos.isEmpty() &&
        !montesinos.startsWith(QLatin1String("Not"), Qt::CaseInsensitive))
    {
        return QCoreApplication::translate("KnotTable", "Montesinos knot");
    }

    if (record.volume > 0.0)
        return QCoreApplication::translate("KnotTable", "Hyperbolic knot");

    return QString();
}

QString KnotTable::descriptiveName(const KnotTableRecord &record)
{
    const QString common = commonName(record.name);

    return common.isEmpty() ? familyName(record) : common;
}

std::map<int, long long> KnotTable::normalisedAlexander(const KnotTableRecord &record)
{
    if (record.alexander.empty())
        return {};

    const int low = record.alexander.begin()->first;
    const int high = record.alexander.rbegin()->first;
    const int shift = -(low + high) / 2;

    long long valueAtOne = 0;

    for (const auto &term : record.alexander)
        valueAtOne += term.second;

    const long long sign = valueAtOne < 0 ? -1 : 1;

    std::map<int, long long> normalised;

    for (const auto &term : record.alexander)
        normalised[term.first + shift] = sign * term.second;

    return normalised;
}

QString KnotTable::alexanderText(const KnotTableRecord &record)
{
    return record.alexander.empty() ? QString()
                                    : formatPolynomial(normalisedAlexander(record));
}

QString KnotTable::jonesText(const KnotTableRecord &record)
{
    return record.jones.empty() ? QString() : formatPolynomial(record.jones);
}

} // namespace KnotMath
