#ifndef WMI_H
#define WMI_H

#include <string>

bool _wmimon();

std::string WaitForRBXEvent();

void _wmishutdown();

#endif