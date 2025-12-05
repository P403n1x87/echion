// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <methodobject.h>

#if PY_VERSION_HEX >= 0x030b0000
#define Py_BUILD_CORE
#include <internal/pycore_frame.h>
#if PY_VERSION_HEX >= 0x030d0000
#include <opcode.h>
#else
#include <internal/pycore_opcode.h>
#endif
#endif

#include <memory>
#include <string>

#include <echion/errors.h>
#include <echion/frame.h>
#include <echion/strings.h>
#include <echion/vm.h>

// ----------------------------------------------------------------------------
// Check if an opcode is a CALL-family instruction
#if PY_VERSION_HEX >= 0x030b0000
inline bool is_call_opcode(uint8_t opcode)
{
#if PY_VERSION_HEX >= 0x030d0000
    // Python 3.13+
    return opcode == CALL ||
           opcode == CALL_KW ||
           opcode == CALL_FUNCTION_EX;
#elif PY_VERSION_HEX >= 0x030c0000
    // Python 3.12
    return opcode == CALL ||
           opcode == CALL_FUNCTION_EX;
#else
    // Python 3.11
    return opcode == CALL_FUNCTION ||
           opcode == CALL_FUNCTION_KW ||
           opcode == CALL_METHOD ||
           opcode == PRECALL;
#endif
}
#endif  // PY_VERSION_HEX >= 0x030b0000

// ----------------------------------------------------------------------------
// Get the qualified name of a C function (e.g., "math.sin")
// Returns a key in the string table, or 0 on failure.
[[nodiscard]] inline Result<StringTable::Key> get_cfunction_name(PyObject* callable_addr)
{
    // First, figure out the type of the callable
    PyObject callable_base;
    if (copy_type(callable_addr, callable_base))
    {
        return ErrorKind::CFunctionError;
    }

    PyTypeObject* type_addr = Py_TYPE(&callable_base);
    PyTypeObject type_obj;
    if (copy_type(type_addr, type_obj))
    {
        return ErrorKind::CFunctionError;
    }

    // Check if it's a PyCFunction (builtin_function_or_method)
    // We check the tp_name to identify the type
    char tp_name[64] = {0};
    if (copy_generic(type_obj.tp_name, tp_name, sizeof(tp_name) - 1))
    {
        return ErrorKind::CFunctionError;
    }

    std::string qualified_name;

    if (strcmp(tp_name, "builtin_function_or_method") == 0)
    {
        // It's a PyCFunctionObject
        PyCFunctionObject cfunc;
        if (copy_type(callable_addr, cfunc))
        {
            return ErrorKind::CFunctionError;
        }

        // Get the method name from m_ml->ml_name
        PyMethodDef ml;
        if (copy_type(cfunc.m_ml, ml))
        {
            return ErrorKind::CFunctionError;
        }

        char method_name[256] = {0};
        if (copy_generic(ml.ml_name, method_name, sizeof(method_name) - 1))
        {
            return ErrorKind::CFunctionError;
        }

        // Get the module name from m_module (if present)
        if (cfunc.m_module != nullptr)
        {
            auto maybe_module_name = pyunicode_to_utf8(cfunc.m_module);
            if (maybe_module_name)
            {
                qualified_name = *maybe_module_name + "." + method_name;
            }
            else
            {
                qualified_name = method_name;
            }
        }
        else if (cfunc.m_self != nullptr)
        {
            // It's a bound method - try to get the type name
            PyObject self_base;
            if (!copy_type(cfunc.m_self, self_base))
            {
                PyTypeObject* self_type_addr = Py_TYPE(&self_base);
                PyTypeObject self_type;
                if (!copy_type(self_type_addr, self_type))
                {
                    char self_type_name[256] = {0};
                    if (!copy_generic(self_type.tp_name, self_type_name, sizeof(self_type_name) - 1))
                    {
                        qualified_name = std::string(self_type_name) + "." + method_name;
                    }
                }
            }
            if (qualified_name.empty())
            {
                qualified_name = method_name;
            }
        }
        else
        {
            qualified_name = method_name;
        }
    }
    else if (strcmp(tp_name, "method-wrapper") == 0 || strcmp(tp_name, "wrapper_descriptor") == 0)
    {
        // These are slot wrappers - we could try to extract more info but for now just skip
        return ErrorKind::CFunctionError;
    }
    else
    {
        // Unknown type, skip
        return ErrorKind::CFunctionError;
    }

    if (qualified_name.empty())
    {
        return ErrorKind::CFunctionError;
    }

    // Register the name in the string table
    // We use the callable address as the key
    auto key = reinterpret_cast<StringTable::Key>(callable_addr);

    // Register the string (will be a no-op if already registered)
    string_table.register_string(key, qualified_name);

    return key;
}

