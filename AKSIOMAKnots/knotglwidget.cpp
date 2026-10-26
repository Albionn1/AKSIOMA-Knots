#include "knotglwidget.h"

#include "sneaker.h"
#include "theme.h"

#include <QFontMetrics>
#include <QHash>
#include <QHideEvent>
#include <QImage>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
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
#include <limits>

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif

#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884C
#endif

#ifndef GL_TEXTURE_COMPARE_FUNC
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#endif

#ifndef GL_COMPARE_REF_TO_TEXTURE
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#endif

namespace
{

constexpr float FieldOfView = 45.0f;
constexpr float NearPlane = 0.1f;
constexpr float FarPlane = 100.0f;

constexpr int MaxParticles = 120;

// side of the shadow map in Photo mode
constexpr int PhotoShadowMapSize = 4096;

// the room blurred for roughness 0, 0.2, .. 1 (mip levels of one texture)
constexpr int SpecularLevels = 6;
constexpr int SpecularWidth = 512;

// Spacing between the ground grid lines and half size of the grid.
constexpr int GridHalfLines = 10;

float groundLevel(float meshMinY)
{
    return meshMinY - 0.3f;
}

// Direction towards the key light for a camera on +z (KeyRig in the
// shaders); the lights turn with the camera (KnotGLWidget::lightRig).
const QVector3D KeyLightRig(-0.54f, 0.71f, 0.45f);

// where the lights stay when they do not turn with the camera (the
// camera's first direction)
constexpr float FixedLightYaw = 35.0f;

// Shadow of the key light, shared by the knot and the floor shaders.
// The map holds the depth of the scene seen from the light; 16 filtered
// lookups spread over a small disk give the shadow a soft edge.
const char *ShadowGlsl = R"(
uniform vec3 uKeyDir;           // towards the key light (world)

uniform sampler2DShadow uShadowMap;
uniform mat4 uLightMatrix;      // world -> shadow map (0..1)
uniform int uShadows;
uniform float uShadowTexel;     // one texel of the map, in world units
uniform float uShadowSoftness;  // radius of the soft edge, in map units
uniform float uShadowDepthBias; // depth change across it on a 45 degree slope

// Photo mode: contact-hardening shadows (PCSS). The edge is sharp where
// the rope touches the floor and softer the farther the shadow falls,
// like the shadow of a window or a lamp of some size.
uniform sampler2D uShadowDepth; // the same map, read as plain depth
uniform int uPcss;
uniform float uLightTan;        // tangent of the light's angular radius
uniform float uDepthRange;      // world units per unit of map depth
uniform float uMapWorld;        // world units across the map

const vec2 Disk[16] = vec2[16](
    vec2(-0.942, -0.399), vec2(0.946, -0.769), vec2(-0.094, -0.929), vec2(0.345, 0.294),
    vec2(-0.916, 0.458), vec2(-0.815, -0.879), vec2(-0.383, 0.277), vec2(0.975, 0.756),
    vec2(0.443, -0.975), vec2(0.537, -0.474), vec2(-0.265, -0.419), vec2(0.792, 0.191),
    vec2(-0.242, 0.997), vec2(-0.814, 0.914), vec2(0.200, 0.786), vec2(0.144, -0.141));

// 1 = lit by the key light, 0 = in shadow (p on a surface with normal n)
float keyShadow(vec3 p, vec3 n)
{
    if (uShadows == 0)
        return 1.0;

    // The point is pushed off its surface (more where the light grazes it)
    // and the comparison allows for the slope of the surface across the
    // soft edge: no surface shadows itself.
    float cosine = clamp(abs(dot(n, uKeyDir)), 0.25, 1.0);
    float slope = sqrt(1.0 - cosine * cosine) / cosine;

    vec3 c = (uLightMatrix * vec4(p + n * uShadowTexel * (1.0 + slope), 1.0)).xyz;

    if (c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z >= 1.0)
        return 1.0;

    float depth = c.z - uShadowDepthBias * slope;
    float lit = 0.0;

    // the disk turned differently at every pixel: fine grain instead of
    // stripes along the soft edge
    float angle = 6.2831853 *
        fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    mat2 turn = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));

    if (uPcss == 0)
    {
        for (int i = 0; i < 16; ++i)
            lit += texture(uShadowMap, vec3(c.xy + turn * Disk[i] * uShadowSoftness, depth));

        return lit / 16.0;
    }

    // What lies between the point and the light: the average depth of the
    // blockers within the widest penumbra the point can have.
    float search = clamp(uLightTan * c.z * uDepthRange / uMapWorld, 2.0 * uShadowSoftness, 0.06);

    float blockers = 0.0;
    float found = 0.0;

    for (int i = 0; i < 16; ++i)
    {
        float d = texture(uShadowDepth, c.xy + turn * Disk[i] * search).r;

        if (d < depth)
        {
            blockers += d;
            found += 1.0;
        }
    }

    if (found == 0.0)
        return 1.0;

    // the penumbra grows with the distance from the blocker
    float gap = (depth - blockers / found) * uDepthRange;
    float radius = clamp(gap * uLightTan / uMapWorld, uShadowSoftness, search);

    mat2 eighth = mat2(0.7071, 0.7071, -0.7071, 0.7071);

    for (int i = 0; i < 16; ++i)
    {
        lit += texture(uShadowMap, vec3(c.xy + turn * Disk[i] * radius, depth));
        lit += texture(uShadowMap, vec3(c.xy + turn * eighth * Disk[i] * (0.5 * radius), depth));
    }

    return lit / 32.0;
}
)";

// Photo mode, shared by the knot, the table, the room and the camera: the
// room (a 360 degree photograph), its light, its shape, and the film.
const char *PhotoGlsl = R"(
uniform int uPhoto;
uniform sampler2D uEnvironment;  // the room, with mipmaps
uniform float uRoomScale;        // the pictures of the room hold light / uRoomScale
uniform sampler2D uSpecular;     // the room blurred for roughness r: mip level 5 r
uniform mat3 uEnvRotation;       // view -> room
uniform vec3 uIrradiance[9];     // the room's light from above the horizon, without the sun
uniform vec3 uKeyIrradiance;     // the sun, on a surface facing it
uniform float uKeySize;          // its size (widens its highlight)
uniform vec3 uFloorBounce;       // light the surface under the knot throws back up (pi L)

// The room's shape: the floor and walls the photograph was taken among,
// where it was taken (view units), how far its walls are and how low its
// floor lies. Parts of the room seen past the table are where they would
// be from the camera, not from the photographer's tripod.
uniform vec3 uRoomCentre;
uniform float uRoomRadius;
uniform float uRoomFloor;

const float PhotoPi = 3.14159265;

// where direction d (in the view) is in the picture of the room
vec2 roomUv(vec3 d)
{
    vec3 e = normalize(uEnvRotation * d);

    return vec2(0.5 + atan(e.x, -e.z) / (2.0 * PhotoPi),
                acos(clamp(e.y, -1.0, 1.0)) / PhotoPi);
}

// where the ray from `eye` along `d` meets the room: its floor, or the
// sphere of its walls and ceiling
vec3 roomHit(vec3 eye, vec3 d)
{
    float floorRadius2 = uRoomRadius * uRoomRadius -
                         (uRoomCentre.y - uRoomFloor) * (uRoomCentre.y - uRoomFloor);

    if (d.y < -1.0e-4)
    {
        vec3 p = eye + d * ((uRoomFloor - eye.y) / d.y);
        vec2 q = p.xz - uRoomCentre.xz;

        if (dot(q, q) < floorRadius2)
            return p;
    }

    vec3 o = eye - uRoomCentre;
    float b = dot(o, d);
    float c = dot(o, o) - uRoomRadius * uRoomRadius;

    return eye + d * (-b + sqrt(max(b * b - c, 0.0)));
}

/*
 * Light of the room (without the sun) falling on a surface facing n. From
 * above the horizon the room's own picture; below it lies the surface the
 * knot is on, an endless plane that sends up uFloorBounce / pi and fills
 * (1 - n.y) / 2 of what the surface sees.
 */
vec3 roomIrradiance(vec3 n)
{
    vec3 e = uEnvRotation * n;

    vec3 c = uIrradiance[0] * 0.282095
           + uIrradiance[1] * (0.488603 * e.y)
           + uIrradiance[2] * (0.488603 * e.z)
           + uIrradiance[3] * (0.488603 * e.x)
           + uIrradiance[4] * (1.092548 * e.x * e.y)
           + uIrradiance[5] * (1.092548 * e.y * e.z)
           + uIrradiance[6] * (0.315392 * (3.0 * e.z * e.z - 1.0))
           + uIrradiance[7] * (1.092548 * e.x * e.z)
           + uIrradiance[8] * (0.546274 * (e.x * e.x - e.y * e.y));

    return max(c, vec3(0.0)) + uFloorBounce * (0.5 - 0.5 * n.y);
}

// The room in direction d as a surface of (perceptual) roughness r sees
// it; below the horizon the surface under the knot.
vec3 roomSpecular(vec3 d, float r)
{
    vec3 c = textureLod(uSpecular, roomUv(d), 5.0 * clamp(r, 0.0, 1.0)).rgb * uRoomScale;

    return mix(c, uFloorBounce / PhotoPi, smoothstep(0.02, -0.02 - 0.3 * r, d.y));
}

// How much of the room a surface reflects (an analytic fit of the
// split-sum table, after Karis).
vec3 roomReflectance(vec3 f0, float r, float nv)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);

    vec4 k = r * c0 + c1;
    float a004 = min(k.x * k.x, exp2(-9.28 * nv)) * k.x + k.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + k.zw;

    return f0 * ab.x + ab.y;
}

// filmic tone mapping (ACES fit), display gamma and a trace of noise
// against banding
vec4 develop(vec3 c)
{
    c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
    c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));

    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);

    return vec4(c + (noise - 0.5) / 255.0, 1.0);
}

// value noise in 2D and 3D (for the fibres of the rope)
float hash12(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);

    return fract((q.x + q.y) * q.z);
}

float noise2(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}
)";

QByteArray withShadows(const char *body)
{
    return QByteArray("#version 330 core\n") + ShadowGlsl + body;
}

QByteArray withPhoto(const char *body)
{
    return QByteArray("#version 330 core\n") + ShadowGlsl + PhotoGlsl + body;
}

} // namespace

KnotGLWidget::KnotGLWidget(QWidget *parent)
    : QOpenGLWidget(parent),
      vertexBuffer_(QOpenGLBuffer::VertexBuffer),
      indexBuffer_(QOpenGLBuffer::IndexBuffer),
      groundBuffer_(QOpenGLBuffer::VertexBuffer),
      particleBuffer_(QOpenGLBuffer::VertexBuffer),
      rng_(std::random_device{}()),
      surfaceBuffer_(QOpenGLBuffer::VertexBuffer),
      shoeBuffer_(QOpenGLBuffer::VertexBuffer),
      shoeIndexBuffer_(QOpenGLBuffer::IndexBuffer),
      shoeShadeBuffer_(QOpenGLBuffer::VertexBuffer)
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

        if (shadowFramebuffer_ != 0)
            glDeleteFramebuffers(1, &shadowFramebuffer_);

        if (shadowTexture_ != 0)
            glDeleteTextures(1, &shadowTexture_);

        destroyPhotoResources();
        destroyPhotoFrame();

        if (surfaceVao_.isCreated())
            surfaceVao_.destroy();

        if (surfaceBuffer_.isCreated())
            surfaceBuffer_.destroy();

        if (depthSampler_ != 0)
            context()->extraFunctions()->glDeleteSamplers(1, &depthSampler_);

        meshProgram_.reset();
        backgroundProgram_.reset();
        flatProgram_.reset();
        shadowProgram_.reset();
        depthProgram_.reset();
        photoBackgroundProgram_.reset();
        photoFloorProgram_.reset();
        prefilterProgram_.reset();
        developProgram_.reset();
        occlusionProgram_.reset();
        shoeProgram_.reset();

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
    initializePhotoShaders();
    initializeShadowMap();
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
layout(location = 5) in float sheet;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec3 vVertexColor;
out vec4 vTexcoord;
out float vOcclusion;
out float vSheet;

void main()
{
    vec4 worldPosition = uModel * vec4(position, 1.0);

    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(uNormalMatrix * normal);
    vVertexColor = vertexColor;
    vTexcoord = texcoord;
    vOcclusion = occlusion;
    vSheet = sheet;

    gl_Position = uProjection * uView * worldPosition;
}
)";

    const char *meshFragmentShader = R"(
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec3 vVertexColor;
in vec4 vTexcoord;
in float vOcclusion;
in float vSheet;

uniform vec3 uCameraPosition;
uniform vec3 uBaseColor;
uniform int uUseVertexColors;
uniform float uMetallic;
uniform float uRoughness;
uniform int uMaterial;      // 0 glossy, 1 satin, 2 metal, 3 rope, 4 lace,
                            // 5 and 6 the real rope and lace of Photo mode
uniform int uDarkTheme;
uniform float uRopeRadius;  // Photo mode: the rope's radius (view units)

// Settings, Lighting: 1 on, 0 off; brightness (1 = normal)
uniform float uKeyOn;
uniform float uFillOn;
uniform float uRimOn;
uniform float uReflections;
uniform float uOcclusion;
uniform float uExposure;

out vec4 fragColor;

const float PI = 3.14159265;

// The lights stand around the camera like a photographer's studio, so the
// knot is always lit from the side whichever way it is turned: directions
// towards the lights for a camera on +z, turned with the camera by uRig.
// Key light high on the left, fill low on the right, rim light behind,
// two tall strip lights at the sides and a soft box overhead.
uniform mat3 uRig;

const vec3 KeyRig = vec3(-0.54, 0.71, 0.45);
const vec3 FillRig = vec3(0.78, 0.30, 0.55);
const vec3 RimRig = vec3(0.35, 0.55, -0.76);
const vec3 LeftStripRig = vec3(-0.97, 0.10, -0.22);
const vec3 RightStripRig = vec3(0.92, 0.12, -0.37);
const vec3 TopRig = vec3(0.0, 0.995, 0.10);

const vec3 KeyColor = vec3(2.1, 2.0, 1.85);
const vec3 FillColor = vec3(0.32, 0.36, 0.45);
const vec3 RimColor = vec3(1.1, 1.1, 1.2);

// roughness of the clear lacquer over the glossy material (squared)
const float CoatAlpha = 0.0225;

struct Surface
{
    vec3 albedo;
    vec3 f0;
    float a;        // roughness squared
    float metal;
    vec3 sheen;     // colour of the velvet sheen at grazing angles
    float sheenRough;
    float coat;     // strength of the clear lacquer
    float coatA;    // its roughness squared
};

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

// The normal of a surface raised by `height` (view units) along n:
// "surface gradient" bump mapping from the screen derivatives (Mikkelsen
// 2010), so no tangents are needed.
vec3 bumped(vec3 n, float height)
{
    vec3 dpdx = dFdx(vWorldPosition);
    vec3 dpdy = dFdy(vWorldPosition);

    vec3 r1 = cross(dpdy, n);
    vec3 r2 = cross(n, dpdx);

    float det = dot(dpdx, r1);

    vec3 gradient = sign(det) * (dFdx(height) * r1 + dFdy(height) * r2);

    return normalize(abs(det) * n - gradient);
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}

// "Charlie" sheen: fibres catch the light at grazing angles (cloth).
float sheenDistribution(float nh, float r)
{
    float inverse = 1.0 / r;
    float sin2 = max(1.0 - nh * nh, 0.0078125);

    return (2.0 + inverse) * pow(sin2, 0.5 * inverse) / (2.0 * PI);
}

// A rectangular soft box in direction `axis`, `size` = tangent of its half
// widths; `blur` softens its edge for rough surfaces.
float softbox(vec3 d, vec3 axis, vec2 size, float blur)
{
    float facing = dot(d, axis);

    if (facing <= 0.0)
        return 0.0;

    vec3 reference = abs(axis.y) > 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(reference, axis));
    vec3 up = cross(axis, right);

    vec2 q = vec2(dot(d, right), dot(d, up)) / facing;
    vec2 e = abs(q) - size;
    float outside = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0);

    return (1.0 - smoothstep(-blur, blur, outside)) * smoothstep(0.0, 0.3, facing);
}

