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

// Sizes of the model railway, in millimetres. The sleepers are 40 long and the
// rails 20 apart; the bed of ballast is wider. The level of the tops of the
// rails over the floor, on the ground and on a bridge, and how deep a bridge's
// deck is under its rails.
constexpr double TrackWidthMm = 40.0;
constexpr double BedWidthMm = 54.0;
constexpr double RailApartMm = 20.0;
constexpr double TrainWidthMm = 34.0;
constexpr double TrainHeightMm = 40.0;
constexpr double DeckDepthMm = 15.0;
constexpr double GroundLevelMm = 12.0;
constexpr double BridgeLevelMm = GroundLevelMm + TrainHeightMm + 4.0 + DeckDepthMm;

// a ramp stands on a bank of ballast until it is this far above the ground
// track; higher, it is a bridge on piers
constexpr double BankHeightMm = 26.0;

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

    /*
     * The train's journey round the track: every time it passes a bridge, in
     * order, and whether it goes over or under. Each bridge comes twice, once
     * each way. This is the knot's Gauss code, the list that knot theory
     * writes a knot down with.
     */
    struct Pass
    {
        double along = 0.0;     // mm along the track
        int bridge = 0;
        bool over = false;
    };

    QVector<Pass> journey;

    // over and under take turns all the way round
    bool alternating = false;

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
 * The train: a locomotive and two wagons, a scale model made here from turned
 * and cut shapes. Per vertex: position (mm; x forward, y up, the top of the
 * track at y = 0), normal, occlusion (1 = open), material (0 red, 1 blue,
 * 2 cream, 3 plain wood, 4 black, 5 brass, 6 green, 7 window, 8 steel,
 * 9 the lit lamp). Bodies stand still on their frames; the wheels turn on
 * their axles and the side rods go round with the crank pins.
 */
struct TrainModel
{
    static constexpr int VertexFloats = 8;
    static constexpr int Vehicles = 3;

    // the axle's height over the track, and the wheels' radius
    static constexpr double AxleHeight = 7.0;
    static constexpr double WheelRadius = 7.0;
    static constexpr double CrankRadius = 3.6;

    struct Part
    {
        int firstIndex = 0;
        int indexCount = 0;
    };

    QVector<float> vertices;
    QVector<quint32> indices;

    Part parts[Vehicles];
    Part wheels;
    Part rods;

    // how far behind the locomotive's middle each vehicle's middle runs,
    // half the distance between its outer axles (mm), and where its axles are
    double behind[Vehicles] = {};
    double axles[Vehicles] = {};
    QVector<double> axleX[Vehicles];
};

const TrainModel &trainModel();

// one thing to draw: a part of the model, and where it stands
struct TrainPiece
{
    QMatrix4x4 place;
    int firstIndex = 0;
    int indexCount = 0;
};

// The turn and place of a vehicle whose middle is `along` millimetres along
// the path, its axles on the track.
QMatrix4x4 placeVehicle(
    const QVector<KnotMath::Vec3> &path,
    double length,
    double along,
    double halfAxles);

// Everything of the train, with the locomotive's middle `along` millimetres
// along the path: bodies, wheels turned as far as they have rolled, side rods.
QVector<TrainPiece> placeTrain(
    const QVector<KnotMath::Vec3> &path,
    double length,
    double along);

} // namespace Photo

#endif // TRAINTRACK_H
