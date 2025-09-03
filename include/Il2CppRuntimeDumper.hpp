#pragma once
#include <Windows.h>
#define RD_DYNAMIC 0

#if defined(RD_STATIC)
# define IL2RD_API
#elif defined(RD_DYNAMIC)
# define IL2RD_API __declspec(dllexport)
#else
# define IL2RD_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

IL2RD_API void DumpIl2CppRuntime(HMODULE gameAssembly, const char* outputPath);
IL2RD_API void RawDumpMetadata(const char* path, const char* outpath);

#ifdef __cplusplus
}
#endif