// Studio surroundings for reflections: floor, walls and ceiling (lighter in
// the light theme, a brighter band of wall just above the horizon) and the
// lights as soft boxes, much brighter than the walls, so that even plastic
// (4% reflection) shows them. On a tube the strips become long highlights
// running along it, as in a product photograph. Rough surfaces see
// everything blurred; `keyLit` dims the key box where the key light is
// shadowed. `d` is in world space.
vec3 environment(vec3 d, float rough, float keyLit)
{
    vec3 e = transpose(uRig) * d;

    bool dark = uDarkTheme != 0;

    vec3 floorColor = dark ? vec3(0.010) : vec3(0.16, 0.158, 0.15);
    vec3 wallColor = dark ? vec3(0.045, 0.045, 0.050) : vec3(0.30, 0.30, 0.31);
    vec3 ceilingColor = dark ? vec3(0.09) : vec3(0.42);

    float blur = mix(0.02, 0.45, rough);

    // horizon: sharp in a mirror, soft on a rough surface
    vec3 c = mix(floorColor, wallColor, smoothstep(-0.10 - blur, 0.02 + blur, d.y));
    c = mix(c, ceilingColor, smoothstep(0.25, 0.95, d.y));
    c *= 1.0 + 0.8 * exp(-pow((d.y - 0.12) / (0.10 + blur), 2.0));

    // a blurred box is larger and dimmer (the same light spread out)
    float dim = 1.0 / (1.0 + 6.0 * blur);

    c += vec3(14.0, 13.3, 12.2) * dim * mix(1.0, keyLit, 0.8) * uKeyOn * uReflections *
         softbox(e, KeyRig, vec2(0.42, 0.30), blur);

    dim *= uReflections;

    c += vec3(4.0, 4.0, 3.9) * dim * softbox(e, TopRig, vec2(0.70, 0.40), blur);
    c += vec3(7.0, 6.8, 6.5) * dim * softbox(e, LeftStripRig, vec2(0.05, 1.20), blur);
    c += vec3(4.6, 5.0, 5.6) * dim * softbox(e, RightStripRig, vec2(0.05, 1.20), blur);
    c += vec3(3.6, 3.8, 4.2) * dim * uRimOn * softbox(e, RimRig, vec2(0.60, 0.05), blur);
    c += vec3(1.0, 1.1, 1.3) * dim * uFillOn * softbox(e, FillRig, vec2(0.50, 0.50), blur);

    return c;
}

// `size`: the light is not a point; its highlight is as wide as on a
// surface that much rougher (the energy stays the same).
vec3 directLight(vec3 n, vec3 v, vec3 l, vec3 radiance, float size, Surface s)
{
    float nl = dot(n, l);

    if (nl <= 0.0)
        return vec3(0.0);

    vec3 h = normalize(l + v);

    float nv = max(dot(n, v), 1.0e-3);
    float nh = max(dot(n, h), 0.0);
    float vh = max(dot(v, h), 0.0);

    vec3 f = fresnelSchlick(vh, s.f0);

    float a = min(s.a + size, 1.0);
    float coatA = min(s.coatA + size, 1.0);

    vec3 specular = distributionGGX(nh, a) * visibilitySmith(nv, nl, a) * f;
    vec3 diffuse = (1.0 - f) * (1.0 - s.metal) * s.albedo / PI;
    vec3 sheen = s.sheen * sheenDistribution(nh, s.sheenRough) /
                 (4.0 * (nl + nv - nl * nv));

    // the lacquer reflects some light before it reaches the colour below
    float fc = s.coat * (0.04 + 0.96 * pow(1.0 - vh, 5.0));
    float coat = fc * distributionGGX(nh, coatA) * visibilitySmith(nv, nl, coatA);

    return ((diffuse + specular + sheen) * (1.0 - fc) + coat) * radiance * nl;
}

void main()
{
    // Seifert surface: a thin painted card whatever the tube material
    bool sheet = vSheet > 0.5;

    vec3 geometric = normalize(vWorldNormal);
    vec3 n = geometric;
    vec3 v = normalize(uCameraPosition - vWorldPosition);

    if (dot(n, v) < 0.0)
        n = normalize(n - 1.01 * dot(n, v) * v);

    vec3 albedo = toLinear((uUseVertexColors != 0) ? vVertexColor : uBaseColor);

    float rough = clamp(uRoughness, 0.04, 1.0);
    float metal = clamp(uMetallic, 0.0, 1.0);
    float ao = mix(1.0, clamp(vOcclusion, 0.0, 1.0), uOcclusion);

    Surface s;

    s.sheen = vec3(0.0);
    s.sheenRough = 0.5;
    s.coat = 0.0;

    if (sheet)
    {
        // half gloss, a faint sheen at the edges
        rough = 0.55;
        metal = 0.0;
        s.sheen = 0.15 * mix(albedo, vec3(1.0), 0.5);
    }
    else if (uMaterial == 0)
    {
        // glossy plastic under a clear lacquer, as glossy as the shine
        s.coat = 0.9 * (1.0 - rough);
    }
    else if (uMaterial == 1)
    {
        s.sheen = 0.35 * mix(albedo, vec3(1.0), 0.3);
    }
    else if (uMaterial == 3)
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

        s.sheen = 0.5 * albedo * mix(0.6, 1.0, fibres);
        s.sheenRough = 0.7;
    }
    else if (uMaterial == 4)
    {
        // Braided lace: eight yarns spiral one way round the tube and eight
        // the other way, passing over and under each other in turn (a
        // plain weave). Integer multiples of the angles keep it seamless.
        float around = atan(vTexcoord.y, vTexcoord.x);
        float along = atan(vTexcoord.w, vTexcoord.z);

        float one = (8.0 * around + 16.0 * along) / (2.0 * PI);
        float other = (8.0 * around - 16.0 * along) / (2.0 * PI);

        // which yarn is on top here: they take turns from cell to cell
        bool oneOnTop = mod(floor(one) + floor(other), 2.0) < 1.0;

        float across = oneOnTop ? fract(one) : fract(other);
        float lengthwise = oneOnTop ? other : one;

        // round profile of the yarn on top, its fibres lengthwise
        float bulge = sqrt(max(sin(PI * across), 0.0));
        float fibres = 0.5 + 0.5 * cos(2.0 * PI * 5.0 * across + 0.7 * lengthwise);

        albedo *= mix(0.45, 1.0, bulge) * mix(0.88, 1.0, fibres);
        ao *= mix(0.55, 1.0, bulge);

        // cotton: matte, with a soft sheen at grazing angles
        s.sheen = 0.6 * mix(albedo, vec3(1.0), 0.2);
        s.sheenRough = 0.6;
    }
    else if (uMaterial == 5)
    {
        /*
         * Laid hemp rope (Photo mode). The strands and the grooves between
         * them are in the geometry (Photo::buildRopeMesh): vTexcoord holds
         * the place across the strand (0..1), along the rope (in radii),
         * the strand and the depth in the groove. On a strand the yarns
         * run nearly along the rope (the strand's twist undoes the
         * rope's), about seven of them side by side; their fibres run
         * along them. Both are small bumps of the surface.
         */
        float across = vTexcoord.x;
        float along = vTexcoord.y;
        float strand = vTexcoord.z;

        // constant angle around the rope: across + along * 3 / lay length
        float yarn = (across + along * 3.0 / 6.4) * 7.0;
        float yarnBump = sqrt(max(sin(PI * fract(yarn)), 0.0));

        vec2 fibreAt = vec2(yarn * 9.0, along * 1.5 + 3.0 * across);
        float fibre = 0.6 * noise2(fibreAt) + 0.4 * noise2(fibreAt * 2.7 + 13.1);

        n = bumped(n, (0.035 * yarnBump + 0.012 * fibre) * uRopeRadius);

        // natural fibre: every strand and every yarn a little different
        float tone = 1.0 + 0.06 * (strand - 1.0)
                   + 0.10 * (hash12(vec2(floor(yarn), strand)) - 0.5)
                   + 0.08 * (noise2(vec2(along * 0.12, strand * 5.0)) - 0.5);

        albedo *= tone * mix(0.72, 1.0, yarnBump) * mix(0.84, 1.0, fibre);

        // loose fibre ends catch the light at grazing angles
        s.sheen = 0.45 * albedo;
        s.sheenRough = 0.6;
    }
    else if (uMaterial == 6)
    {
        /*
         * Braided cotton lace (Photo mode): the yarns going over and
         * under each other are in the geometry; vTexcoord holds the place
         * across the yarn on top (0..1), along it (in crossings), which
         * yarn, and how deep between yarns. Every yarn is plied from
         * twisted fibres: fine slanted ridges.
         */
        float across = vTexcoord.x;
        float along = vTexcoord.y;
        float yarnId = vTexcoord.z;

        float twist = fract(along * 3.0 + across * 1.2);
        float ridge = sqrt(max(sin(PI * twist), 0.0));
        float fibre = noise2(vec2(along * 14.0, across * 5.0 + yarnId * 7.0));

        n = bumped(n, (0.02 * ridge + 0.008 * fibre) * uRopeRadius);

        albedo *= mix(0.86, 1.0, ridge) * mix(0.93, 1.0, fibre) *
                  (1.0 + 0.04 * (hash12(vec2(yarnId, 3.0)) - 0.5));

        s.sheen = 0.6 * mix(albedo, vec3(1.0), 0.2);
        s.sheenRough = 0.55;
    }

    // Where the normal turns quickly across a pixel (thin or far tubes) a
    // sharp highlight would flicker; it is widened to what the pixel sees.
    vec3 dndx = dFdx(geometric);
    vec3 dndy = dFdy(geometric);
    float kernel = min(0.5 * (dot(dndx, dndx) + dot(dndy, dndy)), 0.18);

    float a = sqrt(rough * rough * rough * rough + kernel);

    s.albedo = albedo;
    s.f0 = mix(vec3(0.04), albedo, metal);
    s.a = a;
    s.coatA = sqrt(CoatAlpha * CoatAlpha + kernel);
    s.metal = metal;

    float keyLit = keyShadow(vWorldPosition, geometric);

    if (uPhoto != 0)
    {
        /*
         * Photo mode: lit by the room. Its main light gives the diffuse
         * light, the sheen and the shadows (its reflection is part of the
         * room's picture), the rest of the room the soft light from every
         * side and the reflections.
         */
        Surface matte = s;

        matte.f0 = vec3(0.0);
        matte.coat = 0.0;

        vec3 photo = directLight(n, v, uKeyDir, uKeyIrradiance * keyLit, uKeySize, matte);

        // the real rope shadows itself (the shadow map sees its strands);
        // other tubes keep the depth cue
        bool rope = uMaterial >= 5;

        if (!rope)
            photo *= mix(1.0, ao, 0.6);

        // The room's light, less what the rest of the rope hides (the
        // occlusion is the share of the room still seen); the parts that
        // hide it are lit too and send some back (one bounce).
        photo += roomIrradiance(n) * albedo * (1.0 - metal) / PI *
                 (rope ? vec3(ao) + (1.0 - ao) * albedo : vec3(ao));

        // a little of the main light shines through the sheet from behind
        if (sheet)
        {
            float behind = max(dot(-geometric, uKeyDir), 0.0);

            photo += albedo * uKeyIrradiance * (0.08 * behind / PI) *
                     keyShadow(vWorldPosition, -geometric);
        }

        float pnv = max(dot(n, v), 1.0e-3);
        vec3 pr = reflect(-v, n);

        float occluded = clamp(pow(pnv + ao, exp2(-16.0 * a - 1.0)) - 1.0 + ao, 0.0, 1.0);

        // the reflection of the main light is shadowed with it
        occluded *= mix(1.0, keyLit, pow(max(dot(pr, uKeyDir), 0.0), 4.0));

        float coatRoom = s.coat * (0.04 + 0.96 * pow(1.0 - pnv, 5.0));

        float perceptual = sqrt(a);

        photo += roomSpecular(pr, perceptual) * roomReflectance(s.f0, perceptual, pnv) *
                 occluded * (1.0 - coatRoom) * uReflections;

        photo += roomSpecular(pr, sqrt(s.coatA)) * coatRoom * occluded * uReflections;

        // light as it is (the camera develops the frame)
        fragColor = vec4(photo, 1.0);

        return;
    }

    // direct light, partly shaded where strands are close (depth cue)
    vec3 color = vec3(0.0);

    color += directLight(n, v, uKeyDir, KeyColor * (keyLit * uKeyOn), 0.06, s);
    color += directLight(n, v, uRig * FillRig, FillColor * uFillOn, 0.08, s);
    color += directLight(n, v, uRig * RimRig, RimColor * uRimOn, 0.04, s);

    color *= mix(1.0, ao, 0.6);

    // a little of the key light shines through the sheet from behind
    if (sheet)
    {
        float behind = max(dot(-geometric, uKeyDir), 0.0);

        color += albedo * KeyColor * (0.08 * behind * uKeyOn) * keyShadow(vWorldPosition, -geometric);
    }

    // ambient: sky above, floor below (lighter in the light theme)
    bool dark = uDarkTheme != 0;

    vec3 sky = dark ? vec3(0.10, 0.10, 0.115) : vec3(0.15, 0.15, 0.16);
    vec3 ground = dark ? vec3(0.02, 0.019, 0.018) : vec3(0.07, 0.068, 0.064);
    vec3 irradiance = mix(ground, sky, 0.5 + 0.5 * n.y);

    color += irradiance * albedo * (1.0 - metal) * ao;

    // reflections of the studio; creases and shaded places reflect less
    float nv = max(dot(n, v), 0.0);
    vec3 fEnv = s.f0 + (max(vec3(1.0 - rough), s.f0) - s.f0) * pow(1.0 - nv, 5.0);
    vec3 r = reflect(-v, n);

    float specularAo = clamp(pow(nv + ao, exp2(-16.0 * a - 1.0)) - 1.0 + ao, 0.0, 1.0);
    float coatEnv = s.coat * (0.04 + 0.96 * pow(1.0 - nv, 5.0));

    color += environment(r, rough, keyLit) * fEnv * specularAo * (1.0 - coatEnv);
    color += environment(r, sqrt(s.coatA), keyLit) * coatEnv * specularAo;

    color *= uExposure;

    // filmic tone mapping (ACES fit) and display gamma
    color = (color * (2.51 * color + 0.03)) /
            (color * (2.43 * color + 0.59) + 0.14);

    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));

    // a trace of noise hides the banding of smooth gradients
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);

    fragColor = vec4(color + (noise - 0.5) / 255.0, 1.0);
}
)";

    bool meshOk = meshProgram_->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        meshVertexShader);

    meshOk = meshOk &&
             meshProgram_->addShaderFromSourceCode(
                 QOpenGLShader::Fragment,
                 withPhoto(meshFragmentShader));

    if (meshOk)
    {
        meshProgram_->bindAttributeLocation("position", 0);
        meshProgram_->bindAttributeLocation("normal", 1);
        meshProgram_->bindAttributeLocation("vertexColor", 2);
        meshProgram_->bindAttributeLocation("texcoord", 3);
        meshProgram_->bindAttributeLocation("occlusion", 4);
        meshProgram_->bindAttributeLocation("sheet", 5);

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

    // the shadow cast by the key light, lighter than the one below
    float keyLit = keyShadow(vWorld, vec3(0.0, 1.0, 0.0));

    vec3 floorColor = uFloor * (1.0 - shadow) * mix(0.55, 1.0, keyLit);

    fragColor = vec4(floorColor, alpha);
}
)";

    bool shadowOk = shadowProgram_->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        shadowVertexShader);

    shadowOk = shadowOk &&
               shadowProgram_->addShaderFromSourceCode(
                   QOpenGLShader::Fragment,
                   withShadows(shadowFragmentShader));

    if (shadowOk)
    {
        shadowProgram_->bindAttributeLocation("aPosition", 0);
        shadowProgram_->link();
    }
}

void KnotGLWidget::initializePhotoShaders()
{
    // one full-screen triangle, 0..1 across the screen
    const char *screenVertexShader = R"(
#version 330 core

out vec2 vUv;

void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));

    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

    /*
     * ---------------------------------------------------------
     * The room behind the knot: the photograph laid onto the room's shape
     * (its floor and walls, roomHit), so what is seen past the table lies
     * where it would from the camera. Sharp here: the camera blurs it.
     * ---------------------------------------------------------
     */

    const char *backgroundFragmentShader = R"(
in vec2 vUv;

uniform mat4 uInverseViewProjection;
uniform vec3 uEye;

out vec4 fragColor;

void main()
{
    vec2 ndc = vUv * 2.0 - 1.0;

    vec4 near = uInverseViewProjection * vec4(ndc, -1.0, 1.0);
    vec4 far = uInverseViewProjection * vec4(ndc, 1.0, 1.0);

    vec3 d = normalize(far.xyz / far.w - near.xyz / near.w);

    vec3 hit = roomHit(uEye, d);

    fragColor = vec4(textureLod(uEnvironment, roomUv(normalize(hit - uRoomCentre)), 0.0).rgb * uRoomScale, 1.0);
}
)";

    photoBackgroundProgram_ = std::make_unique<QOpenGLShaderProgram>();

    if (photoBackgroundProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, screenVertexShader) &&
        photoBackgroundProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                         withPhoto(backgroundFragmentShader)))
    {
        photoBackgroundProgram_->link();
    }

    /*
     * ---------------------------------------------------------
     * The surface under the knot: the table top and its sides (or the
     * room's floor), wood from colour, normal and roughness pictures at
     * their real size, lit by the sun (with the rope's shadow) and by the
     * room, less the share of the room the rope hides from each point
     * (occlusionTexture_, view factors).
     * ---------------------------------------------------------
     */

    const char *floorVertexShader = R"(
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aTangent;
layout(location = 3) in vec2 aUv;

