#include <dlfcn.h>
#include <sys/types.h>
#include <cstring>
#include <stdexcept>
#include <string>

#include <Python.h>

static void* resolve_real(const char* name) {
    return dlsym(RTLD_NEXT, name);
}

template<typename FunctionType, typename... Args>
inline FunctionType real_function(const char* name, Args... args) {
    void* real_function = resolve_real(name);
    if (real_function == nullptr) { 
        std::string error = dlerror();
        throw std::runtime_error("Failed to resolve real function '" + std::string(name) + "': " + error);
    }

    auto real_function_typed = (FunctionType(*)(Args...))real_function;
    return real_function_typed(args...);
}

template <class Fn>
Fn resolve_cpp_real(Fn self_fn) {
    static_assert(std::is_pointer<Fn>::value, "resolve_real expects a function pointer");

    // Convert function pointer -> void* (portable via memcpy)
    void* self_void = nullptr;
    std::memcpy(&self_void, &self_fn, sizeof self_void);

    Dl_info di{};
    if (!dladdr(self_void, &di) || !di.dli_sname) {
        std::fprintf(stderr, "resolve_real: dladdr failed or missing symbol name\n");
        return nullptr;
    }

    dlerror(); // clear
    void* sym = dlsym(RTLD_NEXT, di.dli_sname);
    if (const char* err = dlerror()) {
        std::fprintf(stderr, "resolve_real: dlsym(RTLD_NEXT, %s) failed: %s\n", di.dli_sname, err);
        return nullptr;
    }

    // Convert void* -> function pointer (portable via memcpy)
    Fn real{};
    std::memcpy(&real, &sym, sizeof real);
    return real;
}


template<typename FunctionType, typename... Args>
inline auto real_cpp_function(FunctionType self_fn, Args... args) -> decltype(self_fn(args...)) {
    auto real_function = resolve_cpp_real(self_fn);
    return real_function(args...);
}

struct PyObjectHandle {
    PyObject* obj;

    PyObjectHandle(PyObject* obj) : obj(obj) {}

    ~PyObjectHandle() {
        Py_XDECREF(obj);
    }

    PyObject* operator->() const {
        return obj;
    }

    PyObject* operator*() const {
        return obj;
    }

    operator PyObject*() const {
        return obj;
    }
};