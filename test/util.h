#include <dlfcn.h>
#include <sys/types.h>

static void* resolve_real(const char* name) {
    return dlsym(RTLD_NEXT, name);
}

template<typename FunctionType, typename... Args>
inline FunctionType real_function(const char* name, Args... args) {
    static void* real_mmap = nullptr;
    if (!real_mmap) {
        real_mmap = resolve_real(name);
    }

    auto to_call = (FunctionType(*)(Args...))real_mmap;
    return to_call(args...);
}
