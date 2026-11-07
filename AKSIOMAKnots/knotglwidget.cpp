#include "knotglwidget.h"

#include "theme.h"

#include <aksioma/shaderlibrary.h>

#include <utility>

KnotGLWidget::KnotGLWidget(QWidget *parent)
    : Aksioma::View(parent),
      train_(renderer()),
      overlay_(renderer(), train_)
{
    // the toy train, then the overlays over everything
    renderer().addLayer(&train_);
    renderer().addLayer(&overlay_);

    train_.cinemaChanged = [this](bool on)
    {
        emit cinemaChanged(on);
    };

    Aksioma::Renderer *engine = &renderer();

    connect(engine, &Aksioma::Renderer::cameraChanged, this, &KnotGLWidget::cameraChanged);
    connect(engine, &Aksioma::Renderer::curveGrabbed, this, &KnotGLWidget::ropeGrabbed);
    connect(engine, &Aksioma::Renderer::curveDragged, this, &KnotGLWidget::ropeDragged);
    connect(engine, &Aksioma::Renderer::curveReleased, this, &KnotGLWidget::ropeReleased);
    connect(engine, &Aksioma::Renderer::photoLevelChanged, this, &KnotGLWidget::photoLevelChanged);

    // AKSIOMA_KNOTS_SHADER_DIR=<this app's shaders folder>: the toy train's
    // shaders read from disk, with live reload (AKSIOMA_SHADER_DIR does the
    // same for the engine's)
    static const bool knotShadersFromDisk = []
    {
        const QString folder = qEnvironmentVariable("AKSIOMA_KNOTS_SHADER_DIR");

        if (!folder.isEmpty())
            Aksioma::ShaderLibrary::instance().overrideRoot(QStringLiteral(":/knots/shaders"), folder);

        return !folder.isEmpty();
    }();

    Q_UNUSED(knotShadersFromDisk)

    // the wooden track of the toy train is this app's material (7)
    Aksioma::ShaderLibrary::instance().mapInclude(QStringLiteral("studio/app_materials.glsl"),
                                                  QStringLiteral(":/knots/shaders/track_material.glsl"));
}

KnotGLWidget::~KnotGLWidget()
{
    // the layers let go of the graphics card before they go
    releaseGL();

    renderer().removeLayer(&overlay_);
    renderer().removeLayer(&train_);
}

void KnotGLWidget::paintGL()
{
    renderer().setDarkTheme(Theme::isDark());

    Aksioma::View::paintGL();
}

bool KnotGLWidget::setPhotoMode(bool enabled, Photo::Scene scene)
{
    const Photo::SceneInfo info = Photo::sceneInfo(scene);

    train_.setSceneHasTrain(info.train);

    return renderer().setPhotoMode(enabled, Photo::stageOf(info));
}

/*
 * The engine's renderer
 */

void KnotGLWidget::setMesh(const KnotMath::Mesh &mesh)
{
    renderer().setMesh(mesh);
}

void KnotGLWidget::setFixedBounds(const QVector3D &minimum, const QVector3D &maximum)
{
    renderer().setFixedBounds(minimum, maximum);
}

void KnotGLWidget::clearFixedBounds()
{
    renderer().clearFixedBounds();
}

void KnotGLWidget::setRopeTinted(bool tinted)
{
    renderer().setRestingTinted(tinted);
}

void KnotGLWidget::setSoapFilm(bool on)
{
    renderer().setSoapFilm(on);
}

void KnotGLWidget::setClothSheet(bool on)
{
    renderer().setClothSheet(on);
}

void KnotGLWidget::setPhotoAperture(float fNumber)
{
    renderer().setPhotoAperture(fNumber);
}

void KnotGLWidget::setPhotoStage(bool on)
{
    renderer().setPhotoStage(on);
}

void KnotGLWidget::flyWithSunBehind(const QVector3D &centre, float radius, float aside, float pitch)
{
    renderer().flyWithSunBehind(centre, radius, aside, pitch);
}

void KnotGLWidget::setMeshColor(const QColor &color)
{
    renderer().setMeshColor(color);
}

void KnotGLWidget::setMaterialProperties(float metallic)
{
    renderer().setMaterialProperties(metallic);
}

void KnotGLWidget::setLighting(const Lighting::Options &options)
{
    renderer().setLighting(options);
}

const Lighting::Options &KnotGLWidget::lighting() const
{
    return renderer().lighting();
}

KnotGLWidget::MemoryUse KnotGLWidget::memoryUse() const
{
    return renderer().memoryUse();
}

void KnotGLWidget::setSurface(int material, float roughness)
{
    renderer().setSurface(material, roughness);
}

bool KnotGLWidget::graphicsReady() const
{
    return renderer().graphicsReady();
}

bool KnotGLWidget::photoMode() const
{
    return renderer().photoMode();
}

QRectF KnotGLWidget::photoTableArea() const
{
    return renderer().photoTableArea();
}

void KnotGLWidget::setRestingRope(KnotMath::Mesh mesh, const QMatrix4x4 &pose, bool sameTable)
{
    renderer().setRestingMesh(std::move(mesh), pose, sameTable);
}

