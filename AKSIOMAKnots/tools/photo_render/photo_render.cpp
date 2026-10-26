// Renders Photo mode without showing a window (the view is drawn off
// screen and saved as a picture): a knot from the table is laid on the
// table by the rope physics and photographed in each scene.
//
//   photo_render <knot name> <out folder> [width height]

#include "knotglwidget.h"
#include "knotmath.h"
#include "knottable.h"
#include "ropephysics.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>

#include <cstdio>

using namespace KnotMath;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    if (argc >= 2 && QString(argv[1]) == QStringLiteral("env"))
    {
        for (int k = 0; k < static_cast<int>(Photo::Scene::Count); ++k)
        {
            const Photo::Environment e =
                Photo::loadEnvironment(Photo::sceneInfo(static_cast<Photo::Scene>(k)).environment);

            const QVector3D up = e.irradianceAt(QVector3D(0.0f, 1.0f, 0.0f));

            std::printf("scene %d valid %d %dx%d sun (%.3f %.3f %.3f) E (%.2f %.2f %.2f) radius %.2f deg; "
                        "room on table (%.3f %.3f %.3f) %s\n",
                        k, e.valid, e.width, e.height,
                        e.sunDirection.x(), e.sunDirection.y(), e.sunDirection.z(),
                        e.sunIrradiance.x(), e.sunIrradiance.y(), e.sunIrradiance.z(),
                        e.sunRadius * 57.2958, up.x(), up.y(), up.z(), qPrintable(e.error));
        }

        return 0;
    }

    const QString name = argc >= 2 ? QString(argv[1]) : QStringLiteral("3_1");
    const QString folder = argc >= 3 ? QString(argv[2]) : QStringLiteral(".");
    const int width = argc >= 5 ? QString(argv[3]).toInt() : 1600;
    const int height = argc >= 5 ? QString(argv[4]).toInt() : 1000;

    const KnotTable &table = KnotTable::instance();
    const int index = table.indexOf(name);

    if (index < 0)
    {
        std::printf("unknown knot %s\n", qPrintable(name));
        return 1;
    }

    const QVector<Vec3> curve = table.curve(index);

    CurveData data;
    data.points = curve;

    const double radius = safeTubeRadius(data, 0.16);

    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message)
                           {
                               std::fprintf(stderr, "%s\n", qPrintable(message));
                           });

    KnotGLWidget view;

    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(width, height);
    view.show();

    for (int k = 0; k < static_cast<int>(Photo::Scene::Count); ++k)
    {
        const auto scene = static_cast<Photo::Scene>(k);
        const Photo::RopeMaterial material = Photo::sceneInfo(scene).rope;

        QElapsedTimer clock;
        clock.start();

        const Photo::RestingRope rope = Photo::layOnTable(curve, true, radius, material);
        const qint64 physics = clock.restart();

        const Mesh mesh = Photo::buildRopeMesh(rope.points, true, radius, material);
        const qint64 building = clock.restart();

        if (!view.setPhotoMode(true, scene))
        {
            std::printf("the room could not be loaded\n");
            return 1;
        }

        view.setRestingRope(mesh, rope.pose);
        view.fitNewKnot(45.0f);
        view.resetCamera(45.0f, false);

        const qint64 loading = clock.restart();

        QImage picture = view.grabFramebuffer();
        picture = view.grabFramebuffer();

        const qint64 drawing = clock.restart();

        const QString file = QDir(folder).filePath(QStringLiteral("%1_%2.png").arg(name).arg(k));

        picture.save(file);

        std::printf(
            "%s: rope %d points, rest %s in %.2f s (%lld ms), mesh %d vertices (%lld ms), "
            "room %lld ms, two frames %lld ms\n",
            qPrintable(file),
            static_cast<int>(rope.points.size()),
            rope.settled ? "yes" : "no",
            rope.seconds,
            physics,
            static_cast<int>(mesh.positions.size() / 3),
            building,
            loading,
            drawing);

        std::fflush(stdout);
    }

    return 0;
}
