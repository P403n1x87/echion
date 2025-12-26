#include <echion/frame.h>

#include <echion/errors.h>
#include <echion/render.h>

#if PY_VERSION_HEX >= 0x030b0000
#define Py_BUILD_CORE
#if PY_VERSION_HEX >= 0x030d0000
#include <opcode.h>
#else
#include <internal/pycore_opcode.h>
#endif
#else
// Python < 3.11
#include <opcode.h>
#endif

// ----------------------------------------------------------------------------
// Check if an opcode is a CALL to a C/builtin function
// In Python 3.11+, opcodes are specialized at runtime, so we check for
// specialized PRECALL_BUILTIN_* variants that indicate a C function call
static inline bool is_call_opcode([[maybe_unused]] uint8_t opcode)
{
#if PY_VERSION_HEX >= 0x030d0000
    // Python 3.13+: Check for specialized CALL_BUILTIN_* variants
    // These are adaptive specializations that indicate a C/builtin function call
    return opcode == CALL_BUILTIN_CLASS ||
           opcode == CALL_BUILTIN_FAST ||
           opcode == CALL_BUILTIN_FAST_WITH_KEYWORDS ||
           opcode == CALL_BUILTIN_O ||
           opcode == CALL_FUNCTION_EX;
#elif PY_VERSION_HEX >= 0x030c0000
    // Python 3.12: CALL is specialized but no PRECALL
    // Check for CALL and specialized CALL_BUILTIN_* variants
    return opcode == CALL || opcode == CALL_FUNCTION_EX ||
           opcode == CALL_BUILTIN_CLASS ||
           opcode == CALL_BUILTIN_FAST_WITH_KEYWORDS ||
           opcode == CALL_NO_KW_BUILTIN_FAST ||
           opcode == CALL_NO_KW_BUILTIN_O ||
           opcode == CALL_NO_KW_ISINSTANCE ||
           opcode == CALL_NO_KW_LEN ||
           opcode == CALL_NO_KW_LIST_APPEND ||
           opcode == CALL_NO_KW_STR_1 ||
           opcode == CALL_NO_KW_TUPLE_1 ||
           opcode == CALL_NO_KW_TYPE_1;
#elif PY_VERSION_HEX >= 0x030b0000
    // Python 3.11: Check specialized PRECALL_BUILTIN_* variants and CALL
    // When in a C call, prev_instr might point to CALL or the specialized PRECALL
    return opcode == CALL ||
           opcode == PRECALL_BUILTIN_CLASS ||
           opcode == PRECALL_BUILTIN_FAST_WITH_KEYWORDS ||
           opcode == PRECALL_NO_KW_BUILTIN_FAST ||
           opcode == PRECALL_NO_KW_BUILTIN_O ||
           opcode == PRECALL_NO_KW_ISINSTANCE ||
           opcode == PRECALL_NO_KW_LEN ||
           opcode == PRECALL_NO_KW_LIST_APPEND ||
           opcode == PRECALL_NO_KW_STR_1 ||
           opcode == PRECALL_NO_KW_TUPLE_1 ||
           opcode == PRECALL_NO_KW_TYPE_1 ||
           opcode == CALL_FUNCTION_EX;
#else
    // Python 3.10 and earlier: CALL_FUNCTION, CALL_FUNCTION_KW, CALL_FUNCTION_EX, CALL_METHOD
    return opcode == CALL_FUNCTION || opcode == CALL_FUNCTION_KW ||
           opcode == CALL_FUNCTION_EX || opcode == CALL_METHOD;
#endif
}

