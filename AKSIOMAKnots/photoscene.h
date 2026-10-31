#ifndef PHOTOSCENE_H
#define PHOTOSCENE_H

#include <QColor>
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>
#include <QVector>

/*
 * Photo mode: the knot as a real rope or lace lying on a real surface in a
 * real, sunlit room. The room is a 360 degree HDR photograph (an HDRI from
 * Poly Haven, CC0, unclipped, so the sun in it keeps its true brightness):
 * it is seen behind the knot and in its reflections, its light falls on the
 * knot from every side, and the sun in it is a light of its own, with its
 * true direction, size and strength, that casts the shadows.
 *
 * Everything is to the scale of the real rope: the thickness of the rope
 * (ropeMm) sets how large the table, the boards of the floor, the room and
 * the blur of the camera are.
 */
namespace Photo
{

enum class Scene
{
    // hemp rope on a wooden table in a ship's lounge, in the low sun
    LoungeTable = 0,

    // a braided cotton shoelace on the wooden floor of a sunlit ballroom
    BallroomFloor,

    // the knot tied in the lace of a canvas sneaker on that floor
    // (sneaker.h)
    Sneaker,

    Count
};

// What the rope is made of: its look and how it lies down.
struct RopeMaterial
{
    // 1: three twisted strands of hemp (laid rope), 2: cotton yarns
    // braided over and under each other (a shoelace)
    int construction = 1;

    // real thickness (diameter)
    double diameterMm = 12.0;

    // Stiffness against bending, as the bending length of the cantilever
    // test (ASTM D1388): the length c with B = w c^3, B the bending
    // stiffness and w the weight per length. A rope that bends easily
    // under its own weight has a short one.
    double bendingLengthMm = 50.0;

    // stiffness against twisting, as a share of that against bending
    double twistStiffness = 0.7;

    // Natural fibre takes a set: bent tighter than this radius it creeps
    // towards the new shape (at this rate, per second) and keeps part of it.
    double setRadiusMm = 70.0;
    double setRate = 0.8;

    // the curl left in a relaxed rope by the reel it was wound on
    double curlRadiusMm = 110.0;

    // how much of the shape it was tied in a rope keeps when let go
    double tiedSet = 0.55;

    // friction on the surface it lies on, and on itself
    double frictionSurface = 0.5;
    double frictionSelf = 0.7;

    // colour (sRGB) and roughness of the fibres
    QColor color;
    float roughness = 0.7f;
};

struct SceneInfo
{
    // the room (Radiance .hdr, in the resources)
    QString environment;

    // The room as a shape around the camera, for the parts of it seen
    // past the table: where the photograph was taken, in metres above the
    // room's floor, and how far away its walls are (about).
    double cameraHeightM = 1.6;
    double roomRadiusM = 8.0;

    // the surface under the knot: colour, normal map (OpenGL convention)
    // and roughness pictures, and the real width of one picture
    QString surfaceColor;
    QString surfaceNormal;
    QString surfaceRoughness;
    double surfaceTileMm = 1500.0;

    // The surface is a table top of this size (and thickness), standing
    // tableHeightM above the room's floor; or, with no size, the room's
    // own floor.
    double tableWidthMm = 0.0;
    double tableDepthMm = 0.0;
    double tableThicknessMm = 0.0;
    double tableHeightM = 0.0;

    // the room is turned about the vertical by this many degrees (where
    // the knot lies in the room)
    float environmentYaw = 0.0f;

    // What the camera makes of the light on the surface: white paper there
    // comes out this bright (1.45: a little under, moody; about pi: as a
    // meter set for daylight has it).
    float exposure = 1.45f;

    // how much the room is darkened around the knot when nothing is being
    // shown on stage (0: the picture as the camera took it)
    float calmStage = 0.7f;

    RopeMaterial rope;

    // the knot is tied in the lace of the sneaker (sneaker.h)
    bool sneaker = false;

    // colour of the sneaker's canvas (sRGB)
    QColor canvas;
};

SceneInfo sceneInfo(
    Scene scene);

/*
 * A room ready for lighting. Directions are in the room's own frame:
 * y up, the middle of the picture straight ahead along -z.
 */
struct Environment
{
    bool valid = false;
    QString error;

    // the whole picture, linear RGB, top row first
    int width = 0;
    int height = 0;
    QVector<float> pixels;

    // Irradiance of the room without its sun, from the upper half of the
    // room only (what lies below the horizon is the surface the knot lies
    // on, lit by the room): 9 spherical harmonic coefficients (RGB),
    // already multiplied by the cosine lobe, so that E(n) = sum c[i] Y_i(n).
    QVector3D irradiance[9];

    // the sun: direction towards it, its irradiance on a surface facing
    // it, and its angular radius (radians)
    QVector3D sunDirection = QVector3D(0.0f, 1.0f, 0.0f);
    QVector3D sunIrradiance;
    float sunRadius = 0.005f;

    // E(n) from the coefficients
    QVector3D irradianceAt(
        const QVector3D &normal) const;
};

// Reads a Radiance .hdr picture (RGBE, run-length encoded or flat) and
// finds its sun. About half a second for 4096 x 2048.
Environment loadEnvironment(
    const QString &path);

// The turn that lays a knot down flat: its thinnest direction up, its
// longest along x, about the middle of the points. A rotation, never a
// reflection, so the knot keeps its handedness.
QMatrix4x4 flatPose(
    const QVector<QVector3D> &points);

} // namespace Photo

#endif // PHOTOSCENE_H
