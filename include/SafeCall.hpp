#pragma once
#include <windows.h>
#include <debugapi.h>
#include <sstream>
#include <setjmp.h>
#include <mutex>
#include <excpt.h>
template <typename T>
struct SafeCall {};

namespace {
    // MinGW/Clang path: VEH + TLS jump buffer
    static LONG CALLBACK SafeCallVehHandler(PEXCEPTION_POINTERS info);

    static std::once_flag g_scVehOnce;
    static void EnsureVehInstalled() {
        std::call_once(g_scVehOnce, []() {
            // Priority 1 = call our handler first
            AddVectoredExceptionHandler(1, SafeCallVehHandler);
        });
    }

    // Per-thread guard state
    static thread_local jmp_buf* t_scJmp = nullptr;
    static thread_local int      t_scDepth = 0;

    struct VehGuard {
        jmp_buf* buf;
        VehGuard(jmp_buf* b) : buf(b) { t_scJmp = buf; ++t_scDepth; }
        ~VehGuard() { --t_scDepth; t_scJmp = nullptr; }
    };

    static LONG CALLBACK SafeCallVehHandler(PEXCEPTION_POINTERS info) {
        // Only intercept while inside a guarded SafeCall on this thread
        if (t_scDepth <= 0 || !t_scJmp) return EXCEPTION_CONTINUE_SEARCH;

        switch (info->ExceptionRecord->ExceptionCode) {
            case EXCEPTION_ACCESS_VIOLATION:
            case EXCEPTION_ILLEGAL_INSTRUCTION:
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            case EXCEPTION_DATATYPE_MISALIGNMENT:
            case EXCEPTION_IN_PAGE_ERROR:
            case EXCEPTION_PRIV_INSTRUCTION:
                longjmp(*t_scJmp, 1); // does not return
                return EXCEPTION_CONTINUE_SEARCH; // not reached
            case EXCEPTION_STACK_OVERFLOW:
                // Unsafe to continue; let normal handling occur.
                return EXCEPTION_CONTINUE_SEARCH;
            default:
                return EXCEPTION_CONTINUE_SEARCH;
        }
    }
}

template <typename ReturnType, typename... Args>
struct SafeCall<ReturnType(*)(Args...)> {
    using FuncType = ReturnType(*)(Args...);
    std::stringstream oss;

    FuncType func = nullptr;
    SafeCall() = default;

    void operator=(FuncType f) {
        func = f;
    }

    operator bool() const {
        return func != nullptr;
    }

    void OutputException(const char* func) {
        oss << "[Dumper] Exception caught at " << func << "\n";
        OutputDebugStringA(oss.str().c_str());
        oss.str("");
    }

    ReturnType operator()(Args... args) {
        if (!func) return ReturnType{};
        EnsureVehInstalled();
        jmp_buf env;
        VehGuard guard(&env);
        if (setjmp(env) == 0) {
            return func(args...);
        } else {
            OutputException(__FUNCTION__);
            return ReturnType{};
        }
    }
};