// ----------------------------------------------------------------------------
// Check if an opcode is a LOAD_ATTR/LOAD_METHOD (preferred for method names)
// Includes specialized variants that Python 3.11+ uses at runtime
static inline bool is_load_attr_opcode(uint8_t opcode)
{
    if (opcode == LOAD_ATTR)
        return true;
#if PY_VERSION_HEX >= 0x030b0000 && PY_VERSION_HEX < 0x030c0000
    // Python 3.11 specialized LOAD_ATTR variants
    if (opcode == LOAD_ATTR_ADAPTIVE ||
        opcode == LOAD_ATTR_INSTANCE_VALUE ||
        opcode == LOAD_ATTR_MODULE ||
        opcode == LOAD_ATTR_SLOT ||
        opcode == LOAD_ATTR_WITH_HINT ||
        opcode == LOAD_METHOD ||
        opcode == LOAD_METHOD_ADAPTIVE ||
        opcode == LOAD_METHOD_CLASS ||
        opcode == LOAD_METHOD_MODULE ||
        opcode == LOAD_METHOD_NO_DICT ||
        opcode == LOAD_METHOD_WITH_DICT ||
        opcode == LOAD_METHOD_WITH_VALUES)
        return true;
#elif PY_VERSION_HEX >= 0x030c0000
    // Python 3.12+ specialized LOAD_ATTR variants (LOAD_METHOD merged into LOAD_ATTR)
    if (opcode == LOAD_ATTR_CLASS ||
        opcode == LOAD_ATTR_GETATTRIBUTE_OVERRIDDEN ||
        opcode == LOAD_ATTR_INSTANCE_VALUE ||
        opcode == LOAD_ATTR_MODULE ||
        opcode == LOAD_ATTR_PROPERTY ||
        opcode == LOAD_ATTR_SLOT ||
        opcode == LOAD_ATTR_WITH_HINT ||
        opcode == LOAD_ATTR_METHOD_LAZY_DICT ||
        opcode == LOAD_ATTR_METHOD_NO_DICT ||
        opcode == LOAD_ATTR_METHOD_WITH_VALUES)
        return true;
#endif
    return false;
}

// Check if an opcode is a LOAD_GLOBAL/LOAD_NAME (fallback for function names)
static inline bool is_load_global_opcode(uint8_t opcode)
{
    return opcode == LOAD_GLOBAL || opcode == LOAD_NAME;
}

// ----------------------------------------------------------------------------
// Helper to look up a name from co_names by index
static inline StringTable::Key lookup_name_from_code(PyCodeObject* code_addr, int name_idx)
{
    // TODO: Cache this!

    // Copy the code object to get co_names
    PyCodeObject code;
    if (copy_type(code_addr, code))
        return 0;

    // Get the name from co_names[name_idx]
    PyTupleObject names_tuple;
    if (copy_type(code.co_names, names_tuple))
        return 0;

    if (name_idx < 0 || name_idx >= static_cast<int>(names_tuple.ob_base.ob_size))
        return 0;

    // Read the pointer to the name object from the tuple's ob_item array
    PyObject* name_obj_ptr;
    auto item_addr = reinterpret_cast<PyObject**>(
        reinterpret_cast<uintptr_t>(code.co_names) +
        offsetof(PyTupleObject, ob_item) +
        name_idx * sizeof(PyObject*));
    if (copy_type(item_addr, name_obj_ptr))
        return 0;

    // Get the string key for this name
    auto maybe_name = string_table.key(name_obj_ptr);
    return maybe_name ? *maybe_name : 0;
}

