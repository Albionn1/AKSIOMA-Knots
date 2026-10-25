#include "knotexport.h"

#include "knottable.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QTextDocumentFragment>

#include <algorithm>
#include <cmath>

namespace KnotExport
{

namespace
{

QString exportTr(const char *text)
{
    return QCoreApplication::translate("KnotExport", text);
}

const QString FormatName = QStringLiteral("AKSIOMA Knots");
constexpr int FormatVersion = 1;

// Names of the knot types in the file (stable, unlike the enum values).
struct PresetKey
{
    KnotMath::Preset preset;
    const char *key;
};

const PresetKey PresetKeys[] =
{
    {KnotMath::Preset::Unknot, "unknot"},
    {KnotMath::Preset::Trefoil, "trefoil"},
    {KnotMath::Preset::FigureEight, "figure-eight"},
    {KnotMath::Preset::Cinquefoil, "cinquefoil"},
    {KnotMath::Preset::Stevedore, "stevedore"},
    {KnotMath::Preset::DNAHelix, "dna"},
    {KnotMath::Preset::CelticKnot, "celtic"},
    {KnotMath::Preset::CustomTorus, "torus"},
    {KnotMath::Preset::CustomCoordinates, "custom"},
    {KnotMath::Preset::KnotTable, "table"},
};

QString presetKey(KnotMath::Preset preset)
{
    for (const PresetKey &entry : PresetKeys)
    {
        if (entry.preset == preset)
            return QString::fromLatin1(entry.key);
    }

    return QStringLiteral("custom");
}

bool presetFromKey(const QString &key, KnotMath::Preset &preset)
{
    for (const PresetKey &entry : PresetKeys)
    {
        if (key == QLatin1String(entry.key))
        {
            preset = entry.preset;
            return true;
        }
    }

    return false;
}

QString pointsText(const QVector<KnotMath::Vec3> &points)
{
    QString text;

    for (const KnotMath::Vec3 &p : points)
    {
        text += QStringLiteral("%1, %2, %3\n")
                    .arg(p.x, 0, 'f', 6)
                    .arg(p.y, 0, 'f', 6)
                    .arg(p.z, 0, 'f', 6);
    }

    return text;
}

// A coordinate list or a formula from a plain text file.
bool readTextKnot(const QString &content, const QString &baseName, KnotFile &file, QString &error)
{
    file = KnotFile();
    file.name = baseName;
    file.preset = KnotMath::Preset::CustomCoordinates;

    if (content.contains(QLatin1Char('=')))
    {
        file.customText = content.trimmed();
        return true;
    }

    // keep only lines that start like a number (skips headers, comments)
    static const QRegularExpression numberStart(
        QStringLiteral("^\\s*[\\(\\[]?\\s*[-+.0-9]"));

    QStringList lines;

    for (const QString &line : content.split(QRegularExpression(QStringLiteral("[\\r\\n]+"))))
    {
        if (numberStart.match(line).hasMatch())
            lines.push_back(line.trimmed());
    }

    if (lines.isEmpty())
    {
        error = exportTr("The file contains no coordinates (one \"x, y, z\" point per line).");
        return false;
    }

    const QString text = lines.join(QLatin1Char('\n'));

    QString parseError;

    if (!KnotMath::parseCoordinateSequence(text, file.points, parseError))
    {
        error = parseError;
        return false;
    }

    file.customText = text;

    return true;
}

QString plain(const QString &text)
{
    if (!text.contains(QLatin1Char('<')))
        return text;

    return QTextDocumentFragment::fromHtml(text).toPlainText();
}

} // namespace

/*
 * KNOT FILES
 */

bool writeKnotFile(const QString &path, const KnotFile &file, QString &error)
{
    QJsonObject knot;

    knot.insert(QStringLiteral("type"), presetKey(file.preset));
    knot.insert(QStringLiteral("p"), file.torusP);
    knot.insert(QStringLiteral("q"), file.torusQ);
    knot.insert(QStringLiteral("table"), file.tableKnot);
    knot.insert(QStringLiteral("custom"), file.customText);

    QJsonArray points;

    for (const KnotMath::Vec3 &p : file.points)
        points.append(QJsonArray{p.x, p.y, p.z});

    QJsonObject root;

    root.insert(QStringLiteral("format"), FormatName);
    root.insert(QStringLiteral("version"), FormatVersion);
    root.insert(QStringLiteral("name"), file.name);
    root.insert(QStringLiteral("knot"), knot);
    root.insert(QStringLiteral("shapeChanged"), file.shapeChanged);
    root.insert(QStringLiteral("points"), points);

    if (file.hasAppearance)
    {
        QJsonObject appearance;

        appearance.insert(QStringLiteral("thickness"), file.thickness / 100.0);
        appearance.insert(QStringLiteral("samples"), file.samples);
        appearance.insert(QStringLiteral("material"), file.material);
        appearance.insert(QStringLiteral("shine"), file.shine / 100.0);
        appearance.insert(QStringLiteral("colorMode"), file.colorMode);
        appearance.insert(QStringLiteral("color"), file.color.name());

        root.insert(QStringLiteral("appearance"), appearance);
    }

    QSaveFile out(path);

    if (!out.open(QIODevice::WriteOnly))
    {
        error = out.errorString();
        return false;
    }

    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));

    if (!out.commit())
    {
        error = out.errorString();
        return false;
    }

    return true;
}

