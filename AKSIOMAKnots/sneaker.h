#ifndef SNEAKER_H
#define SNEAKER_H

#include "knotmath.h"
#include "ropephysics.h"

#include <QRectF>
#include <QString>
#include <QVector3D>
#include <QVector>

/*
 * Photo mode's "Sneaker" scene: the knot tied in the lace of a canvas
 * sneaker. The shoe was made in Blender (tools/sneaker/make_sneaker.py):
 * its surface, the eyelets on both sides of the tongue and the shade it
 * casts on the floor are in the resources.
 *
 * The lace runs criss-cross through the eyelets, as laces usually do, and
 * comes up out of the top pair; there its two ends are the two ends of the
 * knot, cut open next to the eyelets. The lace is then one closed loop,
 * the same knot: the lacing is two strands twisted around each other and
 * joined at the bottom, which comes undone by untwisting (Reidemeister I
 * moves), so it adds no knotting. Every knot can be tied this way; those
 * with more crossings are too large to lie on a shoe.
 */
namespace Photo
{

struct Sneaker
{
    bool valid = false;
    QString error;

    /*
     * The surface, in millimetres: the floor is y = 0, the toe points
     * along +x, the shoe's middle line is z = 0. Per vertex: position (3),
     * normal (3), ambient occlusion (1 = open), material (0 canvas,
     * 1 rubber sole, 2 metal, 3 lining, 4 rubber toe cap, 5 tongue).
     */
    static constexpr int VertexFloats = 8;

    QVector<float> vertices;
    QVector<quint32> indices;

    QVector3D minimum;
    QVector3D maximum;

    // the lace's thickness, an eyelet's hole and the canvas, in mm
    double laceDiameter = 4.5;
    double holeRadius = 2.7;
    double shell = 2.2;

    struct Eyelet
    {
        // from the toe (0) to the collar
        int row = 0;
        bool left = false;

        // the middle of the eyelet on the top of the flap, and the hole's
        // axis (out of the shoe)
        QVector3D centre;
        QVector3D axis;

        // distance of the slot's edge from the shoe's middle line there
        double edge = 0.0;
    };

    QVector<Eyelet> eyelets;
    int rows = 0;

    const Eyelet *eyelet(int row, bool left) const;

    // height of the top of the shoe over (x, z) (0 off the shoe) and its
    // slope; and of the tongue under the flaps
    double heightAt(double x, double z, double *slopeX = nullptr, double *slopeZ = nullptr) const;
    double tongueAt(double x, double z) const;

    // the floor's view of the room past the shoe (a picture in the
    // resources) and where it lies on the floor (x, z in mm)
    QString floorShade;
    QRectF floorArea;

    // the top of the shoe, a 1 mm grid
    double gridX0 = 0.0;
    double gridZ0 = 0.0;
    int gridNx = 0;
    int gridNz = 0;
    QVector<float> height;

    // the tongue's top on a coarser grid (from the model)
    double tongueX0 = 0.0;
    double tongueZ0 = 0.0;
    double tongueStep = 2.0;
    int tongueNx = 0;
    int tongueNz = 0;
    QVector<float> tongue;
};

// the sneaker in the resources (read once)
const Sneaker &sneaker();

/*
 * Ties the knot (a closed centre line with tubes of `radius`) in the lace
 * of the sneaker, as a lace of `material`: the knot is laid down flat over
 * the lacing, cut open at its point nearest to the top eyelets and joined
 * there to the lace; then it falls onto the shoe and comes to rest
 * (settleRope), the lacing held fast in the eyelets.
 *
 * The result's points are the whole lace, a closed loop (the knot's own
 * coordinates; its pose takes them to the shoe's frame in the knot's
 * units: millimetres times radius / (lace diameter / 2), floor y = 0).
 */
struct TiedLace
{
    RestingRope rope;

    // the knot's part of the lace (points knotFrom .. knotTo), and where
    // along the knot (a fraction of its length) it starts; the cut leaves
    // out gapFraction of the knot
    int knotFrom = 0;
    int knotTo = 0;
    double cutFraction = 0.0;
    double gapFraction = 0.0;

    // millimetres -> the knot's units
    double unitsPerMm = 1.0;

    // the points held fast (the lacing through the eyelets)
    QVector<bool> pinned;
};

TiedLace tieOnSneaker(
    const QVector<KnotMath::Vec3> &curve,
    double radius,
    const RopeMaterial &material);

} // namespace Photo

#endif // SNEAKER_H
