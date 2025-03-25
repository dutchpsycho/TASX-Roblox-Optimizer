#include "Trimmer.h"
#include <psapi.h>
#include <chrono>
#include <iostream>

static std::atomic<bool> g_running = false;
static std::thread g_trimmerThread;

bool StartTrimmer(HANDLE processHandle)
{
    if (g_running.load()) return false;

    if (!processHandle || processHandle == INVALID_HANDLE_VALUE)
    {
        std::wcerr << L"[Trimmer] Invalid handle." << std::endl;
        return false;
    }

    DWORD access = 0;
    if (!GetHandleInformation(processHandle, &access))
    {
        std::wcerr << L"[Trimmer] Cannot access target handle. Are perms right?" << std::endl;
        return false;
    }

    g_running = true;

    g_trimmerThread = std::thread([processHandle]() {
        std::wcout << L"[Trimmer] Starting background memory trim loop..." << std::endl;

        while (g_running)
        {
            // try soft trim
            SetProcessWorkingSetSize(processHandle, -1, -1);
            // try hard trim
            EmptyWorkingSet(processHandle);

            PROCESS_MEMORY_COUNTERS_EX mem{};
            if (GetProcessMemoryInfo(processHandle, (PROCESS_MEMORY_COUNTERS*)&mem, sizeof(mem)))
            {
                std::wcout << L"[Trimmer] WS: " << (mem.WorkingSetSize / 1024)
                    << L" KB | Private: " << (mem.PrivateUsage / 1024) << L" KB" << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::seconds(15));
        }

        std::wcout << L"[Trimmer] Trimmer loop exited." << std::endl;
        });

    return true;
}

void StopTrimmer()
{
    g_running = false;

    if (g_trimmerThread.joinable())
        g_trimmerThread.join();
}