uniform mat4 uViewProjection;

out vec3 vWorld;
out vec3 vNormal;
out vec3 vTangent;
out vec2 vUv;

void main()
{
    vWorld = aPosition;
    vNormal = aNormal;
    vTangent = aTangent;
    vUv = aUv;

    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

    const char *floorFragmentShader = R"(
in vec3 vWorld;
in vec3 vNormal;
in vec3 vTangent;
in vec2 vUv;

uniform sampler2D uFloorColor;
uniform sampler2D uFloorNormal;
uniform sampler2D uFloorRoughness;

uniform float uTile;          // millimetres per picture (vUv is in millimetres)
uniform vec3 uCameraPosition;

// share of the room the rope hides, over the square uOcclusionArea
// (x, z of its middle, half size)
uniform sampler2D uOcclusion;
uniform vec3 uOcclusionArea;
uniform int uUseOcclusion;
uniform vec3 uRopeAlbedo;

// the room's own floor takes over far away (radius 0: never)
uniform vec2 uFadeCentre;
uniform float uFadeRadius;

uniform float uReflections;

// the share of the room the sneaker leaves the floor (baked in Blender),
// over the rectangle uShoeArea (x0, z0, x1, z1)
uniform sampler2D uShoeShade;
uniform vec4 uShoeArea;
uniform int uUseShoeShade;

out vec4 fragColor;

void main()
{
    vec2 uv = vUv / uTile;

    vec3 albedo = pow(texture(uFloorColor, uv).rgb, vec3(2.2));

    // the normal picture in the frame of the face (tangent along u)
    vec3 geometric = normalize(vNormal);
    vec3 tangent = normalize(vTangent - geometric * dot(geometric, vTangent));
    vec3 bitangent = cross(tangent, geometric);

    vec3 t = texture(uFloorNormal, uv).xyz * 2.0 - 1.0;
    vec3 n = normalize(tangent * t.x + bitangent * t.y + geometric * t.z);

    // a waxed, not a lacquered finish
    float rough = clamp(0.3 + 0.7 * texture(uFloorRoughness, uv).r, 0.35, 1.0);

    vec3 v = normalize(uCameraPosition - vWorld);
    float nv = max(dot(n, v), 1.0e-3);

    float hidden = 0.0;

    if (uUseOcclusion != 0)
    {
        vec2 q = (vWorld.xz - uOcclusionArea.xy) / uOcclusionArea.z * 0.5 + 0.5;

        if (q.x > 0.0 && q.x < 1.0 && q.y > 0.0 && q.y < 1.0 && geometric.y > 0.5)
            hidden = texture(uOcclusion, q).r;
    }

    // what the rope hides is replaced by the rope itself, lit by the room
    float seen = 1.0 - hidden + hidden * 0.5 * dot(uRopeAlbedo, vec3(0.2126, 0.7152, 0.0722));

    if (uUseShoeShade != 0)
    {
        vec2 s = (vWorld.xz - uShoeArea.xy) / (uShoeArea.zw - uShoeArea.xy);

        if (s.x > 0.0 && s.x < 1.0 && s.y > 0.0 && s.y < 1.0 && geometric.y > 0.5)
        {
            float open = texture(uShoeShade, s).r;

            seen *= open;
            hidden = 1.0 - (1.0 - hidden) * open;
        }
    }

    float sunLit = keyShadow(vWorld, geometric);

    vec3 light =
        uKeyIrradiance * max(dot(n, uKeyDir), 0.0) * sunLit +
        roomIrradiance(n) * seen;

    vec3 color = albedo * light / PhotoPi;

    vec3 r = reflect(-v, n);

    color += roomSpecular(r, rough) * roomReflectance(vec3(0.04), rough, nv) *
             (1.0 - hidden) * mix(1.0, sunLit, pow(max(dot(r, uKeyDir), 0.0), 4.0)) * uReflections;

    float fade = uFadeRadius > 0.0
        ? 1.0 - smoothstep(0.5, 1.0, length(vWorld.xz - uFadeCentre) / uFadeRadius)
        : 1.0;

    if (fade <= 0.002)
        discard;

    fragColor = vec4(color, fade);
}
)";

    photoFloorProgram_ = std::make_unique<QOpenGLShaderProgram>();

    if (photoFloorProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, floorVertexShader) &&
        photoFloorProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                    withPhoto(floorFragmentShader)))
    {
        photoFloorProgram_->bindAttributeLocation("aPosition", 0);
        photoFloorProgram_->bindAttributeLocation("aNormal", 1);
        photoFloorProgram_->bindAttributeLocation("aTangent", 2);
        photoFloorProgram_->bindAttributeLocation("aUv", 3);
        photoFloorProgram_->link();
    }

    /*
     * ---------------------------------------------------------
     * The camera. A thin lens focused on the knot blurs every point by
     * its circle of confusion, radius
     *     c = f^2 / (2 N (S - f)) |D - S| / D
     * (f focal length, N f-number, S focus distance, D the point's
     * distance), in pixels uCocScale |D - S| / D. Each pixel gathers the
     * points whose circles cover it (a point behind it spreads no more
     * than its own circle, so the sharp rope keeps its edges). Then the
     * exposure and the film.
     * ---------------------------------------------------------
     */

    const char *developFragmentShader = R"(
in vec2 vUv;

uniform sampler2D uColor;   // the frame, light as it is, with mipmaps
uniform sampler2D uDepth;
uniform vec2 uTexel;        // one pixel in texture coordinates

uniform mat4 uInverseProjection;
uniform mat4 uInverseView;
uniform vec3 uEye;

uniform float uFocus;       // distance in focus (along the view)
uniform float uCocScale;
uniform float uMaxCoc;
uniform float uExposure;

out vec4 fragColor;

// distance of what is seen at uv along the view; the room's distance from
// its shape where only the room is seen
float distanceAt(vec2 uv)
{
    float depth = texture(uDepth, uv).r;

    vec2 ndc = uv * 2.0 - 1.0;

    if (depth >= 0.999999)
    {
        vec4 p = uInverseProjection * vec4(ndc, 1.0, 1.0);
        vec3 dirView = normalize(p.xyz / p.w);
        vec3 dir = mat3(uInverseView) * dirView;

        vec3 hit = roomHit(uEye, dir);

        return max(-dirView.z, 1.0e-3) * length(hit - uEye);
    }

    vec4 p = uInverseProjection * vec4(ndc, depth * 2.0 - 1.0, 1.0);

    return -p.z / p.w;
}

float circleAt(float d)
{
    return min(uCocScale * abs(d - uFocus) / max(d, 1.0e-4), uMaxCoc);
}

void main()
{
    float centreDistance = distanceAt(vUv);
    float centreCircle = circleAt(centreDistance);

    vec3 sum = textureLod(uColor, vUv, 0.0).rgb / max(centreCircle * centreCircle, 1.0);
    float weight = 1.0 / max(centreCircle * centreCircle, 1.0);

    if (centreCircle > 0.5)
    {
        const int Taps = 48;

        float radius = centreCircle;

        // samples further apart than a pixel read a blurred level
        float spacing = radius * 1.77 / sqrt(float(Taps));
        float level = max(log2(spacing), 0.0);

        float turn = 6.2831853 *
            fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

        for (int i = 0; i < Taps; ++i)
        {
            float r = radius * sqrt((float(i) + 0.5) / float(Taps));
            float a = float(i) * 2.3999632 + turn;

            vec2 uv = vUv + vec2(cos(a), sin(a)) * r * uTexel;

            float d = distanceAt(uv);
            float circle = circleAt(d);

            // what lies behind spreads no more than this pixel's own blur
            if (d > centreDistance)
                circle = min(circle, centreCircle);

            float cover = clamp(circle - r + 1.0, 0.0, 1.0);
            float w = cover / max(circle * circle, 1.0);

            sum += textureLod(uColor, uv, level).rgb * w;
            weight += w;
        }
    }

    fragColor = develop(sum / weight * uExposure);
}
)";

    developProgram_ = std::make_unique<QOpenGLShaderProgram>();

    if (developProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, screenVertexShader) &&
        developProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                 withPhoto(developFragmentShader)))
    {
        developProgram_->link();
    }

    /*
     * ---------------------------------------------------------
     * How much of the room the table sees past the rope: the view factor
     * of the rope (cylinders of radius r around its centre line) from
     * every point of the table around the knot,
     *     dF = 2 r |s x d| (d . up) ds / (pi D^2)
     * per piece of centre line (direction s, unit direction d to it,
     * distance D); for a straight rope lying on the table it adds up to
     * exactly r^2 / (x^2 + r^2) at a distance x from where it touches.
     * ---------------------------------------------------------
     */

    const char *occlusionFragmentShader = R"(
#version 330 core

in vec2 vUv;

uniform sampler2D uSegments;   // centre line points (x, y, z, -), one row
uniform int uCount;            // points; the last piece joins back if closed
uniform int uClosed;
uniform vec3 uArea;            // x, z of the middle, half size
uniform float uTableY;
uniform float uRadius;

out vec4 fragColor;

void main()
{
    vec3 p = vec3(uArea.x + (vUv.x * 2.0 - 1.0) * uArea.z,
                  uTableY,
                  uArea.y + (vUv.y * 2.0 - 1.0) * uArea.z);

    float reach = 20.0 * uRadius;
    float hidden = 0.0;

    int pieces = uClosed != 0 ? uCount : uCount - 1;

    for (int i = 0; i < pieces; ++i)
    {
        vec3 a = texelFetch(uSegments, ivec2(i, 0), 0).xyz;
        vec3 b = texelFetch(uSegments, ivec2((i + 1) % uCount, 0), 0).xyz;

        vec3 d = 0.5 * (a + b) - p;
        float distance = length(d);

        if (distance > reach)
            continue;

        d /= max(distance, 1.0e-6);

        if (d.y <= 0.0)
            continue;

        vec3 s = b - a;
        float ds = length(s);

        if (ds < 1.0e-6)
            continue;

        float sine = length(cross(s / ds, d));
        float clamped = max(distance, uRadius);

        hidden += 2.0 * uRadius * sine * d.y * ds / (3.14159265 * clamped * clamped);
    }

    fragColor = vec4(min(hidden, 0.97), 0.0, 0.0, 1.0);
}
)";

    occlusionProgram_ = std::make_unique<QOpenGLShaderProgram>();

    if (occlusionProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, screenVertexShader) &&
        occlusionProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, occlusionFragmentShader))
    {
        occlusionProgram_->link();
    }

    /*
     * ---------------------------------------------------------
     * The room blurred for a roughness: what a surface of that roughness
     * reflects in each direction. Directions are drawn the way a rough
     * surface scatters them (GGX importance sampling, viewed head-on),
     * each read from a level of the picture as blurred as the share of the
     * sphere it stands for, so 512 of them give a smooth result.
     * ---------------------------------------------------------
     */

    const char *prefilterFragmentShader = R"(
#version 330 core

in vec2 vUv;

uniform sampler2D uSource;
uniform float uRoughness;
uniform float uSourceTexels;  // texels of the full picture

out vec4 fragColor;

const float PI = 3.14159265;

vec3 directionAt(vec2 uv)
{
    float phi = (uv.x - 0.5) * 2.0 * PI;
    float theta = uv.y * PI;

    return vec3(sin(theta) * sin(phi), cos(theta), -sin(theta) * cos(phi));
}

vec2 uvOf(vec3 d)
{
    return vec2(0.5 + atan(d.x, -d.z) / (2.0 * PI), acos(clamp(d.y, -1.0, 1.0)) / PI);
}

float radicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

    return float(bits) * 2.3283064365386963e-10;
}

void main()
{
    vec3 n = directionAt(vUv);

    // a mirror: the picture itself, at about the size of this level
    if (uRoughness <= 0.0)
    {
        fragColor = vec4(textureLod(uSource, vUv, 2.0).rgb, 1.0);
        return;
    }

    float a = uRoughness * uRoughness;

    vec3 helper = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, n));
    vec3 bitangent = cross(n, tangent);

    const uint Count = 512u;

    vec3 sum = vec3(0.0);
    float weight = 0.0;

    for (uint i = 0u; i < Count; ++i)
    {
        vec2 xi = vec2(float(i) / float(Count), radicalInverse(i));

        float phi = 2.0 * PI * xi.x;
        float cosine = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sine = sqrt(1.0 - cosine * cosine);

        vec3 h = tangent * (sine * cos(phi)) + bitangent * (sine * sin(phi)) + n * cosine;
        vec3 l = 2.0 * dot(n, h) * h - n;

        float nl = dot(n, l);

        if (nl <= 0.0)
            continue;

        // probability of this direction, and the share of the sphere it
        // stands for compared with one texel
        float d = a * a / (PI * pow(cosine * cosine * (a * a - 1.0) + 1.0, 2.0));
        float pdf = d / 4.0;

        float share = 1.0 / (float(Count) * pdf + 1.0e-6);
        float texel = 4.0 * PI / uSourceTexels;

        float level = max(0.5 * log2(share / texel) + 1.0, 0.0);

        sum += textureLod(uSource, uvOf(l), level).rgb * nl;
        weight += nl;
    }

    fragColor = vec4(sum / max(weight, 1.0e-4), 1.0);
}
)";

    prefilterProgram_ = std::make_unique<QOpenGLShaderProgram>();

    if (prefilterProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, screenVertexShader) &&
        prefilterProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, prefilterFragmentShader))
    {
        prefilterProgram_->link();
    }

    /*
     * ---------------------------------------------------------
     * The sneaker: woven canvas, rubber, metal eyelets and the lining,
     * lit like the table (the sun with its shadows, the room less what
     * the shoe itself and the lace hide from each point, the room's
     * reflections). Positions in millimetres, scaled into the view.
     * ---------------------------------------------------------
     */

    const char *shoeVertexShader = R"(
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aOcclusion;
layout(location = 3) in float aMaterial;
layout(location = 4) in float aShade;

uniform mat4 uViewProjection;
uniform float uScale;          // view units per millimetre

out vec3 vWorld;
out vec3 vMm;
out vec3 vNormal;
out float vOpen;
flat out int vMaterial;

void main()
{
    vMm = aPosition;
    vWorld = aPosition * uScale;
    vNormal = aNormal;

    // the room seen past the shoe (baked) and past the lace
    vOpen = aOcclusion * (1.0 - aShade);
    vMaterial = int(aMaterial + 0.5);

    gl_Position = uViewProjection * vec4(vWorld, 1.0);
}
)";

    const char *shoeFragmentShader = R"(
in vec3 vWorld;
in vec3 vMm;
in vec3 vNormal;
in float vOpen;
flat in int vMaterial;

uniform vec3 uCameraPosition;
uniform vec3 uCanvas;          // the canvas's colour (linear)
uniform float uReflections;

out vec4 fragColor;

float ggx(float nh, float a)
{
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;

    return a2 / (PhotoPi * d * d);
}

float smith(float nv, float nl, float a)
{
    float k = a * 0.5;

    return 0.25 / ((nv * (1.0 - k) + k) * (nl * (1.0 - k) + k));
}

// the canvas's threads (0.5 mm apart each way, over and under), seen
// across the face; faded out where a pixel covers several of them
float weave(vec3 p, vec3 n, out vec2 slope)
{
    vec3 w = abs(n);
    vec2 uv = w.x > w.y && w.x > w.z ? p.zy : (w.y > w.z ? p.xz : p.xy);

    vec2 t = uv / 0.5;
    vec2 f = fract(t) - 0.5;

    // the thread on top in this cell: warp or weft
    bool warp = mod(floor(t.x) + floor(t.y), 2.0) < 1.0;

    float across = warp ? f.x : f.y;
    float h = cos(3.14159 * across);

    float fade = 1.0 - smoothstep(0.08, 0.3, length(fwidth(uv)));

    slope = (warp ? vec2(-sin(3.14159 * f.x), 0.0) : vec2(0.0, -sin(3.14159 * f.y))) * 0.35 * fade;

    return mix(1.0, 0.86 + 0.14 * h, fade);
}

