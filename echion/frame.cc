#include <echion/frame.h>

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
Frame::Frame(PyObject *frame)
{
#if PY_VERSION_HEX >= 0x030b0000

#if PY_VERSION_HEX >= 0x030d0000
  _PyInterpreterFrame *iframe = (_PyInterpreterFrame *)frame;
  const int lasti = _PyInterpreterFrame_LASTI(iframe);
  PyCodeObject *code = (PyCodeObject *)iframe->f_executable;
#else
  const _PyInterpreterFrame *iframe = (_PyInterpreterFrame *)frame;
  const int lasti = _PyInterpreterFrame_LASTI(iframe);
  PyCodeObject *code = iframe->f_code;
#endif // PY_VERSION_HEX >= 0x030d0000
  PyCode_Addr2Location(code, lasti << 1, &location.line, &location.column,
                       &location.line_end, &location.column_end);
  location.column++;
  location.column_end++;
  name = string_table.key_unsafe(code->co_qualname);
#if PY_VERSION_HEX >= 0x030c0000
  is_entry = (iframe->owner == FRAME_OWNED_BY_CSTACK); // Shim frame
#else
  is_entry = iframe->is_entry;
#endif

#else
  PyFrameObject *py_frame = (PyFrameObject *)frame;
  PyCodeObject *code = py_frame->f_code;

  location.line = PyFrame_GetLineNumber(py_frame);
  name = string_table.key_unsafe(code->co_name);
#endif
  filename = string_table.key_unsafe(code->co_filename);
}

// ------------------------------------------------------------------------
Frame::Frame(PyCodeObject *code, int lasti)
{
  try
  {
    filename = string_table.key(code->co_filename);
#if PY_VERSION_HEX >= 0x030b0000
    name = string_table.key(code->co_qualname);
#else
    name = string_table.key(code->co_name);
#endif
  }
  catch (StringTable::Error &)
  {
    throw Error();
  }

  infer_location(code, lasti);
}

// ------------------------------------------------------------------------
#ifndef UNWIND_NATIVE_DISABLE
Frame::Frame(unw_cursor_t &cursor, unw_word_t pc)
{
  try
  {
    filename = string_table.key(pc);
    name = string_table.key(cursor);
  }
  catch (StringTable::Error &)
  {
    throw Error();
  }
}
#endif // UNWIND_NATIVE_DISABLE

// ------------------------------------------------------------------------
Frame &Frame::read(PyObject *frame_addr, PyObject **prev_addr)
{
#if PY_VERSION_HEX >= 0x030b0000
  _PyInterpreterFrame iframe;
#if PY_VERSION_HEX >= 0x030d0000
  // From Python versions 3.13, f_executable can have objects other than
  // code objects for an internal frame. We need to skip some frames if
  // its f_executable is not code as suggested here:
  // https://github.com/python/cpython/issues/100987#issuecomment-1485556487
  PyObject f_executable;

  while (frame_addr != NULL)
  {
    if (copy_type((_PyInterpreterFrame *)frame_addr, iframe) ||
        copy_type(iframe.f_executable, f_executable))
    {
      throw Frame::Error();
    }
    if (f_executable.ob_type == &PyCode_Type)
    {
      break;
    }
    frame_addr = (PyObject *)((_PyInterpreterFrame *)frame_addr)->previous;
  }

  if (frame_addr == NULL)
  {
    throw Frame::Error();
  }

#endif // PY_VERSION_HEX >= 0x030d0000

  if (copy_type(frame_addr, iframe))
    throw Error();

  // We cannot use _PyInterpreterFrame_LASTI because _PyCode_CODE reads
  // from the code object.
#if PY_VERSION_HEX >= 0x030d0000
  const int lasti =
      ((int)(iframe.instr_ptr - 1 -
             (_Py_CODEUNIT *)((PyCodeObject *)iframe.f_executable))) -
      offsetof(PyCodeObject, co_code_adaptive) / sizeof(_Py_CODEUNIT);
  auto &frame = Frame::get((PyCodeObject *)iframe.f_executable, lasti);
#else
  const int lasti =
      ((int)(iframe.prev_instr - (_Py_CODEUNIT *)(iframe.f_code))) -
      offsetof(PyCodeObject, co_code_adaptive) / sizeof(_Py_CODEUNIT);
  auto &frame = Frame::get(iframe.f_code, lasti);
#endif // PY_VERSION_HEX >= 0x030d0000
  if (&frame != &INVALID_FRAME)
  {
#if PY_VERSION_HEX >= 0x030c0000
    frame.is_entry = (iframe.owner == FRAME_OWNED_BY_CSTACK); // Shim frame
#else
    frame.is_entry = iframe.is_entry;
#endif
  }

  *prev_addr = &frame == &INVALID_FRAME ? NULL : (PyObject *)iframe.previous;

#else // Python < 3.11
  // Unwind the stack from leaf to root and store it in a stack. This way we
  // can print it from root to leaf.
  PyFrameObject py_frame;

  if (copy_type(frame_addr, py_frame))
    throw Error();

  auto &frame = Frame::get(py_frame.f_code, py_frame.f_lasti);

  *prev_addr = (&frame == &INVALID_FRAME) ? NULL : (PyObject *)py_frame.f_back;
#endif

  return frame;
}