// ----------------------------------------------------------------------------
// Extract the callable name from bytecode by scanning backwards from the current instruction
// Prioritizes LOAD_ATTR/LOAD_METHOD (for method calls) over LOAD_GLOBAL (for direct calls)
// Returns the name key, or 0 if not found
static inline StringTable::Key extract_callable_name(
    [[maybe_unused]] _Py_CODEUNIT* instr_ptr,
    [[maybe_unused]] PyCodeObject* code_addr)
{
#if PY_VERSION_HEX >= 0x030b0000
    // Scan backwards up to 32 code units looking for LOAD instructions
    constexpr int MAX_SCAN = 32;
    _Py_CODEUNIT bytecode[MAX_SCAN];

    // code_addr is a REMOTE pointer, so we calculate the remote code_start address
    auto code_start = reinterpret_cast<_Py_CODEUNIT*>(
        reinterpret_cast<uintptr_t>(code_addr) + offsetof(PyCodeObject, co_code_adaptive));

    // Calculate how many instructions we can scan (don't go before the start of bytecode)
    // Note: both instr_ptr and code_start are remote addresses, so the arithmetic is valid
    if (instr_ptr <= code_start)
        return 0;

    int available = static_cast<int>(instr_ptr - code_start);
    int to_scan = std::min(available, MAX_SCAN);

    if (to_scan <= 0)
        return 0;

    // Copy the bytecode chunk from remote memory
    auto scan_start = instr_ptr - to_scan;
    if (copy_generic(scan_start, bytecode, to_scan * sizeof(_Py_CODEUNIT)))
        return 0;

    // First pass: look for LOAD_ATTR/LOAD_METHOD (the method/attribute being called)
    for (int i = to_scan - 1; i >= 0; --i)
    {
        uint8_t opcode = _Py_OPCODE(bytecode[i]);
        if (is_load_attr_opcode(opcode))
        {
            int arg = _Py_OPARG(bytecode[i]);
#if PY_VERSION_HEX >= 0x030c0000
            // In Python 3.12+, LOAD_ATTR arg = (name_index << 1) | is_method_flag
            int name_idx = arg >> 1;
#else
            // In Python 3.11, LOAD_ATTR arg is just the name index
            int name_idx = arg;
#endif
            return lookup_name_from_code(code_addr, name_idx);
        }
    }

    // Second pass: fall back to LOAD_GLOBAL/LOAD_NAME (for direct function calls)
    for (int i = to_scan - 1; i >= 0; --i)
    {
        uint8_t opcode = _Py_OPCODE(bytecode[i]);
        if (is_load_global_opcode(opcode))
        {
            // In Python 3.11+, LOAD_GLOBAL arg = (name_index << 1) | push_null_flag
            int name_idx = _Py_OPARG(bytecode[i]) >> 1;
            return lookup_name_from_code(code_addr, name_idx);
        }
    }
#endif
    return 0;
}

// ----------------------------------------------------------------------------
#if PY_VERSION_HEX >= 0x030b0000
static inline int _read_varint(unsigned char* table, ssize_t size, ssize_t* i)
{
    ssize_t guard = size - 1;
    if (*i >= guard)
        return 0;

    int val = table[++*i] & 63;
    int shift = 0;
    while (table[*i] & 64 && *i < guard)
    {
        shift += 6;
        val |= (table[++*i] & 63) << shift;
    }
    return val;
}

// ----------------------------------------------------------------------------
static inline int _read_signed_varint(unsigned char* table, ssize_t size, ssize_t* i)
{
    int val = _read_varint(table, size, i);
    return (val & 1) ? -(val >> 1) : (val >> 1);
}
#endif

// ----------------------------------------------------------------------------
void init_frame_cache(size_t capacity)
{
    frame_cache = new LRUCache<uintptr_t, Frame>(capacity);
}

// ----------------------------------------------------------------------------
void reset_frame_cache()
{
    delete frame_cache;
    frame_cache = nullptr;
}

// ------------------------------------------------------------------------
Frame::Frame(PyObject* frame)
{
#if PY_VERSION_HEX >= 0x030b0000

#if PY_VERSION_HEX >= 0x030d0000
    _PyInterpreterFrame* iframe = reinterpret_cast<_PyInterpreterFrame*>(frame);
    const int lasti = _PyInterpreterFrame_LASTI(iframe);
    PyCodeObject* code = reinterpret_cast<PyCodeObject*>(iframe->f_executable);
#else
    const _PyInterpreterFrame* iframe = reinterpret_cast<_PyInterpreterFrame*>(frame);
    const int lasti = _PyInterpreterFrame_LASTI(iframe);
    PyCodeObject* code = iframe->f_code;
#endif  // PY_VERSION_HEX >= 0x030d0000
    PyCode_Addr2Location(code, lasti << 1, &location.line, &location.column, &location.line_end,
                         &location.column_end);
    location.column++;
    location.column_end++;
    name = string_table.key_unsafe(code->co_qualname);
#if PY_VERSION_HEX >= 0x030c0000
    is_entry = (iframe->owner == FRAME_OWNED_BY_CSTACK);  // Shim frame
#else
    is_entry = iframe->is_entry;
#endif

#else
    PyFrameObject* py_frame = reinterpret_cast<PyFrameObject*>(frame);
    PyCodeObject* code = py_frame->f_code;

    location.line = PyFrame_GetLineNumber(py_frame);
    name = string_table.key_unsafe(code->co_name);
#endif
    filename = string_table.key_unsafe(code->co_filename);
}

