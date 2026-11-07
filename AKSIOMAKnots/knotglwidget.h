#ifndef KNOTGLWIDGET_H
#define KNOTGLWIDGET_H

#include "knotmath.h"
#include "knotoverlay.h"
#include "lighting.h"
#include "photoscene.h"
#include "trainlayer.h"
#include "traintrack.h"

#include <aksioma/view.h>

#include <QColor>
#include <QMatrix4x4>
#include <QRectF>
#include <QVector3D>
#include <QVector>

class QPainter;

/*
 * The 3D view of the knot app: AKSIOMA Engine's view (Aksioma::View, its
 * renderer draws the knot) with the app's own layers on it: the toy train of
 * Photo mode (trainlayer.h) and the knot's 2D overlays (knotoverlay.h).
 */
class KnotGLWidget final : public Aksioma::View
{
    Q_OBJECT

public:
    explicit KnotGLWidget(QWidget *parent = nullptr);

    ~KnotGLWidget() override;

    // A mesh may contain several tubes (multi-strand structures); when it
    // carries per-vertex colours they are used, otherwise the base colour.
    void setMesh(const KnotMath::Mesh &mesh);

    void setCrossings(const QVector<KnotMath::CrossingInfo> &crossings);

    void setShowCrossingNumbers(bool enabled);

    // Unknotting animation support: keep the ground/camera framing fixed
    // while the mesh changes every frame, and draw expanding flash rings at
    // the places where strands pass through each other.
    void setFixedBounds(const QVector3D &minimum, const QVector3D &maximum);
    void clearFixedBounds();
    void setFlashes(
        const QVector<KnotMath::Vec3> &positions,
        const QVector<float> &progress,
        const QVector<int> &numbers);
    void clearFlashes();

    // 2D knot diagram shown in a corner during the unknotting animation
    // (knotoverlay.h)
    using DiagramInset = KnotOverlay::DiagramInset;

    void setDiagramInset(const DiagramInset &inset);
    void clearDiagramInset();

    // A caption over the picture, at its foot: a title and a line or two
    // of explanation (rich text) that fade in when they change; an empty
    // title takes it away. Photo mode's unknotting tells with it what the
    // rope is doing and why.
    void setCaption(const QString &title, const QString &body);

    // Photo mode: the rope at rest keeps the scene's colour unless it is
    // tinted (its vertex colours: the strands of a crossing change)
    void setRopeTinted(bool tinted);

    // Photo mode: the sheet of the mesh is a soap film (see the film pass of
    // the mesh shader) and the tube the wire it hangs on
    void setSoapFilm(bool on);

    // Photo mode: the sheet of the mesh is cloth, dyed a colour on each side,
    // stretched on the tube as its wire
    void setClothSheet(bool on);

    // Photo mode: the camera's f-number (8: the room fairly sharp; 2.8:
    // only what is in focus sharp, the room behind blurred away)
    void setPhotoAperture(float fNumber);

    // Photo mode: the knot on a stage (fades in and out): the room behind
    // it sinks into a dark, colourless blur and a spotlight falls on it;
    // off, the calm stage of plain Photo mode (darker, colours kept)
    void setPhotoStage(bool on);

    // Photo mode: a camera flight to the ball (`centre` in the knot's
    // coordinates, `radius`) with the sun behind the camera, turned
    // `aside` degrees from it (light from the side shows the rope's
    // shape), from `pitch` degrees above the table.
    void flyWithSunBehind(const QVector3D &centre, float radius, float aside, float pitch);

    // Draws the diagram of `inset` (no panel, no caption) fitted into
    // `area`; `background` is the colour behind it (the gaps where a
    // strand runs below another are painted with it).
    static void paintDiagram(
        QPainter &painter,
        const QRectF &area,
        const DiagramInset &inset,
        const QColor &background);

    void setMeshColor(const QColor &color);

    void setMaterialProperties(float metallic);

    // Lights and shadows (Settings, Lighting).
    void setLighting(const Lighting::Options &options);

    const Lighting::Options &lighting() const;

    // Video memory this view has asked for, in bytes (an estimate: the
    // driver may round sizes up or keep extra copies).
    using MemoryUse = Aksioma::Renderer::MemoryUse;

    MemoryUse memoryUse() const;

    // Surface look: 0 glossy, 1 satin, 2 metal, 3 rope (fibre pattern),
    // 4 braided lace, roughness 0 (mirror) .. 1 (matte).
    void setSurface(int material, float roughness);

    // Photo mode (photoscene.h): the knot as a real rope lying on a real
    // surface in a photographed room, at the highest quality. The knot is
    // laid down flat (turned so that its thinnest direction points up);
    // positions given to and from the view (crossings, flashes, bounds,
    // the rope) stay in the knot's own coordinates. Call resetCamera
    // afterwards to frame it. False if the room could not be loaded.
    // the view has started and can draw (false: no OpenGL 3.3 here)
    bool graphicsReady() const;

    bool setPhotoMode(bool enabled, Photo::Scene scene);

    bool photoMode() const;

