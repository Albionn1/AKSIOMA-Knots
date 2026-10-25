#include "knotglwidget.h"

#include "theme.h"

#include <QFontMetrics>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>
#include <QPen>
#include <QShowEvent>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

namespace
{

constexpr float FieldOfView = 45.0f;
constexpr float NearPlane = 0.1f;
constexpr float FarPlane = 100.0f;

constexpr int MaxParticles = 120;

// Spacing between the ground grid lines and half size of the grid.
constexpr int GridHalfLines = 10;

float groundLevel(float meshMinY)
{
    return meshMinY - 0.3f;
}

} // namespace

KnotGLWidget::KnotGLWidget(QWidget *parent)
    : QOpenGLWidget(parent),
      vertexBuffer_(QOpenGLBuffer::VertexBuffer),
      indexBuffer_(QOpenGLBuffer::IndexBuffer),
      groundBuffer_(QOpenGLBuffer::VertexBuffer),
      particleBuffer_(QOpenGLBuffer::VertexBuffer),
      rng_(std::random_device{}())
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    QSurfaceFormat format;

    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4); // MSAA (glEnable(GL_MULTISAMPLE) alone does nothing)
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);

    setFormat(format);

    setMinimumSize(500, 500);

    initializeParticles();

    // Animation (particles, auto-rotation) is driven by a timer and uses the
    // real elapsed time, so the speed does not depend on the frame rate.
    // Precise: the coarse timer ticks unevenly on Windows (auto-rotation
    // stutters).
    animationTimer_.setTimerType(Qt::PreciseTimer);
    animationTimer_.setInterval(33);

    connect(
        &animationTimer_,
        &QTimer::timeout,
        this,
        &KnotGLWidget::onAnimationTick);
}

KnotGLWidget::~KnotGLWidget()
{
    animationTimer_.stop();

    if (context())
    {
        makeCurrent();

        destroyMeshBuffers();

        if (groundVao_.isCreated())
            groundVao_.destroy();

        if (groundBuffer_.isCreated())
            groundBuffer_.destroy();

        if (particleVao_.isCreated())
            particleVao_.destroy();

        if (particleBuffer_.isCreated())
            particleBuffer_.destroy();

        if (backgroundVao_.isCreated())
            backgroundVao_.destroy();

        meshProgram_.reset();
        backgroundProgram_.reset();
        flatProgram_.reset();

        doneCurrent();
    }
}

QSize KnotGLWidget::minimumSizeHint() const
{
    return QSize(500, 500);
}

QSize KnotGLWidget::sizeHint() const
{
    return QSize(900, 700);
}

/*
 * =========================================================
 * INITIALISATION
 * =========================================================
 */

void KnotGLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glInitialized_ = true;

    glEnable(GL_MULTISAMPLE);

    glClearColor(0.0235f, 0.0235f, 0.0235f, 1.0f);

    initializeShaders();
    initializeFlatBuffers();

    uploadMesh();
}

void KnotGLWidget::initializeShaders()
{
    /*
     * ---------------------------------------------------------
     * Tube mesh
     * ---------------------------------------------------------
     */

    meshProgram_ = std::make_unique<QOpenGLShaderProgram>();

    const char *meshVertexShader = R"(
#version 330 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 vertexColor;
layout(location = 3) in vec4 texcoord;
layout(location = 4) in float occlusion;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec3 vVertexColor;
out vec4 vTexcoord;
out float vOcclusion;

void main()
{
    vec4 worldPosition = uModel * vec4(position, 1.0);

    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(uNormalMatrix * normal);
    vVertexColor = vertexColor;
    vTexcoord = texcoord;
    vOcclusion = occlusion;

    gl_Position = uProjection * uView * worldPosition;
}
)";

    const char *meshFragmentShader = R"(
#version 330 core

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec3 vVertexColor;
in vec4 vTexcoord;
in float vOcclusion;

uniform vec3 uCameraPosition;
uniform vec3 uBaseColor;
uniform int uUseVertexColors;
uniform float uMetallic;
uniform float uRoughness;
uniform int uMaterial;      // 0 glossy, 1 satin, 2 metal, 3 rope

out vec4 fragColor;

const float PI = 3.14159265;

// Studio lights (directions towards the light) and their colours.
const vec3 KeyDir = vec3(0.37, 0.56, 0.74);
const vec3 FillDir = vec3(-0.80, 0.35, 0.49);
const vec3 RimDir = vec3(0.05, 0.55, -0.83);

const vec3 KeyColor = vec3(2.1, 2.0, 1.85);
const vec3 FillColor = vec3(0.32, 0.36, 0.45);
const vec3 RimColor = vec3(1.1, 1.1, 1.2);

vec3 toLinear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

float distributionGGX(float nh, float a)
{
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;

    return a2 / (PI * d * d);
}

float visibilitySmith(float nv, float nl, float a)
{
    float k = a * 0.5;
    float gv = nv / (nv * (1.0 - k) + k);
    float gl = nl / (nl * (1.0 - k) + k);

    return gv * gl / max(4.0 * nv * nl, 1.0e-4);
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}

// Simple studio surroundings for reflections: dark floor, grey walls, a
// large soft key box and a rim strip. Rough surfaces see them blurred.
vec3 environment(vec3 d, float rough)
{
    vec3 floorColor = vec3(0.010);
    vec3 wallColor = vec3(0.045, 0.045, 0.050);
    vec3 ceilingColor = vec3(0.09);

    vec3 c = mix(floorColor, wallColor, smoothstep(-0.35, 0.05, d.y));
    c = mix(c, ceilingColor, smoothstep(0.25, 0.95, d.y));

    float spread = mix(0.05, 0.8, rough);
    float sharp = mix(1.0, 0.25, rough);

    c += vec3(1.7, 1.6, 1.5) * sharp *
         exp((dot(d, KeyDir) - 1.0) / (spread * spread));

    c += vec3(0.7, 0.75, 0.85) * sharp *
         exp((dot(d, RimDir) - 1.0) / (1.5 * spread * spread));

    return c;
}

vec3 directLight(vec3 n, vec3 v, vec3 l, vec3 radiance,
                 vec3 albedo, vec3 f0, float a, float metal)
{
    float nl = dot(n, l);

    if (nl <= 0.0)
        return vec3(0.0);

    vec3 h = normalize(l + v);

    float nv = max(dot(n, v), 1.0e-3);
    float nh = max(dot(n, h), 0.0);

    vec3 f = fresnelSchlick(max(dot(v, h), 0.0), f0);

    vec3 specular = distributionGGX(nh, a) * visibilitySmith(nv, nl, a) * f;
    vec3 diffuse = (1.0 - f) * (1.0 - metal) * albedo / PI;

    return (diffuse + specular) * radiance * nl;
}

