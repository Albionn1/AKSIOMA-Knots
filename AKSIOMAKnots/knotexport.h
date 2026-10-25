#ifndef KNOTEXPORT_H
#define KNOTEXPORT_H

#include "knotpractice.h"

#include <QColor>
#include <QPainterPath>
#include <QString>
#include <QTransform>
#include <QVector>

class QPainter;
class QRectF;

/*
 * Saving and exporting: knot files, pictures of the knot diagram,
 * coordinate lists and the analysis report.
 */
namespace KnotExport
{

/*
 * =============================================================
 * KNOT FILES (.aknot)
 * =============================================================
 *
 * JSON. The knot is stored the way it was chosen (preset, torus knot,
 * table knot, custom formula or coordinates) and also as the list of
 * points of the curve as it is shown, so that relaxed or scrambled shapes
 * can be opened again and other programs can read the curve:
 *
 *   {
 *     "format": "AKSIOMA Knots", "version": 1,
 *     "name": "Trefoil Knot",
 *     "knot": { "type": "trefoil", "p": 3, "q": 2, "table": "", "custom": "" },
 *     "shapeChanged": false,
 *     "points": [[x, y, z], ...],
 *     "appearance": { "thickness": 0.18, "samples": 420, "material": 0,
 *                     "shine": 0.35, "colorMode": 0, "color": "#d99b42" }
 *   }
 */
struct KnotFile
{
    QString name;

    KnotMath::Preset preset = KnotMath::Preset::Unknot;
    int torusP = 3;
    int torusQ = 2;
    // KnotInfo name for Preset::KnotTable
    QString tableKnot;
    // formula or coordinate text for Preset::CustomCoordinates
    QString customText;

    // The curve as shown (multi-strand structures: the first component).
    QVector<KnotMath::Vec3> points;

    // The shape was relaxed or scrambled: open it from the points.
    bool shapeChanged = false;

    // false: the file has no appearance (keep the current one)
    bool hasAppearance = false;
    // slider values of the main window
    int thickness = 0;
    int samples = 0;
    int material = 0;
    int shine = 0;
    int colorMode = 0;
    QColor color;
};

bool writeKnotFile(
    const QString &path,
    const KnotFile &file,
    QString &error);

// Reads a .aknot file, or a text file with one "x, y, z" point per line
// (.txt, .csv; header and # comment lines are skipped) or a formula
// ("x=...; y=...; z=...").
bool readKnotFile(
    const QString &path,
    KnotFile &file,
    QString &error);

/*
 * =============================================================
 * KNOT DIAGRAM PICTURES
 * =============================================================
 */

// The visible part of arc `arc` of the diagram, from the end of one
// under-pass gap to the start of the next, mapped by `transform`. `gap` is
// half the length of a gap, in diagram units.
QPainterPath diagramArcPath(
    const KnotMath::KnotDiagram &diagram,
    const KnotMath::DiagramArcs &arcs,
    int arc,
    double gap,
    const QTransform &transform);

// Draws the diagram fitted into `area` (lines `lineWidth` wide, gaps of
// `gapLength` on each side of an under-pass, both in painter units).
void paintDiagram(
    QPainter &painter,
    const KnotMath::KnotDiagram &diagram,
    const QRectF &area,
    const QColor &line,
    double lineWidth,
    double gapLength);

// Black diagram on white: .svg (vector) or a 2000 x 2000 pixel image in
// any other format Qt can write (.png, .jpg).
bool saveDiagram(
    const KnotMath::KnotDiagram &diagram,
    const QString &path,
    const QString &title,
    QString &error);

/*
 * =============================================================
 * COORDINATES AND REPORT
 * =============================================================
 */

// One point per line. Text: "x, y, z" (the format of the Custom curve
// input; components of a multi-strand structure are separated by
// "# component N" lines). CSV: a header, and a component column for
// multi-strand structures.
QString coordinatesText(
    const KnotMath::CurveData &data,
    bool csv);

struct ReportData
{
    QString name;
    KnotMath::Invariants invariants;
    int pointCount = 0;
    bool shapeChanged = false;
    // from the cleanest projection; -1 if not available
    int diagramCrossings = -1;
    long long determinant = -1;
    bool tricolourable = false;
};

// Plain text (UTF-8) summary of the analysis panel.
QString reportText(
    const ReportData &data);

// "Trefoil Knot" -> "Trefoil Knot" with characters that are not allowed in
// file names removed.
QString safeFileName(
    const QString &name);

} // namespace KnotExport

#endif // KNOTEXPORT_H
