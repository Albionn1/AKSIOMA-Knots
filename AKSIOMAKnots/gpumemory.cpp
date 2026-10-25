#include "gpumemory.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi1_4.h>
#endif

namespace GpuMemory
{

#ifdef Q_OS_WIN

Info query()
{
    Info best;

    IDXGIFactory1 *factory = nullptr;

    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))))
        return best;

    qint64 bestTotal = -1;

    IDXGIAdapter1 *adapter = nullptr;

    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 description;

        IDXGIAdapter3 *adapter3 = nullptr;

        // the software renderer holds no video memory
        if (SUCCEEDED(adapter->GetDesc1(&description)) &&
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3),
                                              reinterpret_cast<void **>(&adapter3))))
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO local = {};
            DXGI_QUERY_VIDEO_MEMORY_INFO nonLocal = {};

            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(
                    0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)))
            {
                adapter3->QueryVideoMemoryInfo(
                    0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal);

                // the card the program really draws on uses the most
                const qint64 total =
                    static_cast<qint64>(local.CurrentUsage + nonLocal.CurrentUsage);

                if (total > bestTotal)
                {
                    bestTotal = total;

                    best.valid = true;
                    best.adapter = QString::fromWCharArray(description.Description);
                    best.used = static_cast<qint64>(local.CurrentUsage);
                    best.budget = static_cast<qint64>(local.Budget);

                    // less than 512 MB of its own: integrated graphics
                    best.shared = description.DedicatedVideoMemory < (512ull << 20);
                }
            }

            adapter3->Release();
        }

        adapter->Release();
    }

    factory->Release();

    return best;
}

#else

Info query()
{
    return Info();
}

#endif

} // namespace GpuMemory