void main()
{
    vec3 n = normalize(vWorldNormal);
    vec3 v = normalize(uCameraPosition - vWorldPosition);

    if (dot(n, v) < 0.0)
        n = normalize(n - 1.01 * dot(n, v) * v);

    vec3 albedo = toLinear((uUseVertexColors != 0) ? vVertexColor : uBaseColor);

    float rough = clamp(uRoughness, 0.04, 1.0);
    float metal = clamp(uMetallic, 0.0, 1.0);
    float ao = clamp(vOcclusion, 0.0, 1.0);

    if (uMaterial == 3)
    {
        // Three-strand rope: helical grooves between the strands and fine
        // fibres twisted the other way. Integer multiples of the two
        // angles keep the pattern seamless.
        float around = atan(vTexcoord.y, vTexcoord.x);
        float along = atan(vTexcoord.w, vTexcoord.z);

        // position across a strand: 0 and 1 are the grooves (the phase
        // only jumps by whole turns, so fract() is seamless)
        float across = fract((3.0 * around + along) / (2.0 * PI));

        // round profile of a strand: bright on top, dark in the grooves
        float bulge = sqrt(max(sin(PI * across), 0.0));

        float fibres = 0.5 + 0.5 * cos(29.0 * around - 6.0 * along);

        albedo *= mix(0.3, 1.0, bulge) * mix(0.82, 1.0, fibres);
        ao *= mix(0.5, 1.0, bulge);
    }

    float a = rough * rough;
    vec3 f0 = mix(vec3(0.04), albedo, metal);

    // direct light, partly shaded where strands are close (depth cue)
    vec3 color = vec3(0.0);

    color += directLight(n, v, KeyDir, KeyColor, albedo, f0, a, metal);
    color += directLight(n, v, FillDir, FillColor, albedo, f0, a, metal);
    color += directLight(n, v, RimDir, RimColor, albedo, f0, a, metal);

    color *= mix(1.0, ao, 0.6);

    // ambient: sky above, dark floor below
    vec3 sky = vec3(0.10, 0.10, 0.115);
    vec3 ground = vec3(0.02, 0.019, 0.018);
    vec3 irradiance = mix(ground, sky, 0.5 + 0.5 * n.y);

    color += irradiance * albedo * (1.0 - metal) * ao;

    // reflections of the studio
    float nv = max(dot(n, v), 0.0);
    vec3 fEnv = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - nv, 5.0);
    vec3 r = reflect(-v, n);

    color += environment(r, rough) * fEnv * ao;

    // filmic tone mapping (ACES fit) and display gamma
    color = (color * (2.51 * color + 0.03)) /
            (color * (2.43 * color + 0.59) + 0.14);

    fragColor = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
)";

    bool meshOk = meshProgram_->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        meshVertexShader);

    meshOk = meshOk &&
             meshProgram_->addShaderFromSourceCode(
                 QOpenGLShader::Fragment,
                 meshFragmentShader);

    if (meshOk)
    {
        meshProgram_->bindAttributeLocation("position", 0);
        meshProgram_->bindAttributeLocation("normal", 1);
        meshProgram_->bindAttributeLocation("vertexColor", 2);
        meshProgram_->bindAttributeLocation("texcoord", 3);
        meshProgram_->bindAttributeLocation("occlusion", 4);

        meshProgram_->link();
    }

    /*
     * ---------------------------------------------------------
     * Background (one full-screen triangle, no vertex buffer)
     * ---------------------------------------------------------
     */

    backgroundProgram_ = std::make_unique<QOpenGLShaderProgram>();

    const char *backgroundVertexShader = R"(
#version 330 core

out vec2 vUv;

void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));

    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

    const char *backgroundFragmentShader = R"(
#version 330 core

in vec2 vUv;

uniform vec2 uGrid; // cell size and line width in device pixels
uniform vec3 uLower; // bottom and top colour, from the theme
uniform vec3 uUpper;
uniform float uLine; // brightness change on the grid lines

out vec4 fragColor;

void main()
{
    // flat, with a very slight lift towards the top; no glow, no vignette
    vec3 color = mix(uLower, uUpper, smoothstep(0.0, 1.0, vUv.y));

    // faint technical grid
    vec2 m = mod(gl_FragCoord.xy, vec2(uGrid.x));
    float line = (m.x < uGrid.y || m.y < uGrid.y) ? 1.0 : 0.0;

    color += vec3(uLine) * line;

    fragColor = vec4(color, 1.0);
}
)";

    bool backgroundOk = backgroundProgram_->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        backgroundVertexShader);

    backgroundOk = backgroundOk &&
                   backgroundProgram_->addShaderFromSourceCode(
                       QOpenGLShader::Fragment,
                       backgroundFragmentShader);

    if (backgroundOk)
        backgroundProgram_->link();

    /*
     * ---------------------------------------------------------
     * Flat shader: ground grid, shadow blob and particles
     * ---------------------------------------------------------
     */

    flatProgram_ = std::make_unique<QOpenGLShaderProgram>();

    const char *flatVertexShader = R"(
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aSize;

uniform mat4 uViewProjection;
uniform float uPointScale;

out vec4 vColor;
out vec3 vWorld;

void main()
{
    vColor = aColor;
    vWorld = aPosition;

    gl_Position = uViewProjection * vec4(aPosition, 1.0);
    gl_PointSize = aSize * uPointScale;
}
)";

    const char *flatFragmentShader = R"(
#version 330 core

in vec4 vColor;
in vec3 vWorld;

uniform int uMode;          // 0 = ground (fades with distance), 1 = point sprite
uniform vec2 uFadeCenter;
uniform vec2 uFadeRadii;
uniform float uFadeInner;
uniform float uGridAlpha;   // the grid is fainter on a light floor

out vec4 fragColor;

void main()
{
    float alpha = vColor.a;

    if (uMode == 1)
    {
        vec2 d = gl_PointCoord * 2.0 - vec2(1.0);
        alpha *= 1.0 - smoothstep(0.55, 1.0, length(d));
    }
    else
    {
        vec2 q = (vWorld.xz - uFadeCenter) / uFadeRadii;
        alpha *= (1.0 - smoothstep(uFadeInner, 1.0, length(q))) * uGridAlpha;
    }

    if (alpha <= 0.003)
        discard;

    fragColor = vec4(vColor.rgb, alpha);
}
)";

    bool flatOk = flatProgram_->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        flatVertexShader);

    flatOk = flatOk &&
             flatProgram_->addShaderFromSourceCode(
                 QOpenGLShader::Fragment,
                 flatFragmentShader);

    if (flatOk)
    {
        flatProgram_->bindAttributeLocation("aPosition", 0);
        flatProgram_->bindAttributeLocation("aColor", 1);
        flatProgram_->bindAttributeLocation("aSize", 2);

        flatProgram_->link();
    }

    /*
     * ---------------------------------------------------------
     * Soft ground shadow (light from above). Every centre-line point
     * casts a Gaussian blob that widens with its height above the ground:
     * a tube lying on the ground gives a dark, tight shadow, a high one a
     * faint, wide one.
     * ---------------------------------------------------------
     */

    shadowProgram_ = std::make_unique<QOpenGLShaderProgram>();

    const char *shadowVertexShader = R"(
#version 330 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProjection;