void main()
{
    vec3 geometric = normalize(vNormal);
    vec3 n = geometric;

    vec3 v = normalize(uCameraPosition - vWorld);

    // seen from inside (the lining in the opening), the face looks back
    if (dot(n, v) < 0.0)
        n = -n;

    vec3 albedo;
    float rough;
    float metal = 0.0;
    float sheen = 0.0;

    // slow changes of the cloth's colour, a few millimetres across
    float cloth = noise2(vMm.xz * 0.35 + vMm.y * 0.2) * 0.6 + noise2(vMm.xy * 1.3) * 0.4;

    if (vMaterial == 0 || vMaterial == 5)
    {
        vec2 slope;
        float threads = weave(vMm, geometric, slope);

        albedo = uCanvas * threads * (0.9 + 0.2 * cloth);
        rough = 0.85;
        sheen = 0.25;

        vec3 helper = abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 t1 = normalize(cross(helper, n));
        vec3 t2 = cross(n, t1);

        n = normalize(n + t1 * slope.x + t2 * slope.y);
    }
    else if (vMaterial == 1)
    {
        // the rubber sole, with a stripe of the canvas's colour round it
        // and a thin one above; its underside worn grey
        albedo = vec3(0.78, 0.76, 0.71);
        rough = 0.6;

        float side = 1.0 - smoothstep(0.3, 0.6, abs(geometric.y));

        if (vMm.y > 13.0 && vMm.y < 16.0 && side > 0.5)
            albedo = uCanvas * 1.3;
        else if (vMm.y > 17.2 && vMm.y < 18.0 && side > 0.5)
            albedo = vec3(0.45, 0.06, 0.05);

        if (geometric.y < -0.5)
            albedo = vec3(0.35, 0.34, 0.32);

        albedo *= 0.94 + 0.06 * cloth;
    }
    else if (vMaterial == 2)
    {
        // nickel-plated brass
        albedo = vec3(0.86, 0.85, 0.82);
        rough = 0.28;
        metal = 1.0;
    }
    else if (vMaterial == 4)
    {
        albedo = vec3(0.80, 0.78, 0.73) * (0.95 + 0.05 * cloth);
        rough = 0.5;
    }
    else
    {
        // the lining
        albedo = vec3(0.42, 0.40, 0.37) * (0.9 + 0.2 * cloth);
        rough = 0.9;
        sheen = 0.15;
    }

    float nv = max(dot(n, v), 1.0e-3);

    vec3 l = uKeyDir;
    float nl = max(dot(n, l), 0.0);

    float sunLit = keyShadow(vWorld, geometric);

    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 diffuse = albedo * (1.0 - metal);

    // the sun
    vec3 h = normalize(l + v);
    float nh = max(dot(n, h), 0.0);
    float a = max(rough * rough, 0.02);

    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    vec3 specular = fresnel * ggx(nh, a) * smith(nv, nl, a);

    vec3 color = (diffuse / PhotoPi + specular) * uKeyIrradiance * nl * sunLit;

    // the room
    color += diffuse * roomIrradiance(n) * vOpen / PhotoPi;

    vec3 r = reflect(-v, n);

    color += roomSpecular(r, rough) * roomReflectance(f0, rough, nv) * vOpen * uReflections;

    // the soft glow of cloth at grazing angles
    color += sheen * pow(1.0 - nv, 4.0) * roomIrradiance(n) * vOpen * albedo * 2.0 / PhotoPi;

    fragColor = vec4(color, 1.0);
}
)";

    shoeProgram_ = std::make_unique<QOpenGLShaderProgram>();

    if (shoeProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, shoeVertexShader) &&
        shoeProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, withPhoto(shoeFragmentShader)))
    {
        shoeProgram_->bindAttributeLocation("aPosition", 0);
        shoeProgram_->bindAttributeLocation("aNormal", 1);
        shoeProgram_->bindAttributeLocation("aOcclusion", 2);
        shoeProgram_->bindAttributeLocation("aMaterial", 3);
        shoeProgram_->bindAttributeLocation("aShade", 4);
        shoeProgram_->link();
    }

    // the shadow map read as plain depth (no comparison)
    QOpenGLExtraFunctions *extra = context()->extraFunctions();

    extra->glGenSamplers(1, &depthSampler_);
    extra->glSamplerParameteri(depthSampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    extra->glSamplerParameteri(depthSampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    extra->glSamplerParameteri(depthSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    extra->glSamplerParameteri(depthSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    extra->glSamplerParameteri(depthSampler_, GL_TEXTURE_COMPARE_MODE, GL_NONE);
}

void KnotGLWidget::initializeShadowMap()
{
    depthProgram_ = std::make_unique<QOpenGLShaderProgram>();

    const char *depthVertexShader = R"(
#version 330 core

layout(location = 0) in vec3 position;

uniform mat4 uLightViewProjection;

void main()
{
    gl_Position = uLightViewProjection * vec4(position, 1.0);
}
)";

    // only the depth is written
    const char *depthFragmentShader = R"(
#version 330 core

void main()
{
}
)";

    bool depthOk = depthProgram_->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        depthVertexShader);

    depthOk = depthOk &&
              depthProgram_->addShaderFromSourceCode(
                  QOpenGLShader::Fragment,
                  depthFragmentShader);

    if (depthOk)
    {
        depthProgram_->bindAttributeLocation("position", 0);
        depthOk = depthProgram_->link();
    }

    // depth texture compared in the shaders (sampler2DShadow); linear
    // filtering blends four comparisons for free
    glGenTextures(1, &shadowTexture_);

    allocateShadowMap();

    glBindTexture(GL_TEXTURE_2D, shadowTexture_);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &shadowFramebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer_);

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D,
        shadowTexture_,
        0);

    // no colour attachment
    const GLenum none = GL_NONE;

    QOpenGLExtraFunctions *extra = context()->extraFunctions();

    extra->glDrawBuffers(1, &none);
    extra->glReadBuffer(GL_NONE);

    shadowsReady_ =
        depthOk &&
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
}

void KnotGLWidget::allocateShadowMap()
{
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_DEPTH_COMPONENT24,
        shadowMapSize_,
        shadowMapSize_,
        0,
        GL_DEPTH_COMPONENT,
        GL_UNSIGNED_INT,
        nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void KnotGLWidget::drawShadowMap()
{
    if (!shadowsReady_ || !mesh_.isValid() || indexCount_ <= 0)
        return;

    // nothing casts a shadow without the key light
    if (!lighting_.keyLight || (!lighting_.knotShadows && !lighting_.floorShadow))
        return;

    // The light looks at the bounding sphere of the knot from the key
    // light's direction; the depth range reaches the floor below it.
    const QVector3D centre = 0.5f * (meshMin_ + meshMax_);
    const float radius = 1.05f * std::max(0.5f * (meshMax_ - meshMin_).length(), 0.5f);

    // In Photo mode the light may be low (a window): its shadows run far
    // across the floor, so the depth range reaches much farther.
    const float nearPlane = 0.5f * radius;
    const float farPlane = (photo_ ? 14.0f : 6.0f) * radius + 1.0f;

    const QVector3D toLight =
        photo_ ? photoKeyDirection() : lightRig().map(KeyLightRig);

    QMatrix4x4 lightView;

    lightView.lookAt(
        centre + toLight * (2.0f * radius),
        centre,
        std::abs(toLight.y()) > 0.98f ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 1.0f, 0.0f));

    QMatrix4x4 lightProjection;

    lightProjection.ortho(-radius, radius, -radius, radius, nearPlane, farPlane);

    const QMatrix4x4 lightViewProjection = lightProjection * lightView;

    // clip space (-1..1) -> map coordinates (0..1)
    QMatrix4x4 toMap;

    toMap.translate(0.5f, 0.5f, 0.5f);
    toMap.scale(0.5f);

    lightMatrix_ = toMap * lightViewProjection;

    // The soft edge is about half a tube radius wide (at least a few
    // texels), like a key light of some size.
    shadowTexel_ = 2.0f * radius / static_cast<float>(shadowMapSize_);

    const float tubeRadius =
        mesh_.centerline.size() >= 4 ? mesh_.centerline.at(3) : 0.05f * radius;

    const float softness =
        std::max(0.5f * tubeRadius * static_cast<float>(lighting_.shadowSoftness) / 100.0f,
                 2.5f * shadowTexel_);

    shadowSoftness_ = softness / (2.0f * radius);
    shadowDepthBias_ = 1.25f * softness / (farPlane - nearPlane);

    shadowDepthRange_ = farPlane - nearPlane;
    shadowMapWorld_ = 2.0f * radius;

    // Photo mode: the softness is set by the size of the light (PCSS);
    // this is only the sharpest edge, where the rope touches the floor
    if (photo_)
    {
        shadowSoftness_ = 1.5f * shadowTexel_ / (2.0f * radius);
        shadowDepthBias_ = 3.0f * shadowTexel_ / (farPlane - nearPlane);
    }

    // not bound for reading while it is drawn into
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer_);
    glViewport(0, 0, shadowMapSize_, shadowMapSize_);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);

    // both sides (the Seifert surface is a sheet), pushed back a little
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    depthProgram_->bind();
    depthProgram_->setUniformValue("uLightViewProjection", lightViewProjection);

    meshVao_.bind();

    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);

    meshVao_.release();

    // the sneaker casts its shadow too (on the floor and on the lace)
    if (showShoe())
    {
        QMatrix4x4 scale;

        scale.scale(worldPerMm());

        depthProgram_->setUniformValue("uLightViewProjection", lightViewProjection * scale);

        shoeVao_.bind();

        glDrawElements(GL_TRIANGLES, shoeIndexCount_, GL_UNSIGNED_INT, nullptr);

        shoeVao_.release();
    }

    depthProgram_->release();

    glDisable(GL_POLYGON_OFFSET_FILL);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
}

void KnotGLWidget::setShadowUniforms(QOpenGLShaderProgram &program, bool wanted)
{
    const bool shadows =
        wanted && lighting_.keyLight &&
        shadowsReady_ && mesh_.isValid() && indexCount_ > 0;

    // the map is on texture unit 1, and again on unit 2 as plain depth
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadows ? shadowTexture_ : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadows ? shadowTexture_ : 0);
    glActiveTexture(GL_TEXTURE0);

    context()->extraFunctions()->glBindSampler(2, depthSampler_);

    program.setUniformValue(
        "uKeyDir",
        photo_ ? photoKeyDirection() : lightRig().map(KeyLightRig));

    program.setUniformValue("uShadowMap", 1);
    program.setUniformValue("uShadowDepth", 2);
    program.setUniformValue("uShadows", shadows ? 1 : 0);
    program.setUniformValue("uPcss", photo_ ? 1 : 0);
    program.setUniformValue("uLightTan", std::tan(environment_.sunRadius));
    program.setUniformValue("uDepthRange", shadowDepthRange_);
    program.setUniformValue("uMapWorld", shadowMapWorld_);
    program.setUniformValue("uLightMatrix", lightMatrix_);
    program.setUniformValue("uShadowTexel", shadowTexel_);
    program.setUniformValue("uShadowSoftness", shadowSoftness_);
    program.setUniformValue("uShadowDepthBias", shadowDepthBias_);
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

    groundBytes_ =
        static_cast<qint64>(vertices.size()) * static_cast<qint64>(sizeof(FlatVertex));

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

    // the sneaker under the lace (its sole on the floor)
    if (showShoe())
    {
        const Photo::Sneaker &shoe = Photo::sneaker();
        const float mm = worldPerMm();

        for (const QVector3D &corner : {shoe.minimum * mm, shoe.maximum * mm})
        {
            minimum = QVector3D(std::min(minimum.x(), corner.x()), std::min(minimum.y(), corner.y()),
                                std::min(minimum.z(), corner.z()));
            maximum = QVector3D(std::max(maximum.x(), corner.x()), std::max(maximum.y(), corner.y()),
                                std::max(maximum.z(), corner.z()));
        }
    }

    meshMin_ = minimum;
    meshMax_ = maximum;

    tableLevel_ = minimum.y();
}

float KnotGLWidget::meshBottom() const
{
    float bottom = 1.0e30f;

    const int vertexCount = mesh_.positions.size() / 3;

    for (int i = 0; i < vertexCount; ++i)
        bottom = std::min(bottom, mesh_.positions.at(3 * i + 1));

    return vertexCount > 0 ? bottom : meshMin_.y();
}

void KnotGLWidget::setMesh(const KnotMath::Mesh &mesh)
{
    rawMesh_ = mesh;

    // a moving tube, not the rope at rest (the pose stays)
    restingRope_ = false;
    surfaceDirty_ = true;
    occlusionDirty_ = true;

    // laid down in Photo mode (the pose stays until the next knot)
    applyPose();

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
    rawFixedMin_ = minimum;
    rawFixedMax_ = maximum;

    poseFixedBounds();

    tableLevel_ = meshBottom();

    groundDirty_ = true;
    update();
}

