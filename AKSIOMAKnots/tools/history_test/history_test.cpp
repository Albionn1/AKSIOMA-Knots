// Headless check of undo and redo (knothistory.cpp).

#include "knothistory.h"

#include <cstdio>

namespace
{

int failures = 0;

void check(bool passed, const char *what)
{
    std::printf("%s  %s\n", passed ? "ok  " : "FAIL", what);

    if (!passed)
        ++failures;
}

KnotState numbered(int thickness)
{
    KnotState state;

    state.thickness = thickness;

    return state;
}

} // namespace

int main()
{
    {
        KnotHistory history;

        check(history.isEmpty() && !history.canUndo() && !history.canRedo(), "a new history has nothing to undo");
        check(!history.isSaved(), "and nothing saved");

        check(history.record(numbered(1)), "the first state is recorded");
        check(!history.canUndo(), "one state: nothing to go back to");
        check(!history.record(numbered(1)), "the same state again is no new step");

        history.record(numbered(2));
        history.record(numbered(3));

        check(history.canUndo() && !history.canRedo(), "three steps: back yes, forward no");
        check(history.undo().thickness == 2, "undo goes back one step");
        check(history.undo().thickness == 1, "and one more");
        check(!history.canUndo(), "the first step is the end");
        check(history.undo().thickness == 1, "undo at the end stays there");
        check(history.redo().thickness == 2, "redo goes forward");
        check(history.canRedo(), "one more to redo");

        history.record(numbered(9));

        check(!history.canRedo(), "a new change drops what had been undone");
        check(history.current().thickness == 9, "and is the current state");
        check(history.undo().thickness == 2, "going back from it skips the dropped step");
    }

    {
        KnotHistory history;

        history.record(numbered(1));
        history.markSaved();

        check(history.isSaved(), "marked as saved");

        history.record(numbered(2));

        check(!history.isSaved(), "a change is not saved");

        history.undo();

        check(history.isSaved(), "undone back to the saved step: nothing to save");

        history.redo();

        check(!history.isSaved(), "forward again: not saved");

        history.undo();
        history.record(numbered(3));

        check(!history.isSaved(), "a different change from the saved step: not saved");

        history.undo();

        check(history.isSaved(), "and back again: saved");
    }

    {
        KnotHistory history;

        for (int k = 0; k < 3 * KnotHistory::Limit; ++k)
            history.record(numbered(k));

        int steps = 0;

        while (history.canUndo())
        {
            history.undo();
            ++steps;
        }

        check(steps == KnotHistory::Limit - 1, "the history keeps its limit of steps");
        check(history.current().thickness == 2 * KnotHistory::Limit, "the oldest are the ones dropped");
    }

    {
        KnotState a = numbered(1);
        KnotState b = numbered(1);

        a.shape = {KnotMath::Vec3(0.0, 0.0, 0.0), KnotMath::Vec3(1.0, 0.0, 0.0)};
        b.shape = a.shape;

        check(a == b, "two equal shapes are the same state");

        b.shape[1].y = 1.0e-9;

        check(a != b, "a moved point is a different state");

        b.shape = a.shape;
        b.customText = QStringLiteral("x");

        check(a != b, "other text is a different state");

        b = a;
        b.color = QColor(1, 2, 3);

        check(a != b, "another colour is a different state");
    }

    std::printf("\n%d failures\n", failures);

    return failures == 0 ? 0 : 1;
}