out vec3 vWorld;

void main()
{
    vWorld = aPosition;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

    const char *shadowFragmentShader = R"(
#version 330 core

in vec3 vWorld;

// x, z, blur (sigma), weight of every shadow-casting point
uniform vec4 uPoints[96];
uniform int uCount;

uniform vec2 uFadeCenter;
uniform vec2 uFadeRadii;
uniform float uStrength;
uniform vec3 uFloor;   // the pool of light, from the theme

out vec4 fragColor;

void main()
{
    float coverage = 0.0;

    for (int i = 0; i < uCount; ++i)
    {
        vec4 p = uPoints[i];
        vec2 d = vWorld.xz - p.xy;
        float s2 = p.z * p.z;

        coverage += p.w / s2 * exp(-dot(d, d) / (2.0 * s2));
    }

    float shadow = (1.0 - exp(-coverage)) * uStrength;

    // A faint pool of light on the floor under the knot;
    // the shadow is only visible against it.
    vec2 q = (vWorld.xz - uFadeCenter) / uFadeRadii;
    float pool = 1.0 - smoothstep(0.1, 1.0, length(q));

    float alpha = pool * 0.65;

    if (alpha <= 0.003)
        discard;

    vec3 floorColor = uFloor * (1.0 - shadow);

    fragColor = vec4(floorColor, alpha);
}
)";

    bool shadowOk = shadowProgram_->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        shadowVertexShader);

    shadowOk = shadowOk &&
               shadowProgram_->addShaderFromSourceCode(
                   QOpenGLShader::Fragment,
                   shadowFragmentShader);

    if (shadowOk)
    {
        shadowProgram_->bindAttributeLocation("aPosition", 0);
        shadowProgram_->link();
    }
}

void KnotGLWidget::configureFlatAttributes()
{
    const int stride = static_cast<int>(sizeof(FlatVertex));

    flatProgram_->enableAttributeArray(0);
    flatProgram_->setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);

    flatProgram_->enableAttributeArray(1);
    flatProgram_->setAttributeBuffer(
        1,
        GL_FLOAT,
        static_cast<int>(3 * sizeof(float)),
        4,
        stride);

    flatProgram_->enableAttributeArray(2);
    flatProgram_->setAttributeBuffer(
        2,
        GL_FLOAT,
        static_cast<int>(7 * sizeof(float)),
        1,
        stride);
}

void KnotGLWidget::initializeFlatBuffers()
{
    if (!backgroundVao_.isCreated())
        backgroundVao_.create();

    if (!flatProgram_ || !flatProgram_->isLinked())
        return;

    flatProgram_->bind();

    // ground (grid + shadow); the data is uploaded in rebuildGroundGeometry()
    if (!groundVao_.isCreated())
        groundVao_.create();

    groundVao_.bind();

    if (!groundBuffer_.isCreated())
        groundBuffer_.create();

    groundBuffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    groundBuffer_.bind();

    configureFlatAttributes();

    groundVao_.release();
    groundBuffer_.release();

    // particles (dynamic)
    if (!particleVao_.isCreated())
        particleVao_.create();

    particleVao_.bind();

    if (!particleBuffer_.isCreated())
        particleBuffer_.create();

    particleBuffer_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    particleBuffer_.bind();

    particleBuffer_.allocate(
        nullptr,
        static_cast<int>(MaxParticles * sizeof(FlatVertex)));

    configureFlatAttributes();

    particleVao_.release();
    particleBuffer_.release();

    flatProgram_->release();

    groundDirty_ = true;
}

void KnotGLWidget::rebuildGroundGeometry()
{
    if (!groundBuffer_.isCreated())
        return;

    const float y = groundLevel(meshMin_.y());
    const float extent = static_cast<float>(GridHalfLines);

    QVector<FlatVertex> vertices;

    const FlatVertex base{0, 0, 0, 0.325f, 0.318f, 0.298f, 0.35f, 1.0f};

    // grid lines along x and z
    for (int i = -GridHalfLines; i <= GridHalfLines; ++i)
    {
        const float offset = static_cast<float>(i);

        FlatVertex a = base;
        FlatVertex b = base;

        a.x = -extent;
        a.y = y;
        a.z = offset;

        b.x = extent;
        b.y = y;
        b.z = offset;

        vertices.push_back(a);
        vertices.push_back(b);

        a.x = offset;
        a.z = -extent;

        b.x = offset;
        b.z = extent;

        vertices.push_back(a);
        vertices.push_back(b);
    }

    gridVertexCount_ = vertices.size();

    // soft shadow (quad, shaped by the shadow shader)
    float cx = 0.0f;
    float cz = 0.0f;
    float rx = 1.0f;
    float rz = 1.0f;

    shadowExtent(cx, cz, rx, rz);

    FlatVertex shadow = base;

    shadow.r = 0.0f;
    shadow.g = 0.0f;
    shadow.b = 0.0f;
    shadow.a = 0.4f;
    shadow.y = y + 0.003f;

    auto corner = [&](float sx, float sz)
    {
        FlatVertex v = shadow;

        v.x = cx + sx * rx;
        v.z = cz + sz * rz;

        return v;
    };

    vertices.push_back(corner(-1.0f, -1.0f));
    vertices.push_back(corner(1.0f, -1.0f));
    vertices.push_back(corner(1.0f, 1.0f));

    vertices.push_back(corner(-1.0f, -1.0f));
    vertices.push_back(corner(1.0f, 1.0f));
    vertices.push_back(corner(-1.0f, 1.0f));

    groundBuffer_.bind();

    groundBuffer_.allocate(
        vertices.constData(),
        static_cast<int>(vertices.size() * sizeof(FlatVertex)));

    groundBuffer_.release();

    groundDirty_ = false;
}

/*
 * =========================================================
 * DATA
 * =========================================================
 */

void KnotGLWidget::computeMeshBounds()
{
    if (!mesh_.isValid())
    {
        meshMin_ = QVector3D(-2.0f, -2.0f, -2.0f);
        meshMax_ = QVector3D(2.0f, 2.0f, 2.0f);

        return;
    }

    QVector3D minimum(1.0e30f, 1.0e30f, 1.0e30f);
    QVector3D maximum(-1.0e30f, -1.0e30f, -1.0e30f);

    const int vertexCount = mesh_.positions.size() / 3;

    for (int i = 0; i < vertexCount; ++i)
    {
        const float x = mesh_.positions.at(i * 3);
        const float y = mesh_.positions.at(i * 3 + 1);
        const float z = mesh_.positions.at(i * 3 + 2);

        minimum.setX(std::min(minimum.x(), x));
        minimum.setY(std::min(minimum.y(), y));
        minimum.setZ(std::min(minimum.z(), z));

        maximum.setX(std::max(maximum.x(), x));
        maximum.setY(std::max(maximum.y(), y));
        maximum.setZ(std::max(maximum.z(), z));
    }

    meshMin_ = minimum;
    meshMax_ = maximum;
}

