#include "CPU.h"

#include <iostream>
#include <vector>

#include <windows.h>
#include <processthreadsapi.h>
#include <powerbase.h>
#include <tlhelp32.h>

static int g_osMajorVersion = 0;

static void WinVer()
{
    if (g_osMajorVersion != 0) return;

    OSVERSIONINFOEXW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
#pragma warning(suppress : 4996)
    if (GetVersionExW((LPOSVERSIONINFOW)&osvi))
    {
        if (osvi.dwMajorVersion == 10 && osvi.dwMinorVersion == 0)
        {
            if (osvi.dwBuildNumber >= 22000)
                g_osMajorVersion = 11;
            else
                g_osMajorVersion = 10;
        }
    }

    std::wcout << L"[CPU] Detected Windows " << g_osMajorVersion << L"." << std::endl;
}

static DWORD LogicalCores()
{
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return 0;

    std::vector<BYTE> buffer(len);
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());

    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &len))
        return 0;

    DWORD count = 0;
    BYTE* ptr = buffer.data();
    while (ptr < buffer.data() + len)

    {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX coreInfo = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
        count += static_cast<DWORD>(__popcnt64(coreInfo->Processor.GroupMask[0].Mask));
        ptr += coreInfo->Size;
    }

    return count;
}

static DWORD_PTR GetEssentialCPUMask(int useCores)
{
    DWORD_PTR mask = 0;
    for (int i = 0; i < useCores; ++i)
        mask |= (1ull << i);
    return mask;
}

bool ApplyCPULimits(HANDLE hProcess)
{
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE)
        return false;

    WinVer();

    DWORD logicalCores = LogicalCores();
    if (logicalCores < 2)

    {
        std::wcerr << L"[CPU] Not enough cores to adjust affinity." << std::endl;
        return false;
    }

    DWORD useCores = max(2, logicalCores / 2);
    DWORD_PTR mask = GetEssentialCPUMask(useCores);

    if (!SetProcessAffinityMask(hProcess, mask))

    {
        std::wcerr << L"[CPU] Failed to set affinity. Code: " << GetLastError() << std::endl;
    }

    else

    {
        std::wcout << L"[CPU] Affinity limited to " << useCores << L" cores." << std::endl;
    }

    if (!SetPriorityClass(hProcess, IDLE_PRIORITY_CLASS))

    {
        std::wcerr << L"[CPU] Failed to set idle priority." << std::endl;
    }

    else

    {
        std::wcout << L"[CPU] Priority set to IDLE." << std::endl;
    }

    if (g_osMajorVersion == 10)

    {
        DWORD boost = 0;
        auto hPowrProf = LoadLibraryW(L"PowrProf.dll");
        if (hPowrProf)
        {
            using PowerSetInformationFn = NTSTATUS(WINAPI*)(HANDLE, int, PVOID, ULONG);
            auto fn = reinterpret_cast<PowerSetInformationFn>(
                GetProcAddress(hPowrProf, "PowerSetInformation")
                );

            if (fn && fn(nullptr, 35 /* ProcessorPerformanceBoostMode */, &boost, sizeof(boost)) == 0)
                std::wcout << L"[CPU] Boost disabled (Win10)." << std::endl;
            else
                std::wcerr << L"[CPU] Failed to disable boost (Win10)." << std::endl;

            FreeLibrary(hPowrProf);
        }
        else
        {
            std::wcerr << L"[CPU] Could not load PowrProf.dll" << std::endl;
        }
    }

    if (g_osMajorVersion == 11)
    {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap != INVALID_HANDLE_VALUE)
        {
            THREADENTRY32 te{};
            te.dwSize = sizeof(te);
            if (Thread32First(hSnap, &te))
            {
                do {
                    if (te.th32OwnerProcessID == GetProcessId(hProcess))
                    {
                        HANDLE hThread = OpenThread(THREAD_SET_INFORMATION, FALSE, te.th32ThreadID);
                        if (hThread)
                        {
                            DWORD mode = 1; // 1 = Eco/Efficient
                            SetThreadInformation(hThread, ThreadPowerThrottling, &mode, sizeof(mode));
                            CloseHandle(hThread);
                        }
                    }
                } while (Thread32Next(hSnap, &te));
            }
            CloseHandle(hSnap);
            std::wcout << L"[CPU] Efficiency mode applied (Win11)." << std::endl;
        }
    }

    return true;
}