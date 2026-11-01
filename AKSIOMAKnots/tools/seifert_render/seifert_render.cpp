// Renders the Seifert surface of a table knot without showing a window, in
// the plain 3D view and in Photo mode, and saves the pictures.
//
//   seifert_render <knot> <out folder> [width height [step [scene]]]
//
// step: 0.. the build-up steps, -1 (default) the finished surface; scene: -1
// (default) every Photo scene, -2 none.
// SEIFERT_CAM="yaw pitch distance": the camera, instead of the usual view.

#include "knotglwidget.h"
#include "knotmath.h"
#include "knotseifert.h"
#include "knottable.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>

#include <cstdio>

using namespace KnotMath;

namespace
{

const QColor Front(217, 155, 66);
const QColor Back(64, 118, 190);
const QColor Knot(186, 185, 180);

void place(KnotGLWidget &view)
{
    if (!qEnvironmentVariableIsSet("SEIFERT_CAM"))
        return;

    const QStringList p = qEnvironmentVariable("SEIFERT_CAM").split(' ');

    view.placeCamera(QVector3D(0.0f, p.value(3).toFloat(), 0.0f), p.value(2).toFloat(), p.value(0).toFloat(), p.value(1).toFloat());
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const QString name = argc >= 2 ? QString(argv[1]) : QStringLiteral("3_1");
    const QString folder = argc >= 3 ? QString(argv[2]) : QStringLiteral(".");
    const int width = argc >= 5 ? QString(argv[3]).toInt() : 1600;
    const int height = argc >= 5 ? QString(argv[4]).toInt() : 1000;
    const int step = argc >= 6 ? QString(argv[5]).toInt() : -1;
    const int only = argc >= 7 ? QString(argv[6]).toInt() : -1;

    const KnotTable &table = KnotTable::instance();
    const int index = table.indexOf(name);

    if (index < 0)
    {
        std::printf("unknown knot %s\n", qPrintable(name));
        return 1;
    }

    SeifertSurface surface = buildSeifertSurface(table.curve(index), true);

    if (!surface.valid)
    {
        std::printf("no surface: %s\n", qPrintable(surface.error));
        return 1;
    }

    surface.sheet = buildSeifertSheet(surface);

    const int shown = step >= 0 ? step : 3;
    const double radius = seifertTubeRadius(surface, 0.16);

    const Mesh mesh = seifertMesh(surface, shown, radius, Knot, Front, Back, SeifertHighlight(), step < 0 ? 1.0 : 0.0);

    std::printf("%s: %d crossings, %d circles, genus %d, %d vertices, sheet %d: %d points, boundary %d, area %.2f -> %.2f\n",
                qPrintable(name), surface.crossings, surface.circles, surface.genus,
                static_cast<int>(mesh.positions.size() / 3),
                surface.sheet.valid, static_cast<int>(surface.sheet.film.size()),
                static_cast<int>(surface.sheet.boundary.size()),
                surface.sheet.areaBefore, surface.sheet.areaAfter);

    // Photo mode: a brass wire
    const Mesh wireMesh = seifertMesh(surface, shown, radius, QColor(214, 170, 96), Front, Back, SeifertHighlight(), step < 0 ? 1.0 : 0.0);

    KnotGLWidget view;

    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(width, height);
    view.show();

    view.setSurface(0, 0.3f);
    view.setMaterialProperties(0.0f);
    view.setMesh(mesh);
    view.fitNewKnot(45.0f);
    view.resetCamera(45.0f, false);
    place(view);

    view.grabFramebuffer();

    view.grabFramebuffer().save(QDir(folder).filePath(QStringLiteral("%1_view.png").arg(name)));

    if (only <= -2)
        return 0;

    for (int k = 0; k < static_cast<int>(Photo::Scene::Count); ++k)
    {
        if (only >= 0 && k != only)
            continue;

        const auto scene = static_cast<Photo::Scene>(k);

        if (Photo::sceneInfo(scene).sneaker)
            continue;

        if (!view.setPhotoMode(true, scene))
            return 1;

        view.setSoapFilm(qEnvironmentVariableIntValue("SEIFERT_SOAP") != 0);
        view.setClothSheet(qEnvironmentVariableIntValue("SEIFERT_SOAP") == 0);
        view.setMesh(wireMesh);
        view.fitNewKnot(45.0f);
        view.resetCamera(45.0f, false);
        place(view);

        if (qEnvironmentVariableIntValue("SEIFERT_STAGE") != 0)
        {
            view.setPhotoStage(true);
            view.setPhotoAperture(qEnvironmentVariable("SEIFERT_F", "4").toFloat());

            // (they fade in)
            QElapsedTimer clock;

            clock.start();

            while (clock.elapsed() < 2500)
            {
                view.grabFramebuffer();
                QCoreApplication::processEvents();
            }
        }

        QImage picture = view.grabFramebuffer();

        for (int frame = 0; frame < 70; ++frame)
            picture = view.grabFramebuffer();

        picture.save(QDir(folder).filePath(QStringLiteral("%1_photo%2.png").arg(name).arg(k)));
    }

    return 0;
}