void KnotGLWidget::setMesh(const KnotMath::Mesh &mesh)
{
    mesh_ = mesh;

    if (!boundsFixed_)
    {
        computeMeshBounds();

        groundDirty_ = true;
    }

    if (glInitialized_)
    {
        makeCurrent();

        uploadMesh();

        doneCurrent();
    }

    update();
}

void KnotGLWidget::setFixedBounds(
    const QVector3D &minimum,
    const QVector3D &maximum)
{
    boundsFixed_ = true;
    meshMin_ = minimum;
    meshMax_ = maximum;
    groundDirty_ = true;
    update();
}

void KnotGLWidget::clearFixedBounds()
{
    boundsFixed_ = false;
    computeMeshBounds();
    groundDirty_ = true;
    update();
}

void KnotGLWidget::setFlashes(
    const QVector<KnotMath::Vec3> &positions,
    const QVector<float> &progress,
    const QVector<int> &numbers)
{
    flashPositions_ = positions;
    flashProgress_ = progress;
    flashNumbers_ = numbers;
    update();
}

void KnotGLWidget::clearFlashes()
{
    flashPositions_.clear();
    flashProgress_.clear();
    flashNumbers_.clear();
    update();
}

void KnotGLWidget::drawFlashes(QPainter &painter)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    const double scale =
        std::max(
            1.0,
            static_cast<double>(std::min(width(), height())) / 600.0);

    for (int i = 0; i < flashPositions_.size() &&
                    i < flashProgress_.size(); ++i)
    {
        const double p = std::clamp(
            static_cast<double>(flashProgress_.at(i)), 0.0, 1.0);

        if (p <= 0.0 || p >= 1.0)
            continue;

        const QPointF centre = projectPointToScreen(flashPositions_.at(i));

        const double fade = (1.0 - p) * (1.0 - p);
        const double radius = (10.0 + 90.0 * p) * scale;

        const QColor ring(255, 235, 140,
                          static_cast<int>(255.0 * fade));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ring, 3.0 * (1.0 - p) + 1.0));
        painter.drawEllipse(centre, radius, radius);

        const double r2 = radius * 0.55;
        painter.setPen(QPen(QColor(255, 255, 255,
                                   static_cast<int>(200.0 * fade)), 2.0));
        painter.drawEllipse(centre, r2, r2);

        QRadialGradient glow(centre, radius * 0.8);
        glow.setColorAt(0.0, QColor(255, 250, 200,
                                    static_cast<int>(170.0 * fade)));
        glow.setColorAt(1.0, QColor(255, 220, 90, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(centre, radius * 0.8, radius * 0.8);

        // number 0: a flash without a number
        if (i < flashNumbers_.size() && flashNumbers_.at(i) > 0)
        {
            QFont font = painter.font();
            font.setBold(true);
            font.setPointSize(11);
            painter.setFont(font);
            painter.setPen(QColor(255, 245, 190,
                                  static_cast<int>(255.0 * (1.0 - p))));
            painter.drawText(
                QPointF(centre.x() + radius * 0.7,
                        centre.y() - radius * 0.7 - 6.0 * p * scale),
                QString::number(flashNumbers_.at(i)));
        }
    }
}

KnotGLWidget::DiagramInset KnotGLWidget::DiagramInset::fromUnknotting(
    const KnotMath::UnknottingAnimation::Diagram &diagram,
    int changesDone,
    int activeChange)
{
    DiagramInset inset;

    inset.points = diagram.points;

    for (const KnotMath::UnknottingAnimation::DiagramCrossing &crossing :
         diagram.crossings)
    {
        Crossing shown;

        shown.position = crossing.position;
        shown.change = crossing.change;

        // after its crossing change the other strand is on top
        const bool changed =
            crossing.change > 0 && crossing.change <= changesDone;

        const int over =
            changed ? 1 - crossing.overAtStart : crossing.overAtStart;

        shown.overU = crossing.u[over];
        shown.underU = crossing.u[1 - over];

        if (crossing.change == 0)
            shown.state = 0;
        else if (crossing.change == activeChange)
            shown.state = 2;
        else
            shown.state = changed ? 3 : 1;

        // the colours stay with the strands: blue started on top
        shown.swapped = changed;

        inset.crossings.push_back(shown);
    }

    return inset;
}

void KnotGLWidget::setDiagramInset(const DiagramInset &inset)
{
    inset_ = inset;
    showInset_ = inset.points.size() >= 3;
    update();
}

void KnotGLWidget::clearDiagramInset()
{
    inset_ = DiagramInset();
    showInset_ = false;
    update();
}

