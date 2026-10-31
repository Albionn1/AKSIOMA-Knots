#include "knothistory.h"

#include <algorithm>

namespace
{

bool sameShape(
    const QVector<KnotMath::Vec3> &a,
    const QVector<KnotMath::Vec3> &b)
{
    return std::equal(
        a.cbegin(),
        a.cend(),
        b.cbegin(),
        b.cend(),
        [](const KnotMath::Vec3 &p, const KnotMath::Vec3 &q)
        {
            return p.x == q.x && p.y == q.y && p.z == q.z;
        });
}

} // namespace

bool KnotState::operator==(const KnotState &other) const
{
    return presetIndex == other.presetIndex &&
           torusP == other.torusP &&
           torusQ == other.torusQ &&
           tableKnot == other.tableKnot &&
           customText == other.customText &&
           thickness == other.thickness &&
           samples == other.samples &&
           material == other.material &&
           shine == other.shine &&
           colorMode == other.colorMode &&
           color == other.color &&
           sameShape(shape, other.shape);
}

bool KnotState::operator!=(const KnotState &other) const
{
    return !(*this == other);
}

bool KnotHistory::record(const KnotState &state)
{
    if (position_ >= 0 && steps_.at(position_).state == state)
        return false;

    steps_.resize(position_ + 1);

    Step step;

    step.state = state;
    step.serial = nextSerial_++;

    steps_.push_back(step);

    if (steps_.size() > Limit)
        steps_.removeFirst();

    position_ = static_cast<int>(steps_.size()) - 1;

    return true;
}

bool KnotHistory::isEmpty() const
{
    return steps_.isEmpty();
}

bool KnotHistory::canUndo() const
{
    return position_ > 0;
}

bool KnotHistory::canRedo() const
{
    return position_ >= 0 && position_ + 1 < steps_.size();
}

const KnotState &KnotHistory::current() const
{
    return steps_.at(position_).state;
}

const KnotState &KnotHistory::undo()
{
    if (canUndo())
        --position_;

    return current();
}

const KnotState &KnotHistory::redo()
{
    if (canRedo())
        ++position_;

    return current();
}

void KnotHistory::markSaved()
{
    if (position_ >= 0)
        savedSerial_ = steps_.at(position_).serial;
}

bool KnotHistory::isSaved() const
{
    return position_ >= 0 && steps_.at(position_).serial == savedSerial_;
}