void KnotGLWidget::poseFixedBounds()
{
    if (!photo_)
    {
        meshMin_ = rawFixedMin_;
        meshMax_ = rawFixedMax_;

        return;
    }

    // the box around the eight corners of the turned box
    QVector3D minimum(1.0e30f, 1.0e30f, 1.0e30f);
    QVector3D maximum(-1.0e30f, -1.0e30f, -1.0e30f);

    for (int corner = 0; corner < 8; ++corner)
    {
        const QVector3D p = posed(QVector3D(
            (corner & 1) ? rawFixedMax_.x() : rawFixedMin_.x(),
            (corner & 2) ? rawFixedMax_.y() : rawFixedMin_.y(),
            (corner & 4) ? rawFixedMax_.z() : rawFixedMin_.z()));

        minimum = QVector3D(std::min(minimum.x(), p.x()), std::min(minimum.y(), p.y()), std::min(minimum.z(), p.z()));
        maximum = QVector3D(std::max(maximum.x(), p.x()), std::max(maximum.y(), p.y()), std::max(maximum.z(), p.z()));
    }

    meshMin_ = minimum;
    meshMax_ = maximum;
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

void KnotGLWidget::setLighting(const Lighting::Options &options)
{
    lighting_ = options;

    normalShadowMapSize_ = Lighting::shadowMapSize(options.shadowQuality);

    // Photo mode always uses the sharpest map
    const int size = photo_ ? PhotoShadowMapSize : normalShadowMapSize_;

    if (size != shadowMapSize_)
    {
        shadowMapSize_ = size;

        if (glInitialized_ && shadowTexture_ != 0)
        {
            makeCurrent();

            allocateShadowMap();

            doneCurrent();
        }
    }

    update();
}

const Lighting::Options &KnotGLWidget::lighting() const
{
    return lighting_;
}

KnotGLWidget::MemoryUse KnotGLWidget::memoryUse() const
{
    MemoryUse use;

    if (!glInitialized_)
        return use;

    use.mesh = meshBytes_;

    // depth, 4 bytes per texel
    use.shadowMap =
        shadowsReady_
            ? static_cast<qint64>(shadowMapSize_) * shadowMapSize_ * 4
            : 0;

    // The view is drawn into a framebuffer with several samples per pixel
    // (colour and depth/stencil, 4 bytes each), then resolved into a
    // texture of one colour per pixel.
    const double dpr = devicePixelRatioF();

    const qint64 pixels =
        static_cast<qint64>(width() * dpr) * static_cast<qint64>(height() * dpr);

    use.view = pixels * (std::max(1, format().samples()) * 8 + 4);

    use.other =
        groundBytes_ + photoBytes_ + photoFrameBytes_ +
        static_cast<qint64>(MaxParticles) * static_cast<qint64>(sizeof(FlatVertex));

    return use;
}

void KnotGLWidget::setSurface(int material, float roughness)
{
    material_ = std::clamp(material, 0, 4);
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
    const bool hasSheet = mesh_.sheet.size() == vertexCount;

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
        vertex.sheet = hasSheet ? mesh_.sheet.at(i) : 0.0f;

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

    meshProgram_->enableAttributeArray(5);
    meshProgram_->setAttributeBuffer(
        5,
        GL_FLOAT,
        static_cast<int>(14 * sizeof(float)),
        1,
        stride);

    meshProgram_->release();

    meshVao_.release();

    indexCount_ = mesh_.indices.size();

    meshBytes_ =
        static_cast<qint64>(vertices.size()) * static_cast<qint64>(sizeof(Vertex)) +
        static_cast<qint64>(indexCount_) * static_cast<qint64>(sizeof(unsigned int));

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
    meshBytes_ = 0;
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

QMatrix4x4 KnotGLWidget::lightRig() const
{
    // the camera's turn about the vertical (it sits on +z at yaw 0)
    QMatrix4x4 rig;

    rig.rotate(lighting_.followCamera ? yaw_ : FixedLightYaw, 0.0f, 1.0f, 0.0f);

    return rig;
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
    // the point is in the knot's own coordinates (laid down in Photo mode)
    const QVector4D clip =
        lastViewProjection_ *
        QVector4D(
            posed(QVector3D(
                static_cast<float>(point.x),
                static_cast<float>(point.y),
                static_cast<float>(point.z))),
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

    if (lighting_.floorGrid)
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

    const QVector<QVector4D> points = contactShadowPoints(groundLevel(meshMin_.y()));

    if (points.isEmpty())
        return;

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
    shadowProgram_->setUniformValue("uStrength", lighting_.contactShadow ? 0.8f : 0.0f);

    setShadowUniforms(*shadowProgram_, lighting_.floorShadow);

    shadowProgram_->setUniformValue(
        "uFloor",
        Theme::isDark() ? QVector3D(0.26f, 0.255f, 0.24f) : QVector3D(0.95f, 0.945f, 0.93f));

    groundVao_.bind();

    glDrawArrays(GL_TRIANGLES, gridVertexCount_, 6);

    groundVao_.release();
    shadowProgram_->release();
}

QVector<QVector4D> KnotGLWidget::contactShadowPoints(float groundY) const
{
    QVector<QVector4D> points;

    const int samples = mesh_.centerline.size() / 4;

    if (samples == 0 || mesh_.centerlineStrand.size() != samples)
        return points;

    // must match the size of uPoints in the shaders
    constexpr int MaxPoints = 96;

    const int stride = (samples + MaxPoints - 1) / MaxPoints;

    auto centre = [this](int s)
    {
        return QVector3D(
            mesh_.centerline.at(4 * s),
            mesh_.centerline.at(4 * s + 1),
            mesh_.centerline.at(4 * s + 2));
    };

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

    return points;
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

    // Photo mode: the rope of the scene (colours that carry meaning, such
    // as the two sides of a Seifert surface, are kept)
    const QVector3D photoColor(
        static_cast<float>(photoInfo_.rope.color.redF()),
        static_cast<float>(photoInfo_.rope.color.greenF()),
        static_cast<float>(photoInfo_.rope.color.blueF()));

    meshProgram_->setUniformValue("uBaseColor", photo_ ? photoColor : meshColor_);

    meshProgram_->setUniformValue(
        "uUseVertexColors",
        useVertexColors_ && !(photo_ && restingRope_) ? 1 : 0);

    meshProgram_->setUniformValue("uMetallic", photo_ ? 0.0f : metallic_);
    // Photo mode: the rope at rest has its real strands (5) or yarns (6);
    // a moving tube the painted pattern of rope (3) or lace (4)
    const int construction = photoInfo_.rope.construction;

    const int photoMaterial =
        restingRope_ ? 4 + construction : (construction == 2 ? 4 : 3);

    meshProgram_->setUniformValue("uRoughness", photo_ ? photoInfo_.rope.roughness : roughness_);
    meshProgram_->setUniformValue("uMaterial", photo_ ? photoMaterial : material_);
    meshProgram_->setUniformValue(
        "uRopeRadius",
        mesh_.centerline.size() >= 4 ? mesh_.centerline.at(3) : 0.1f);

    setPhotoUniforms(*meshProgram_);
    meshProgram_->setUniformValue("uDarkTheme", Theme::isDark() ? 1 : 0);
    meshProgram_->setUniformValue("uRig", lightRig().normalMatrix());

    meshProgram_->setUniformValue("uKeyOn", lighting_.keyLight ? 1.0f : 0.0f);
    meshProgram_->setUniformValue("uFillOn", lighting_.fillLight ? 1.0f : 0.0f);
    meshProgram_->setUniformValue("uRimOn", lighting_.rimLight ? 1.0f : 0.0f);
    meshProgram_->setUniformValue("uReflections", lighting_.reflections ? 1.0f : 0.0f);
    meshProgram_->setUniformValue("uOcclusion", lighting_.occlusion ? 1.0f : 0.0f);
    meshProgram_->setUniformValue(
        "uExposure",
        static_cast<float>(lighting_.brightness) / 100.0f);

    setShadowUniforms(*meshProgram_, lighting_.knotShadows);

    meshVao_.bind();

    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);

    meshVao_.release();
    meshProgram_->release();

    glDisable(GL_CULL_FACE);
}

void KnotGLWidget::drawParticles(const QMatrix4x4 &viewProjection)
{
    // No floating particles are drawn (flat, calm look).
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

    // a knot that arrived before the view had its size
    if (pendingFitPitch_ >= 0.0f)
    {
        const float pitch = pendingFitPitch_;

        pendingFitPitch_ = -1.0f;

        resetCamera(pitch, false);
    }

    // Photo mode turned on before the view was ready: the room now
    if (photo_ && loadedScene_ != static_cast<int>(photoScene_) && !loadPhotoScene())
        photo_ = false;

    // first the shadow map (its own framebuffer and viewport)
    drawShadowMap();

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

    // 1. background (no depth), 2. ground, 3. knot, 4. particles; in Photo
    // mode the room, the table and the rope
    const int frameWidth = static_cast<int>(static_cast<float>(width()) * dpr);
    const int frameHeight = static_cast<int>(static_cast<float>(height()) * dpr);

    if (photo_ && preparePhotoFrame(frameWidth, frameHeight))
    {
        // the table and the rope's shade on it follow the rope
        if (surfaceDirty_)
            rebuildSurfaceGeometry();

        if (occlusionDirty_)
            bakeSurfaceOcclusion();

        // the light of the scene as it is, then the camera
        glBindFramebuffer(GL_FRAMEBUFFER, photoFramebuffer_);
        glViewport(0, 0, frameWidth, frameHeight);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.0235f, 0.0235f, 0.0235f, 1.0f);

        drawPhotoBackground(projection * view);
        drawPhotoFloor(projection * view);
        drawShoe(projection * view * model);
        drawMesh(model, view, projection);

        developPhotoFrame(view, projection);
    }
    else
    {
        drawBackground();
        drawGround(lastViewProjection_);
        drawMesh(model, view, projection);
        drawParticles(lastViewProjection_);
    }

    // the plain-depth reader off unit 2 (QPainter may use the unit)
    context()->extraFunctions()->glBindSampler(2, 0);

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

    if (cameraFlight_)
    {
        advanceCameraFlight(dt);
    }
    else if (autoRotation_ && !ropeDragging_)
    {
        // 18 degrees per second
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

    animationTimer_.setInterval(enabled || cameraFlight_ ? 16 : 33);

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

void KnotGLWidget::setRopeGrab(bool enabled)
{
    ropeGrab_ = enabled;

    // the cursor shows when the mouse is over the rope
    setMouseTracking(enabled);

    if (!enabled)
    {
        ropeDragging_ = false;
        grabCurve_.clear();
        unsetCursor();
    }
}

void KnotGLWidget::setGrabCurve(const QVector<KnotMath::Vec3> &curve, double radius)
{
    grabCurve_ = curve;
    grabRadius_ = radius;
}

void KnotGLWidget::cancelRopeGrab()
{
    ropeDragging_ = false;

    if (ropeGrab_)
        setCursor(Qt::OpenHandCursor);
}

bool KnotGLWidget::pickRope(const QPointF &position, QVector3D &point) const
{
    const int n = grabCurve_.size();

    if (n < 2 || width() <= 0 || height() <= 0)
        return false;

    const QVector3D eye = computeCameraPosition() + panOffset_;
    const QVector3D forward = (panOffset_ - eye).normalized();

    // pixels per unit at distance 1 from the camera
    const float focal =
        static_cast<float>(height()) /
        (2.0f * std::tan(qDegreesToRadians(FieldOfView * 0.5f)));

    bool found = false;
    float nearest = std::numeric_limits<float>::max();

    for (int i = 0; i < n; ++i)
    {
        const KnotMath::Vec3 &a = grabCurve_.at(i);
        const KnotMath::Vec3 &b = grabCurve_.at((i + 1) % n);

        const QPointF pa = projectPointToScreen(a);
        const QPointF pb = projectPointToScreen(b);

        // the point of the segment on screen nearest to the mouse
        const QPointF d = pb - pa;
        const double length2 = QPointF::dotProduct(d, d);

        const double u =
            length2 > 1.0e-9
                ? std::clamp(QPointF::dotProduct(position - pa, d) / length2, 0.0, 1.0)
                : 0.0;

        const QPointF onScreen = pa + d * u;
        const QPointF offset = position - onScreen;

        const KnotMath::Vec3 p = a + (b - a) * u;
        const QVector3D world = posed(QVector3D(
            static_cast<float>(p.x),
            static_cast<float>(p.y),
            static_cast<float>(p.z)));

        const float depth = QVector3D::dotProduct(world - eye, forward);

        if (depth <= NearPlane)
            continue;

        // the tube on screen, and a few pixels more so a thin one can be
        // caught
        const double reach =
            grabRadius_ * static_cast<double>(focal / depth) + 4.0;

        if (std::hypot(offset.x(), offset.y()) <= reach && depth < nearest)
        {
            found = true;
            nearest = depth;
            point = world;
        }
    }

    return found;
}

QVector3D KnotGLWidget::pointOnViewPlane(
    const QPointF &position,
    const QVector3D &through) const
{
    const QMatrix4x4 inverse = lastViewProjection_.inverted();

    const float x = static_cast<float>(2.0 * position.x() / std::max(1, width()) - 1.0);
    const float y = static_cast<float>(1.0 - 2.0 * position.y() / std::max(1, height()));

    auto unproject = [&](float z)
    {
        const QVector4D p = inverse * QVector4D(x, y, z, 1.0f);

        return std::abs(p.w()) > 1.0e-9f ? p.toVector3D() / p.w() : p.toVector3D();
    };

    const QVector3D nearPoint = unproject(-1.0f);
    const QVector3D direction = unproject(1.0f) - nearPoint;

    const QVector3D eye = computeCameraPosition() + panOffset_;
    const QVector3D forward = (panOffset_ - eye).normalized();

    const float along = QVector3D::dotProduct(direction, forward);

    if (std::abs(along) < 1.0e-9f)
        return through;

    return nearPoint +
           direction * (QVector3D::dotProduct(through - nearPoint, forward) / along);
}

void KnotGLWidget::mousePressEvent(QMouseEvent *event)
{
    lastMousePosition_ = event->position().toPoint();

    // the user takes over the camera
    stopCameraFlight();

    // a press on the rope grabs it (the view does not turn)
    QVector3D point;

    if (ropeGrab_ &&
        event->button() == Qt::LeftButton &&
        pickRope(event->position(), point))
    {
        ropeDragging_ = true;
        grabPoint_ = point;
        grabStart_ = pointOnViewPlane(event->position(), point);

        setCursor(Qt::ClosedHandCursor);

        emit ropeGrabbed(unposed(point));

        event->accept();

        return;
    }

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

    if (ropeDragging_)
    {
        // the grabbed point follows the mouse in the plane facing the camera
        emit ropeDragged(unposed(
            grabPoint_ +
            pointOnViewPlane(event->position(), grabPoint_) -
            grabStart_));

        event->accept();

        return;
    }

    if (ropeGrab_ && !leftMouseDown_ && !rightMouseDown_)
    {
        QVector3D point;

        if (pickRope(event->position(), point))
            setCursor(Qt::OpenHandCursor);
        else
            unsetCursor();
    }

    if (leftMouseDown_)
    {
        yaw_ += static_cast<float>(delta.x()) * 0.55f;
        pitch_ += static_cast<float>(delta.y()) * 0.55f;

        // in Photo mode the camera stays above the table
        pitch_ = std::clamp(pitch_, photo_ ? 6.0f : -89.0f, 89.0f);

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
    if (event->button() == Qt::LeftButton && ropeDragging_)
    {
        ropeDragging_ = false;

        setCursor(Qt::OpenHandCursor);

        emit ropeReleased();
    }

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

    stopCameraFlight();

    distance_ *= static_cast<float>(std::pow(0.88, delta));

    // the whole sneaker needs more room
    distance_ = std::clamp(distance_, 2.2f, showShoe() ? 90.0f : 30.0f);

    emit cameraChanged(yaw_, pitch_, distance_);

    update();

    event->accept();
}

float KnotGLWidget::sphereDistance(float radius) const
{
    const float aspect =
        height() > 0
            ? static_cast<float>(width()) / static_cast<float>(height())
            : 1.0f;

    const float halfY = qDegreesToRadians(FieldOfView * 0.5f);
    const float halfX = std::atan(std::tan(halfY) * aspect);

    // the ball takes 80% of the narrower angle of view
    return std::clamp(
        radius / std::sin(0.8f * std::min(halfX, halfY)),
        2.2f,
        30.0f);
}

void KnotGLWidget::flyToSphere(
    const QVector3D &centre,
    float radius,
    float yaw,
    float pitch)
{
    if (!photo_)
    {
        flyCameraTo(yaw, pitch, sphereDistance(radius), centre);
        return;
    }

    // the direction is meant for the knot's own coordinates: turned with
    // the knot, and never from below the table
    const float yr = qDegreesToRadians(yaw);
    const float pr = qDegreesToRadians(pitch);

    const QVector3D toCamera =
        pose_.mapVector(QVector3D(
            std::cos(pr) * std::sin(yr),
            std::sin(pr),
            std::cos(pr) * std::cos(yr)));

    const float worldYaw = qRadiansToDegrees(std::atan2(toCamera.x(), toCamera.z()));
    const float worldPitch =
        std::clamp(qRadiansToDegrees(std::asin(std::clamp(toCamera.y(), -1.0f, 1.0f))), 25.0f, 70.0f);

    flyCameraTo(worldYaw, worldPitch, sphereDistance(radius), posed(centre));
}

void KnotGLWidget::fitNewKnot(float highestPitch)
{
    // a new knot is laid down anew in Photo mode
    poseDirty_ = true;

    if (glInitialized_ && isVisible() && width() > 0 && height() > 0)
    {
        resetCamera(highestPitch);
        return;
    }

    pendingFitPitch_ = highestPitch;
    update();
}

void KnotGLWidget::resetCamera(float highestPitch, bool animate)
{
    /*
     * Best view of the current knot: the direction in which the curve
     * spreads out most while its strands overlap least, then the
     * distance at which the whole tube fills about half of the view.
     */

    // a new knot in Photo mode is laid down first
    if (photo_ && poseDirty_)
    {
        computePose();
        refreshPose();
    }

    // Photo mode: from where a photographer would stand, above the table
    const float lowestPitch = photo_ ? 25.0f : 5.0f;

    if (photo_)
        highestPitch = std::min(highestPitch, 45.0f);

    const int samples = mesh_.centerline.size() / 4;

    if (samples < 3)
    {
        flyCameraTo(35.0f, 18.0f, 8.0f, QVector3D(0.0f, 0.0f, 0.0f));

        return;
    }

    /*
     * The sneaker: the knot and the lacing with the shoe around them, from
     * above the front of its outer side (steeper than a table: the knot
     * lies on top of the shoe).
     */
    if (showShoe())
    {
        const Photo::Sneaker &shoe = Photo::sneaker();
        const float mm = worldPerMm();

        const float yaw = 38.0f;
        const float pitch = 55.0f;

        const float pr = qDegreesToRadians(pitch);
        const float yr = qDegreesToRadians(yaw);

        const QVector3D toCamera(std::cos(pr) * std::sin(yr), std::sin(pr), std::cos(pr) * std::cos(yr));

        const QVector3D right = QVector3D::crossProduct(-toCamera, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, -toCamera);

        // the lace, and the shoe within a few centimetres of it
        QVector<QVector3D> points;

        QVector3D low(1e30f, 1e30f, 1e30f);
        QVector3D high(-1e30f, -1e30f, -1e30f);

        for (int i = 0; i + 2 < mesh_.positions.size(); i += 3 * 37)
        {
            const QVector3D p(mesh_.positions.at(i), mesh_.positions.at(i + 1), mesh_.positions.at(i + 2));

            points.push_back(p);

            low = QVector3D(std::min(low.x(), p.x()), std::min(low.y(), p.y()), std::min(low.z(), p.z()));
            high = QVector3D(std::max(high.x(), p.x()), std::max(high.y(), p.y()), std::max(high.z(), p.z()));
        }

        const float margin = 30.0f * mm;

        const int count = shoe.vertices.size() / Photo::Sneaker::VertexFloats;

        for (int i = 0; i < count; i += 23)
        {
            const float *v = shoe.vertices.constData() + i * Photo::Sneaker::VertexFloats;
            const QVector3D p = QVector3D(v[0], v[1], v[2]) * mm;

            if (p.x() > low.x() - margin && p.x() < high.x() + margin &&
                p.z() > low.z() - margin && p.z() < high.z() + margin)
            {
                points.push_back(p);
            }
        }

        QVector3D centre = 0.5f * (low + high);

        float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;

        for (const QVector3D &p : points)
        {
            const QVector3D d = p - centre;

            minX = std::min(minX, QVector3D::dotProduct(d, right));
            maxX = std::max(maxX, QVector3D::dotProduct(d, right));
            minY = std::min(minY, QVector3D::dotProduct(d, up));
            maxY = std::max(maxY, QVector3D::dotProduct(d, up));
        }

        centre += right * (0.5f * (minX + maxX)) + up * (0.5f * (minY + maxY));

        constexpr float ShoeFill = 0.75f;

        const float aspect =
            height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

        const float tanY = std::tan(qDegreesToRadians(FieldOfView * 0.5f)) * ShoeFill;
        const float tanX = tanY * aspect;

        float distance = 0.0f;

        for (const QVector3D &p : points)
        {
            const QVector3D d = p - centre;
            const float depth = QVector3D::dotProduct(d, toCamera);

            distance = std::max(distance, depth + std::abs(QVector3D::dotProduct(d, right)) / tanX);
            distance = std::max(distance, depth + std::abs(QVector3D::dotProduct(d, up)) / tanY);
        }

        flyCameraTo(yaw, pitch, std::clamp(distance, 2.2f, 90.0f), centre);

        if (!animate)
        {
            flightProgress_ = 1.0f;
            advanceCameraFlight(0.0f);
        }

        return;
    }

    const bool hasStrands = mesh_.centerlineStrand.size() == samples;

    // about 160 evenly spread points of the centre line
    const int stride = std::max(1, samples / 160);

    QVector<QVector3D> curve;
    QVector<int> strand;

    float tube = 0.0f;

    for (int s = 0; s < samples; s += stride)
    {
        curve.append(QVector3D(
            mesh_.centerline.at(4 * s),
            mesh_.centerline.at(4 * s + 1),
            mesh_.centerline.at(4 * s + 2)));

        strand.append(hasStrands ? mesh_.centerlineStrand.at(s) : 0);

        tube = std::max(tube, mesh_.centerline.at(4 * s + 3));
    }

    const int n = curve.size();

    QVector3D low = curve.first();
    QVector3D high = curve.first();

    for (const QVector3D &p : curve)
    {
        low = QVector3D(std::min(low.x(), p.x()), std::min(low.y(), p.y()), std::min(low.z(), p.z()));
        high = QVector3D(std::max(high.x(), p.x()), std::max(high.y(), p.y()), std::max(high.z(), p.z()));
    }

    QVector3D center = 0.5f * (low + high);

    // where each strand starts and how many points it has (strands are
    // stored one after the other and are closed loops)
    QVector<int> strandStart(n), strandLength(n);

    float length = 0.0f;

    for (int i = 0; i < n;)
    {
        int j = i;

        while (j < n && strand.at(j) == strand.at(i))
            ++j;

        for (int k = i; k < j; ++k)
        {
            strandStart[k] = i;
            strandLength[k] = j - i;

            if (k + 1 < j)
                length += (curve.at(k + 1) - curve.at(k)).length();
        }

        i = j;
    }

    const float spacing = std::max(length / n, 1e-4f);
    const float extent = std::max((high - low).length(), 1e-3f);

    // two strands closer than this on screen look like one
    const float closeDistance = std::max(2.4f * tube, 0.03f * extent);

    // neighbours along the curve are always close, so they do not count
    const int neighbourGap =
        static_cast<int>(std::ceil(3.0f * closeDistance / spacing)) + 1;

    float bestScore = -1.0f;
    float bestYaw = 35.0f;
    float bestPitch = 18.0f;

    QVector<QVector2D> flat(n);

    // from slightly above to well above: never from below the floor
    for (float pitch = lowestPitch; pitch <= highestPitch + 0.01f; pitch += 5.0f)
    {
        for (float yaw = 0.0f; yaw < 360.0f; yaw += 15.0f)
        {
            const float pr = qDegreesToRadians(pitch);
            const float yr = qDegreesToRadians(yaw);

            const QVector3D toCamera(
                std::cos(pr) * std::sin(yr),
                std::sin(pr),
                std::cos(pr) * std::cos(yr));

            const QVector3D u =
                QVector3D::crossProduct(QVector3D(0.0f, 1.0f, 0.0f), toCamera).normalized();
            const QVector3D v =
                QVector3D::crossProduct(toCamera, u);

            double sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0;

            for (int i = 0; i < n; ++i)
            {
                const QVector3D d = curve.at(i) - center;

                flat[i] = QVector2D(QVector3D::dotProduct(d, u), QVector3D::dotProduct(d, v));

                sx += flat[i].x();
                sy += flat[i].y();
                sxx += flat[i].x() * flat[i].x();
                syy += flat[i].y() * flat[i].y();
                sxy += flat[i].x() * flat[i].y();
            }

            sx /= n;
            sy /= n;

            const double cxx = sxx / n - sx * sx;
            const double cyy = syy / n - sy * sy;
            const double cxy = sxy / n - sx * sy;

            // area the curve covers on screen
            const double spread =
                std::sqrt(std::max(0.0, cxx * cyy - cxy * cxy));

            // pairs of far-apart points that land on top of each other
            int close = 0;

            const float close2 = closeDistance * closeDistance;

            for (int i = 0; i < n; ++i)
            {
                for (int j = i + 1; j < n; ++j)
                {
                    if (strandStart.at(i) == strandStart.at(j))
                    {
                        const int gap = j - i;

                        if (std::min(gap, strandLength.at(i) - gap) <= neighbourGap)
                            continue;
                    }

                    if ((flat.at(i) - flat.at(j)).lengthSquared() < close2)
                        ++close;
                }
            }

            // a little depth reads better than looking straight down
            const float tilt =
                1.0f + 0.08f * std::cos(qDegreesToRadians(pitch - 25.0f));

            const float score =
                static_cast<float>(spread) * tilt /
                (1.0f + static_cast<float>(close) / n);

            if (score > bestScore)
            {
                bestScore = score;
                bestYaw = yaw;
                bestPitch = pitch;
            }
        }
    }

    /*
     * Fit the whole tube (its surface vertices) into the view.
     */

    const float pr = qDegreesToRadians(bestPitch);
    const float yr = qDegreesToRadians(bestYaw);

    const QVector3D toCamera(
        std::cos(pr) * std::sin(yr),
        std::sin(pr),
        std::cos(pr) * std::cos(yr));

    const QVector3D right =
        QVector3D::crossProduct(-toCamera, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
    const QVector3D up =
        QVector3D::crossProduct(right, -toCamera);

    const int vertexCount = mesh_.positions.size() / 3;
    const int vertexStride = std::max(1, vertexCount / 4000);

    QVector<QVector3D> surface;

    for (int i = 0; i < vertexCount; i += vertexStride)
    {
        surface.append(QVector3D(
            mesh_.positions.at(3 * i),
            mesh_.positions.at(3 * i + 1),
            mesh_.positions.at(3 * i + 2)));
    }

    if (surface.isEmpty())
        surface = curve;

    // centre the picture, not the box
    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;

    for (const QVector3D &p : surface)
    {
        const QVector3D d = p - center;

        minX = std::min(minX, QVector3D::dotProduct(d, right));
        maxX = std::max(maxX, QVector3D::dotProduct(d, right));
        minY = std::min(minY, QVector3D::dotProduct(d, up));
        maxY = std::max(maxY, QVector3D::dotProduct(d, up));
    }

    center += right * (0.5f * (minX + maxX)) + up * (0.5f * (minY + maxY));

    // the knot fills this much of the shorter side, with calm room
    // around it
    constexpr float Fill = 0.50f;

    const float aspect =
        height() > 0
            ? static_cast<float>(width()) / static_cast<float>(height())
            : 1.0f;

    const float tanY = std::tan(qDegreesToRadians(FieldOfView * 0.5f)) * Fill;
    const float tanX = tanY * aspect;

    float distance = 0.0f;

    for (const QVector3D &p : surface)
    {
        const QVector3D d = p - center;

        const float depth = QVector3D::dotProduct(d, toCamera);

        distance = std::max(distance, depth + std::abs(QVector3D::dotProduct(d, right)) / tanX);
        distance = std::max(distance, depth + std::abs(QVector3D::dotProduct(d, up)) / tanY);
    }

    flyCameraTo(bestYaw, bestPitch, std::clamp(distance, 2.2f, 30.0f), center);

    // straight there
    if (!animate)
    {
        flightProgress_ = 1.0f;
        advanceCameraFlight(0.0f);
    }
}

void KnotGLWidget::flyCameraTo(
    float yaw,
    float pitch,
    float distance,
    const QVector3D &target)
{
    flightFromYaw_ = yaw_;
    flightFromPitch_ = pitch_;
    flightFromDistance_ = distance_;
    flightFromPan_ = panOffset_;

    // the short way round
    flightToYaw_ =
        yaw_ + std::fmod(std::fmod(yaw - yaw_, 360.0f) + 540.0f, 360.0f) - 180.0f;
    flightToPitch_ = pitch;
    flightToDistance_ = distance;
    flightToPan_ = target;

    flightProgress_ = 0.0f;
    cameraFlight_ = true;

    animationTimer_.setInterval(16);

    update();
}

void KnotGLWidget::stopCameraFlight()
{
    if (!cameraFlight_)
        return;

    cameraFlight_ = false;

    animationTimer_.setInterval(autoRotation_ ? 16 : 33);
}

void KnotGLWidget::advanceCameraFlight(
    float dt)
{
    // 0.6 s, easing in and out
    flightProgress_ = std::min(1.0f, flightProgress_ + dt / 0.6f);

    const float t = flightProgress_;
    const float e = t * t * (3.0f - 2.0f * t);

    yaw_ = flightFromYaw_ + (flightToYaw_ - flightFromYaw_) * e;
    pitch_ = flightFromPitch_ + (flightToPitch_ - flightFromPitch_) * e;
    distance_ = flightFromDistance_ + (flightToDistance_ - flightFromDistance_) * e;
    panOffset_ = flightFromPan_ + (flightToPan_ - flightFromPan_) * e;

    if (flightProgress_ >= 1.0f)
    {
        yaw_ = std::fmod(yaw_ + 360.0f, 360.0f);

        stopCameraFlight();
    }

    emit cameraChanged(yaw_, pitch_, distance_);
}

/*
 * =========================================================
 * PHOTO MODE
 * =========================================================
 */

bool KnotGLWidget::setPhotoMode(bool enabled, Photo::Scene scene)
{
    photo_ = enabled;
    photoScene_ = scene;
    photoInfo_ = Photo::sceneInfo(scene);

    bool ok = true;

    if (glInitialized_ && context())
    {
        makeCurrent();

        if (photo_ && loadedScene_ != static_cast<int>(scene))
            ok = loadPhotoScene();

        if (!ok)
            photo_ = false;

        const int size = photo_ ? PhotoShadowMapSize : normalShadowMapSize_;

        if (size != shadowMapSize_ && shadowTexture_ != 0)
        {
            shadowMapSize_ = size;

            allocateShadowMap();
        }

        // the frame is only kept while it is used
        if (!photo_)
            destroyPhotoFrame();

        doneCurrent();
    }
    else
    {
        // allocated at that size when the view is set up
        shadowMapSize_ = photo_ ? PhotoShadowMapSize : normalShadowMapSize_;
    }

    // Laid down at the next framing (resetCamera); standing as given again
    // at once when Photo mode ends.
    poseDirty_ = true;
    surfaceDirty_ = true;
    occlusionDirty_ = true;

    if (!photo_)
    {
        restingRope_ = false;
        restingPose_ = false;

        pose_.setToIdentity();

        refreshPose();
    }

    update();

    return ok;
}

bool KnotGLWidget::photoMode() const
{
    return photo_;
}

void KnotGLWidget::setRestingRope(const KnotMath::Mesh &mesh, const QMatrix4x4 &pose)
{
    restPose_ = pose;
    restingPose_ = true;
    restingRope_ = true;

    pose_ = pose;
    poseDirty_ = false;

    rawMesh_ = mesh;

    surfaceDirty_ = true;
    occlusionDirty_ = true;
    shoeShadeDirty_ = true;

    refreshPose();
}

void KnotGLWidget::clearRestingPose()
{
    restingPose_ = false;
    restingRope_ = false;
    poseDirty_ = true;
}

QVector3D KnotGLWidget::posed(const QVector3D &point) const
{
    return photo_ ? pose_.map(point) : point;
}

QVector3D KnotGLWidget::unposed(const QVector3D &point) const
{
    return photo_ ? pose_.inverted().map(point) : point;
}

void KnotGLWidget::computePose()
{
    poseDirty_ = false;

    // the rope at rest came with the turn that laid it down
    if (restingPose_)
    {
        pose_ = restPose_;
        return;
    }

    const int samples = rawMesh_.centerline.size() / 4;

    QVector<QVector3D> points;

    points.reserve(samples);

    for (int s = 0; s < samples; ++s)
    {
        points.push_back(QVector3D(
            rawMesh_.centerline.at(4 * s),
            rawMesh_.centerline.at(4 * s + 1),
            rawMesh_.centerline.at(4 * s + 2)));
    }

    pose_ = Photo::flatPose(points);
}

void KnotGLWidget::applyPose()
{
    mesh_ = rawMesh_;

    if (!photo_)
        return;

    const int vertexCount = mesh_.positions.size() / 3;

    for (int i = 0; i < vertexCount; ++i)
    {
        const QVector3D p = pose_.map(QVector3D(
            mesh_.positions.at(3 * i),
            mesh_.positions.at(3 * i + 1),
            mesh_.positions.at(3 * i + 2)));

        mesh_.positions[3 * i] = p.x();
        mesh_.positions[3 * i + 1] = p.y();
        mesh_.positions[3 * i + 2] = p.z();
    }

    const int normalCount = mesh_.normals.size() / 3;

    for (int i = 0; i < normalCount; ++i)
    {
        const QVector3D n = pose_.mapVector(QVector3D(
            mesh_.normals.at(3 * i),
            mesh_.normals.at(3 * i + 1),
            mesh_.normals.at(3 * i + 2)));

        mesh_.normals[3 * i] = n.x();
        mesh_.normals[3 * i + 1] = n.y();
        mesh_.normals[3 * i + 2] = n.z();
    }

    const int samples = mesh_.centerline.size() / 4;

    for (int s = 0; s < samples; ++s)
    {
        const QVector3D c = pose_.map(QVector3D(
            mesh_.centerline.at(4 * s),
            mesh_.centerline.at(4 * s + 1),
            mesh_.centerline.at(4 * s + 2)));

        mesh_.centerline[4 * s] = c.x();
        mesh_.centerline[4 * s + 1] = c.y();
        mesh_.centerline[4 * s + 2] = c.z();
    }
}

void KnotGLWidget::refreshPose()
{
    applyPose();

    if (boundsFixed_)
    {
        poseFixedBounds();

        tableLevel_ = meshBottom();
    }
    else
    {
        computeMeshBounds();
    }

    groundDirty_ = true;
    surfaceDirty_ = true;
    occlusionDirty_ = true;

    if (glInitialized_ && context())
    {
        // also called while painting (a framing waiting for the first
        // paint): the context must then stay current
        const bool current = QOpenGLContext::currentContext() == context();

        if (!current)
            makeCurrent();

        uploadMesh();

        if (!current)
            doneCurrent();
    }

    update();
}

GLuint KnotGLWidget::loadTexture(const QString &path)
{
    QImage image(path);

    if (image.isNull())
        return 0;

    image = image.convertToFormat(QImage::Format_RGBA8888);

    GLuint texture = 0;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        image.width(),
        image.height(),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image.constBits());

    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // the table seen at a low angle stays sharp
    if (context()->hasExtension(QByteArrayLiteral("GL_EXT_texture_filter_anisotropic")) ||
        context()->hasExtension(QByteArrayLiteral("GL_ARB_texture_filter_anisotropic")))
    {
        GLfloat most = 1.0f;

        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &most);

        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(most, 16.0f));
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    // with the mipmaps, a third more
    photoBytes_ += static_cast<qint64>(image.width()) * image.height() * 4 * 4 / 3;

    return texture;
}

bool KnotGLWidget::loadPhotoScene()
{
    destroyPhotoResources();

    if (!prefilterProgram_ || !prefilterProgram_->isLinked() ||
        !photoFloorProgram_ || !photoFloorProgram_->isLinked() ||
        !photoBackgroundProgram_ || !photoBackgroundProgram_->isLinked() ||
        !developProgram_ || !developProgram_->isLinked() ||
        !occlusionProgram_ || !occlusionProgram_->isLinked())
    {
        return false;
    }

    environment_ = Photo::loadEnvironment(photoInfo_.environment);

    if (!environment_.valid)
        return false;

    const int width = environment_.width;
    const int height = environment_.height;

    /*
     * Half floats reach 65504; an unclipped sun can be brighter (the
     * lounge's is about 100000). The pictures hold the room scaled down
     * so that nothing overflows, and the shaders scale it back up.
     */
    float brightest = 0.0f;

    for (const float value : environment_.pixels)
        brightest = std::max(brightest, value);

    photoRoomScale_ = std::max(1.0f, brightest / 30000.0f);

    for (float &value : environment_.pixels)
        value /= photoRoomScale_;

    // the room, in half floats, with mipmaps
    glGenTextures(1, &environmentTexture_);
    glBindTexture(GL_TEXTURE_2D, environmentTexture_);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB16F,
        width,
        height,
        0,
        GL_RGB,
        GL_FLOAT,
        environment_.pixels.constData());

    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // (most drivers keep three channels as four)
    photoBytes_ += static_cast<qint64>(width) * height * 8 * 4 / 3;

    // the pixels are on the card now
    environment_.pixels = QVector<float>();

    // the room blurred for each roughness, one mip level each
    glGenTextures(1, &specularTexture_);
    glBindTexture(GL_TEXTURE_2D, specularTexture_);

    for (int level = 0; level < SpecularLevels; ++level)
    {
        glTexImage2D(
            GL_TEXTURE_2D,
            level,
            GL_RGBA16F,
            SpecularWidth >> level,
            (SpecularWidth / 2) >> level,
            0,
            GL_RGBA,
            GL_FLOAT,
            nullptr);

        photoBytes_ +=
            static_cast<qint64>(SpecularWidth >> level) * ((SpecularWidth / 2) >> level) * 8;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, SpecularLevels - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint framebuffer = 0;

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);

    prefilterProgram_->bind();

    prefilterProgram_->setUniformValue("uSource", 0);
    prefilterProgram_->setUniformValue(
        "uSourceTexels",
        static_cast<float>(width) * static_cast<float>(height));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, environmentTexture_);

    backgroundVao_.bind();

    bool complete = true;

    for (int level = 0; level < SpecularLevels; ++level)
    {
        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            specularTexture_,
            level);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            complete = false;
            break;
        }

        glViewport(0, 0, SpecularWidth >> level, (SpecularWidth / 2) >> level);

        prefilterProgram_->setUniformValue(
            "uRoughness",
            static_cast<float>(level) / static_cast<float>(SpecularLevels - 1));

        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    backgroundVao_.release();
    prefilterProgram_->release();

    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glDeleteFramebuffers(1, &framebuffer);

    glDepthMask(GL_TRUE);

    // the surface, and its average colour (the light it throws back up)
    floorColorTexture_ = loadTexture(photoInfo_.surfaceColor);
    floorNormalTexture_ = loadTexture(photoInfo_.surfaceNormal);
    floorRoughnessTexture_ = loadTexture(photoInfo_.surfaceRoughness);

    const QImage small =
        QImage(photoInfo_.surfaceColor)
            .scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB32);

    QVector3D sum;

    for (int y = 0; y < small.height(); ++y)
    {
        for (int x = 0; x < small.width(); ++x)
        {
            const QColor c = small.pixelColor(x, y);

            sum += QVector3D(
                std::pow(static_cast<float>(c.redF()), 2.2f),
                std::pow(static_cast<float>(c.greenF()), 2.2f),
                std::pow(static_cast<float>(c.blueF()), 2.2f));
        }
    }

    if (!small.isNull())
        floorAlbedo_ = sum / static_cast<float>(small.width() * small.height());

    // the table's view of the room past the rope (one channel)
    constexpr int OcclusionSize = 512;

    glGenTextures(1, &occlusionTexture_);
    glBindTexture(GL_TEXTURE_2D, occlusionTexture_);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, OcclusionSize, OcclusionSize, 0, GL_RED, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    photoBytes_ += static_cast<qint64>(OcclusionSize) * OcclusionSize * 2;

    glGenFramebuffers(1, &occlusionFramebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, occlusionFramebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, occlusionTexture_, 0);

    const bool occlusionReady = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

    glGenTextures(1, &segmentTexture_);

    if (!surfaceVao_.isCreated())
        surfaceVao_.create();

    if (!surfaceBuffer_.isCreated())
    {
        surfaceBuffer_.create();
        surfaceBuffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }

    if (!complete || !occlusionReady || floorColorTexture_ == 0 || floorNormalTexture_ == 0 ||
        floorRoughnessTexture_ == 0)
    {
        destroyPhotoResources();
        return false;
    }

    // the sneaker (its scene only)
    if (photoInfo_.sneaker)
        uploadShoe();

    loadedScene_ = static_cast<int>(photoScene_);

    surfaceDirty_ = true;
    occlusionDirty_ = true;

    return true;
}

