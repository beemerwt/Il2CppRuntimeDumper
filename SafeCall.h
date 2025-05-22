#pragma once

template <typename T>
struct SafeCall {};

template <typename ReturnType, typename... Args>
struct SafeCall<ReturnType(*)(Args...)> {
    using FuncType = ReturnType(*)(Args...);

    FuncType func = nullptr;
    SafeCall() = default;

    void operator=(FuncType f) {
        func = f;
    }

    ReturnType operator()(Args... args) {
        __try {
            return func(args...);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            std::cout << "[Dumper] Exception caught at " << __FUNCTION__ << "\n";
            return 0;
        }
    }
};