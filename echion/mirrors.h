// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#if PY_VERSION_HEX >= 0x030c0000
// https://github.com/python/cpython/issues/108216#issuecomment-1696565797
#undef _PyGC_FINALIZED
#endif

#include <dictobject.h>
#include <setobject.h>

#include <memory>

#include <echion/errors.h>

#if PY_VERSION_HEX >= 0x030b0000
#define Py_BUILD_CORE
#if defined __GNUC__ && defined HAVE_STD_ATOMIC
#undef HAVE_STD_ATOMIC
#endif
#include <internal/pycore_dict.h>
#else
typedef struct
{
    Py_hash_t me_hash;
    PyObject* me_key;
    PyObject* me_value; /* This field is only meaningful for combined tables */
} PyDictKeyEntry;

typedef Py_ssize_t (*dict_lookup_func)(PyDictObject* mp, PyObject* key, Py_hash_t hash,
                                       PyObject** value_addr);

/* See dictobject.c for actual layout of DictKeysObject */
typedef struct _dictkeysobject
{
    Py_ssize_t dk_refcnt;

    /* Size of the hash table (dk_indices). It must be a power of 2. */
    Py_ssize_t dk_size;

    dict_lookup_func dk_lookup;

    /* Number of usable entries in dk_entries. */
    Py_ssize_t dk_usable;

    /* Number of used entries in dk_entries. */
    Py_ssize_t dk_nentries;

    char dk_indices[]; /* char is required to avoid strict aliasing. */

} PyDictKeysObject;

typedef PyObject* PyDictValues;
#endif

#include <unordered_set>

#include <echion/vm.h>

class MirrorObject
{
public:
    [[nodiscard]] Result<PyObject*> reflect();

protected:
    MirrorObject(std::unique_ptr<char[]> data, PyObject* reflected);

    std::unique_ptr<char[]> data = nullptr;
    PyObject* reflected = NULL;
};

// ----------------------------------------------------------------------------
class MirrorDict : public MirrorObject
{
public:
    [[nodiscard]] static Result<MirrorDict> create(PyObject* dict_addr);

    [[nodiscard]] Result<PyObject*> get_item(PyObject* key);

private:
    MirrorDict(PyDictObject dict, std::unique_ptr<char[]> data, PyObject* reflected);
    PyDictObject dict;
};

// ----------------------------------------------------------------------------
class MirrorSet : public MirrorObject
{
public:
    [[nodiscard]] static Result<MirrorSet> create(PyObject*);
    [[nodiscard]] Result<std::unordered_set<PyObject*>> as_unordered_set();

private:
    MirrorSet(size_t size, PySetObject set, std::unique_ptr<char[]> data, PyObject* reflected);

    size_t size;
    PySetObject set;
};
