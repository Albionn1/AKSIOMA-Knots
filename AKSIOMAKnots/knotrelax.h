#ifndef KNOTRELAX_H
#define KNOTRELAX_H

#include "knotmath.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>

#include <random>

namespace KnotMath
{

/*
 * Moves a closed curve without ever letting a strand pass through another,
 * so the knot type never changes - only the shape does.
 *
 * Relax: every point pushes the others away (energy sum 1/r^2 over pairs of
 * points), springs keep the points evenly spaced and a little stiffness
 * smooths sharp bends, while the total length stays the same. The knot
 * opens up towards an "ideal" shape and its pictures need fewer crossings.
 *
 * Scramble: random swirls and a squeeze crumple the curve into a tangle.
 * The swirls are smoothed along the curve and strands keep about the
 * thickness of the tube apart (setTubeRadius()), so it still looks like a
 * rope being crumpled, not a wire being crushed.
 *
 * Pull: one point follows the mouse and the rest of the curve is dragged
 * after it like a thick rope that neither stretches nor bends sharply;
 * strands in the way are shoved aside (beginPull()).
 *
 * No strand passes through another: in every step no point moves more than
 * 0.4 times the smallest distance between two segments of the polygon that
 * do not share a point. Every point of a segment then moves at most that
 * far, so two such segments come at most 0.8 times their distance closer
 * and cannot touch. (Rescaling the whole curve afterwards changes nothing.)
 */
class KnotRelaxation
{
    Q_DECLARE_TR_FUNCTIONS(KnotMath::KnotRelaxation)

public:
    // Takes a closed curve (in the program's coordinates) and resamples it
    // at evenly spaced points. False if the curve cannot be used; the
    // reason is in error(). Resampling a curve whose strands come very
    // close could change the knot, so the Alexander polynomial is checked
    // before and after.
    bool start(const QVector<Vec3> &curve);

    bool isValid() const;
    QString error() const;

    // Scramble for the next scrambleSteps() steps (random from `seed`),
    // relax after that.
    void beginScramble(quint32 seed);
    bool scrambling() const;

    // Runs steps for at most `milliseconds`, but at most `scrambleLimit`
    // scramble steps (so that scrambling can be watched); returns the
    // number of steps. Relaxing stops by itself once the curve has settled.
    int advance(double milliseconds, int scrambleLimit = scrambleSteps());

    // Grab and pull: the point of the curve nearest to `grabbed` (in the
    // coordinates of curve()) is pulled towards the target until
    // endPull(), and the rest of the curve follows like a rope; no strand
    // passes through another. While a pull goes on (and after it, until
    // the curve is relaxed again) curve() keeps its place and size, so the
    // rope stays under the mouse.
    void beginPull(const Vec3 &grabbed);
    void setPullTarget(const Vec3 &target);
    void endPull();
    bool pulling() const;

    // true when relaxing has (nearly) stopped moving the curve
    bool settled() const;

    // steps done since start()
    int steps() const;

    // 0..1 while scrambling
    double scrambleProgress() const;

    // The curve centred at the origin and scaled like the presets (outer
    // radius 2.7).
    QVector<Vec3> curve() const;

    // Radius (in the coordinates of curve()) of the tube the caller would
    // like to show; scrambling keeps the strands that far apart (as far
    // as it can).
    void setTubeRadius(double radius);

    // Largest tube radius (in the coordinates of curve(), never more than
    // `requested`) for which no two pieces of the tube that are more than
    // a few radii apart along the curve touch, and no bend is so sharp
    // that the tube folds into itself.
    double tubeLimit(double requested) const;

    // Rotation (unit axis, angle) after which looking along z shows the
    // fewest crossings; false if the current view is already best.
    bool bestViewRotation(Vec3 &axis, double &angle) const;

    // Turns the whole curve (does not change its shape).
    void rotate(const Vec3 &axis, double angle);

    static constexpr int scrambleSteps() { return 420; }

private:
    void relaxStep();
    void scrambleStep();

    // one step of a pull; returns how far the points moved at most
    double pullStep();

    // Moves every point by `move` (scaled down where needed so that no
    // strand can pass through another). Needs measureGaps() for the
    // current points. Relaxing and scrambling take out the motion of the
    // whole curve and hold back only the points near a close strand. A
    // pull (`pulling`) keeps that motion (the hand moves the rope) and
    // scales all moves by the same factor: the edges of the new shape and
    // of the old one are (about) at most 1 long, and so are the edges of
    // a shape in between, so the rope never stretches where it is held
    // back.
    void applyMoves(QVector<Vec3> &move, double maxStep, bool pulling = false);

    // How far the curve has changed its shape since `before` (the same
    // points earlier): the largest distance of a point from that polygon.
    double shapeChange(const QVector<Vec3> &before) const;

    // Takes out the part of `move` that only shifts or turns the whole
    // curve (the forces have none, but the step limits can add some, and
    // the knot would slowly drift and spin).
    void removeRigidMotion(QVector<Vec3> &move) const;

    // For every segment the smallest distance to a segment that does not
    // share a point with it (segmentGap_, the smallest of all in gap_), and
    // the pairs of such segments closer than `contactRange` (contacts_).
    void measureGaps(double contactRange);

    // repulsion between the closest points of the contacts_, fading out at
    // distance `range`, added to `force`; contacts of segments fewer than
    // `minimumGap` apart along the curve are left out
    void addContactRepulsion(
        QVector<Vec3> &force,
        double strength,
        double range,
        int minimumGap = 3) const;

    // springs towards edge length 1 and smoothing, added to `force`
    void addSpringsAndBending(
        QVector<Vec3> &force,
        double spring,
        double bending) const;

    // centre at the origin and total length = number of points
    void normalise();

    // factor from the coordinates of points_ to those of curve()
    double displayScale() const;

    // The centre and factor curve() would use when it is not held in
    // place: the centre of the points, outer radius 2.7.
    void freshFrame(Vec3 &centre, double &scale) const;

    // After start() and after a pull curve() is (p - frameCentre_) x
    // frameScale_ for every point p of points_ (the curve stays where it
    // was on screen). Relaxing and scrambling bring that frame to
    // freshFrame() a little in every step, so that nothing jumps.
    void easeFrame();

    bool framed_ = false;
    Vec3 frameCentre_;
    double frameScale_ = 1.0;

    QVector<Vec3> points_;
    QString error_;

    void resetSettling();

    int steps_ = 0;
    double gap_ = 1.0;

    // closest points p_i + s (p_i+1 - p_i) and p_j + t (p_j+1 - p_j) of two
    // nearby segments
    struct Contact
    {
        int i = 0;
        int j = 0;
        double s = 0.0;
        double t = 0.0;
        double distance = 0.0;
    };

    QVector<double> segmentGap_;
    QVector<Contact> contacts_;

    // settling: the points at the start of the current window of steps
    bool settled_ = false;
    int relaxSteps_ = 0;
    int windowSteps_ = 0;
    QVector<Vec3> windowStart_;

    // scramble
    struct Swirl
    {
        Vec3 wave;
        Vec3 amplitude;
        double phase = 0.0;
        double speed = 0.0;
    };

    // pull: the grabbed point and where the hand is (coordinates of
    // points_)
    bool pulling_ = false;
    int pullIndex_ = 0;
    Vec3 pullTarget_;

    int scrambleLeft_ = 0;
    double scrambleRadius_ = 1.0;
    double tubeRadius_ = 0.16;
    QVector<Swirl> swirls_;
    std::mt19937 random_;
};

} // namespace KnotMath

#endif // KNOTRELAX_H