void KnotGLWidget::drawDiagramInset(QPainter &painter)
{
    const QVector<QPointF> &points = inset_.points;
    const int n = points.size();

    if (n < 3)
        return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // panel in the lower left corner
    const double side =
        std::clamp(0.32 * std::min(width(), height()), 180.0, 300.0);

    const QRectF panel(16.0, height() - side - 16.0, side, side);

    const QColor background(14, 14, 14, 225);

    painter.setPen(QPen(QColor(48, 47, 45), 1.0));
    painter.setBrush(background);
    painter.drawRoundedRect(panel, 6.0, 6.0);

    QFont font = painter.font();
    font.setPointSizeF(7.5);
    font.setBold(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    painter.setFont(font);
    painter.setPen(QColor(138, 135, 128));
    painter.drawText(
        panel.adjusted(12.0, 8.0, -12.0, 0.0),
        Qt::AlignLeft | Qt::AlignTop,
        inset_.caption.isEmpty() ? tr("KNOT DIAGRAM") : inset_.caption);

    paintDiagram(painter, panel.adjusted(18.0, 28.0, -18.0, -18.0), inset_, background);
}

void KnotGLWidget::paintDiagram(
    QPainter &painter,
    const QRectF &area,
    const DiagramInset &inset,
    const QColor &background)
{
    const QVector<QPointF> &points = inset.points;
    const int n = points.size();

    if (n < 3)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // fit the diagram (y up) into the area
    double minX = points.first().x();
    double maxX = minX;
    double minY = points.first().y();
    double maxY = minY;

    for (const QPointF &p : points)
    {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }

    const double scale =
        std::min(area.width() / std::max(maxX - minX, 1.0e-9),
                 area.height() / std::max(maxY - minY, 1.0e-9));

    const QPointF offset(
        area.center().x() - 0.5 * (minX + maxX) * scale,
        area.center().y() + 0.5 * (minY + maxY) * scale);

    auto map = [&](const QPointF &p)
    {
        return QPointF(offset.x() + p.x() * scale, offset.y() - p.y() * scale);
    };

    // a short piece of the curve around position u (0..1)
    auto piece = [&](double u, double halfLength)
    {
        QPainterPath path;

        const int reach = std::max(2, static_cast<int>(std::ceil(halfLength * n)));
        const int centre = static_cast<int>(std::lround(u * n));

        for (int k = -reach; k <= reach; ++k)
        {
            const QPointF p = map(points.at(((centre + k) % n + n) % n));

            if (k == -reach)
                path.moveTo(p);
            else
                path.lineTo(p);
        }

        return path;
    };

    const QColor strandColor(205, 201, 192);
    const QColor overColor(91, 155, 255);
    const QColor underColor(217, 155, 66);
    const QColor doneColor(108, 192, 139);

    const double strandWidth = 2.4;
    const double gapWidth = 9.0;
    const double pieceLength = 0.025;

    // whole curve
    QPainterPath curve;
    curve.moveTo(map(points.first()));

    for (int i = 1; i < n; ++i)
        curve.lineTo(map(points.at(i)));

    curve.closeSubpath();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(strandColor, strandWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(curve);

    // crossings: the strand below is broken by the one on top
    for (const DiagramInset::Crossing &crossing : inset.crossings)
    {
        const bool active = crossing.state == 2;

        // blue: the strand that started on top, amber: the other one
        const QColor topColor = crossing.swapped ? underColor : overColor;
        const QColor bottomColor = crossing.swapped ? overColor : underColor;

        if (active)
        {
            painter.setPen(QPen(bottomColor, strandWidth + 0.8, Qt::SolidLine, Qt::RoundCap));
            painter.drawPath(piece(crossing.underU, 1.6 * pieceLength));
        }

        const QPainterPath over = piece(crossing.overU, pieceLength);

        painter.setPen(QPen(background, gapWidth, Qt::SolidLine, Qt::FlatCap));
        painter.drawPath(over);

        painter.setPen(QPen(active ? topColor : strandColor,
                            strandWidth + (active ? 0.8 : 0.0),
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawPath(over);
    }

    // markers of the crossing changes
    QFont numberFont = painter.font();
    numberFont.setPointSizeF(7.5);
    numberFont.setBold(true);
    numberFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    painter.setFont(numberFont);

    for (const DiagramInset::Crossing &crossing : inset.crossings)
    {
        if (crossing.change <= 0)
            continue;

        const QPointF c = map(crossing.position);

        const QColor color =
            crossing.state == 3 ? doneColor : underColor;

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, crossing.state == 2 ? 2.4 : 1.4));
        painter.drawEllipse(c, crossing.state == 2 ? 10.0 : 8.0, crossing.state == 2 ? 10.0 : 8.0);

        painter.setPen(color);
        painter.drawText(
            QRectF(c.x() + 7.0, c.y() - 20.0, 20.0, 14.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            QString::number(crossing.change));
    }

    // base point and walking direction
    const QPointF base = map(points.first());
    const QPointF ahead = map(points.at(std::min(n - 1, 3)));

    QPointF direction = ahead - base;
    const double length = std::hypot(direction.x(), direction.y());

    painter.setPen(QPen(QColor(20, 20, 20), 1.2));
    painter.setBrush(QColor(245, 245, 240));
    painter.drawEllipse(base, 4.5, 4.5);

    if (length > 1.0e-6)
    {
        direction /= length;

        const QPointF normal(-direction.y(), direction.x());
        const QPointF tip = base + direction * 16.0;

        QPolygonF arrow;
        arrow << tip
              << tip - direction * 7.0 + normal * 4.0
              << tip - direction * 7.0 - normal * 4.0;

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(245, 245, 240));
        painter.drawPolygon(arrow);
    }

    painter.restore();
}

void KnotGLWidget::setCrossings(
    const QVector<KnotMath::CrossingInfo> &crossings)
{
    crossings_ = crossings;

    update();
}

void KnotGLWidget::setShowCrossingNumbers(bool enabled)
{
    showCrossingNumbers_ = enabled;

    update();
}

void KnotGLWidget::setMeshColor(const QColor &color)
{
    meshColor_ = QVector3D(
        static_cast<float>(color.redF()),
        static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()));

    update();
}

void KnotGLWidget::setMaterialProperties(float metallic)
{
    metallic_ = metallic;

    update();
}

void KnotGLWidget::setSurface(int material, float roughness)
{
    material_ = std::clamp(material, 0, 3);
    roughness_ = std::clamp(roughness, 0.0f, 1.0f);

    update();
}

void KnotGLWidget::uploadMesh()
{
    if (!glInitialized_ || !context())
        return;

    destroyMeshBuffers();

    if (!mesh_.isValid() || !meshProgram_ || !meshProgram_->isLinked())
        return;

    const int vertexCount = mesh_.positions.size() / 3;

    QVector<Vertex> vertices;
    vertices.reserve(vertexCount);

    useVertexColors_ = mesh_.colors.size() == vertexCount * 3;

    const bool hasTexcoords = mesh_.texcoords.size() == vertexCount * 4;
    const bool hasOcclusion = mesh_.occlusion.size() == vertexCount;

    for (int i = 0; i < vertexCount; ++i)
    {
        Vertex vertex;

        vertex.px = mesh_.positions.at(i * 3);
        vertex.py = mesh_.positions.at(i * 3 + 1);
        vertex.pz = mesh_.positions.at(i * 3 + 2);

        vertex.nx = mesh_.normals.at(i * 3);
        vertex.ny = mesh_.normals.at(i * 3 + 1);
        vertex.nz = mesh_.normals.at(i * 3 + 2);

        if (useVertexColors_)
        {
            vertex.cr = mesh_.colors.at(i * 3);
            vertex.cg = mesh_.colors.at(i * 3 + 1);
            vertex.cb = mesh_.colors.at(i * 3 + 2);
        }
        else
        {
            vertex.cr = 1.0f;
            vertex.cg = 1.0f;
            vertex.cb = 1.0f;
        }

        vertex.t0 = hasTexcoords ? mesh_.texcoords.at(i * 4) : 1.0f;
        vertex.t1 = hasTexcoords ? mesh_.texcoords.at(i * 4 + 1) : 0.0f;
        vertex.t2 = hasTexcoords ? mesh_.texcoords.at(i * 4 + 2) : 1.0f;
        vertex.t3 = hasTexcoords ? mesh_.texcoords.at(i * 4 + 3) : 0.0f;

        vertex.ao = hasOcclusion ? mesh_.occlusion.at(i) : 1.0f;

        vertices.push_back(vertex);
    }

    meshVao_.create();
    meshVao_.bind();

    vertexBuffer_.create();
    vertexBuffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    vertexBuffer_.bind();

    vertexBuffer_.allocate(
        vertices.constData(),
        static_cast<int>(vertices.size() * sizeof(Vertex)));

    indexBuffer_.create();
    indexBuffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    indexBuffer_.bind();

    indexBuffer_.allocate(
        mesh_.indices.constData(),
        static_cast<int>(mesh_.indices.size() * sizeof(unsigned int)));

    const int stride = static_cast<int>(sizeof(Vertex));

    meshProgram_->bind();

    meshProgram_->enableAttributeArray(0);
    meshProgram_->setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);

    meshProgram_->enableAttributeArray(1);
    meshProgram_->setAttributeBuffer(
        1,
        GL_FLOAT,
        static_cast<int>(3 * sizeof(float)),
        3,
        stride);

    meshProgram_->enableAttributeArray(2);
    meshProgram_->setAttributeBuffer(
        2,
        GL_FLOAT,
        static_cast<int>(6 * sizeof(float)),
        3,
        stride);

    meshProgram_->enableAttributeArray(3);
    meshProgram_->setAttributeBuffer(
        3,
        GL_FLOAT,
        static_cast<int>(9 * sizeof(float)),
        4,
        stride);

    meshProgram_->enableAttributeArray(4);
    meshProgram_->setAttributeBuffer(
        4,
        GL_FLOAT,
        static_cast<int>(13 * sizeof(float)),
        1,
        stride);

    meshProgram_->release();

    meshVao_.release();

    indexCount_ = mesh_.indices.size();

    vertexBuffer_.release();
    indexBuffer_.release();
}