void KnotGLWidget::destroyPhotoResources()
{
    for (GLuint *texture : {&environmentTexture_, &specularTexture_, &floorColorTexture_,
                            &floorNormalTexture_, &floorRoughnessTexture_, &occlusionTexture_,
                            &segmentTexture_})
    {
        if (*texture != 0)
            glDeleteTextures(1, texture);

        *texture = 0;
    }

    if (occlusionFramebuffer_ != 0)
        glDeleteFramebuffers(1, &occlusionFramebuffer_);

    occlusionFramebuffer_ = 0;

    destroyShoe();

    loadedScene_ = -1;
    photoBytes_ = 0;
}

bool KnotGLWidget::preparePhotoFrame(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (photoFramebuffer_ != 0 && width == photoFrameWidth_ && height == photoFrameHeight_)
        return true;

    destroyPhotoFrame();

    QOpenGLExtraFunctions *extra = context()->extraFunctions();

    GLint most = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &most);

    const int samples = std::clamp(4, 1, std::max(1, static_cast<int>(most)));

    // drawn into: colour as light (half floats) and depth, several
    // samples per pixel for smooth edges
    glGenRenderbuffers(1, &photoColorBuffer_);
    glBindRenderbuffer(GL_RENDERBUFFER, photoColorBuffer_);
    extra->glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, width, height);

    glGenRenderbuffers(1, &photoDepthBuffer_);
    glBindRenderbuffer(GL_RENDERBUFFER, photoDepthBuffer_);
    extra->glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);

    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &photoFramebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, photoFramebuffer_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, photoColorBuffer_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, photoDepthBuffer_);

    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    // resolved: the colour with mipmaps (the blur reads coarser levels)
    // and the depth
    const int levels = 1 + static_cast<int>(std::floor(std::log2(std::max(width, height))));

    glGenTextures(1, &resolvedColor_);
    glBindTexture(GL_TEXTURE_2D, resolvedColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);

    glGenTextures(1, &resolvedDepth_);
    glBindTexture(GL_TEXTURE_2D, resolvedDepth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &resolveFramebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFramebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolvedColor_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, resolvedDepth_, 0);

    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

    photoFrameWidth_ = width;
    photoFrameHeight_ = height;

    const qint64 pixels = static_cast<qint64>(width) * height;

    photoFrameBytes_ = pixels * samples * (8 + 4) + pixels * 8 * 4 / 3 + pixels * 4;

    if (!ok)
        destroyPhotoFrame();

    return ok;
}