// ----------------------------------------------------------------------------
// Try to detect if the leaf frame is currently executing a C function call
// by parsing the bytecode to find what was loaded before the CALL instruction.
//
// In Python 3.11+, when a C function is called:
// 1. LOAD_GLOBAL/LOAD_ATTR instructions load the callable
// 2. Arguments are pushed onto the stack
// 3. CALL instruction is executed
// 4. The callable and args are popped, C function is called
//
// By examining the bytecode, we can find the LOAD sequence that preceded
// the CALL and reconstruct the function name.
//
// Returns a Frame reference for the C function if detected.
#if PY_VERSION_HEX >= 0x030b0000
[[nodiscard]] inline Result<Frame::Ref> detect_cfunction_call(
    _PyInterpreterFrame* frame_addr,
    PyCodeObject* code_addr)
{
    // Read the frame
    _PyInterpreterFrame iframe;
    if (copy_type(frame_addr, iframe))
    {
        return ErrorKind::CFunctionError;
    }

    // Read the code object to get bytecode info
    PyCodeObject code;
    if (copy_type(code_addr, code))
    {
        return ErrorKind::CFunctionError;
    }

    // Get the current instruction pointer
#if PY_VERSION_HEX >= 0x030d0000
    _Py_CODEUNIT* instr_ptr = iframe.instr_ptr;
#else
    _Py_CODEUNIT* instr_ptr = iframe.prev_instr;
#endif
    if (instr_ptr == nullptr)
    {
        return ErrorKind::CFunctionError;
    }

    // Calculate the bytecode start address
    // In Python 3.11+, bytecode is stored in co_code_adaptive
    _Py_CODEUNIT* code_start = reinterpret_cast<_Py_CODEUNIT*>(
        reinterpret_cast<char*>(code_addr) + offsetof(PyCodeObject, co_code_adaptive));

    // Calculate the current instruction index
    ptrdiff_t instr_offset = instr_ptr - code_start;
    if (instr_offset < 0)
    {
        return ErrorKind::CFunctionError;
    }

    // Read a chunk of bytecode around the current position
    // We'll read backwards to find the LOAD sequence before the CALL
    constexpr int BYTECODE_WINDOW = 20;  // Number of instructions to read
    int start_offset = (instr_offset > BYTECODE_WINDOW) ? 
                       static_cast<int>(instr_offset) - BYTECODE_WINDOW : 0;
    int read_count = static_cast<int>(instr_offset) - start_offset + 1;
    if (read_count <= 0 || read_count > BYTECODE_WINDOW + 1)
    {
        return ErrorKind::CFunctionError;
    }

    auto bytecode = std::make_unique<_Py_CODEUNIT[]>(read_count);
    if (copy_generic(code_start + start_offset, bytecode.get(),
                     read_count * sizeof(_Py_CODEUNIT)))
    {
        return ErrorKind::CFunctionError;
    }

    // Find the CALL instruction (should be at or near the end)
    int call_idx = -1;
    for (int i = read_count - 1; i >= 0; i--)
    {
#if PY_VERSION_HEX >= 0x030c0000
        uint8_t opcode = bytecode[i].op.code;
#else
        uint8_t opcode = _Py_OPCODE(bytecode[i]);
#endif
        if (is_call_opcode(opcode))
        {
            call_idx = i;
            break;
        }
    }

    if (call_idx < 0)
    {
        return ErrorKind::CFunctionError;
    }

    // Read co_names tuple to get name strings
    // First, get the tuple size and item pointers
    PyTupleObject names_tuple;
    if (copy_type(code.co_names, names_tuple))
    {
        return ErrorKind::CFunctionError;
    }

    Py_ssize_t names_count = names_tuple.ob_base.ob_size;
    if (names_count <= 0 || names_count > 10000)
    {
        return ErrorKind::CFunctionError;
    }

    // Read the tuple items (pointers to name strings)
    auto names_items = std::make_unique<PyObject*[]>(names_count);
    PyObject** names_items_addr = reinterpret_cast<PyObject**>(
        reinterpret_cast<char*>(code.co_names) + offsetof(PyTupleObject, ob_item));
    if (copy_generic(names_items_addr, names_items.get(), names_count * sizeof(PyObject*)))
    {
        return ErrorKind::CFunctionError;
    }

    // Now parse backwards from the CALL to find LOAD_GLOBAL/LOAD_ATTR
    // We're looking for patterns like:
    //   LOAD_GLOBAL 'math' -> LOAD_ATTR 'sin' -> ... -> CALL
    //   LOAD_GLOBAL 'sin' -> ... -> CALL
    std::string global_name;
    std::string attr_name;

    for (int i = call_idx - 1; i >= 0; i--)
    {
#if PY_VERSION_HEX >= 0x030c0000
        uint8_t opcode = bytecode[i].op.code;
        uint8_t arg = bytecode[i].op.arg;
#else
        uint8_t opcode = _Py_OPCODE(bytecode[i]);
        uint8_t arg = _Py_OPARG(bytecode[i]);
#endif

        // Skip CACHE entries (opcode 0 in Python 3.11+)
        if (opcode == 0)
        {
            continue;
        }

        // Skip PUSH_NULL and LOAD_FAST helper instructions
        if (opcode == PUSH_NULL || opcode == LOAD_FAST)
        {
            continue;
        }

        // Check if this is a nested call - stop here
        if (is_call_opcode(opcode))
        {
            break;
        }

        // In Python 3.11+, LOAD_ATTR and LOAD_GLOBAL have modified arg encoding
        // The actual index is arg >> 1 for some versions
        int name_idx = arg >> 1;  // Most common encoding
        if (name_idx < 0 || name_idx >= names_count)
        {
            name_idx = arg;  // Try without shifting
            if (name_idx < 0 || name_idx >= names_count)
            {
                continue;
            }
        }

        if (opcode == LOAD_ATTR)
        {
            if (attr_name.empty())
            {
                // Read the attribute name
                auto maybe_name = pyunicode_to_utf8(names_items[name_idx]);
                if (maybe_name)
                {
                    attr_name = *maybe_name;
                }
            }
        }
        else if (opcode == LOAD_GLOBAL)
        {
            // Read the global name
            auto maybe_name = pyunicode_to_utf8(names_items[name_idx]);
            if (maybe_name)
            {
                global_name = *maybe_name;
            }
            break;  // Found the start of the load sequence
        }
    }

    // Construct the qualified name
    std::string qualified_name;
    if (!global_name.empty() && !attr_name.empty())
    {
        qualified_name = global_name + "." + attr_name;
    }
    else if (!global_name.empty())
    {
        qualified_name = global_name;
    }
    else if (!attr_name.empty())
    {
        qualified_name = attr_name;
    }
    else
    {
        return ErrorKind::CFunctionError;
    }

    // Register and return
    auto key = std::hash<std::string>{}(qualified_name);
    string_table.register_string(key, qualified_name);
    return std::ref(Frame::get(key));
}
#endif  // PY_VERSION_HEX >= 0x030b0000