// ------------------------------------------------------------------------
Result<Frame::Ptr> Frame::create(PyCodeObject* code, int lasti)
{
    auto maybe_filename = string_table.key(code->co_filename);
    if (!maybe_filename)
    {
        return ErrorKind::FrameError;
    }

#if PY_VERSION_HEX >= 0x030b0000
    auto maybe_name = string_table.key(code->co_qualname);
#else
    auto maybe_name = string_table.key(code->co_name);
#endif

    if (!maybe_name)
    {
        return ErrorKind::FrameError;
    }

    auto frame = std::make_unique<Frame>(*maybe_filename, *maybe_name);
    auto infer_location_success = frame->infer_location(code, lasti);
    if (!infer_location_success)
    {
        return ErrorKind::LocationError;
    }

    return frame;
}

// ------------------------------------------------------------------------
#ifndef UNWIND_NATIVE_DISABLE
Result<Frame::Ptr> Frame::create(unw_cursor_t& cursor, unw_word_t pc)
{
    auto filename = string_table.key(pc);

    auto maybe_name = string_table.key(cursor);
    if (!maybe_name)
    {
        return ErrorKind::FrameError;
    }

    return std::make_unique<Frame>(filename, *maybe_name);
}
#endif  // UNWIND_NATIVE_DISABLE

// ----------------------------------------------------------------------------
Result<void> Frame::infer_location(PyCodeObject* code_obj, int lasti)
{
    unsigned int lineno = code_obj->co_firstlineno;
    Py_ssize_t len = 0;

#if PY_VERSION_HEX >= 0x030b0000
    auto table = pybytes_to_bytes_and_size(code_obj->co_linetable, &len);
    if (table == nullptr)
    {
        return ErrorKind::LocationError;
    }

    auto table_data = table.get();

    for (Py_ssize_t i = 0, bc = 0; i < len; i++)
    {
        bc += (table[i] & 7) + 1;
        int code = (table[i] >> 3) & 15;
        unsigned char next_byte = 0;
        switch (code)
        {
            case 15:
                break;

            case 14:  // Long form
                lineno += _read_signed_varint(table_data, len, &i);

                this->location.line = lineno;
                this->location.line_end = lineno + _read_varint(table_data, len, &i);
                this->location.column = _read_varint(table_data, len, &i);
                this->location.column_end = _read_varint(table_data, len, &i);

                break;

            case 13:  // No column data
                lineno += _read_signed_varint(table_data, len, &i);

                this->location.line = lineno;
                this->location.line_end = lineno;
                this->location.column = this->location.column_end = 0;

                break;

            case 12:  // New lineno
            case 11:
            case 10:
                if (i >= len - 2)
                {
                    return ErrorKind::LocationError;
                }

                lineno += code - 10;

                this->location.line = lineno;
                this->location.line_end = lineno;
                this->location.column = 1 + table[++i];
                this->location.column_end = 1 + table[++i];

                break;

            default:
                if (i >= len - 1)
                {
                    return ErrorKind::LocationError;
                }

                next_byte = table[++i];

                this->location.line = lineno;
                this->location.line_end = lineno;
                this->location.column = 1 + (code << 3) + ((next_byte >> 4) & 7);
                this->location.column_end = this->location.column + (next_byte & 15);
        }

        if (bc > lasti)
            break;
    }

#elif PY_VERSION_HEX >= 0x030a0000
    auto table = pybytes_to_bytes_and_size(code_obj->co_linetable, &len);
    if (table == nullptr)
    {
        return ErrorKind::LocationError;
    }

    lasti <<= 1;
    for (int i = 0, bc = 0; i < len; i++)
    {
        int sdelta = table[i++];
        if (sdelta == 0xff)
            break;

        bc += sdelta;

        int ldelta = table[i];
        if (ldelta == 0x80)
            ldelta = 0;
        else if (ldelta > 0x80)
            lineno -= 0x100;

        lineno += ldelta;
        if (bc > lasti)
            break;
    }

#else
    auto table = pybytes_to_bytes_and_size(code_obj->co_lnotab, &len);
    if (table == nullptr)
    {
        return ErrorKind::LocationError;
    }

    for (int i = 0, bc = 0; i < len; i++)
    {
        bc += table[i++];
        if (bc > lasti)
            break;

        if (table[i] >= 0x80)
            lineno -= 0x100;

        lineno += table[i];
    }

#endif

    this->location.line = lineno;
    this->location.line_end = lineno;
    // this->location.column = 0;
    // this->location.column_end = 0;

    return Result<void>::ok();
}