void KnotGLWidget::destroyPhotoFrame()
{
    for (GLuint *framebuffer : {&photoFramebuffer_, &resolveFramebuffer_})
    {
        if (*framebuffer != 0)
            glDeleteFramebuffers(1, framebuffer);

        *framebuffer = 0;
    }

    for (GLuint *buffer : {&photoColorBuffer_, &photoDepthBuffer_})
    {
        if (*buffer != 0)
            glDeleteRenderbuffers(1, buffer);

        *buffer = 0;
    }

    for (GLuint *texture : {&resolvedColor_, &resolvedDepth_})
    {
        if (*texture != 0)
            glDeleteTextures(1, texture);

        *texture = 0;
    }

    photoFrameWidth_ = 0;
    photoFrameHeight_ = 0;
    photoFrameBytes_ = 0;
}

QVector3D KnotGLWidget::photoKeyDirection() const
{
    // the sun where it is in the photograph (the room turned by its yaw)
    QMatrix4x4 turn;

    turn.rotate(photoInfo_.environmentYaw, 0.0f, 1.0f, 0.0f);

    return turn.mapVector(environment_.sunDirection).normalized();
}

float KnotGLWidget::worldPerMm() const
{
    const float tubeRadius =
        mesh_.centerline.size() >= 4 ? mesh_.centerline.at(3) : 0.1f;

    return 2.0f * tubeRadius / static_cast<float>(std::max(photoInfo_.rope.diameterMm, 0.1));
}

void KnotGLWidget::setPhotoUniforms(QOpenGLShaderProgram &program)
{
    // the units are set even when unused, so no two kinds of sampler
    // share a unit
    program.setUniformValue("uEnvironment", 3);
    program.setUniformValue("uSpecular", 4);
    program.setUniformValue("uPhoto", photo_ ? 1 : 0);

    if (!photo_)
        return;

    program.setUniformValue("uRoomScale", photoRoomScale_);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, environmentTexture_);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, specularTexture_);
    glActiveTexture(GL_TEXTURE0);

    QMatrix4x4 toRoom;

    toRoom.rotate(-photoInfo_.environmentYaw, 0.0f, 1.0f, 0.0f);

    program.setUniformValue("uEnvRotation", toRoom.normalMatrix());
    program.setUniformValueArray("uIrradiance", environment_.irradiance, 9);
    program.setUniformValue("uKeyIrradiance", environment_.sunIrradiance);

    // a light of some size gives a wider, softer highlight
    program.setUniformValue("uKeySize", 0.5f * environment_.sunRadius);

    // light falling on the table, and what it throws back up
    const QVector3D sun = photoKeyDirection();

    const QVector3D onTable =
        environment_.irradianceAt(QVector3D(0.0f, 1.0f, 0.0f)) +
        environment_.sunIrradiance * std::max(sun.y(), 0.0f);

    program.setUniformValue("uFloorBounce", floorAlbedo_ * onTable);

    /*
     * The room's shape, to the scale of the rope: the photograph was taken
     * cameraHeightM above the room's floor, above the knot; the table top
     * is tableHeightM above that floor.
     */
    const float metre = 1000.0f * worldPerMm();
    const float top = photoFloorLevel();

    const float roomFloor = top - static_cast<float>(photoInfo_.tableHeightM) * metre;

    const QVector3D middle(
        0.5f * (meshMin_.x() + meshMax_.x()),
        roomFloor + static_cast<float>(photoInfo_.cameraHeightM) * metre,
        0.5f * (meshMin_.z() + meshMax_.z()));

    program.setUniformValue("uRoomCentre", middle);
    program.setUniformValue("uRoomRadius", static_cast<float>(photoInfo_.roomRadiusM) * metre);
    program.setUniformValue("uRoomFloor", roomFloor);

    // The camera is set for the light on the table: white paper there
    // comes out a light grey, as a photographer would expose it.
    const float lightOnTable =
        0.2126f * onTable.x() + 0.7152f * onTable.y() + 0.0722f * onTable.z();

    photoExposure_ = 1.9f / std::max(lightOnTable, 1.0e-3f);
}

float KnotGLWidget::photoFloorLevel() const
{
    // the rope touches the table (a hair lower, so they do not flicker)
    return tableLevel_ - 0.0005f * worldPerMm() * static_cast<float>(photoInfo_.rope.diameterMm);
}

void KnotGLWidget::rebuildSurfaceGeometry()
{
    surfaceDirty_ = false;
    surfaceVertexCount_ = 0;

    if (!surfaceBuffer_.isCreated() || !photoFloorProgram_ || !photoFloorProgram_->isLinked())
        return;

    const float mm = worldPerMm();
    const float top = photoFloorLevel();

    const QVector3D middle(
        0.5f * (meshMin_.x() + meshMax_.x()),
        top,
        0.5f * (meshMin_.z() + meshMax_.z()));

    QVector<float> data;

    // one face: four corners, a normal and a tangent (picture u); picture
    // coordinates in millimetres from the knot's middle
    auto face = [&](const QVector3D corners[4], const QVector3D &normal, const QVector3D &tangent)
    {
        const QVector3D bitangent = QVector3D::crossProduct(tangent, normal);

        const int order[6] = {0, 1, 2, 0, 2, 3};

        for (const int k : order)
        {
            const QVector3D p = corners[k];
            const QVector3D local = p - middle;

            data << p.x() << p.y() << p.z()
                 << normal.x() << normal.y() << normal.z()
                 << tangent.x() << tangent.y() << tangent.z()
                 << QVector3D::dotProduct(local, tangent) / mm
                 << QVector3D::dotProduct(local, bitangent) / mm;
        }
    };

    const QVector3D up(0.0f, 1.0f, 0.0f);

    if (photoInfo_.tableWidthMm > 0.0)
    {
        // a table top of its real size, the knot in its middle, and its
        // four edges
        const float hx = 0.5f * static_cast<float>(photoInfo_.tableWidthMm) * mm;
        const float hz = 0.5f * static_cast<float>(photoInfo_.tableDepthMm) * mm;
        const float thick = static_cast<float>(photoInfo_.tableThicknessMm) * mm;

        const float x0 = middle.x() - hx;
        const float x1 = middle.x() + hx;
        const float z0 = middle.z() - hz;
        const float z1 = middle.z() + hz;
        const float y1 = top;
        const float y0 = top - thick;

        // counter-clockwise seen from outside
        const QVector3D topFace[4] = {{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}};
        const QVector3D front[4] = {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
        const QVector3D back[4] = {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}};
        const QVector3D right[4] = {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}};
        const QVector3D left[4] = {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}};

        face(topFace, up, QVector3D(1.0f, 0.0f, 0.0f));
        face(front, QVector3D(0.0f, 0.0f, 1.0f), QVector3D(1.0f, 0.0f, 0.0f));
        face(back, QVector3D(0.0f, 0.0f, -1.0f), QVector3D(-1.0f, 0.0f, 0.0f));
        face(right, QVector3D(1.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, -1.0f));
        face(left, QVector3D(-1.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));
    }
    else
    {
        // the room's floor, as far as the room's own floor reaches
        const float metre = 1000.0f * mm;
        const float radius = static_cast<float>(photoInfo_.roomRadiusM) * metre;
        const float height = static_cast<float>(photoInfo_.cameraHeightM) * metre;
        const float h = std::sqrt(std::max(radius * radius - height * height, 1.0f));

        const QVector3D floorFace[4] = {
            {middle.x() - h, top, middle.z() + h},
            {middle.x() + h, top, middle.z() + h},
            {middle.x() + h, top, middle.z() - h},
            {middle.x() - h, top, middle.z() - h}};

        face(floorFace, up, QVector3D(1.0f, 0.0f, 0.0f));
    }

    surfaceVertexCount_ = data.size() / 11;

    surfaceVao_.bind();
    surfaceBuffer_.bind();

    surfaceBuffer_.allocate(data.constData(), static_cast<int>(data.size() * sizeof(float)));

    const int stride = static_cast<int>(11 * sizeof(float));

    photoFloorProgram_->bind();

    photoFloorProgram_->enableAttributeArray(0);
    photoFloorProgram_->setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);
    photoFloorProgram_->enableAttributeArray(1);
    photoFloorProgram_->setAttributeBuffer(1, GL_FLOAT, static_cast<int>(3 * sizeof(float)), 3, stride);
    photoFloorProgram_->enableAttributeArray(2);
    photoFloorProgram_->setAttributeBuffer(2, GL_FLOAT, static_cast<int>(6 * sizeof(float)), 3, stride);
    photoFloorProgram_->enableAttributeArray(3);
    photoFloorProgram_->setAttributeBuffer(3, GL_FLOAT, static_cast<int>(9 * sizeof(float)), 2, stride);

    photoFloorProgram_->release();

    surfaceVao_.release();
    surfaceBuffer_.release();
}

void KnotGLWidget::bakeSurfaceOcclusion()
{
    occlusionDirty_ = false;

    if (occlusionFramebuffer_ == 0 || segmentTexture_ == 0 ||
        !occlusionProgram_ || !occlusionProgram_->isLinked())
    {
        return;
    }

    const int samples = mesh_.centerline.size() / 4;

    // one closed or open strand (several strands are not baked)
    for (const int strand : mesh_.centerlineStrand)
    {
        if (strand != 0)
            return;
    }

    if (samples < 3)
        return;

    // pieces of about 0.4 radii, at most 2048 points
    const float radius = mesh_.centerline.at(3);
    const float length = mesh_.strandLength.isEmpty() ? 0.0f : mesh_.strandLength.first();

    const int wanted = std::clamp(static_cast<int>(length / (0.4f * radius)), 16, 2048);
    const int stride = std::max(1, samples / wanted);

    QVector<float> points;

    for (int s = 0; s < samples; s += stride)
    {
        points << mesh_.centerline.at(4 * s) << mesh_.centerline.at(4 * s + 1)
               << mesh_.centerline.at(4 * s + 2) << 0.0f;
    }

    const int count = points.size() / 4;

    glBindTexture(GL_TEXTURE_2D, segmentTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, count, 1, 0, GL_RGBA, GL_FLOAT, points.constData());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // the square around the knot where the rope's shade can fall
    const float half =
        0.5f * std::max(meshMax_.x() - meshMin_.x(), meshMax_.z() - meshMin_.z()) + 10.0f * radius;

    occlusionArea_ = QVector3D(
        0.5f * (meshMin_.x() + meshMax_.x()),
        0.5f * (meshMin_.z() + meshMax_.z()),
        half);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, occlusionFramebuffer_);
    glViewport(0, 0, 512, 512);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    occlusionProgram_->bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, segmentTexture_);

    occlusionProgram_->setUniformValue("uSegments", 0);
    occlusionProgram_->setUniformValue("uCount", count);
    occlusionProgram_->setUniformValue(
        "uClosed",
        mesh_.strandClosed.isEmpty() || mesh_.strandClosed.first() ? 1 : 0);
    occlusionProgram_->setUniformValue("uArea", occlusionArea_);
    occlusionProgram_->setUniformValue("uTableY", photoFloorLevel());
    occlusionProgram_->setUniformValue("uRadius", radius);

    backgroundVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    backgroundVao_.release();

    occlusionProgram_->release();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

