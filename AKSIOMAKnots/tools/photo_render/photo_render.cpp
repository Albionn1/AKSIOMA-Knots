// Renders Photo mode without showing a window (the view is drawn off
// screen and saved as a picture): a knot from the table is laid on the
// table by the rope physics and photographed in each scene.
//
//   photo_render <knot name> <out folder> [width height [scene]]
//
// PHOTO_CAM="yaw pitch radius along": the camera at yaw, pitch degrees on a
// ball of `radius` rope radii around the point `along` (0..1) of the rope.

#include "knotglwidget.h"
#include "knotmath.h"
#include "knottable.h"
#include "ropephysics.h"
#include "sneaker.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>

#include <cmath>
#include <cstdio>

using namespace KnotMath;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // photo_render envfile <file.hdr> ...: the sun and the room of pictures
    if (argc >= 3 && QString(argv[1]) == QStringLiteral("envfile"))
    {
        for (int k = 2; k < argc; ++k)
        {
            const Photo::Environment e = Photo::loadEnvironment(QString::fromLocal8Bit(argv[k]));

            const QVector3D room = e.irradianceAt(QVector3D(0.0f, 1.0f, 0.0f));
            const float ground = 0.2126f * room.x() + 0.7152f * room.y() + 0.0722f * room.z();
            const float sun = (0.2126f * e.sunIrradiance.x() + 0.7152f * e.sunIrradiance.y() +
                               0.0722f * e.sunIrradiance.z()) * std::max(e.sunDirection.y(), 0.0f);

            std::printf("%-40s valid %d  sun up %5.1f deg  size %.2f deg  sun on table / room %.2f  (room %.2f)\n",
                        argv[k], e.valid,
                        std::asin(std::clamp(e.sunDirection.y(), -1.0f, 1.0f)) * 57.2958f,
                        e.sunRadius * 57.2958f,
                        sun / std::max(ground, 1.0e-4f), ground);
        }

        return 0;
    }

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

    /*
     * photo_render bench <knot> <scene> [width height]: frames of the rope
     * lying still, then frames of the rope held and dragged across the
     * table as with the mouse (physics, mesh and frame timed apart). Run
     * with AKSIOMA_PROFILE=1 for the stages of the frame.
     */
    if (argc >= 4 && QString(argv[1]) == QStringLiteral("bench"))
    {
        const KnotTable &knots = KnotTable::instance();
        const int index = knots.indexOf(QString(argv[2]));
        const auto scene = static_cast<Photo::Scene>(QString(argv[3]).toInt());
        const int width = argc >= 6 ? QString(argv[4]).toInt() : 1600;
        const int height = argc >= 6 ? QString(argv[5]).toInt() : 1000;

        if (index < 0)
            return 1;

        const QVector<Vec3> curve = knots.curve(index);

        CurveData data;
        data.points = curve;

        const double radius = safeTubeRadius(data, 0.16);
        const Photo::RopeMaterial material = Photo::sceneInfo(scene).rope;
        const Photo::RestingRope rope = Photo::layOnTable(curve, true, radius, material);

        KnotGLWidget view;

        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(width, height);
        view.show();

        if (!view.setPhotoMode(true, scene))
            return 1;

        view.setRestingRope(Photo::buildRopeMesh(rope.points, true, radius, material), rope.pose);
        view.fitNewKnot(45.0f);
        view.resetCamera(45.0f, false);

        view.grabFramebuffer();

        QElapsedTimer clock;

        clock.start();

        for (int frame = 0; frame < 120; ++frame)
            view.grabFramebuffer();

        std::printf("still: %.2f ms a frame (with reading it back)\n", clock.nsecsElapsed() / 1.0e6 / 120.0);

        // the rope in the hand, carried across the table and back
        QVector<Vec3> points;

        for (const Vec3 &p : rope.points)
        {
            const QVector3D q = rope.pose.map(QVector3D(float(p.x), float(p.y), float(p.z)));

            points.push_back(Vec3(q.x(), q.y(), q.z()) / radius);
        }

        Photo::RopeSimulation hand(points, {}, true, material);

        const Vec3 start = points.first();

        hand.hold(0, Vec3(start.x, 6.5, start.z), false);

        const QMatrix4x4 back = rope.pose.inverted();
        const int steps = std::max(1, static_cast<int>(std::lround(1.0 / 60.0 / hand.secondsPerStep())));

        double physics = 0.0;
        double meshing = 0.0;
        double handing = 0.0;
        double drawing = 0.0;

        for (int frame = 0; frame < 120; ++frame)
        {
            const double t = frame / 119.0;

            hand.moveHand(Vec3(start.x + 30.0 * std::sin(3.14159 * t), 6.5, start.z + 10.0 * t));

            clock.restart();

            for (int k = 0; k < steps; ++k)
                hand.step();

            physics += clock.nsecsElapsed() / 1.0e6;
            clock.restart();

            QVector<Vec3> knot;

            for (const Vec3 &p : hand.points())
            {
                const QVector3D q = back.map(QVector3D(float(p.x * radius), float(p.y * radius), float(p.z * radius)));

                knot.push_back(Vec3(q.x(), q.y(), q.z()));
            }

            Mesh moving = Photo::buildRopeMesh(knot, true, radius, material, true);

            meshing += clock.nsecsElapsed() / 1.0e6;
            clock.restart();

            view.setRestingRope(std::move(moving), rope.pose, true);

            handing += clock.nsecsElapsed() / 1.0e6;
            clock.restart();

            view.grabFramebuffer();

            drawing += clock.nsecsElapsed() / 1.0e6;
        }

        std::printf("moving: physics %.2f ms (%d steps), mesh %.2f ms, to the view %.2f ms, frame %.2f ms\n",
                    physics / 120.0, steps, meshing / 120.0, handing / 120.0, drawing / 120.0);

        view.grabFramebuffer().save(QStringLiteral("bench_last.png"));

        return 0;
    }

    const QString name = argc >= 2 ? QString(argv[1]) : QStringLiteral("3_1");
    const QString folder = argc >= 3 ? QString(argv[2]) : QStringLiteral(".");
    const int width = argc >= 5 ? QString(argv[3]).toInt() : 1600;
    const int height = argc >= 5 ? QString(argv[4]).toInt() : 1000;
    const int only = argc >= 6 ? QString(argv[5]).toInt() : -1;

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
        if (only >= 0 && k != only)
            continue;

        const auto scene = static_cast<Photo::Scene>(k);
        const Photo::RopeMaterial material = Photo::sceneInfo(scene).rope;

        QElapsedTimer clock;
        clock.start();

        // tied in the sneaker's lace, or dropped on the table
        const Photo::RestingRope rope =
            Photo::sceneInfo(scene).sneaker
                ? Photo::tieOnSneaker(curve, radius, material).rope
                : Photo::layOnTable(curve, true, radius, material);
        const qint64 physics = clock.restart();

        const Mesh mesh = Photo::buildRopeMesh(rope.points, true, radius, material);
        const qint64 building = clock.restart();

        // as while the rope is in the hand (built for every frame)
        Photo::buildRopeMesh(rope.points, true, radius, material, true);
        const qint64 quick = clock.restart();

        if (!view.setPhotoMode(true, scene))
        {
            std::printf("the room could not be loaded\n");
            return 1;
        }

        view.setRestingRope(mesh, rope.pose);
        view.fitNewKnot(45.0f);
        view.resetCamera(45.0f, false);

        if (qEnvironmentVariableIsSet("PHOTO_CAM"))
        {
            const QStringList place = qEnvironmentVariable("PHOTO_CAM").split(' ');

            const int at = std::clamp(static_cast<int>(place.value(3).toDouble() * rope.points.size()),
                                      0, static_cast<int>(rope.points.size()) - 1);

            const Vec3 p = rope.points.at(at);

            view.placeCamera(rope.pose.map(QVector3D(float(p.x), float(p.y), float(p.z))),
                             place.value(2).toFloat() * float(radius),
                             place.value(0).toFloat(),
                             place.value(1).toFloat());
        }

        const qint64 loading = clock.restart();

        // the frames add up to the clean picture
        QImage picture = view.grabFramebuffer();

        for (int frame = 0; frame < 70; ++frame)
            picture = view.grabFramebuffer();

        const qint64 drawing = clock.restart();

        const QString file = QDir(folder).filePath(QStringLiteral("%1_%2.png").arg(name).arg(k));

        picture.save(file);

        std::printf(
            "%s: rope %d points, rest %s in %.2f s (%lld ms), mesh %d vertices (%lld ms, quick %lld ms), "
            "room %lld ms, two frames %lld ms\n",
            qPrintable(file),
            static_cast<int>(rope.points.size()),
            rope.settled ? "yes" : "no",
            rope.seconds,
            physics,
            static_cast<int>(mesh.positions.size() / 3),
            building,
            quick,
            loading,
            drawing);

        std::fflush(stdout);
    }

    return 0;
}