void KnotGLWidget::clearRestingPose()
{
    renderer().clearRestingPose();
}

void KnotGLWidget::setAutoRotation(bool enabled)
{
    renderer().setAutoRotation(enabled);
}

void KnotGLWidget::setReduceMotion(bool reduced)
{
    Aksioma::Renderer::setReduceMotion(reduced);
}

void KnotGLWidget::setShowFrameRate(bool shown)
{
    Aksioma::Renderer::setShowFrameRate(shown);
}

double KnotGLWidget::frameRate() const
{
    return renderer().frameRate();
}

QString KnotGLWidget::graphicsInfo() const
{
    return renderer().graphicsInfo();
}

bool KnotGLWidget::photoPictureFinished() const
{
    return renderer().photoPictureFinished();
}

void KnotGLWidget::setRopeGrab(bool enabled)
{
    renderer().setCurveGrab(enabled);
}

void KnotGLWidget::setGrabCurve(const QVector<KnotMath::Vec3> &curve, double radius, bool closed)
{
    renderer().setGrabCurve(curve, radius, closed);
}

void KnotGLWidget::setRopeHandLevel(float level)
{
    renderer().setGrabLevel(level);
}

bool KnotGLWidget::ropeGrabUnder() const
{
    return renderer().grabbedWithShift();
}

void KnotGLWidget::cancelRopeGrab()
{
    renderer().cancelCurveGrab();
}

bool KnotGLWidget::autoRotation() const
{
    return renderer().autoRotation();
}

void KnotGLWidget::resetCamera(float highestPitch, bool animate)
{
    renderer().resetCamera(highestPitch, animate);
}

void KnotGLWidget::fitNewKnot(float highestPitch)
{
    renderer().fitNewMesh(highestPitch);
}

void KnotGLWidget::flyToSphere(const QVector3D &centre, float radius, float yaw, float pitch)
{
    renderer().flyToSphere(centre, radius, yaw, pitch);
}

void KnotGLWidget::flyToMesh(float yaw, float pitch)
{
    renderer().flyToMesh(yaw, pitch);
}

void KnotGLWidget::setPhotoAutoLevel(int level)
{
    renderer().setPhotoAutoLevel(level);
}

int KnotGLWidget::photoLevelInUse() const
{
    return renderer().photoLevelInUse();
}

void KnotGLWidget::placeCamera(const QVector3D &centre, float radius, float yaw, float pitch)
{
    renderer().placeCamera(centre, radius, yaw, pitch);
}

/*
 * The toy train and the knot's overlays (trainlayer.h, knotoverlay.h)
 */

void KnotGLWidget::setTrainPath(const QVector<KnotMath::Vec3> &path, double length)
{
    train_.setTrainPath(path, length);
}

void KnotGLWidget::setTrainJourney(const QVector<Photo::TrainTrack::Pass> &journey)
{
    train_.setTrainJourney(journey);
    overlay_.resetJourney();

    update();
}

void KnotGLWidget::trainSoundScene(double *speed, float *gain, float *pan) const
{
    train_.trainSoundScene(speed, gain, pan);
}

void KnotGLWidget::setCinema(bool on)
{
    train_.setCinema(on);
}

void KnotGLWidget::playCinema(float seconds)
{
    train_.playCinema(seconds);
}

bool KnotGLWidget::cinema() const
{
    return train_.cinema();
}

void KnotGLWidget::setTrainRunning(bool running)
{
    train_.setTrainRunning(running);
}

bool KnotGLWidget::trainRunning() const
{
    return train_.trainRunning();
}

void KnotGLWidget::setTrainAlong(double millimetres)
{
    train_.setTrainAlong(millimetres);
}

void KnotGLWidget::runSteam(float seconds)
{
    train_.runSteam(seconds);
}

double KnotGLWidget::trainAlong() const
{
    return train_.trainAlong();
}

void KnotGLWidget::setCrossings(const QVector<KnotMath::CrossingInfo> &crossings)
{
    overlay_.setCrossings(crossings);
}

void KnotGLWidget::setShowCrossingNumbers(bool enabled)
{
    overlay_.setShowCrossingNumbers(enabled);
}

void KnotGLWidget::setFlashes(const QVector<KnotMath::Vec3> &positions, const QVector<float> &progress,
                              const QVector<int> &numbers)
{
    overlay_.setFlashes(positions, progress, numbers);
}

void KnotGLWidget::clearFlashes()
{
    overlay_.clearFlashes();
}

void KnotGLWidget::setDiagramInset(const DiagramInset &inset)
{
    overlay_.setDiagramInset(inset);
}

void KnotGLWidget::clearDiagramInset()
{
    overlay_.clearDiagramInset();
}

void KnotGLWidget::setCaption(const QString &title, const QString &body)
{
    overlay_.setCaption(title, body);
}

void KnotGLWidget::paintDiagram(QPainter &painter, const QRectF &area, const DiagramInset &inset, const QColor &background)
{
    KnotOverlay::paintDiagram(painter, area, inset, background);
}