void KnotGLWidget::destroyMeshBuffers()
{
    if (meshVao_.isCreated())
        meshVao_.destroy();

    if (vertexBuffer_.isCreated())
        vertexBuffer_.destroy();

    if (indexBuffer_.isCreated())
        indexBuffer_.destroy();

    indexCount_ = 0;
}

/*
 * =========================================================
 * CAMERA
 * =========================================================
 */

QMatrix4x4 KnotGLWidget::projectionMatrix() const
{
    QMatrix4x4 projection;

    const float aspect =
        height() > 0
            ? static_cast<float>(width()) / static_cast<float>(height())
            : 1.0f;

    projection.perspective(FieldOfView, aspect, NearPlane, FarPlane);

    return projection;
}

QMatrix4x4 KnotGLWidget::viewMatrix() const
{
    QMatrix4x4 view;

    view.lookAt(
        computeCameraPosition() + panOffset_,
        panOffset_,
        QVector3D(0.0f, 1.0f, 0.0f));

    return view;
}

QMatrix4x4 KnotGLWidget::modelMatrix() const
{
    QMatrix4x4 model;

    model.setToIdentity();

    return model;
}

QVector3D KnotGLWidget::computeCameraPosition() const
{
    const float yawRadians = qDegreesToRadians(yaw_);
    const float pitchRadians = qDegreesToRadians(pitch_);

    const float cosPitch = std::cos(pitchRadians);

    return QVector3D(
        distance_ * cosPitch * std::sin(yawRadians),
        distance_ * std::sin(pitchRadians),
        distance_ * cosPitch * std::cos(yawRadians));
}

QPointF KnotGLWidget::projectPointToScreen(const KnotMath::Vec3 &point) const
{
    const QVector4D clip =
        lastViewProjection_ *
        QVector4D(
            static_cast<float>(point.x),
            static_cast<float>(point.y),
            static_cast<float>(point.z),
            1.0f);

    if (std::abs(clip.w()) < 1.0e-6f)
        return QPointF(-10000.0, -10000.0);

    const double ndcX = static_cast<double>(clip.x() / clip.w());
    const double ndcY = static_cast<double>(clip.y() / clip.w());

    return QPointF(
        (ndcX + 1.0) * 0.5 * static_cast<double>(width()),
        (1.0 - (ndcY + 1.0) * 0.5) * static_cast<double>(height()));
}

/*
 * =========================================================
 * DRAWING
 * =========================================================
 */

void KnotGLWidget::drawBackground()
{
    if (!backgroundProgram_ || !backgroundProgram_->isLinked())
        return;

    const float dpr = static_cast<float>(devicePixelRatioF());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);

    backgroundProgram_->bind();

    backgroundProgram_->setUniformValue(
        "uGrid",
        QVector2D(50.0f * dpr, std::max(1.0f, dpr)));

    // dark: #060606 rising to #0D0D0D; light: warm paper grey
    const bool dark = Theme::isDark();

    backgroundProgram_->setUniformValue(
        "uLower",
        dark ? QVector3D(0.0235f, 0.0235f, 0.0235f) : QVector3D(0.835f, 0.828f, 0.808f));

    backgroundProgram_->setUniformValue(
        "uUpper",
        dark ? QVector3D(0.0510f, 0.0510f, 0.0510f) : QVector3D(0.905f, 0.898f, 0.878f));

    backgroundProgram_->setUniformValue(
        "uLine",
        dark ? 0.012f : -0.018f);

    backgroundVao_.bind();

    glDrawArrays(GL_TRIANGLES, 0, 3);

    backgroundVao_.release();
    backgroundProgram_->release();

    glDepthMask(GL_TRUE);
}