// ----------------------------------------------------------------------------
// Check if the current frame is calling into a C function and return the
// C function's name as a frame.
// This should be called when we detect a FRAME_OWNED_BY_CSTACK or when
// we're at a CALL instruction with no further Python frames.
//
// Note: This only works on Python 3.13+ where f_executable can hold
// callables (not just code objects).
#if PY_VERSION_HEX >= 0x030d0000
[[nodiscard]] inline Result<Frame::Ref> get_cfunction_frame(_PyInterpreterFrame* frame_addr)
{
    // In Python 3.13+, shim frames (FRAME_OWNED_BY_CSTACK) have the callable
    // in f_executable when it's not a code object
    _PyInterpreterFrame iframe;
    if (copy_type(frame_addr, iframe))
    {
        return ErrorKind::CFunctionError;
    }

    if (iframe.owner != FRAME_OWNED_BY_CSTACK)
    {
        return ErrorKind::CFunctionError;
    }

    // Check if f_executable is a code object or a callable
    PyObject* executable = iframe.f_executable;
    if (executable == nullptr)
    {
        return ErrorKind::CFunctionError;
    }

    // Read the type to check if it's a code object
    PyObject executable_base;
    if (copy_type(executable, executable_base))
    {
        return ErrorKind::CFunctionError;
    }

    PyTypeObject* type_addr = Py_TYPE(&executable_base);
    if (type_addr == &PyCode_Type)
    {
        // It's a code object, not a C function shim
        return ErrorKind::CFunctionError;
    }

    // It's a callable - try to get its name
    auto maybe_name_key = get_cfunction_name(executable);
    if (!maybe_name_key)
    {
        return ErrorKind::CFunctionError;
    }

    return std::ref(Frame::get(*maybe_name_key));
}
#endif  // PY_VERSION_HEX >= 0x030d0000