bool KnotGLWidget::showShoe() const
{
    return photo_ && photoInfo_.sneaker && restingRope_ && shoeIndexCount_ > 0;
}

void KnotGLWidget::uploadShoe()
{
    destroyShoe();

    const Photo::Sneaker &shoe = Photo::sneaker();

    if (!shoe.valid || !shoeProgram_ || !shoeProgram_->isLinked())
        return;

    const int vertexCount = shoe.vertices.size() / Photo::Sneaker::VertexFloats;

    shoeVao_.create();
    shoeVao_.bind();

    shoeBuffer_.create();
    shoeBuffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    shoeBuffer_.bind();
    shoeBuffer_.allocate(shoe.vertices.constData(), static_cast<int>(shoe.vertices.size() * sizeof(float)));

    const int stride = static_cast<int>(Photo::Sneaker::VertexFloats * sizeof(float));

    shoeProgram_->bind();

    shoeProgram_->enableAttributeArray(0);
    shoeProgram_->setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);
    shoeProgram_->enableAttributeArray(1);
    shoeProgram_->setAttributeBuffer(1, GL_FLOAT, static_cast<int>(3 * sizeof(float)), 3, stride);
    shoeProgram_->enableAttributeArray(2);
    shoeProgram_->setAttributeBuffer(2, GL_FLOAT, static_cast<int>(6 * sizeof(float)), 1, stride);
    shoeProgram_->enableAttributeArray(3);
    shoeProgram_->setAttributeBuffer(3, GL_FLOAT, static_cast<int>(7 * sizeof(float)), 1, stride);

    // the lace's shade: none until the lace is there
    shoeShadeBuffer_.create();
    shoeShadeBuffer_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    shoeShadeBuffer_.bind();

    const QVector<float> open(vertexCount, 0.0f);

    shoeShadeBuffer_.allocate(open.constData(), static_cast<int>(open.size() * sizeof(float)));

    shoeProgram_->enableAttributeArray(4);
    shoeProgram_->setAttributeBuffer(4, GL_FLOAT, 0, 1, static_cast<int>(sizeof(float)));

    shoeProgram_->release();

    shoeIndexBuffer_.create();
    shoeIndexBuffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    shoeIndexBuffer_.bind();
    shoeIndexBuffer_.allocate(shoe.indices.constData(), static_cast<int>(shoe.indices.size() * sizeof(quint32)));

    shoeVao_.release();

    shoeBuffer_.release();
    shoeShadeBuffer_.release();
    shoeIndexBuffer_.release();

    shoeIndexCount_ = shoe.indices.size();
    shoeShadeDirty_ = true;

    photoBytes_ += static_cast<qint64>(shoe.vertices.size() + vertexCount) * 4 +
                   static_cast<qint64>(shoe.indices.size()) * 4;

    // the floor's view of the room past the shoe (one channel is enough;
    // it lies over a rectangle, not repeated)
    shoeFloorTexture_ = loadTexture(shoe.floorShade);

    if (shoeFloorTexture_ != 0)
    {
        glBindTexture(GL_TEXTURE_2D, shoeFloorTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void KnotGLWidget::destroyShoe()
{
    if (shoeVao_.isCreated())
        shoeVao_.destroy();

    for (QOpenGLBuffer *buffer : {&shoeBuffer_, &shoeIndexBuffer_, &shoeShadeBuffer_})
    {
        if (buffer->isCreated())
            buffer->destroy();
    }

    if (shoeFloorTexture_ != 0)
        glDeleteTextures(1, &shoeFloorTexture_);

    shoeFloorTexture_ = 0;
    shoeIndexCount_ = 0;
}

void KnotGLWidget::updateShoeShade()
{
    shoeShadeDirty_ = false;

    if (!shoeShadeBuffer_.isCreated())
        return;

    const Photo::Sneaker &shoe = Photo::sneaker();
    const int vertexCount = shoe.vertices.size() / Photo::Sneaker::VertexFloats;

    QVector<float> hidden(vertexCount, 0.0f);

    const int samples = mesh_.centerline.size() / 4;

    if (samples >= 3 && restingRope_)
    {
        /*
         * The lace's centre line in millimetres, in pieces of about 0.4
         * radii, sorted into cubes of the reach of their shade. Each point
         * of the shoe adds up the view factors of the pieces near it,
         *     dF = 2 r |s x d| max(n . d, 0) ds / (pi D^2)
         * (as for the table, bakeSurfaceOcclusion).
         */
        const float mm = worldPerMm();
        const double r = 0.5 * photoInfo_.rope.diameterMm;

        const float length = mesh_.strandLength.isEmpty() ? 0.0f : mesh_.strandLength.first();
        const int wanted = std::clamp(static_cast<int>(length / (0.4f * mesh_.centerline.at(3))), 16, 8192);
        const int stride = std::max(1, samples / wanted);

        QVector<KnotMath::Vec3> line;

        for (int s = 0; s < samples; s += stride)
        {
            line.push_back(KnotMath::Vec3(
                mesh_.centerline.at(4 * s) / mm,
                mesh_.centerline.at(4 * s + 1) / mm,
                mesh_.centerline.at(4 * s + 2) / mm));
        }

        const int pieces = line.size();
        const double reach = 12.0 * r;

        struct Piece
        {
            KnotMath::Vec3 middle;
            KnotMath::Vec3 along;
        };

        QHash<qint64, QVector<Piece>> cubes;

        auto key = [reach](const KnotMath::Vec3 &p)
        {
            const qint64 i = static_cast<qint64>(std::floor(p.x / reach)) + 1000;
            const qint64 j = static_cast<qint64>(std::floor(p.y / reach)) + 1000;
            const qint64 k = static_cast<qint64>(std::floor(p.z / reach)) + 1000;

            return (i * 4096 + j) * 4096 + k;
        };

        for (int i = 0; i < pieces; ++i)
        {
            const KnotMath::Vec3 a = line.at(i);
            const KnotMath::Vec3 b = line.at((i + 1) % pieces);

            cubes[key((a + b) * 0.5)].push_back(Piece{(a + b) * 0.5, b - a});
        }

        for (int v = 0; v < vertexCount; ++v)
        {
            const float *f = shoe.vertices.constData() + v * Photo::Sneaker::VertexFloats;

            const KnotMath::Vec3 p(f[0], f[1], f[2]);
            const KnotMath::Vec3 n(f[3], f[4], f[5]);

            const qint64 i0 = static_cast<qint64>(std::floor(p.x / reach)) + 1000;
            const qint64 j0 = static_cast<qint64>(std::floor(p.y / reach)) + 1000;
            const qint64 k0 = static_cast<qint64>(std::floor(p.z / reach)) + 1000;

            double sum = 0.0;

            for (qint64 i = i0 - 1; i <= i0 + 1; ++i)
            {
                for (qint64 j = j0 - 1; j <= j0 + 1; ++j)
                {
                    for (qint64 k = k0 - 1; k <= k0 + 1; ++k)
                    {
                        const auto found = cubes.constFind((i * 4096 + j) * 4096 + k);

                        if (found == cubes.constEnd())
                            continue;

                        for (const Piece &piece : *found)
                        {
                            KnotMath::Vec3 d = piece.middle - p;

                            const double distance = d.length();

                            if (distance > reach || distance < 1.0e-9)
                                continue;

                            d = d / distance;

                            const double facing = KnotMath::Vec3::dot(n, d);

                            if (facing <= 0.0)
                                continue;

                            const double ds = piece.along.length();

                            if (ds < 1.0e-12)
                                continue;

                            const double sine = KnotMath::Vec3::cross(piece.along / ds, d).length();
                            const double clamped = std::max(distance, 1.2 * r);

                            sum += 2.0 * r * sine * facing * ds / (3.14159265 * clamped * clamped);
                        }
                    }
                }
            }

            hidden[v] = float(std::min(sum, 0.9));
        }
    }

    shoeShadeBuffer_.bind();
    shoeShadeBuffer_.write(0, hidden.constData(), static_cast<int>(hidden.size() * sizeof(float)));
    shoeShadeBuffer_.release();
}

void KnotGLWidget::drawShoe(const QMatrix4x4 &viewProjection)
{
    if (!showShoe())
        return;

    if (shoeShadeDirty_)
        updateShoeShade();

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // the lining is seen from inside: both sides
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    shoeProgram_->bind();

    setShadowUniforms(*shoeProgram_, lighting_.knotShadows);
    setPhotoUniforms(*shoeProgram_);

    shoeProgram_->setUniformValue("uViewProjection", viewProjection);
    shoeProgram_->setUniformValue("uScale", worldPerMm());
    shoeProgram_->setUniformValue("uCameraPosition", computeCameraPosition() + panOffset_);
    shoeProgram_->setUniformValue("uReflections", lighting_.reflections ? 1.0f : 0.0f);

    const QColor canvas = photoInfo_.canvas;

    shoeProgram_->setUniformValue(
        "uCanvas",
        QVector3D(
            std::pow(static_cast<float>(canvas.redF()), 2.2f),
            std::pow(static_cast<float>(canvas.greenF()), 2.2f),
            std::pow(static_cast<float>(canvas.blueF()), 2.2f)));

    shoeVao_.bind();

    glDrawElements(GL_TRIANGLES, shoeIndexCount_, GL_UNSIGNED_INT, nullptr);

    shoeVao_.release();
    shoeProgram_->release();
}

void KnotGLWidget::drawPhotoBackground(const QMatrix4x4 &viewProjection)
{
    if (!photoBackgroundProgram_ || !photoBackgroundProgram_->isLinked() ||
        environmentTexture_ == 0)
    {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);

    photoBackgroundProgram_->bind();

    setPhotoUniforms(*photoBackgroundProgram_);

    photoBackgroundProgram_->setUniformValue("uInverseViewProjection", viewProjection.inverted());
    photoBackgroundProgram_->setUniformValue("uEye", computeCameraPosition() + panOffset_);

    backgroundVao_.bind();

    glDrawArrays(GL_TRIANGLES, 0, 3);

    backgroundVao_.release();
    photoBackgroundProgram_->release();

    glDepthMask(GL_TRUE);
}

void KnotGLWidget::drawPhotoFloor(const QMatrix4x4 &viewProjection)
{
    if (!photoFloorProgram_ || !photoFloorProgram_->isLinked() ||
        floorColorTexture_ == 0 || surfaceVertexCount_ == 0)
    {
        return;
    }

    const float mm = worldPerMm();

    const QVector3D eye = computeCameraPosition() + panOffset_;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    photoFloorProgram_->bind();

    setShadowUniforms(*photoFloorProgram_, lighting_.floorShadow);
    setPhotoUniforms(*photoFloorProgram_);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, floorColorTexture_);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, floorNormalTexture_);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, floorRoughnessTexture_);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, occlusionTexture_);
    glActiveTexture(GL_TEXTURE0);

    photoFloorProgram_->setUniformValue("uFloorColor", 5);
    photoFloorProgram_->setUniformValue("uFloorNormal", 6);
    photoFloorProgram_->setUniformValue("uFloorRoughness", 7);
    photoFloorProgram_->setUniformValue("uOcclusion", 8);

    photoFloorProgram_->setUniformValue("uViewProjection", viewProjection);
    photoFloorProgram_->setUniformValue("uTile", static_cast<float>(photoInfo_.surfaceTileMm));
    photoFloorProgram_->setUniformValue("uCameraPosition", eye);

    photoFloorProgram_->setUniformValue("uOcclusionArea", occlusionArea_);
    photoFloorProgram_->setUniformValue(
        "uUseOcclusion",
        lighting_.contactShadow && occlusionTexture_ != 0 && occlusionArea_.z() > 0.0f ? 1 : 0);

    const QColor rope = photoInfo_.rope.color;

    photoFloorProgram_->setUniformValue(
        "uRopeAlbedo",
        QVector3D(
            std::pow(static_cast<float>(rope.redF()), 2.2f),
            std::pow(static_cast<float>(rope.greenF()), 2.2f),
            std::pow(static_cast<float>(rope.blueF()), 2.2f)));

    // the room's own floor takes over where the room's picture has it
    const QVector3D middle(0.5f * (meshMin_.x() + meshMax_.x()), 0.0f, 0.5f * (meshMin_.z() + meshMax_.z()));

    float fade = 0.0f;

    if (photoInfo_.tableWidthMm <= 0.0)
    {
        const float metre = 1000.0f * mm;
        const float radius = static_cast<float>(photoInfo_.roomRadiusM) * metre;
        const float height = static_cast<float>(photoInfo_.cameraHeightM) * metre;

        fade = std::sqrt(std::max(radius * radius - height * height, 1.0f));
    }

    photoFloorProgram_->setUniformValue("uFadeCentre", QVector2D(middle.x(), middle.z()));
    photoFloorProgram_->setUniformValue("uFadeRadius", fade);

    photoFloorProgram_->setUniformValue("uReflections", lighting_.reflections ? 1.0f : 0.0f);

    // the sneaker's shade
    const bool shoeShade = showShoe() && shoeFloorTexture_ != 0;

    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, shoeShade ? shoeFloorTexture_ : 0);
    glActiveTexture(GL_TEXTURE0);

    photoFloorProgram_->setUniformValue("uShoeShade", 9);
    photoFloorProgram_->setUniformValue("uUseShoeShade", shoeShade ? 1 : 0);

    if (shoeShade)
    {
        const QRectF area = Photo::sneaker().floorArea;

        photoFloorProgram_->setUniformValue(
            "uShoeArea",
            QVector4D(float(area.left()), float(area.top()), float(area.right()), float(area.bottom())) * mm);
    }

    surfaceVao_.bind();

    glDrawArrays(GL_TRIANGLES, 0, surfaceVertexCount_);

    surfaceVao_.release();
    photoFloorProgram_->release();

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
}

void KnotGLWidget::developPhotoFrame(const QMatrix4x4 &view, const QMatrix4x4 &projection)
{
    QOpenGLExtraFunctions *extra = context()->extraFunctions();

    const int width = photoFrameWidth_;
    const int height = photoFrameHeight_;

    // the samples of every pixel averaged, the depth of one of them
    glBindFramebuffer(GL_READ_FRAMEBUFFER, photoFramebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFramebuffer_);

    extra->glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    extra->glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

    glBindTexture(GL_TEXTURE_2D, resolvedColor_);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    glViewport(0, 0, width, height);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);

    developProgram_->bind();

    setPhotoUniforms(*developProgram_);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, resolvedColor_);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, resolvedDepth_);
    glActiveTexture(GL_TEXTURE0);

    developProgram_->setUniformValue("uColor", 5);
    developProgram_->setUniformValue("uDepth", 6);
    developProgram_->setUniformValue("uTexel", QVector2D(1.0f / width, 1.0f / height));
    developProgram_->setUniformValue("uInverseProjection", projection.inverted());
    developProgram_->setUniformValue("uInverseView", view.inverted());
    developProgram_->setUniformValue("uEye", computeCameraPosition() + panOffset_);

    /*
     * The camera: a full-frame sensor (24 mm high) behind a lens with the
     * view's angle (f = 12 mm / tan(half the angle), about 29 mm) at f/8,
     * focused on the knot. Everything in millimetres, from the rope.
     */
    constexpr float SensorHeight = 24.0f;
    constexpr float FNumber = 8.0f;

    const float focal = 0.5f * SensorHeight / std::tan(qDegreesToRadians(0.5f * FieldOfView));
    const float focus = std::max(distance_, 0.01f);
    const float focusMm = std::max(focus / worldPerMm(), focal * 1.5f);

    // blur radius on the sensor per |D - S| / D, in pixels
    const float cocScale =
        0.5f * focal * focal / (FNumber * (focusMm - focal)) / SensorHeight * static_cast<float>(height);

    developProgram_->setUniformValue("uFocus", focus);
    developProgram_->setUniformValue("uCocScale", cocScale);
    developProgram_->setUniformValue("uMaxCoc", 0.03f * static_cast<float>(height));
    developProgram_->setUniformValue(
        "uExposure",
        photoExposure_ * static_cast<float>(lighting_.brightness) / 100.0f);

    backgroundVao_.bind();

    glDrawArrays(GL_TRIANGLES, 0, 3);

    backgroundVao_.release();
    developProgram_->release();

    for (const GLenum unit : {GL_TEXTURE5, GL_TEXTURE6})
    {
        glActiveTexture(unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glActiveTexture(GL_TEXTURE0);

    glDepthMask(GL_TRUE);
}