void KnotGLWidget::drawGround(const QMatrix4x4 &viewProjection)
{
    if (!flatProgram_ || !flatProgram_->isLinked() ||
        !groundVao_.isCreated())
    {
        return;
    }

    if (groundDirty_)
        rebuildGroundGeometry();

    if (gridVertexCount_ <= 0)
        return;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    flatProgram_->bind();

    flatProgram_->setUniformValue("uViewProjection", viewProjection);
    flatProgram_->setUniformValue("uMode", 0);
    flatProgram_->setUniformValue("uPointScale", 1.0f);

    groundVao_.bind();

    // grid
    flatProgram_->setUniformValue("uFadeCenter", QVector2D(0.0f, 0.0f));

    flatProgram_->setUniformValue(
        "uFadeRadii",
        QVector2D(
            static_cast<float>(GridHalfLines) + 1.0f,
            static_cast<float>(GridHalfLines) + 1.0f));

    flatProgram_->setUniformValue("uFadeInner", 0.35f);
    flatProgram_->setUniformValue("uGridAlpha", Theme::isDark() ? 1.0f : 0.5f);

    glDrawArrays(GL_LINES, 0, gridVertexCount_);

    groundVao_.release();
    flatProgram_->release();

    drawShadow(viewProjection);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void KnotGLWidget::shadowExtent(float &cx, float &cz, float &rx, float &rz) const
{
    cx = 0.5f * (meshMin_.x() + meshMax_.x());
    cz = 0.5f * (meshMin_.z() + meshMax_.z());

    // the blur reaches beyond the footprint of the knot
    const float margin = 1.4f;

    rx = std::max(0.5f * (meshMax_.x() - meshMin_.x()), 0.8f) + margin;
    rz = std::max(0.5f * (meshMax_.z() - meshMin_.z()), 0.8f) + margin;
}

void KnotGLWidget::drawShadow(const QMatrix4x4 &viewProjection)
{
    if (!shadowProgram_ || !shadowProgram_->isLinked())
        return;

    const int samples = mesh_.centerline.size() / 4;

    if (samples == 0 || mesh_.centerlineStrand.size() != samples)
        return;

    // must match the size of uPoints in the shader
    constexpr int MaxPoints = 96;

    const int stride = (samples + MaxPoints - 1) / MaxPoints;
    const float groundY = groundLevel(meshMin_.y());

    auto centre = [this](int s)
    {
        return QVector3D(
            mesh_.centerline.at(4 * s),
            mesh_.centerline.at(4 * s + 1),
            mesh_.centerline.at(4 * s + 2));
    };

    QVector<QVector4D> points;
    points.reserve(MaxPoints);

    for (int s = 0; s < samples && points.size() < MaxPoints; s += stride)
    {
        const QVector3D c = centre(s);
        const float r = mesh_.centerline.at(4 * s + 3);

        // length of tube this point stands for
        int neighbour = s;

        if (s + 1 < samples &&
            mesh_.centerlineStrand.at(s + 1) == mesh_.centerlineStrand.at(s))
        {
            neighbour = s + 1;
        }
        else if (s > 0 &&
                 mesh_.centerlineStrand.at(s - 1) == mesh_.centerlineStrand.at(s))
        {
            neighbour = s - 1;
        }

        const float spacing =
            (neighbour != s ? (centre(neighbour) - c).length() : r) *
            static_cast<float>(stride);

        const float height = std::max(0.0f, c.y() - r - groundY);
        const float sigma = 0.9f * r + 0.35f * height;

        // A tube lying on the ground then covers about 2r/(sqrt(2 pi) sigma).
        const float weight = spacing * 2.0f * r / (2.0f * 3.14159265f);

        points.push_back(QVector4D(c.x(), c.z(), sigma, weight));
    }

    float cx = 0.0f;
    float cz = 0.0f;
    float rx = 1.0f;
    float rz = 1.0f;

    shadowExtent(cx, cz, rx, rz);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shadowProgram_->bind();

    shadowProgram_->setUniformValue("uViewProjection", viewProjection);
    shadowProgram_->setUniformValueArray("uPoints", points.constData(), points.size());
    shadowProgram_->setUniformValue("uCount", static_cast<int>(points.size()));
    shadowProgram_->setUniformValue("uFadeCenter", QVector2D(cx, cz));
    shadowProgram_->setUniformValue("uFadeRadii", QVector2D(rx, rz));
    shadowProgram_->setUniformValue("uStrength", 0.8f);

    shadowProgram_->setUniformValue(
        "uFloor",
        Theme::isDark() ? QVector3D(0.26f, 0.255f, 0.24f) : QVector3D(0.95f, 0.945f, 0.93f));

    groundVao_.bind();

    glDrawArrays(GL_TRIANGLES, gridVertexCount_, 6);

    groundVao_.release();
    shadowProgram_->release();
}

void KnotGLWidget::drawMesh(
    const QMatrix4x4 &model,
    const QMatrix4x4 &view,
    const QMatrix4x4 &projection)
{
    if (!meshProgram_ || !meshProgram_->isLinked() ||
        !mesh_.isValid() || indexCount_ <= 0)
    {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    meshProgram_->bind();

    meshProgram_->setUniformValue("uModel", model);
    meshProgram_->setUniformValue("uView", view);
    meshProgram_->setUniformValue("uProjection", projection);
    meshProgram_->setUniformValue("uNormalMatrix", model.normalMatrix());

    meshProgram_->setUniformValue(
        "uCameraPosition",
        computeCameraPosition() + panOffset_);

    meshProgram_->setUniformValue("uBaseColor", meshColor_);

    meshProgram_->setUniformValue(
        "uUseVertexColors",
        useVertexColors_ ? 1 : 0);

    meshProgram_->setUniformValue("uMetallic", metallic_);
    meshProgram_->setUniformValue("uRoughness", roughness_);
    meshProgram_->setUniformValue("uMaterial", material_);

    meshVao_.bind();

    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);

    meshVao_.release();
    meshProgram_->release();

    glDisable(GL_CULL_FACE);
}

void KnotGLWidget::drawParticles(const QMatrix4x4 &viewProjection)
{
    // Floating glow particles were removed on purpose (flat, calm look).
    (void)viewProjection;
    return;

    if (!flatProgram_ || !flatProgram_->isLinked() ||
        !particleVao_.isCreated() || particles_.isEmpty())
    {
        return;
    }

    QVector<FlatVertex> vertices;
    vertices.reserve(particles_.size());

    for (const Particle &particle : particles_)
    {
        const float phase =
            std::sin(3.14159265f * std::clamp(particle.age / particle.maxAge, 0.0f, 1.0f));

        FlatVertex vertex;

        vertex.x = particle.x;
        vertex.y = particle.y;
        vertex.z = particle.z;

        vertex.r = particle.r;
        vertex.g = particle.g;
        vertex.b = particle.b;
        vertex.a = particle.a * phase;

        vertex.size = particle.size;

        vertices.push_back(vertex);
    }

    const int count = std::min<int>(vertices.size(), MaxParticles);

    particleBuffer_.bind();

    particleBuffer_.write(
        0,
        vertices.constData(),
        static_cast<int>(count * sizeof(FlatVertex)));

    particleBuffer_.release();

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    flatProgram_->bind();

    flatProgram_->setUniformValue("uViewProjection", viewProjection);
    flatProgram_->setUniformValue("uMode", 1);

    flatProgram_->setUniformValue(
        "uPointScale",
        static_cast<float>(devicePixelRatioF()));

    flatProgram_->setUniformValue("uFadeCenter", QVector2D(0.0f, 0.0f));
    flatProgram_->setUniformValue("uFadeRadii", QVector2D(1.0f, 1.0f));
    flatProgram_->setUniformValue("uFadeInner", 1.0f);

    particleVao_.bind();

    glDrawArrays(GL_POINTS, 0, count);

    particleVao_.release();
    flatProgram_->release();

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void KnotGLWidget::drawCrossingLabels(QPainter &painter)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font = painter.font();

    font.setBold(true);
    font.setPointSize(9);

    painter.setFont(font);

    const QFontMetrics metrics(font);

    for (const KnotMath::CrossingInfo &crossing : crossings_)
    {
        const QPointF position = projectPointToScreen(crossing.midpoint);

        if (position.x() < -50.0 ||
            position.x() > static_cast<double>(width()) + 50.0 ||
            position.y() < -50.0 ||
            position.y() > static_cast<double>(height()) + 50.0)
        {
            continue;
        }

        const QColor labelColor =
            KnotMath::crossingColor(crossing.number - 1);

        const QString text = QString::number(crossing.number);

        const QRectF textRect = metrics.boundingRect(text);

        const double radius =
            std::max(
                10.0,
                std::max(textRect.width(), textRect.height()) * 0.75);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(18, 18, 18, 225));
        painter.drawEllipse(position, radius, radius);

        painter.setPen(QPen(labelColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(position, radius, radius);

        painter.setPen(Qt::white);

        const QRectF centeredRect(
            position.x() - textRect.width() * 0.5,
            position.y() - textRect.height() * 0.5 - textRect.top(),
            textRect.width(),
            textRect.height());

        painter.drawText(centeredRect, Qt::AlignCenter, text);
    }
}

void KnotGLWidget::paintGL()
{
    if (!glInitialized_)
        return;

    const float dpr = static_cast<float>(devicePixelRatioF());

    glViewport(
        0,
        0,
        static_cast<int>(static_cast<float>(width()) * dpr),
        static_cast<int>(static_cast<float>(height()) * dpr));

    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT |
        GL_STENCIL_BUFFER_BIT);

    const QMatrix4x4 model = modelMatrix();
    const QMatrix4x4 view = viewMatrix();
    const QMatrix4x4 projection = projectionMatrix();

    lastViewProjection_ = projection * view * model;

    // 1. background (no depth), 2. ground, 3. knot, 4. particles
    drawBackground();
    drawGround(lastViewProjection_);
    drawMesh(model, view, projection);
    drawParticles(lastViewProjection_);

    // Restore the state QPainter expects before using it.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    // One QPainter per frame, only for the 2D overlays.
    const bool labels = showCrossingNumbers_ && !crossings_.isEmpty();
    const bool flashes = !flashPositions_.isEmpty();

    if (labels || flashes || showInset_)
    {
        QPainter painter(this);

        if (labels)
            drawCrossingLabels(painter);

        if (flashes)
            drawFlashes(painter);

        if (showInset_)
            drawDiagramInset(painter);
    }
}

void KnotGLWidget::resizeGL(int, int)
{
    // The viewport is set in paintGL (device pixel ratio aware).
}

/*
 * =========================================================
 * ANIMATION
 * =========================================================
 */

float KnotGLWidget::randomFloat(float minimum, float maximum)
{
    std::uniform_real_distribution<float> distribution(minimum, maximum);

    return distribution(rng_);
}

void KnotGLWidget::respawnParticle(Particle &particle, bool randomAge)
{
    particle.x = randomFloat(-7.0f, 7.0f);
    particle.y = randomFloat(-4.0f, 6.0f);
    particle.z = randomFloat(-7.0f, 7.0f);

    // units per second
    particle.vx = randomFloat(-0.12f, 0.12f);
    particle.vy = randomFloat(-0.08f, 0.12f);
    particle.vz = randomFloat(-0.12f, 0.12f);

    particle.maxAge = randomFloat(6.0f, 12.0f);

    particle.age = randomAge ? randomFloat(0.0f, particle.maxAge) : 0.0f;

    const int colorType = std::uniform_int_distribution<int>(0, 2)(rng_);

    if (colorType == 0)
    {
        particle.r = 0.4f;
        particle.g = 0.6f;
        particle.b = 0.9f;
    }
    else if (colorType == 1)
    {
        particle.r = 0.3f;
        particle.g = 0.7f;
        particle.b = 0.8f;
    }
    else
    {
        particle.r = 0.6f;
        particle.g = 0.6f;
        particle.b = 0.7f;
    }

    particle.a = randomFloat(0.3f, 0.8f);
    particle.size = randomFloat(3.0f, 6.0f);
}

void KnotGLWidget::initializeParticles()
{
    particles_.clear();
    particles_.reserve(MaxParticles);

    for (int i = 0; i < MaxParticles; ++i)
    {
        Particle particle;

        respawnParticle(particle, true);

        particles_.push_back(particle);
    }
}

void KnotGLWidget::updateParticles(float dt)
{
    for (Particle &particle : particles_)
    {
        particle.x += particle.vx * dt;
        particle.y += particle.vy * dt;
        particle.z += particle.vz * dt;

        particle.age += dt;

        if (particle.age >= particle.maxAge)
            respawnParticle(particle, false);
    }
}

void KnotGLWidget::onAnimationTick()
{
    float dt = 0.033f;

    if (frameClock_.isValid())
    {
        dt = static_cast<float>(frameClock_.restart()) / 1000.0f;
        dt = std::clamp(dt, 0.0f, 0.1f);
    }
    else
    {
        frameClock_.start();
    }

    updateParticles(dt);

    if (autoRotation_)
    {
        // 18 degrees per second (the old code advanced 0.3 degrees per frame)
        yaw_ += 18.0f * dt;

        if (yaw_ > 360.0f)
            yaw_ -= 360.0f;
    }

    update();
}

void KnotGLWidget::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);

    frameClock_.invalidate();

    animationTimer_.start();
}

