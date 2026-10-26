#ifndef KNOTGLWIDGET_H
#define KNOTGLWIDGET_H

#include "knotmath.h"
#include "lighting.h"
#include "photoscene.h"

#include <QColor>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QRectF>
#include <QTimer>
#include <QVector3D>
#include <QVector>

#include <memory>
#include <random>

class QPainter;

class KnotGLWidget final
    : public QOpenGLWidget
    , protected QOpenGLFunctions
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

    // 2D knot diagram shown in a corner during the unknotting animation.
    struct DiagramInset
    {
        // closed curve in walking order; index 0 is the base point
        QVector<QPointF> points;

        struct Crossing
        {
            QPointF position;

            // passes on top and below right now (0..1 along the curve)
            double overU = 0.0;
            double underU = 0.0;

            // crossing change number (1-based), 0 = not changed
            int change = 0;

            // 0 plain, 1 still to be changed, 2 changing now, 3 changed
            int state = 0;

            // the strand on top now is the one that started below (for the
            // colours of the crossing that is changing)
            bool swapped = false;
        };

        QVector<Crossing> crossings;

        QString caption;

        // The diagram of an unknotting animation after `changesDone`
        // crossing changes; crossing change `activeChange` (1-based, 0 for
        // none) is drawn as the one changing now.
        static DiagramInset fromUnknotting(
            const KnotMath::UnknottingAnimation::Diagram &diagram,
            int changesDone,
            int activeChange);
    };

    void setDiagramInset(const DiagramInset &inset);
    void clearDiagramInset();

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
    struct MemoryUse
    {
        qint64 mesh = 0;
        qint64 shadowMap = 0;

        // the framebuffer the view is drawn into
        qint64 view = 0;

        // floor and particles
        qint64 other = 0;

        qint64 total() const
        {
            return mesh + shadowMap + view + other;
        }
    };

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
    bool setPhotoMode(bool enabled, Photo::Scene scene);

    bool photoMode() const;

    // Photo mode: the rope at rest on the table (ropephysics.h), as built
    // by Photo::buildRopeMesh, and the turn that took the knot onto the
    // table (the table top is y = 0 after it). The knot keeps this pose
    // (also for the animations that follow) until clearRestingPose.
    void setRestingRope(const KnotMath::Mesh &mesh, const QMatrix4x4 &pose);

    // back to laying the knot down by its shape
    void clearRestingPose();

    void setAutoRotation(bool enabled);

    // Grab and pull the rope. While it is on, a left press on the tube
    // around `curve` (drawn with tubes of `radius`) grabs the rope:
    // ropeGrabbed gives the point of the curve under the mouse,
    // ropeDragged where that point should go (it moves in the plane that
    // faces the camera) and ropeReleased the end of the pull. A press next
    // to the rope turns the view as usual.
    void setRopeGrab(bool enabled);
    void setGrabCurve(const QVector<KnotMath::Vec3> &curve, double radius);

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

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    void cameraChanged(double yaw, double pitch, double distance);

    void ropeGrabbed(const QVector3D &point);
    void ropeDragged(const QVector3D &point);
    void ropeReleased();

protected:
    void initializeGL() override;

    void resizeGL(int width, int height) override;

    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onAnimationTick();

