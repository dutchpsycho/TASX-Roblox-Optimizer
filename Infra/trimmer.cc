#include "Trimmer.h"

#include <psapi.h>
#include <chrono>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>

static std::unordered_map<DWORD, std::thread> g_trimmerThreads;
static std::unordered_map<DWORD, std::atomic<bool>> g_trimmerStates;
static std::mutex g_trimmerMutex;

bool StartTrimmer(DWORD pid, HANDLE processHandle)
{
    std::lock_guard<std::mutex> lock(g_trimmerMutex);

    if (g_trimmerStates.contains(pid) && g_trimmerStates[pid].load())
        return false;

    DWORD access = 0;
    if (!GetHandleInformation(processHandle, &access)) {
        std::wcerr << L"[Trimmer] Can't access handle for PID " << pid << std::endl;
        return false;
    }

    g_trimmerStates[pid] = true;

    g_trimmerThreads[pid] = std::thread([pid, processHandle]() {
        std::wcout << L"[Trimmer] PID " << pid << L" trim loop starting..." << std::endl;

        while (g_trimmerStates[pid])
        {
            SetProcessWorkingSetSize(processHandle, -1, -1);
            EmptyWorkingSet(processHandle);

            PROCESS_MEMORY_COUNTERS_EX mem{};
            if (GetProcessMemoryInfo(processHandle, (PROCESS_MEMORY_COUNTERS*)&mem, sizeof(mem))) {
                std::wcout << L"[Trimmer] PID " << pid
                    << L" | WS: " << (mem.WorkingSetSize / 1024)
                    << L" KB | Private: " << (mem.PrivateUsage / 1024) << L" KB" << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::seconds(15));
        }

        std::wcout << L"[Trimmer] PID " << pid << L" trim loop stopped." << std::endl;
        });

    return true;
}

void StopTrimmer(DWORD pid)
{
    std::lock_guard<std::mutex> lock(g_trimmerMutex);

    if (g_trimmerStates.contains(pid)) {
        g_trimmerStates[pid] = false;

        if (g_trimmerThreads[pid].joinable())
            g_trimmerThreads[pid].join();

        g_trimmerStates.erase(pid);
        g_trimmerThreads.erase(pid);
    }
}

void StopAllTrimmers()
{
    std::lock_guard<std::mutex> lock(g_trimmerMutex);

    for (auto& [pid, running] : g_trimmerStates)
        running = false;

    for (auto& [pid, thread] : g_trimmerThreads)
        if (thread.joinable())
            thread.join();

    g_trimmerStates.clear();
    g_trimmerThreads.clear();
}