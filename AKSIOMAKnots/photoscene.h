#ifndef PHOTOSCENE_H
#define PHOTOSCENE_H

#include <aksioma/environment.h>
#include <aksioma/photostage.h>
#include <aksioma/pose.h>
#include <aksioma/rod.h>

#include <QColor>
#include <QImage>
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

    // the knot as the track of a wooden toy train (traintrack.h), on the
    // paving of a courtyard at dusk, in the warm light from its windows
    TrainTrack,

    // the same, on the stone of the garden by day
    TrainDay,

    Count
};

// What the rope is made of: how it lies down (the engine's rod: stiffness,
// damping, set, curl, friction) and its look.
struct RopeMaterial : Aksioma::Physics::RodMaterial
{
    // 1: three twisted strands of hemp (laid rope), 2: cotton yarns
    // braided over and under each other (a shoelace), 3: the wooden track
    // of the toy train
    int construction = 1;

    // how much of the shape it was tied in a rope keeps when let go
    double tiedSet = 0.55;

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

    // the room's floor is dark stone, as the table always is, this much
    // paler
    bool stoneFloor = false;
    float stoneLight = 1.0f;

    // the knot is the track of the toy train (traintrack.h)
    bool train = false;

    /*
     * Dusk: no sun in the picture to take the main light from, so the scene
     * says where it comes from: low, from the side, as light from lit windows
     * across a square does. The direction towards it, how strong it is, and
     * how wide the source looks (the softness of its shadows, a tangent).
     */
    bool evening = false;
    QVector3D sideLightDirection;
    QVector3D sideLight;
    float sideLightWidth = 0.05f;
};

SceneInfo sceneInfo(
    Scene scene);

// the room's light (aksioma/environment.h)
using Aksioma::Environment;
using Aksioma::loadEnvironment;
using Aksioma::forgetPreloaded;
using Aksioma::preloadedPicture;
using Aksioma::flatPose;

// What the renderer needs of a scene (AKSIOMA Engine's PhotoStage): the room,
// the surface and the look of the rope; its physics stays here.
Aksioma::PhotoStage stageOf(const SceneInfo &info);

// Reads the room and the pictures of a scene ahead of time, on any thread
// (Aksioma::preloadEnvironment).
void preloadScene(const SceneInfo &info);

} // namespace Photo

#endif // PHOTOSCENE_H
