#ifndef PHOTOSCENE_H
#define PHOTOSCENE_H

#include <QColor>
#include <QString>
#include <QVector3D>
#include <QVector>

/*
 * Photo mode: the knot as a real rope or lace lying on a real surface, lit
 * by a photographed room. The room is a 360 degree HDR photograph (an HDRI
 * from Poly Haven, CC0): it is seen behind the knot and in its reflections,
 * its soft light lights the knot from every side, and its brightest part
 * (a window, a lamp) becomes a light of its own that casts shadows.
 */
namespace Photo
{

enum class Scene
{
    // hemp rope on a wooden table in a bright photo studio
    StudioTable = 0,

    // a braided cotton shoelace on a wooden floor at home
    Shoelaces,

    Count
};

struct SceneInfo
{
    // the room (Radiance .hdr, in the resources)
    QString environment;

    // the surface under the knot: colour, normal map (OpenGL convention)
    // and roughness
    QString floorColor;
    QString floorNormal;
    QString floorRoughness;

    // real width of one tile of the surface pictures, in millimetres
    double floorTileMm = 1500.0;

    // the room is turned about the vertical by this many degrees, so that
    // its main light falls on the knot from a good side
    float environmentYaw = 0.0f;

    // the main light is kept at least this many degrees above the table
    // (its compass direction stays), as a photographer would place it: a
    // window low in the picture would throw shadows far across the floor
    float keyMinElevation = 25.0f;

    // tube material (KnotGLWidget::setSurface: 3 rope, 4 braided lace),
    // its colour and roughness
    int material = 3;
    QColor color;
    float roughness = 0.6f;

    // real thickness of the rope: sets the size of everything else
    double ropeMm = 10.0;
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

    // Irradiance of the room without its main light: 9 spherical harmonic
    // coefficients (RGB), already multiplied by the cosine lobe, so that
    // E(n) = sum c[i] * Y_i(n).
    QVector3D irradiance[9];

    // the main light: direction towards it, its irradiance on a surface
    // facing it, and its angular radius (radians)
    QVector3D keyDirection = QVector3D(0.0f, 1.0f, 0.0f);
    QVector3D keyIrradiance;
    float keyRadius = 0.05f;

    // E(n) from the coefficients
    QVector3D irradianceAt(
        const QVector3D &normal) const;
};

// Reads a Radiance .hdr picture (RGBE, run-length encoded or flat) and
// finds its light. Slow for big pictures (some 100 ms for 2048 x 1024).
Environment loadEnvironment(
    const QString &path);

} // namespace Photo

#endif // PHOTOSCENE_H
