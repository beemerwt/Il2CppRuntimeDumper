#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#ifndef IL2CPP_RUNTIME_DUMPER_STATIC
#define DLLIMPORT __declspec(dllimport)
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLIMPORT
#define DLLEXPORT
#endif

DLLIMPORT void DumpIl2CppRuntime(HMODULE gameAssembly, const char* outputPath);