// ------------------------------------------------------------------------
Frame::Key Frame::key(PyCodeObject* code, int lasti)
{
    return ((static_cast<uintptr_t>(((reinterpret_cast<uintptr_t>(code)))) << 16) | lasti);
}

// ----------------------------------------------------------------------------
Frame::Key Frame::key(PyObject* frame)
{
#if PY_VERSION_HEX >= 0x030d0000
    _PyInterpreterFrame* iframe = reinterpret_cast<_PyInterpreterFrame*>(frame);
    const int lasti = _PyInterpreterFrame_LASTI(iframe);
    PyCodeObject* code = reinterpret_cast<PyCodeObject*>(iframe->f_executable);
#elif PY_VERSION_HEX >= 0x030b0000
    const _PyInterpreterFrame* iframe = reinterpret_cast<_PyInterpreterFrame*>(frame);
    const int lasti = _PyInterpreterFrame_LASTI(iframe);
    PyCodeObject* code = iframe->f_code;
#else
    const PyFrameObject* py_frame = reinterpret_cast<PyFrameObject*>(frame);
    const int lasti = py_frame->f_lasti;
    PyCodeObject* code = py_frame->f_code;
#endif
    return key(code, lasti);
}

// ------------------------------------------------------------------------
#if PY_VERSION_HEX >= 0x030b0000
Result<std::reference_wrapper<Frame>> Frame::read(_PyInterpreterFrame* frame_addr,
                                                  _PyInterpreterFrame** prev_addr)
