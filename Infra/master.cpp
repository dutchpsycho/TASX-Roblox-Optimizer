#include "WMI.h"
#include "CPU.h"
#include "trimmer.h"

#include <windows.h>
#include <tlhelp32.h>

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>

static std::mutex g_mutex;
static std::unordered_map<DWORD, HANDLE> g_rbxHandles;

std::vector<DWORD> Scope()
{
    std::vector<DWORD> pids;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return pids;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(hSnap, &entry))
    {
        do {
            if (_wcsicmp(entry.szExeFile, L"RobloxPlayerBeta.exe") == 0)
            {
                pids.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(hSnap, &entry));
    }

    CloseHandle(hSnap);
    return pids;
}

void MonNew()
{
    auto pids = Scope();

    std::lock_guard<std::mutex> lock(g_mutex);
    for (DWORD pid : pids)
    {
        if (g_rbxHandles.contains(pid)) continue;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_QUOTA | PROCESS_SET_INFORMATION, FALSE, pid);
        if (!hProc) {
            std::cerr << "[TASX] Failed to open PID " << pid << std::endl;
            continue;
        }

        g_rbxHandles[pid] = hProc;
        std::cout << "[TASX] New Roblox instance PID " << pid << " hooked." << std::endl;

        std::thread([] {
            Sleep(2000);

            HANDLE hSnapCrash = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapCrash != INVALID_HANDLE_VALUE)
            {
                PROCESSENTRY32W crash{};
                crash.dwSize = sizeof(crash);

                if (Process32FirstW(hSnapCrash, &crash))
                {
                    do {
                        if (_wcsicmp(crash.szExeFile, L"RobloxCrashHandler.exe") == 0)
                        {
                            HANDLE hCrashProc = OpenProcess(PROCESS_TERMINATE, FALSE, crash.th32ProcessID);
                            if (hCrashProc)
                            {
                                TerminateProcess(hCrashProc, 0);
                                CloseHandle(hCrashProc);
                                std::cout << "[TASX] Killed CrashHandler PID " << crash.th32ProcessID << std::endl;
                            }
                        }
                    } while (Process32NextW(hSnapCrash, &crash));
                }

                CloseHandle(hSnapCrash);
            }
            }).detach();

        std::thread([pid, hProc]() {
            if (StartTrimmer(hProc))
                std::cout << "[TASX] Trimmer started for PID " << pid << std::endl;
            else
                std::cerr << "[TASX] Trimmer failed for PID " << pid << std::endl;

            if (ApplyCPULimits(hProc))
                std::cout << "[TASX] CPU limits applied for PID " << pid << std::endl;
            else
                std::cerr << "[TASX] CPU limit failed for PID " << pid << std::endl;

            }).detach();
    }
}

void CleanupExited()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_rbxHandles.begin(); it != g_rbxHandles.end(); )
    {
        DWORD code = 0;
        if (GetExitCodeProcess(it->second, &code) && code != STILL_ACTIVE)
        {
            std::cout << "[TASX] Roblox PID " << it->first << " exited." << std::endl;

            StopTrimmer();
            CloseHandle(it->second);
            it = g_rbxHandles.erase(it);
        }
        else {
            ++it;
        }
    }
}

int main()
{
    if (!_wmimon())
    {
        std::cerr << "Failed to initialize WMI monitor." << std::endl;
        return -1;
    }

    std::cout << "TASX watching for Roblox..." << std::endl;

    while (true)
    {
        std::string signal = WaitForRBXEvent();
        if (signal == "RBX_ON")
        {
            MonNew();
        }
        else if (signal == "RBX_OFF")
        {
            CleanupExited();
        }
    }

    _wmishutdown();
    return 0;
}

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
)

{
    if (!_wmimon())
    {
        MessageBoxW(nullptr, L"Failed to initialize WMI monitor.", L"TASX", MB_ICONERROR);
        return -1;
    }

    while (true)
    {
        std::string signal = WaitForRBXEvent();
        if (signal == "RBX_ON")
        {
            MonNew();
        }
        else if (signal == "RBX_OFF")
        {
            CleanupExited();
        }
    }

    _wmishutdown();
    return 0;
}