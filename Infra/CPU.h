#ifndef CPU_CONTROL_H
#define CPU_CONTROL_H

#include <windows.h>

#pragma comment(lib, "PowrProf.lib")

#ifndef ProcessorPerformanceBoostMode
#define ProcessorPerformanceBoostMode ((POWER_INFORMATION_LEVEL)35)
#endif

bool ApplyCPULimits(HANDLE hProcess);

#endif