# Porting echion to a new CPython version

This document covers porting the **standalone echion repo**
(`github.com/P403n1x87/echion`) to a new CPython minor version.
It lives in `DataDog/echion` and is intended for eventual upstreaming to `P403n1x87/echion`.

The companion document for the full dd-trace-py integration (locks, memalloc,
asyncio Python-side, CI, Riot) is
[`docs/contributing-profiling-new-cpython.rst`](https://github.com/DataDog/dd-trace-py/blob/main/docs/contributing-profiling-new-cpython.rst)
in the `DataDog/dd-trace-py` repo.

---

## Context and ground truth

| Repo | What it is | Status |
|------|-----------|--------|
| `P403n1x87/echion` | Standalone echion library | 3.13 is latest merged |
| `DataDog/dd-trace-py` `stack/echion/` | Vendor copy inside dd-trace-py | **3.14 already merged** (PR #15546) |
| echion PR #179 | Attempt at 3.14 support in standalone | Open, untested — inspiration only |

**The dd-trace-py vendor copy is the authoritative reference implementation.**
When porting the standalone repo, treat the files under
`ddtrace/internal/datadog/profiling/stack/` as your diff guide — the hard
problems have already been solved there.

File mapping between the two repos:

| Standalone echion | dd-trace-py vendor copy |
|-------------------|------------------------|
| `echion/frame.h` | `stack/echion/echion/frame.h` |
| `echion/cpython/tasks.h` | `stack/echion/echion/cpython/tasks.h` |
| `echion/tasks.h` | `stack/echion/echion/tasks.h` |
| `echion/threads.h` | `stack/echion/echion/threads.h` |
| `echion/state.h` | `stack/echion/echion/state.h` |
| `echion/greenlets.h` | `stack/echion/echion/greenlets.h` |
| `echion/stacks.h` | `stack/echion/echion/stacks.h` |
| `src/echion/frame.cc` | `stack/src/echion/frame.cc` |
| `src/echion/threads.cc` | `stack/src/echion/threads.cc` |
| `src/echion/tasks.cc` | `stack/src/echion/tasks.cc` |
| `src/echion/stack_chunk.cc` | `stack/src/echion/stack_chunk.cc` |

---

## Version hex quick reference

```
Python 3.11  →  0x030b0000
Python 3.12  →  0x030c0000
Python 3.13  →  0x030d0000   ← last merged in standalone
Python 3.14  →  0x030e0000   ← merged in dd-trace-py vendor copy
Python 3.15  →  0x030f0000   ← next target
```

---

## Phase 1 — Port to 3.14

### Step 1: Diff CPython 3.13 → 3.14 on headers echion depends on

Run the `compare-cpython-versions` skill (dd-trace-py) or manually clone
`python/cpython` and diff between `v3.13.x` and `v3.14.x`:

```bash
git diff v3.13.0 v3.14.0 -- \
  Include/internal/pycore_interpframe_structs.h \
  Include/internal/pycore_frame.h \
  Include/internal/pycore_interpframe.h \
  Include/internal/pycore_stackref.h \
  Include/internal/pycore_llist.h \
  Include/internal/pycore_tstate.h \
  Include/internal/pycore_runtime.h \
  Include/cpython/genobject.h \
  Modules/_asynciomodule.c
```

### Step 2: Apply changes — 3.14 deltas already solved in dd-trace-py

The following summarises every concrete change needed. All of these are
already implemented in the dd-trace-py vendor copy under `0x030e0000` guards —
copy them verbatim and adjust indentation/style for the standalone codebase.

#### `echion/frame.h` — header includes

```cpp
// Before (3.11–3.13):
#elif PY_VERSION_HEX >= 0x030b0000
#include <internal/pycore_frame.h>

// After (3.14+):
#if PY_VERSION_HEX >= 0x030e0000
#include <internal/pycore_interpframe_structs.h>
#elif PY_VERSION_HEX >= 0x030b0000
#include <internal/pycore_frame.h>
```

`pycore_frame.h` was split in 3.14. `pycore_interpframe_structs.h` now holds
the actual `_PyInterpreterFrame` struct definition.

#### `echion/cpython/tasks.h` — five changes

**1. New includes for 3.14+:**

```cpp
#if PY_VERSION_HEX >= 0x030e0000
#include <cstddef>
#include <internal/pycore_frame.h>
#include <internal/pycore_interpframe.h>
#include <internal/pycore_interpframe_structs.h>
#include <internal/pycore_llist.h>
#include <internal/pycore_stackref.h>
#include <opcode.h>
```

**2. `FutureObj_HEAD` for 3.14 (`>= 0x030e0000`)** — adds three new fields:
`_awaited_by`, `_is_task` (char), `_awaited_by_is_set` (char), and converts
`_log_tb` / `_blocking` to bitfields at the end of the macro.

**3. `TaskObj` for 3.14** — adds `task_node` (embedded `struct llist_node`)
for linked-list storage. In free-threaded builds, adds `task_tid` (uintptr_t):

```cpp
#if PY_VERSION_HEX >= 0x030e0000
typedef struct {
    FutureObj_HEAD(task)
    unsigned task_must_cancel : 1;
    unsigned task_log_destroy_pending : 1;
    int task_num_cancels_requested;
    PyObject *task_fut_waiter;
    PyObject *task_coro;
    PyObject *task_name;
    PyObject *task_context;
    struct llist_node task_node;
#ifdef Py_GIL_DISABLED
    uintptr_t task_tid;
#endif
} TaskObj;
```

**4. `PyGen_yf()` for 3.14** — `stacktop` is **removed**; replaced by
`stackpointer`. New implementation:

- Check `gen->gi_frame_state == FRAME_SUSPENDED_YIELD_FROM`
- Copy `_PyInterpreterFrame` from remote memory
- Copy `PyCodeObject` to get `co_nlocalsplus`
- Compute `stackbase = &frame.localsplus + co_nlocalsplus * sizeof(_PyStackRef)`
- Assert `frame.stackpointer > stackbase` (stack not empty)
- Read `_PyStackRef` at `stackpointer[-1]` via `copy_type`
- Extract `PyObject*` with `BITS_TO_PTR_MASKED(top_ref)` (clears LSB tag;
  see cpython/cpython#123923)

**5. `BITS_TO_PTR_MASKED` macro** — define it if the standalone repo doesn't
already have it:

```cpp
#ifndef BITS_TO_PTR_MASKED
// Clear the LSB tag bit introduced for _PyStackRef in Python 3.14.
#define BITS_TO_PTR_MASKED(ref) \
    reinterpret_cast<PyObject*>((ref).bits & ~(uintptr_t)1)
#endif
```

#### `echion/state.h` — add `pycore_runtime.h` for 3.14

```cpp
#if PY_VERSION_HEX >= 0x030e0000
#include <internal/pycore_runtime.h>
#endif
```

Needed for interpreter-level task-list traversal.

#### `echion/threads.h` — `pycore_tstate.h` + linked-list task methods

```cpp
#if PY_VERSION_HEX >= 0x030e0000
#include <internal/pycore_tstate.h>
#endif
```

Add three private methods to `ThreadInfo` under `#if PY_VERSION_HEX >= 0x030e0000`:

```cpp
[[nodiscard]] Result<void> get_tasks_from_thread_linked_list(
    EchionSampler&, std::vector<TaskInfo::Ptr>&);
[[nodiscard]] Result<void> get_tasks_from_interpreter_linked_list(
    EchionSampler&, PyThreadState*, std::vector<TaskInfo::Ptr>&);
[[nodiscard]] Result<void> get_tasks_from_linked_list(
    EchionSampler&, uintptr_t head_addr, std::vector<TaskInfo::Ptr>&);
```

Also store `tstate_addr` on `ThreadInfo` so the linked-list traversal can
read `asyncio_tasks_head` from the remote `PyThreadState`.

#### `src/echion/threads.cc` — linked-list task enumeration

Replace the 3.13 approach (Python-level `_scheduled_tasks` / `_eager_tasks`
sets) with linked-list traversal for 3.14+:

```
#if PY_VERSION_HEX >= 0x030e0000
    // Tasks live in per-thread or per-interpreter llist_node chains.
    // Walk asyncio_tasks_head in PyThreadState (thread-local tasks),
    // then walk the interpreter-level list for tasks not pinned to a thread.
#else
    // Pre-3.14: read asyncio._scheduled_tasks / asyncio._eager_tasks from Python.
#endif
```

Refer to the dd-trace-py vendor copy
(`stack/src/echion/threads.cc`) for the full implementation.

#### `src/echion/stack_chunk.cc` — new include for 3.14

```cpp
#if PY_VERSION_HEX >= 0x030e0000
#include <internal/pycore_interpframe_structs.h>
#endif
```

#### `PyThreadState::thread_id` field rename

In 3.14, `tstate->thread_id` moved to `tstate->base.thread_id`:

```cpp
#if PY_VERSION_HEX >= 0x030e0000
    auto tid = static_cast<PyThreadState*>(tstate)->base.thread_id;
#else
    auto tid = tstate->thread_id;
#endif
```

### Step 3: Update build and CI

- `pyproject.toml` / `setup.py`: add `Programming Language :: Python :: 3.14`
  classifier; un-gate any `python_requires` upper bound at 3.13.
- `.github/workflows/build_release.yml`: add `cp314-*` to the `cibuildwheel`
  matrix; bump `cibuildwheel` to v3+ (required for 3.14 wheels).
- `.github/workflows/tests.yml`: add Python 3.14 (and optionally 3.14t
  free-threaded) to the test matrix; bump `setup-python` to v6.
- `austin-python` dependency: bump to a version that supports 3.14.

### Step 4: Validate

```bash
# Build against CPython 3.14 (e.g. via pyenv or deadsnakes):
pyenv install 3.14.0
pyenv local 3.14.0
pip install -e ".[dev]"

# Run echion's own test suite:
pytest tests/
```

Check that stack samples, asyncio task enumeration, and generator/coroutine
unwinding all produce correct output. Compare against 3.13 output for
regression.

---

## Phase 2 — Port to 3.15

### Step 1: Diff CPython 3.14 → 3.15

Same set of headers as Phase 1, but between `v3.14.x` and `v3.15.x` (or
`main` near RC):

```bash
git diff v3.14.0 v3.15.0 -- \
  Include/internal/pycore_interpframe_structs.h \
  Include/internal/pycore_frame.h \
  Include/internal/pycore_interpframe.h \
  Include/internal/pycore_stackref.h \
  Include/internal/pycore_llist.h \
  Include/internal/pycore_tstate.h \
  Include/internal/pycore_runtime.h \
  Include/cpython/genobject.h \
  Modules/_asynciomodule.c
```

Known changes as of CPython 3.15 (March 2026):

| Area | Change | Impact |
|------|--------|--------|
| `frame.f_locals` | PEP 667: now a write-through `FrameLocalsProxyType` | Frame locals introspection pattern may differ |
| Free-threaded allocator | `mimalloc` default for `PyMem_RawMalloc()` in nogil builds | Not direct echion impact; relevant for memalloc |
| `sys.abi_info` | New ABI introspection namespace | Diagnostic only |
| C API removals | Section exists in docs; full list needs source diff | Run the diff above |

**The 3.15 picture is incomplete** — the What's New C API section was not
fully published at the time of writing (March 2026). Always do the source diff
before writing any code.

### Step 2: Apply changes under `0x030f0000` guards

Only add a new `#if PY_VERSION_HEX >= 0x030f0000` branch where layout or
behavior **actually diverges** from 3.14. If 3.15 is compatible with the 3.14
implementation, reuse the existing `>= 0x030e0000` branch.

### Step 3: Update build and CI

Same as Phase 1 Step 3, targeting `cp315-*` and Python 3.15.

### Step 4: Validate

Same as Phase 1 Step 4, targeting CPython 3.15-dev or RC.

---

## Free-threaded builds (`Py_GIL_DISABLED`)

Starting with 3.13t, CPython ships a free-threaded variant. Echion guards are:

- `TaskObj::task_tid` — only present `#ifdef Py_GIL_DISABLED` (3.14+).
- On **Windows** (3.14+), `Py_GIL_DISABLED` must be passed explicitly by the
  build backend; it is no longer inferred from the compiler.
- `cibuildwheel`: add `CIBW_FREE_THREADED_SUPPORT = true` or equivalent to
  build `cp314t-*` wheels.

Where struct layout diverges for nogil builds, use `#ifdef Py_GIL_DISABLED`
inside the existing version guard — do not introduce a separate version ladder.

---

## PR checklist (upstream echion)

- [ ] Title: `feat: support Python 3.X`
- [ ] Body references the CPython issues / commits that drove each struct
  change (e.g. `python/cpython#123923` for the `_PyStackRef` tagged pointer).
- [ ] Body links to the corresponding dd-trace-py PR for context.
- [ ] CI matrix green on 3.X (GIL and free-threaded if applicable).
- [ ] `CHANGELOG` / release notes updated per echion conventions.
- [ ] No `FIXME` or `NOTE THAT THIS HAS NOT BEEN TESTED` left in the diff.