bool readKnotFile(const QString &path, KnotFile &file, QString &error)
{
    QFile in(path);

    if (!in.open(QIODevice::ReadOnly))
    {
        error = in.errorString();
        return false;
    }

    const QByteArray bytes = in.readAll();
    const QString baseName = QFileInfo(path).completeBaseName();

    QJsonParseError parseError;

    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        // not JSON: a coordinate list or a formula
        if (QFileInfo(path).suffix().compare(QStringLiteral("aknot"), Qt::CaseInsensitive) == 0)
        {
            error = exportTr("The file is damaged (%1).").arg(parseError.errorString());
            return false;
        }

        return readTextKnot(QString::fromUtf8(bytes), baseName, file, error);
    }

    const QJsonObject root = document.object();

    if (root.value(QStringLiteral("format")).toString() != FormatName)
    {
        error = exportTr("This is not an AKSIOMA Knots file.");
        return false;
    }

    if (root.value(QStringLiteral("version")).toInt() > FormatVersion)
    {
        error = exportTr("The file was saved by a newer version of AKSIOMA Knots.");
        return false;
    }

    file = KnotFile();

    file.name = root.value(QStringLiteral("name")).toString(baseName);
    file.shapeChanged = root.value(QStringLiteral("shapeChanged")).toBool();

    for (const QJsonValue &value : root.value(QStringLiteral("points")).toArray())
    {
        const QJsonArray p = value.toArray();

        if (p.size() != 3)
            continue;

        const KnotMath::Vec3 point(p.at(0).toDouble(), p.at(1).toDouble(), p.at(2).toDouble());

        if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z))
            file.points.push_back(point);
    }

    const QJsonObject knot = root.value(QStringLiteral("knot")).toObject();

    const bool knownType =
        presetFromKey(knot.value(QStringLiteral("type")).toString(), file.preset);

    file.torusP = std::clamp(knot.value(QStringLiteral("p")).toInt(3), 1, 20);
    file.torusQ = std::clamp(knot.value(QStringLiteral("q")).toInt(2), 1, 20);
    file.tableKnot = knot.value(QStringLiteral("table")).toString();
    file.customText = knot.value(QStringLiteral("custom")).toString();

    const bool unknownTableKnot =
        file.preset == KnotMath::Preset::KnotTable &&
        KnotMath::KnotTable::instance().indexOf(file.tableKnot) < 0;

    // a type (or table knot) this version does not know, or a changed
    // shape: the points
    if (!knownType || unknownTableKnot || file.shapeChanged)
    {
        if (file.points.size() < 3)
        {
            error = exportTr("The file contains no curve.");
            return false;
        }

        file.preset = KnotMath::Preset::CustomCoordinates;
        file.customText = pointsText(file.points);
    }

    if (file.preset == KnotMath::Preset::CustomCoordinates && file.customText.trimmed().isEmpty())
    {
        if (file.points.size() < 3)
        {
            error = exportTr("The file contains no curve.");
            return false;
        }

        file.customText = pointsText(file.points);
    }

    const QJsonObject appearance = root.value(QStringLiteral("appearance")).toObject();

    if (!appearance.isEmpty())
    {
        file.hasAppearance = true;
        file.thickness = static_cast<int>(std::lround(
            appearance.value(QStringLiteral("thickness")).toDouble(0.18) * 100.0));
        file.samples = appearance.value(QStringLiteral("samples")).toInt();
        file.material = appearance.value(QStringLiteral("material")).toInt();
        file.shine = static_cast<int>(std::lround(
            appearance.value(QStringLiteral("shine")).toDouble() * 100.0));
        file.colorMode = appearance.value(QStringLiteral("colorMode")).toInt();
        file.color = QColor(appearance.value(QStringLiteral("color")).toString());
    }

    return true;
}