private:
    struct Vertex
    {
        float px;
        float py;
        float pz;

        float nx;
        float ny;
        float nz;

        float cr;
        float cg;
        float cb;

        // cos/sin around the tube, cos/sin of the twist phase
        float t0;
        float t1;
        float t2;
        float t3;

        // ambient occlusion (1 = open)
        float ao;

        // 1 = Seifert surface sheet (Mesh::sheet)
        float sheet;
    };

    // position, colour (rgba), size (points)
    struct FlatVertex
    {
        float x;
        float y;
        float z;

        float r;
        float g;
        float b;
        float a;

        float size;
    };

    struct Particle
    {
        float x;
        float y;
        float z;

        float vx;
        float vy;
        float vz;

        float age;
        float maxAge;

        float r;
        float g;
        float b;
        float a;

        float size;
    };

    void initializeShaders();
    void initializeShadowMap();

    // (re)allocates the shadow map texture at shadowMapSize_
    void allocateShadowMap();
    void initializeFlatBuffers();

    void uploadMesh();
    void destroyMeshBuffers();
    void rebuildGroundGeometry();

    void computeMeshBounds();

    void flyCameraTo(
        float yaw,
        float pitch,
        float distance,
        const QVector3D &target);

    void advanceCameraFlight(
        float dt);

    void stopCameraFlight();

    // camera distance at which a ball of `radius` fills most of the view
    float sphereDistance(
        float radius) const;

    QMatrix4x4 projectionMatrix() const;
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 modelMatrix() const;

    // The studio lights turn with the camera about the vertical: this
    // rotation takes their directions for a camera on +z to the world.
    QMatrix4x4 lightRig() const;

    QVector3D computeCameraPosition() const;

    QPointF projectPointToScreen(const KnotMath::Vec3 &point) const;

    // The point of the grab curve under `position` (on the strand nearest
    // to the camera there); false if the mouse is not on the rope.
    bool pickRope(const QPointF &position, QVector3D &point) const;

    // where the line of sight through `position` meets the plane through
    // `through` that faces the camera
    QVector3D pointOnViewPlane(const QPointF &position, const QVector3D &through) const;

    // depth of the scene seen from the key light (self-shadowing)
    void drawShadowMap();
    // `wanted`: this kind of shadow is turned on
    void setShadowUniforms(QOpenGLShaderProgram &program, bool wanted);

    void drawBackground();
    void drawGround(const QMatrix4x4 &viewProjection);
    void drawShadow(const QMatrix4x4 &viewProjection);

    // centre and half sizes (x, z) of the ground area the shadow may cover
    void shadowExtent(float &cx, float &cz, float &rx, float &rz) const;
    void drawMesh(
        const QMatrix4x4 &model,
        const QMatrix4x4 &view,
        const QMatrix4x4 &projection);
    void drawParticles(const QMatrix4x4 &viewProjection);
    void drawCrossingLabels(QPainter &painter);
    void drawFlashes(QPainter &painter);
    void drawDiagramInset(QPainter &painter);

    void configureFlatAttributes();

    // Photo mode
    void initializePhotoShaders();

    // the room and the surface of photoScene_ on the graphics card
    // (context current); false if they could not be loaded
    bool loadPhotoScene();
    void destroyPhotoResources();

    // GL texture from a picture in the resources, with mipmaps
    GLuint loadTexture(const QString &path);

    // pose_ from the shape of rawMesh_, and mesh_ = rawMesh_ in that pose
    void computePose();
    void applyPose();

    // applyPose, then the bounds and the mesh on the card
    void refreshPose();

    // a point of the knot (its own coordinates) in the view, and back
    QVector3D posed(const QVector3D &point) const;
    QVector3D unposed(const QVector3D &point) const;

    // meshMin_ and meshMax_ from the fixed bounds (in the pose)
    void poseFixedBounds();

    // the room, the lights and the exposure, for the photo shaders
    void setPhotoUniforms(QOpenGLShaderProgram &program);

    // direction towards the sun, in the view
    QVector3D photoKeyDirection() const;

    // millimetres -> the view's units (from the rope's real thickness)
    float worldPerMm() const;

    void drawPhotoBackground(const QMatrix4x4 &viewProjection);
    void drawPhotoFloor(const QMatrix4x4 &viewProjection);

    // the frame drawn in Photo mode (HDR, multisampled) and its resolved
    // copy; (re)made at the size of the view
    bool preparePhotoFrame(int width, int height);
    void destroyPhotoFrame();

    // the camera: depth of field, exposure and film, into the view
    void developPhotoFrame(const QMatrix4x4 &view, const QMatrix4x4 &projection);

    // the table top (or floor) and its sides, around the knot
    void rebuildSurfaceGeometry();

    // How much of the room the table sees past the rope at every point
    // around the knot (view factors), drawn into occlusionTexture_.
    void bakeSurfaceOcclusion();

    // The Sneaker scene (sneaker.h): the shoe stands under the lace at
    // rest (in the frame of setRestingRope's pose, scaled from mm).
    bool showShoe() const;

    // its surface and its shade on the floor onto the card (with the
    // scene), and off again
    void uploadShoe();
    void destroyShoe();

    // How much of the room each point of the shoe sees past the lace
    // (view factors, as for the table), into shoeShadeBuffer_.
    void updateShoeShade();

    void drawShoe(const QMatrix4x4 &viewProjection);

    // height of the floor in Photo mode: right under the knot
    float photoFloorLevel() const;

    // Blurred soft-shadow blobs under the knot (x, z, sigma, weight) for
    // the ground at height groundY.
    QVector<QVector4D> contactShadowPoints(float groundY) const;

    void initializeParticles();
    void respawnParticle(Particle &particle, bool randomAge);
    void updateParticles(float dt);

    float randomFloat(float minimum, float maximum);