#else
Result<std::reference_wrapper<Frame>> Frame::read(PyObject* frame_addr, PyObject** prev_addr)
#endif
{
#if PY_VERSION_HEX >= 0x030b0000
    _PyInterpreterFrame iframe;
    auto resolved_addr =
        stack_chunk ? reinterpret_cast<_PyInterpreterFrame*>(stack_chunk->resolve(frame_addr))
                    : frame_addr;
    if (resolved_addr != frame_addr)
    {
        frame_addr = resolved_addr;
    }
    else
    {
        if (copy_type(frame_addr, iframe))
        {
            return ErrorKind::FrameError;
        }
        frame_addr = &iframe;
    }
    if (frame_addr == NULL)
    {
        return ErrorKind::FrameError;
    }

#if PY_VERSION_HEX >= 0x030c0000
    if (frame_addr->owner == FRAME_OWNED_BY_CSTACK)
    {
        *prev_addr = frame_addr->previous;
        // This is a C frame, we just need to ignore it
        return std::ref(C_FRAME);
    }

    if (frame_addr->owner != FRAME_OWNED_BY_THREAD && frame_addr->owner != FRAME_OWNED_BY_GENERATOR)
    {
        return ErrorKind::FrameError;
    }
#endif  // PY_VERSION_HEX >= 0x030c0000

    // We cannot use _PyInterpreterFrame_LASTI because _PyCode_CODE reads
    // from the code object.
#if PY_VERSION_HEX >= 0x030d0000
    const int lasti =
        (static_cast<int>((frame_addr->instr_ptr - 1 -
                           reinterpret_cast<_Py_CODEUNIT*>(
                               (reinterpret_cast<PyCodeObject*>(frame_addr->f_executable)))))) -
        offsetof(PyCodeObject, co_code_adaptive) / sizeof(_Py_CODEUNIT);
    auto maybe_frame = Frame::get(reinterpret_cast<PyCodeObject*>(frame_addr->f_executable), lasti);
    if (!maybe_frame)
    {
        return ErrorKind::FrameError;
    }

    auto& frame = maybe_frame->get();
#else
    const int lasti = (static_cast<int>((frame_addr->prev_instr -
                                         reinterpret_cast<_Py_CODEUNIT*>((frame_addr->f_code))))) -
                      offsetof(PyCodeObject, co_code_adaptive) / sizeof(_Py_CODEUNIT);
    auto maybe_frame = Frame::get(frame_addr->f_code, lasti);
    if (!maybe_frame)
    {
        return ErrorKind::FrameError;
    }

    auto& frame = maybe_frame->get();
#endif  // PY_VERSION_HEX >= 0x030d0000
    if (&frame != &INVALID_FRAME)
    {
#if PY_VERSION_HEX >= 0x030c0000
        frame.is_entry = (frame_addr->owner == FRAME_OWNED_BY_CSTACK);  // Shim frame
#else   // PY_VERSION_HEX < 0x030c0000
        frame.is_entry = frame_addr->is_entry;
#endif  // PY_VERSION_HEX >= 0x030c0000

        // Detect if we're paused at a CALL instruction (likely in C code)
        // Read only the opcode byte to minimize memory copying
        _Py_CODEUNIT instr;
#if PY_VERSION_HEX >= 0x030d0000
        // In 3.13+, instr_ptr points to the current instruction
        if (!copy_type(frame_addr->instr_ptr, instr))
        {
            frame.in_c_call = is_call_opcode(_Py_OPCODE(instr));
            if (frame.in_c_call)
            {
                frame.c_call_name = extract_callable_name(
                    frame_addr->instr_ptr,
                    reinterpret_cast<PyCodeObject*>(frame_addr->f_executable));
            }
        }
#else
        // In 3.11-3.12, prev_instr points to the last executed instruction
        // When in a C call, prev_instr points to the specialized PRECALL_BUILTIN_* instruction
        if (!copy_type(frame_addr->prev_instr, instr))
        {
            frame.in_c_call = is_call_opcode(_Py_OPCODE(instr));
            if (frame.in_c_call)
            {
                frame.c_call_name = extract_callable_name(
                    frame_addr->prev_instr,
                    frame_addr->f_code);
            }
        }
#endif
    }

    *prev_addr = &frame == &INVALID_FRAME ? NULL : frame_addr->previous;

#else   // PY_VERSION_HEX < 0x030b0000
    // Unwind the stack from leaf to root and store it in a stack. This way we
    // can print it from root to leaf.
    PyFrameObject py_frame;

    if (copy_type(frame_addr, py_frame))
    {
        return ErrorKind::FrameError;
    }

    auto maybe_frame = Frame::get(py_frame.f_code, py_frame.f_lasti);
    if (!maybe_frame)
    {
        return ErrorKind::FrameError;
    }

    auto& frame = maybe_frame->get();

    // Detect if we're paused at a CALL instruction (likely in C code)
    // For Python < 3.11, we need to read the bytecode from the code object
    if (&frame != &INVALID_FRAME && py_frame.f_lasti >= 0)
    {
        // py_frame.f_code is a remote pointer, so we need to copy the code object first
        PyCodeObject code;
        if (!copy_type(py_frame.f_code, code))
        {
            // Now code.co_code is also a remote pointer to the bytecode bytes object
            // Read just the opcode byte at f_lasti from the bytecode
            uint8_t opcode;
            auto bytecode_ptr = reinterpret_cast<uint8_t*>(
                reinterpret_cast<uintptr_t>(code.co_code) +
                offsetof(PyBytesObject, ob_sval) + py_frame.f_lasti);
            if (!copy_type(bytecode_ptr, opcode))
            {
                frame.in_c_call = is_call_opcode(opcode);
            }
        }
    }

    *prev_addr = (&frame == &INVALID_FRAME) ? NULL : reinterpret_cast<PyObject*>(py_frame.f_back);
#endif  // PY_VERSION_HEX >= 0x030b0000

    if (frame.in_c_call) {
        StringTable::Key c_frame_name;
        if (frame.c_call_name != 0) {
            c_frame_name = frame.c_call_name;
        } else {
            c_frame_name = string_table.key("(C function)");
        }
        
        const auto& c_frame_filename = frame.filename;
        const auto& c_frame_location = frame.location;

        uintptr_t c_frame_key = c_frame_filename;
        c_frame_key = (c_frame_key * 31) + c_frame_name;
        c_frame_key = (c_frame_key * 31) + static_cast<uintptr_t>(c_frame_location.line);
        c_frame_key = (c_frame_key * 31) + static_cast<uintptr_t>(c_frame_location.column);        

        auto c_frame = std::make_unique<Frame>(c_frame_filename, c_frame_name, c_frame_location);
        c_frame->cache_key = c_frame_key;
        frame.c_frame_key = c_frame_key;

        frame_cache->store(c_frame_key, std::move(c_frame));

        Renderer::get().frame(c_frame_key, c_frame_filename, c_frame_name, c_frame_location.line,
                              c_frame_location.line_end, c_frame_location.column,
                              c_frame_location.column_end);
    }


    return std::ref(frame);
}

