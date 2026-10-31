#ifndef KNOTHISTORY_H
#define KNOTHISTORY_H

#include "knotmath.h"

#include <QColor>
#include <QString>
#include <QVector>

/*
 * Undo and redo: what the knot and its look were at each step, whole. Every
 * step keeps its own full state, so going back never has to work out how
 * to reverse an operation, whatever that operation was (a knot from the
 * table, a drawn knot, two knots joined, a scrambled shape, a thicker tube).
 */
struct KnotState
{
    int presetIndex = 0;
    int torusP = 3;
    int torusQ = 2;
    QString tableKnot;
    QString customText;

    // the moved curve, when the shape was relaxed or pulled about
    QVector<KnotMath::Vec3> shape;

    int thickness = 0;
    int samples = 0;
    int material = 0;
    int shine = 0;
    int colorMode = 0;
    QColor color;

    bool operator==(const KnotState &other) const;
    bool operator!=(const KnotState &other) const;
};

class KnotHistory
{
public:
    // the oldest steps go when there are more than this
    static constexpr int Limit = 100;

    // A new step, unless it is the state already here (false). The steps
    // that had been undone are gone: they led away from this one.
    bool record(const KnotState &state);

    bool isEmpty() const;
    bool canUndo() const;
    bool canRedo() const;

    const KnotState &current() const;

    // the state to go back or forward to
    const KnotState &undo();
    const KnotState &redo();

    // this step is what the file holds; after undoing back to it there is
    // nothing to save again
    void markSaved();
    bool isSaved() const;

private:
    struct Step
    {
        KnotState state;
        quint64 serial = 0;
    };

    QVector<Step> steps_;
    int position_ = -1;
    quint64 nextSerial_ = 1;
    quint64 savedSerial_ = 0;
};

#endif // KNOTHISTORY_H