private:
    std::unique_ptr<QOpenGLShaderProgram> meshProgram_;
    std::unique_ptr<QOpenGLShaderProgram> backgroundProgram_;
    std::unique_ptr<QOpenGLShaderProgram> flatProgram_;

    // soft ground shadow computed from the centre line of the tubes
    std::unique_ptr<QOpenGLShaderProgram> shadowProgram_;

    // shadow map of the key light: depth program, texture and framebuffer
    std::unique_ptr<QOpenGLShaderProgram> depthProgram_;
    GLuint shadowTexture_ = 0;
    GLuint shadowFramebuffer_ = 0;
    bool shadowsReady_ = false;
    int shadowMapSize_ = Lighting::shadowMapSize(1);

    Lighting::Options lighting_;

    // bytes in the mesh and floor buffers (memoryUse)
    qint64 meshBytes_ = 0;
    qint64 groundBytes_ = 0;

    // world -> shadow map coordinates (0..1), the size of one texel in
    // world units, the softness of the shadow edge (map units) and the
    // matching depth bias
    QMatrix4x4 lightMatrix_;
    float shadowTexel_ = 0.0f;
    float shadowSoftness_ = 0.0f;
    float shadowDepthBias_ = 0.0f;

    QOpenGLVertexArrayObject meshVao_;
    QOpenGLBuffer vertexBuffer_;
    QOpenGLBuffer indexBuffer_;

    QOpenGLVertexArrayObject backgroundVao_;

    QOpenGLVertexArrayObject groundVao_;
    QOpenGLBuffer groundBuffer_;
    int gridVertexCount_ = 0;

    QOpenGLVertexArrayObject particleVao_;
    QOpenGLBuffer particleBuffer_;

    int indexCount_ = 0;

    KnotMath::Mesh mesh_;

    QVector<KnotMath::CrossingInfo> crossings_;

    QVector3D meshColor_ = QVector3D(0.729f, 0.725f, 0.706f);

    // bounding box of the mesh (for the ground plane and shadow)
    QVector3D meshMin_ = QVector3D(-2.0f, -2.0f, -2.0f);
    QVector3D meshMax_ = QVector3D(2.0f, 2.0f, 2.0f);
    bool groundDirty_ = true;

    bool boundsFixed_ = false;

    DiagramInset inset_;
    bool showInset_ = false;

    QVector<KnotMath::Vec3> flashPositions_;
    QVector<float> flashProgress_;
    QVector<int> flashNumbers_;

    float yaw_ = 35.0f;
    float pitch_ = 18.0f;
    float distance_ = 8.0f;

    QVector3D panOffset_ = QVector3D(0.0f, 0.0f, 0.0f);

    // camera flight of Reset view
    bool cameraFlight_ = false;
    float flightProgress_ = 0.0f;
    float flightFromYaw_ = 0.0f;
    float flightFromPitch_ = 0.0f;
    float flightFromDistance_ = 0.0f;
    QVector3D flightFromPan_;
    float flightToYaw_ = 0.0f;
    float flightToPitch_ = 0.0f;
    float flightToDistance_ = 0.0f;
    QVector3D flightToPan_;

    QPoint lastMousePosition_;

    bool leftMouseDown_ = false;
    bool rightMouseDown_ = false;

    // grab and pull: the curve that can be grabbed, the point grabbed and
    // where the press met the plane of the drag
    bool ropeGrab_ = false;
    bool ropeDragging_ = false;
    QVector<KnotMath::Vec3> grabCurve_;
    double grabRadius_ = 0.1;
    QVector3D grabPoint_;
    QVector3D grabStart_;

    bool glInitialized_ = false;
    bool useVertexColors_ = false;
    bool showCrossingNumbers_ = false;

    float metallic_ = 0.5f;
    float roughness_ = 0.3f;
    int material_ = 0;

    bool autoRotation_ = false;

    // highest pitch of a framing still to be done at the next paint
    // (fitNewKnot); negative: none
    float pendingFitPitch_ = -1.0f;

    // view-projection used by the last paint (needed to place the labels)
    QMatrix4x4 lastViewProjection_;

    QVector<Particle> particles_;

    std::mt19937 rng_;

    QTimer animationTimer_;
    QElapsedTimer frameClock_;

    /*
     * Photo mode
     */
    bool photo_ = false;
    Photo::Scene photoScene_ = Photo::Scene::LoungeTable;
    Photo::SceneInfo photoInfo_;

    // scene whose textures are loaded (-1: none)
    int loadedScene_ = -1;

    // the room's light (the pictures themselves are on the card)
    Photo::Environment environment_;

    // the room (with mipmaps), the room blurred for each roughness (mip
    // level i is roughness i / 5) and the surface
    GLuint environmentTexture_ = 0;
    GLuint specularTexture_ = 0;
    GLuint floorColorTexture_ = 0;
    GLuint floorNormalTexture_ = 0;
    GLuint floorRoughnessTexture_ = 0;

    // the mesh is the rope at rest (setRestingRope): drawn with its real
    // strands or yarns; and the pose came with it
    bool restingRope_ = false;
    bool restingPose_ = false;
    QMatrix4x4 restPose_;

    // the frame: multisampled HDR colour and depth, resolved into textures
    // (the colour with mipmaps, for the blur)
    GLuint photoFramebuffer_ = 0;
    GLuint photoColorBuffer_ = 0;
    GLuint photoDepthBuffer_ = 0;
    GLuint resolveFramebuffer_ = 0;
    GLuint resolvedColor_ = 0;
    GLuint resolvedDepth_ = 0;
    int photoFrameWidth_ = 0;
    int photoFrameHeight_ = 0;
    qint64 photoFrameBytes_ = 0;

    // the table top and its sides: position, normal, tangent, picture
    // coordinates (11 floats a vertex)
    QOpenGLVertexArrayObject surfaceVao_;
    QOpenGLBuffer surfaceBuffer_;
    int surfaceVertexCount_ = 0;
    bool surfaceDirty_ = true;

    // the table's view of the room past the rope, over the square
    // occlusionArea_ (x, z of the middle, half size)
    GLuint occlusionTexture_ = 0;
    GLuint occlusionFramebuffer_ = 0;
    GLuint segmentTexture_ = 0;
    QVector3D occlusionArea_;
    bool occlusionDirty_ = true;

    std::unique_ptr<QOpenGLShaderProgram> developProgram_;
    std::unique_ptr<QOpenGLShaderProgram> occlusionProgram_;

    // reads the shadow map as plain depth (the blocker search of the
    // contact-hardening shadows)
    GLuint depthSampler_ = 0;

    // average colour of the surface (linear), the camera's exposure for
    // the room, and the video memory of the photo pictures
    QVector3D floorAlbedo_ = QVector3D(0.3f, 0.3f, 0.3f);
    float photoExposure_ = 1.0f;

    // the room's pictures hold its light divided by this (half floats
    // cannot hold an unclipped sun)
    float photoRoomScale_ = 1.0f;
    qint64 photoBytes_ = 0;

    std::unique_ptr<QOpenGLShaderProgram> photoBackgroundProgram_;
    std::unique_ptr<QOpenGLShaderProgram> photoFloorProgram_;
    std::unique_ptr<QOpenGLShaderProgram> prefilterProgram_;

    // the shadow map size outside Photo mode (it is larger in it)
    int normalShadowMapSize_ = Lighting::shadowMapSize(1);

    // light-space depth range and map width in world units (PCSS)
    float shadowDepthRange_ = 1.0f;
    float shadowMapWorld_ = 1.0f;

    // the knot as given, and the turn that lays it down in Photo mode
    KnotMath::Mesh rawMesh_;
    QMatrix4x4 pose_;
    bool poseDirty_ = true;

    // fixed bounds as given (the knot's own coordinates)
    QVector3D rawFixedMin_;
    QVector3D rawFixedMax_;

    // Photo mode: the table is right under the rope. During an animation
    // (fixed bounds) it stays where the rope lay when it began.
    float tableLevel_ = 0.0f;

    // lowest point of the mesh (the view's coordinates)
    float meshBottom() const;

    // the sneaker: its surface (millimetres), the lace's shade on it (one
    // float a vertex) and the floor's view of the room past it
    std::unique_ptr<QOpenGLShaderProgram> shoeProgram_;
    QOpenGLVertexArrayObject shoeVao_;
    QOpenGLBuffer shoeBuffer_;
    QOpenGLBuffer shoeIndexBuffer_;
    QOpenGLBuffer shoeShadeBuffer_;
    int shoeIndexCount_ = 0;
    bool shoeShadeDirty_ = true;
    GLuint shoeFloorTexture_ = 0;
};

#endif // KNOTGLWIDGET_H