void KnotGLWidget::hideEvent(QHideEvent *event)
{
    animationTimer_.stop();

    QOpenGLWidget::hideEvent(event);
}

void KnotGLWidget::setAutoRotation(bool enabled)
{
    autoRotation_ = enabled;

    animationTimer_.setInterval(enabled ? 16 : 33);

    update();
}

bool KnotGLWidget::autoRotation() const
{
    return autoRotation_;
}

/*
 * =========================================================
 * INPUT
 * =========================================================
 */

void KnotGLWidget::mousePressEvent(QMouseEvent *event)
{
    lastMousePosition_ = event->position().toPoint();

    leftMouseDown_ = event->button() == Qt::LeftButton;
    rightMouseDown_ = event->button() == Qt::RightButton;

    if (leftMouseDown_ || rightMouseDown_)
        event->accept();
    else
        QOpenGLWidget::mousePressEvent(event);
}

void KnotGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint current = event->position().toPoint();

    const QPoint delta = current - lastMousePosition_;

    lastMousePosition_ = current;

    if (leftMouseDown_)
    {
        yaw_ += static_cast<float>(delta.x()) * 0.55f;
        pitch_ += static_cast<float>(delta.y()) * 0.55f;

        pitch_ = std::clamp(pitch_, -89.0f, 89.0f);

        emit cameraChanged(yaw_, pitch_, distance_);

        update();

        event->accept();

        return;
    }

    if (rightMouseDown_)
    {
        // Pan in the screen plane (camera right / up), not along world axes.
        const QVector3D forward = (-computeCameraPosition()).normalized();

        QVector3D right =
            QVector3D::crossProduct(forward, QVector3D(0.0f, 1.0f, 0.0f));

        right.normalize();

        const QVector3D up = QVector3D::crossProduct(right, forward);

        const float worldPerPixel =
            2.0f * distance_ *
            std::tan(qDegreesToRadians(FieldOfView * 0.5f)) /
            static_cast<float>(std::max(1, height()));

        panOffset_ -= right * (static_cast<float>(delta.x()) * worldPerPixel);
        panOffset_ += up * (static_cast<float>(delta.y()) * worldPerPixel);

        emit cameraChanged(yaw_, pitch_, distance_);

        update();

        event->accept();

        return;
    }

    QOpenGLWidget::mouseMoveEvent(event);
}

void KnotGLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        leftMouseDown_ = false;

    if (event->button() == Qt::RightButton)
        rightMouseDown_ = false;

    event->accept();
}

void KnotGLWidget::wheelEvent(QWheelEvent *event)
{
    const double delta =
        static_cast<double>(event->angleDelta().y()) / 120.0;

    distance_ *= static_cast<float>(std::pow(0.88, delta));

    distance_ = std::clamp(distance_, 2.2f, 30.0f);

    emit cameraChanged(yaw_, pitch_, distance_);

    update();

    event->accept();
}

void KnotGLWidget::resetCamera()
{
    yaw_ = 35.0f;
    pitch_ = 18.0f;
    distance_ = 8.0f;

    panOffset_ = QVector3D(0.0f, 0.0f, 0.0f);

    emit cameraChanged(yaw_, pitch_, distance_);

    update();
}