    // Photo mode: the rope at rest on the table (ropephysics.h), as built
    // by Photo::buildRopeMesh, and the turn that took the knot onto the
    // table (the table top is y = 0 after it). The knot keeps this pose
    // (also for the animations that follow) until clearRestingPose.
    // the table top in Photo mode, x across and y along z in the rope's own
    // table coordinates; empty where the scene has no table
    QRectF photoTableArea() const;

    // `sameTable`: the same rope moved (picked up with the mouse): the
    // table, the room and their light stay where they are.
    void setRestingRope(KnotMath::Mesh mesh, const QMatrix4x4 &pose, bool sameTable = false);

    // back to laying the knot down by its shape
    void clearRestingPose();

    // The Toy train scene: the track the train runs on (TrainTrack::path, in
    // millimetres, and its length; empty: no train), whether it is running,
    // and how far along the track the locomotive is (millimetres).
    void setTrainPath(const QVector<KnotMath::Vec3> &path, double length);

    // the bridges in the order the train meets them, shown as it goes
    void setTrainJourney(const QVector<Photo::TrainTrack::Pass> &journey);

    // what the train's sound needs to know: its speed (mm/s, 0: standing),
    // how loud from where the camera is, where it is across the picture
    void trainSoundScene(double *speed, float *gain, float *pan) const;

    // Cinema: the camera follows the train by itself, in shots cut one after
    // the other, between the black bars of a wide film. The mouse takes the
    // camera back.
    void setCinema(bool on);
    void playCinema(float seconds);
    bool cinema() const;

    void setTrainRunning(bool running);
    bool trainRunning() const;
    void setTrainAlong(double millimetres);

    // steam as if the train had been running this long, then held still
    // (for a still picture)
    void runSteam(float seconds);
    double trainAlong() const;

    void setAutoRotation(bool enabled);

    // no drifting particles and no turning, in every view
    static void setReduceMotion(bool reduced);

    // the frames per second in a corner of the view
    static void setShowFrameRate(bool shown);

    // the frames per second last measured (negative: nothing measured yet,
    // 0: the view is still and nothing is being drawn)
    double frameRate() const;

    // the graphics card and its OpenGL, for a report of a problem
    QString graphicsInfo() const;

    // Photo mode has gathered all the samples of this picture
    bool photoPictureFinished() const;

    // Grab and pull the rope. While it is on, a left press on the tube
    // around `curve` (drawn with tubes of `radius`) grabs the rope:
    // ropeGrabbed gives the point of the curve under the mouse,
    // ropeDragged where that point should go (it moves in the plane that
    // faces the camera) and ropeReleased the end of the pull. A press next
    // to the rope turns the view as usual.
    void setRopeGrab(bool enabled);
    void setGrabCurve(const QVector<KnotMath::Vec3> &curve, double radius, bool closed = true);

    // Photo mode, the rope on the table in the hand: the grabbed point
    // follows the mouse on the level `height` (world, above the table)
    // instead of in the plane facing the camera; NaN for that plane.
    void setRopeHandLevel(float level);

    // Shift was held when the rope was grabbed (push it under the rest)
    bool ropeGrabUnder() const;

    // ends a pull without ropeReleased (the caller has stopped it)
    void cancelRopeGrab();

    bool autoRotation() const;

    // Turns to the clearest view of the current knot and fits it into
    // the window, with a short camera flight. The view is never steeper
    // than `highestPitch` degrees above the floor.
    void resetCamera(
        float highestPitch = 75.0f,
        bool animate = true);

    // A new knot: the same framing as resetCamera. Before the view is on
    // screen (no size yet) it is done at the first paint, without a flight.
    void fitNewKnot(
        float highestPitch = 75.0f);

    // For animations that place things themselves: a camera flight to
    // (yaw, pitch) degrees that shows the ball (`centre`, `radius`) whole.
    void flyToSphere(
        const QVector3D &centre,
        float radius,
        float yaw,
        float pitch);

    // a camera flight to (yaw, pitch) degrees that shows the whole mesh
    void flyToMesh(
        float yaw,
        float pitch);

    // Photo mode's automatic quality starts at this level (1 high, 2 medium,
    // 3 low), and the level in use now.
    void setPhotoAutoLevel(int level);
    int photoLevelInUse() const;

    // The camera at once at (yaw, pitch) degrees, showing the ball (`centre`
    // in the view, `radius`) whole. For pictures from a chosen place.
    void placeCamera(
        const QVector3D &centre,
        float radius,
        float yaw,
        float pitch);

signals:
    void cinemaChanged(bool on);

    void cameraChanged(double yaw, double pitch, double distance);

    void ropeGrabbed(const QVector3D &point);
    void ropeDragged(const QVector3D &point);
    void ropeReleased();

    // Photo mode's level changed (1 high, 2 medium, 3 low); `automatic`: the
    // view chose it
    void photoLevelChanged(int level, bool automatic);

protected:
    // (the studio's colours follow the app's theme)
    void paintGL() override;

private:
    // the layers, in the order they are drawn: the toy train, then the
    // knot's 2D overlays
    TrainLayer train_;
    KnotOverlay overlay_;
};

#endif // KNOTGLWIDGET_H
