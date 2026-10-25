#ifndef KNOTGLWIDGET_H
#define KNOTGLWIDGET_H

#include "knotmath.h"

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

    // Surface look: 0 glossy, 1 satin, 2 metal, 3 rope (fibre pattern),
    // roughness 0 (mirror) .. 1 (matte).
    void setSurface(int material, float roughness);

    void setAutoRotation(bool enabled);

    bool autoRotation() const;

    // Turns to the clearest view of the current knot and fits it into
    // the window, with a short camera flight.
    void resetCamera();

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    void cameraChanged(double yaw, double pitch, double distance);

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

    QMatrix4x4 projectionMatrix() const;
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 modelMatrix() const;

    QVector3D computeCameraPosition() const;

    QPointF projectPointToScreen(const KnotMath::Vec3 &point) const;

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

    bool glInitialized_ = false;
    bool useVertexColors_ = false;
    bool showCrossingNumbers_ = false;

    float metallic_ = 0.5f;
    float roughness_ = 0.3f;
    int material_ = 0;

    bool autoRotation_ = false;

    // view-projection used by the last paint (needed to place the labels)
    QMatrix4x4 lastViewProjection_;

    QVector<Particle> particles_;

    std::mt19937 rng_;

    QTimer animationTimer_;
    QElapsedTimer frameClock_;
};

#endif // KNOTGLWIDGET_H
