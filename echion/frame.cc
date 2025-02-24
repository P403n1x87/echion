#include <echion/frame.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>
#if PY_VERSION_HEX >= 0x030d0000
#define Py_BUILD_CORE
#include <internal/pycore_code.h>
#endif // PY_VERSION_HEX >= 0x030d0000
#if PY_VERSION_HEX >= 0x030b0000
#include <internal/pycore_frame.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>

#ifndef UNWIND_NATIVE_DISABLE
#include <cxxabi.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif // UNWIND_NATIVE_DISABLE

#include <echion/cache.h>
#include <echion/strings.h>

// ----------------------------------------------------------------------------
#if PY_VERSION_HEX >= 0x030b0000
static inline int
_read_varint(unsigned char *table, ssize_t size, ssize_t *i)
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
static inline int
_read_signed_varint(unsigned char *table, ssize_t size, ssize_t *i)
{
  int val = _read_varint(table, size, i);
  return (val & 1) ? -(val >> 1) : (val >> 1);
}
#endif

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
  PyCode_Addr2Location(code, lasti << 1, &location.line, &location.column, &location.line_end, &location.column_end);
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
void Frame::render()
{
  Renderer::get().render_frame(*this);
}

// ------------------------------------------------------------------------
void Frame::render_where()
{
  WhereRenderer::get().render_frame(*this);
}

// ------------------------------------------------------------------------
void Frame::infer_location(PyCodeObject *code, int lasti)
{
  unsigned int lineno = code->co_firstlineno;
  Py_ssize_t len = 0;

#if PY_VERSION_HEX >= 0x030b0000
  auto table = pybytes_to_bytes_and_size(code->co_linetable, &len);
  if (table == nullptr)
    throw LocationError();

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

    case 14: // Long form
      lineno += _read_signed_varint(table_data, len, &i);

      this->location.line = lineno;
      this->location.line_end = lineno + _read_varint(table_data, len, &i);
      this->location.column = _read_varint(table_data, len, &i);
      this->location.column_end = _read_varint(table_data, len, &i);

      break;

    case 13: // No column data
      lineno += _read_signed_varint(table_data, len, &i);

      this->location.line = lineno;
      this->location.line_end = lineno;
      this->location.column = this->location.column_end = 0;

      break;

    case 12: // New lineno
    case 11:
    case 10:
      if (i >= len - 2)
        throw LocationError();

      lineno += code - 10;

      this->location.line = lineno;
      this->location.line_end = lineno;
      this->location.column = 1 + table[++i];
      this->location.column_end = 1 + table[++i];

      break;

    default:
      if (i >= len - 1)
        throw LocationError();

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
  auto table = pybytes_to_bytes_and_size(code->co_linetable, &len);
  if (table == nullptr)
    throw LocationError();

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
  auto table = pybytes_to_bytes_and_size(code->co_lnotab, &len);
  if (table == nullptr)
    throw LocationError();

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
  this->location.column = 0;
  this->location.column_end = 0;
}

// ------------------------------------------------------------------------
Frame::Key Frame::key(PyCodeObject *code, int lasti)
{
  return (((uintptr_t)(((uintptr_t)code) & MOJO_INT32) << 16) | lasti);
}

// ------------------------------------------------------------------------
Frame::Key Frame::key(PyObject *frame)
{

#if PY_VERSION_HEX >= 0x030d0000
  _PyInterpreterFrame *iframe = (_PyInterpreterFrame *)frame;
  const int lasti = _PyInterpreterFrame_LASTI(iframe);
  PyCodeObject *code = (PyCodeObject *)iframe->f_executable;
#elif PY_VERSION_HEX >= 0x030b0000
  const _PyInterpreterFrame *iframe = (_PyInterpreterFrame *)frame;
  const int lasti = _PyInterpreterFrame_LASTI(iframe);
  PyCodeObject *code = iframe->f_code;
#else
  const PyFrameObject *py_frame = (PyFrameObject *)frame;
  const int lasti = py_frame->f_lasti;
  PyCodeObject *code = py_frame->f_code;
#endif
  return key(code, lasti);
}
