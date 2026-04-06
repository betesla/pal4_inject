#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {

DWORD WINAPI RuntimeBootstrapThread(LPVOID module_handle);

}  // namespace pal4::inject
