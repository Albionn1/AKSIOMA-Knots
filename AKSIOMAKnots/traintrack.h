#ifndef TRAINTRACK_H
#define TRAINTRACK_H

#include "knotmath.h"
#include "ropephysics.h"

#include <QMatrix4x4>
#include <QVector>

/*
 * Photo mode's "Toy train" scene: the knot as the closed track of a wooden
 * toy train. The two things a beginner has to see about a knot are built
 * into the toy:
 *
 *   a knot is one closed loop: the train goes round and round and never
 *   comes to an end;
 *
 *   a crossing is a place where the loop passes over itself: every crossing
 *   is a bridge, with the train going over it now and under it later.
 *
 * The track is the knot's own drawing (diagramsFromCurve), opened up
 * (openDiagram) until a train fits: the level of every piece comes from over
 * and under, so it is the same knot, which buildTrainTrack checks with the
 * polynomials of the finished centre line.
 */
namespace Photo
{

// Sizes of the toy, in millimetres. The track is the usual 40 mm wide; the
// level of the rails over the floor on the ground and on a bridge.
constexpr double TrackWidthMm = 40.0;
constexpr double TrackThicknessMm = 10.0;
constexpr double TrainWidthMm = 34.0;
constexpr double TrainHeightMm = 40.0;
constexpr double GroundLevelMm = TrackThicknessMm;
constexpr double BridgeLevelMm = GroundLevelMm + TrainHeightMm + 4.0 + TrackThicknessMm;

struct TrainTrack
{
    bool valid = false;
    QString error;

    // stopped on request (RopeProgress): there is no track
    bool cancelled = false;

    // The middle of the top of the track, millimetres, y up, the floor at
    // y = 0, closed, points evenly spaced; its length.
    QVector<KnotMath::Vec3> path;
    double length = 0.0;

    // millimetres -> view units (the track's half width is the "tube radius")
    double unitsPerMm = 1.0;

    // the centre line in view units, as a rope lying there would be
    // (pose: none, the floor at y = 0)
    RestingRope rope;

    // the track and the blocks under it
    KnotMath::Mesh mesh;

    // every bridge, numbered, its label on the deck
    QVector<KnotMath::CrossingInfo> bridges;

    // how it came out (train_test): the steepest slope, the tightest
    // bend (mm), the closest two pieces of track side by side (mm, middle
    // to middle), the blocks under the bridges and how long it took
    double steepest = 0.0;
    double tightestBend = 0.0;
    double closestStrands = 0.0;
    int blocks = 0;
    int piers = 0;
};

/*
 * The track of the knot (a closed curve). Takes a moment: meant for a worker
 * thread. A knot that cannot be drawn cleanly comes back not valid.
 */
TrainTrack buildTrainTrack(
    const QVector<KnotMath::Vec3> &curve,
    RopeProgress *progress = nullptr);

/*
 * The train: a locomotive and two wagons, made here from the shapes a wooden
 * toy is made of. Per vertex: position (mm; x forward, y up, the top of the
 * track at y = 0), normal, occlusion (1 = open), material (0 red, 1 blue,
 * 2 yellow, 3 plain wood, 4 black, 5 brass, 6 green, 7 dark window).
 */
struct TrainModel
{
    static constexpr int VertexFloats = 8;
    static constexpr int Vehicles = 3;

    QVector<float> vertices;
    QVector<quint32> indices;

    // the indices of each vehicle
    int firstIndex[Vehicles] = {};
    int indexCount[Vehicles] = {};

    // how far behind the locomotive's middle each vehicle's middle runs,
    // and half the distance between its axles (mm)
    double behind[Vehicles] = {};
    double axles[Vehicles] = {};
};

const TrainModel &trainModel();

// The turn and place of a vehicle whose middle is `along` millimetres along
// the path, its axles on the track.
QMatrix4x4 placeVehicle(
    const QVector<KnotMath::Vec3> &path,
    double length,
    double along,
    double halfAxles);

} // namespace Photo

#endif // TRAINTRACK_H
