#ifndef TRIMMER_H
#define TRIMMER_H

#include <windows.h>
#include <atomic>
#include <thread>

bool StartTrimmer(HANDLE processHandle);

void StopTrimmer();

#endif