// ----------------------------------------------------------------------------
Result<std::reference_wrapper<Frame>> Frame::get(PyCodeObject* code_addr, int lasti)
{
    auto frame_key = Frame::key(code_addr, lasti);

    auto maybe_frame = frame_cache->lookup(frame_key);
    if (maybe_frame)
    {
        return *maybe_frame;
    }

    PyCodeObject code;
    if (copy_type(code_addr, code))
    {
        return std::ref(INVALID_FRAME);
    }

    auto maybe_new_frame = Frame::create(&code, lasti);
    if (!maybe_new_frame)
    {
        return std::ref(INVALID_FRAME);
    }

    auto new_frame = std::move(*maybe_new_frame);
    new_frame->cache_key = frame_key;
    auto& f = *new_frame;
    Renderer::get().frame(frame_key, new_frame->filename, new_frame->name, new_frame->location.line,
                          new_frame->location.line_end, new_frame->location.column,
                          new_frame->location.column_end);
    frame_cache->store(frame_key, std::move(new_frame));
    return std::ref(f);
}

// ----------------------------------------------------------------------------
Frame& Frame::get(PyObject* frame)
{
    auto frame_key = Frame::key(frame);

    auto maybe_frame = frame_cache->lookup(frame_key);
    if (maybe_frame)
    {
        return *maybe_frame;
    }

    auto new_frame = std::make_unique<Frame>(frame);
    new_frame->cache_key = frame_key;
    auto& f = *new_frame;
    Renderer::get().frame(frame_key, new_frame->filename, new_frame->name, new_frame->location.line,
                          new_frame->location.line_end, new_frame->location.column,
                          new_frame->location.column_end);
    frame_cache->store(frame_key, std::move(new_frame));
    return f;
}

// ----------------------------------------------------------------------------
#ifndef UNWIND_NATIVE_DISABLE
Result<std::reference_wrapper<Frame>> Frame::get(unw_cursor_t& cursor)
{
    unw_word_t pc;
    unw_get_reg(&cursor, UNW_REG_IP, &pc);
    if (pc == 0)
    {
        return ErrorKind::FrameError;
    }

    uintptr_t frame_key = static_cast<uintptr_t>(pc);
    auto maybe_frame = frame_cache->lookup(frame_key);
    if (maybe_frame)
    {
        return *maybe_frame;
    }

    auto maybe_new_frame = Frame::create(cursor, pc);
    if (!maybe_new_frame)
    {
        return std::ref(UNKNOWN_FRAME);
    }

    auto frame = std::move(*maybe_new_frame);
    frame->cache_key = frame_key;
    auto& f = *frame;
    Renderer::get().frame(frame_key, frame->filename, frame->name, frame->location.line,
                          frame->location.line_end, frame->location.column,
                          frame->location.column_end);
    frame_cache->store(frame_key, std::move(frame));
    return std::ref(f);
}
#endif  // UNWIND_NATIVE_DISABLE

// ----------------------------------------------------------------------------
Frame& Frame::get(StringTable::Key name)
{
    uintptr_t frame_key = static_cast<uintptr_t>(name);

    auto maybe_frame = frame_cache->lookup(frame_key);
    if (maybe_frame)
    {
        return *maybe_frame;
    }

    auto frame = std::make_unique<Frame>(name);
    frame->cache_key = frame_key;
    auto& f = *frame;
    Renderer::get().frame(frame_key, frame->filename, frame->name, frame->location.line,
                          frame->location.line_end, frame->location.column,
                          frame->location.column_end);
    frame_cache->store(frame_key, std::move(frame));
    return f;
}