/*
 * KNOT DIAGRAM PICTURES
 */

QPainterPath diagramArcPath(
    const KnotMath::KnotDiagram &diagram,
    const KnotMath::DiagramArcs &arcs,
    int arc,
    double gap,
    const QTransform &transform)
{
    QPainterPath path;

    const int n = diagram.points.size();
    const int c = arcs.underS.size();

    if (n < 3)
        return path;

    auto pointAt = [&](double s)
    {
        double w = std::fmod(s, static_cast<double>(n));

        if (w < 0.0)
            w += n;

        const int i = static_cast<int>(std::floor(w)) % n;
        const double f = w - std::floor(w);

        const KnotMath::Vec3 &a = diagram.points[i];
        const KnotMath::Vec3 &b = diagram.points[(i + 1) % n];

        return transform.map(QPointF(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f));
    };

    if (c == 0)
    {
        path.moveTo(pointAt(0.0));

        for (int i = 1; i < n; ++i)
            path.lineTo(pointAt(i));

        path.closeSubpath();

        return path;
    }

    // length along the curve at every point (n + 1 values)
    QVector<double> cum(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
    {
        const KnotMath::Vec3 &a = diagram.points[i];
        const KnotMath::Vec3 &b = diagram.points[(i + 1) % n];

        cum[i + 1] = cum[i] + std::hypot(b.x - a.x, b.y - a.y);
    }

    const double total = cum[n];

    auto lengthAt = [&](double s)
    {
        const double wraps = std::floor(s / n);

        s -= wraps * n;

        const int i = std::clamp(static_cast<int>(std::floor(s)), 0, n - 1);

        return cum[i] + (s - i) * (cum[i + 1] - cum[i]) + wraps * total;
    };

    auto sAtLength = [&](double length)
    {
        const double wraps = std::floor(length / total);

        length -= wraps * total;

        const int i = std::clamp(
            static_cast<int>(std::upper_bound(cum.begin(), cum.end(), length) - cum.begin()) - 1,
            0, n - 1);

        const double segment = cum[i + 1] - cum[i];

        return i + (segment > 1e-15 ? (length - cum[i]) / segment : 0.0) + wraps * n;
    };

    const double s0 = arcs.underS[arc];
    double s1 = arcs.underS[(arc + 1) % c];

    if (s1 <= s0)
        s1 += n;

    const double l0 = lengthAt(s0);
    const double l1 = lengthAt(s1);

    // two under-passes close together: smaller gaps
    const double g = std::min(gap, 0.3 * (l1 - l0));

    const double from = sAtLength(l0 + g);
    const double to = sAtLength(l1 - g);

    path.moveTo(pointAt(from));

    for (int i = static_cast<int>(std::floor(from)) + 1; i < to; ++i)
        path.lineTo(pointAt(i));

    path.lineTo(pointAt(to));

    return path;
}

void paintDiagram(
    QPainter &painter,
    const KnotMath::KnotDiagram &diagram,
    const QRectF &area,
    const QColor &line,
    double lineWidth,
    double gapLength)
{
    if (!diagram.isValid())
        return;

    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;

    for (const KnotMath::Vec3 &p : diagram.points)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    const double margin = lineWidth * 2.0;

    const double scale = std::min(
        (area.width() - 2.0 * margin) / std::max(maxX - minX, 1e-9),
        (area.height() - 2.0 * margin) / std::max(maxY - minY, 1e-9));

    // y points up in the diagram
    QTransform transform;

    transform.translate(area.center().x(), area.center().y());
    transform.scale(scale, -scale);
    transform.translate(-0.5 * (minX + maxX), -0.5 * (minY + maxY));

    const KnotMath::DiagramArcs arcs = KnotMath::diagramArcs(diagram);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(line, lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    for (int arc = 0; arc < arcs.arcCount; ++arc)
        painter.drawPath(diagramArcPath(diagram, arcs, arc, gapLength / scale, transform));

    painter.restore();
}

bool saveDiagram(
    const KnotMath::KnotDiagram &diagram,
    const QString &path,
    const QString &title,
    QString &error)
{
    if (QFileInfo(path).suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0)
    {
        constexpr int size = 800;

        QSvgGenerator generator;

        generator.setFileName(path);
        generator.setSize(QSize(size, size));
        generator.setViewBox(QRect(0, 0, size, size));
        generator.setTitle(title);
        generator.setDescription(exportTr("Knot diagram made with AKSIOMA Knots"));

        QPainter painter;

        if (!painter.begin(&generator))
        {
            error = exportTr("The file could not be written.");
            return false;
        }

        paintDiagram(painter, diagram, QRectF(0, 0, size, size), Qt::black, 6.0, 11.0);

        painter.end();

        if (!QFileInfo::exists(path))
        {
            error = exportTr("The file could not be written.");
            return false;
        }

        return true;
    }

    constexpr int size = 2000;

    QImage image(size, size, QImage::Format_RGB32);

    image.fill(Qt::white);

    {
        QPainter painter(&image);

        paintDiagram(painter, diagram, QRectF(0, 0, size, size), Qt::black, 14.0, 26.0);
    }

    if (!image.save(path))
    {
        error = exportTr("The file could not be written.");
        return false;
    }

    return true;
}

/*
 * COORDINATES AND REPORT
 */

QString coordinatesText(const KnotMath::CurveData &data, bool csv)
{
    QVector<QVector<KnotMath::Vec3>> components;

    if (data.multiStrand && !data.separateStrands.isEmpty())
        components = data.separateStrands;
    else
        components.push_back(data.points);

    const bool several = components.size() > 1;

    QString text;

    if (csv)
        text += several ? QStringLiteral("component,x,y,z\n") : QStringLiteral("x,y,z\n");

    for (int k = 0; k < components.size(); ++k)
    {
        if (several && !csv)
            text += QStringLiteral("%1# component %2\n").arg(k == 0 ? QString() : QStringLiteral("\n")).arg(k + 1);

        for (const KnotMath::Vec3 &p : components.at(k))
        {
            if (csv)
            {
                if (several)
                    text += QStringLiteral("%1,").arg(k + 1);

                text += QStringLiteral("%1,%2,%3\n")
                            .arg(p.x, 0, 'f', 6)
                            .arg(p.y, 0, 'f', 6)
                            .arg(p.z, 0, 'f', 6);
            }
            else
            {
                text += QStringLiteral("%1, %2, %3\n")
                            .arg(p.x, 0, 'f', 6)
                            .arg(p.y, 0, 'f', 6)
                            .arg(p.z, 0, 'f', 6);
            }
        }
    }

    return text;
}

QString reportText(const ReportData &data)
{
    const KnotMath::Invariants &inv = data.invariants;

    auto row = [](const QString &caption, const QString &value)
    {
        return QStringLiteral("%1: %2\n").arg(caption, value.isEmpty() ? QStringLiteral("—") : value);
    };

    QString text;

    text += exportTr("AKSIOMA: Knots - analysis report") + QLatin1Char('\n');
    text += QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) + QStringLiteral("\n\n");

    text += row(exportTr("Knot"), plain(data.name));

    if (data.shapeChanged)
        text += exportTr("(shape changed by relaxing or scrambling - still the same knot)") + QLatin1Char('\n');

    text += row(exportTr("Classification"), plain(inv.classification));

    text += QLatin1Char('\n');

    text += row(exportTr("Alexander polynomial"), plain(inv.alexanderPolynomial));
    text += row(exportTr("Jones polynomial"), plain(inv.jonesPolynomial));

    if (data.determinant >= 0)
    {
        text += row(exportTr("Determinant |Δ(−1)|"), QString::number(data.determinant));
        text += row(exportTr("Tricolourable"),
                    data.tricolourable ? exportTr("yes") : exportTr("no"));
    }

    if (inv.knownCrossingNumber >= 0)
        text += row(exportTr("Crossing number"), QString::number(inv.knownCrossingNumber));

    text += QLatin1Char('\n');

    text += row(exportTr("Writhe of the 3D curve"), QString::number(inv.writhe, 'f', 5));

    if (inv.crossingNumber >= 0)
        text += row(exportTr("Crossings in the XY projection"), QString::number(inv.crossingNumber));

    if (data.diagramCrossings >= 0)
        text += row(exportTr("Crossings in the cleanest projection"), QString::number(data.diagramCrossings));

    text += row(exportTr("Points of the curve"), QString::number(data.pointCount));

    if (!inv.methodology.isEmpty())
        text += QLatin1Char('\n') + row(exportTr("Method"), plain(inv.methodology));

    return text;
}

QString safeFileName(const QString &name)
{
    QString result = plain(name);

    static const QRegularExpression forbidden(QStringLiteral("[\\\\/:*?\"<>|\\r\\n\\t]"));

    result.replace(forbidden, QStringLiteral(" "));
    result = result.simplified();

    if (result.isEmpty())
        result = QStringLiteral("knot");

    return result;
}

} // namespace KnotExport
