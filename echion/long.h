// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.
#pragma once

#include <Python.h>
#if defined __GNUC__ && defined HAVE_STD_ATOMIC
#undef HAVE_STD_ATOMIC
#endif
#if PY_VERSION_HEX >= 0x030c0000
#define Py_BUILD_CORE
#include <internal/pycore_long.h>
#endif

#include <exception>

#include <echion/vm.h>

class LongError : public std::exception {
  const char *what() const noexcept override { return "LongError"; }
};

// ----------------------------------------------------------------------------
#if PY_VERSION_HEX >= 0x030c0000
static long long pylong_to_llong(PyObject *long_addr) {
  // Only used to extract a task-id on Python 3.12, omits overflow checks
  PyLongObject long_obj;
  long long ret = 0;

  if (copy_type(long_addr, long_obj))
    throw LongError();

  if (!PyLong_CheckExact(&long_obj))
    throw LongError();

  if (_PyLong_IsCompact(&long_obj)) {
    ret = (long long)_PyLong_CompactValue(&long_obj);
  } else {
    // If we're here, then we need to iterate over the digits
    // We might overflow, but we don't care for now
    int sign = _PyLong_NonCompactSign(&long_obj);
    Py_ssize_t i = _PyLong_DigitCount(&long_obj);
    while (--i >= 0) {
      ret <<= PyLong_SHIFT;
      ret |= long_obj.long_value.ob_digit[i];
    }
    ret *= sign;
  }

  return ret;
}
#endif
