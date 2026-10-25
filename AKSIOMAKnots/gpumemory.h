#ifndef GPUMEMORY_H
#define GPUMEMORY_H

#include <QString>
#include <QtGlobal>

/*
 * Video memory of this program as Windows counts it (DXGI): what the
 * program has in use on its graphics card and how much the system lets it
 * use there (the budget). The same numbers as in Task Manager.
 */
namespace GpuMemory
{

struct Info
{
    // false: not available (not Windows, or an old system)
    bool valid = false;

    QString adapter;

    // bytes
    qint64 used = 0;
    qint64 budget = 0;

    // integrated graphics: the "video memory" is shared main memory
    bool shared = false;
};

// The graphics card this program uses the most memory on.
Info query();

} // namespace GpuMemory

#endif // GPUMEMORY_H