#if PY_VERSION_HEX >= 0x030b0000
// ------------------------------------------------------------------------
Frame &Frame::read_local(_PyInterpreterFrame *frame_addr,
                         PyObject **prev_addr)
{
#if PY_VERSION_HEX >= 0x030d0000
  // From Python versions 3.13, f_executable can have objects other than
  // code objects for an internal frame. We need to skip some frames if
  // its f_executable is not code as suggested here:
  // https://github.com/python/cpython/issues/100987#issuecomment-1485556487
  PyObject f_executable;

  for (; frame_addr; frame_addr = frame_addr->previous)
  {
    // TODO: Cache the executable address for faster reads.
    if (copy_type(frame_addr->f_executable, f_executable))
    {
      throw Frame::Error();
    }
    if (f_executable.ob_type == &PyCode_Type)
    {
      break;
    }
  }

  if (frame_addr == NULL)
  {
    throw Frame::Error();
  }

#endif // PY_VERSION_HEX >= 0x030d0000

  // We cannot use _PyInterpreterFrame_LASTI because _PyCode_CODE reads
  // from the code object.
#if PY_VERSION_HEX >= 0x030d0000
  const int lasti =
      ((int)(frame_addr->instr_ptr - 1 -
             (_Py_CODEUNIT *)((PyCodeObject *)frame_addr->f_executable))) -
      offsetof(PyCodeObject, co_code_adaptive) / sizeof(_Py_CODEUNIT);
  auto &frame = Frame::get((PyCodeObject *)frame_addr->f_executable, lasti);
#else
  const int lasti =
      ((int)(frame_addr->prev_instr - (_Py_CODEUNIT *)(frame_addr->f_code))) -
      offsetof(PyCodeObject, co_code_adaptive) / sizeof(_Py_CODEUNIT);
  auto &frame = Frame::get(frame_addr->f_code, lasti);
#endif // PY_VERSION_HEX >= 0x030d0000
  if (&frame != &INVALID_FRAME)
  {
#if PY_VERSION_HEX >= 0x030c0000
    frame.is_entry = (frame_addr->owner == FRAME_OWNED_BY_CSTACK); // Shim frame
#else
    frame.is_entry = frame_addr->is_entry;
#endif
  }

  *prev_addr =
      &frame == &INVALID_FRAME ? NULL : (PyObject *)frame_addr->previous;

  return frame;
}
#endif

// ----------------------------------------------------------------------------
Frame &Frame::get(PyCodeObject *code_addr, int lasti)
{
  PyCodeObject code;
  if (copy_type(code_addr, code))
    return INVALID_FRAME;

  uintptr_t frame_key = Frame::key(code_addr, lasti);

  try
  {
    return frame_cache->lookup(frame_key);
  }
  catch (LRUCache<uintptr_t, Frame>::LookupError &)
  {
    try
    {
      auto new_frame = std::make_unique<Frame>(&code, lasti);
      new_frame->cache_key = frame_key;
      auto &f = *new_frame;
      Renderer::get().frame(
          frame_key, new_frame->filename, new_frame->name,
          new_frame->location.line, new_frame->location.line_end,
          new_frame->location.column, new_frame->location.column_end);
      frame_cache->store(frame_key, std::move(new_frame));
      return f;
    }
    catch (Frame::Error &)
    {
      return INVALID_FRAME;
    }
  }
}

// ----------------------------------------------------------------------------
Frame &Frame::get(PyObject *frame)
{
  auto frame_key = Frame::key(frame);

  try
  {
    return frame_cache->lookup(frame_key);
  }
  catch (LRUCache<uintptr_t, Frame>::LookupError &)
  {
    auto new_frame = std::make_unique<Frame>(frame);
    new_frame->cache_key = frame_key;
    auto &f = *new_frame;
    Renderer::get().frame(
        frame_key, new_frame->filename, new_frame->name,
        new_frame->location.line, new_frame->location.line_end,
        new_frame->location.column, new_frame->location.column_end);
    frame_cache->store(frame_key, std::move(new_frame));
    return f;
  }
}

// ----------------------------------------------------------------------------
#ifndef UNWIND_NATIVE_DISABLE
Frame &Frame::get(unw_cursor_t &cursor)
{
  unw_word_t pc;
  unw_get_reg(&cursor, UNW_REG_IP, &pc);
  if (pc == 0)
    throw Error();

  uintptr_t frame_key = (uintptr_t)pc;
  try
  {
    return frame_cache->lookup(frame_key);
  }
  catch (LRUCache<uintptr_t, Frame>::LookupError &)
  {
    try
    {
      auto frame = std::make_unique<Frame>(cursor, pc);
      frame->cache_key = frame_key;
      auto &f = *frame;
      Renderer::get().frame(frame_key, frame->filename, frame->name,
                            frame->location.line, frame->location.line_end,
                            frame->location.column, frame->location.column_end);
      frame_cache->store(frame_key, std::move(frame));
      return f;
    }
    catch (Frame::Error &)
    {
      return UNKNOWN_FRAME;
    }
  }
}
#endif // UNWIND_NATIVE_DISABLE

// ----------------------------------------------------------------------------
Frame &Frame::get(StringTable::Key name)
{
  uintptr_t frame_key = (uintptr_t)name;
  try
  {
    return frame_cache->lookup(frame_key);
  }
  catch (LRUCache<uintptr_t, Frame>::LookupError &)
  {
    auto frame = std::make_unique<Frame>(name);
    frame->cache_key = frame_key;
    auto &f = *frame;
    Renderer::get().frame(frame_key, frame->filename, frame->name,
                          frame->location.line, frame->location.line_end,
                          frame->location.column, frame->location.column_end);
    frame_cache->store(frame_key, std::move(frame));
    return f;
  }
}
