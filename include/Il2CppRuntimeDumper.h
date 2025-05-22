#pragma once
#include <Windows.h>
#include <string>

#if defined(RD_STATIC)
# define IL2RD_API
#elif defined(RD_DYNAMIC)
# define IL2RD_API __declspec(dllexport)
#else
# define IL2RD_API __declspec(dllimport)
#endif

IL2RD_API void DumpIl2CppRuntime(HMODULE gameAssembly, const std::string& outputPath);