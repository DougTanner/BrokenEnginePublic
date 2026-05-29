# Thread-Safety: Racy Shared `static` char Buffers on the Log / Diagnostic / Fault Path

## Context

Three sites in `common` share the same anti-pattern: a function formats into (or returns a pointer/`string_view` over) a **function-local `static` char buffer** that is process-wide and **unsynchronized across threads**. Because the buffer is a single shared object, two threads using the function concurrently write into the same array with no mutex, producing torn/garbled output, and a returned view can be overwritten mid-use by another caller. The C++11 static-init guard only protects the *first* construction — it provides no protection for the concurrent *writes* that follow.

The three sites:

1. **Log** (`Log.h:82-83`) — when `gpThreadLocal == nullptr` the `LOG` path falls back to `static char spcLogBuffer[kiLogBufferSize] {}`. The `std::format_to` fill (`Log.h:93`) happens *before* any lock; the `gLogMutex` is taken only later inside `LogWrite` (`Log.cpp:82`), so it covers emission, not formatting. Two ThreadLocal-less threads (early startup, OS/driver/COM/gamepad threads) racing here produce interleaved/garbage log lines.

2. **StackWalker** (`StackWalker.h:28`) — `LogStackWalker::OnCallstackEntry` calls `LOG(...)` on the crash/fault path. The process-wide vectored exception handler fires on *any* faulting thread, including ThreadLocal-less foreign threads, so this drives the same shared `spcLogBuffer` while other live threads may be logging normally — racing/garbling the very crash log meant to diagnose the fault. This is the **fault path**, which must stay heap-allocation-free.

3. **WindowsUtils** (`WindowsUtils.cpp:10`, `:17`) — `LastErrorString()` and `HresultToString()` each return a `std::string_view` over a function-local `static char spcReturn[MAX_PATH]` (verified symbol name). Concurrent callers race the buffer, and a returned view is silently overwritten by the next call (even single-threaded re-entry). Each function has its *own* static, so cross-function aliasing is not an issue; the intra-function multithread/re-entry race is.

The fix family is uniform: route formatting into per-thread storage instead of a shared static. Two flavors apply depending on path:
- **Normal paths** (Log fallback, WindowsUtils helpers): write into `gpThreadLocal->mWorkbuffer` where a ThreadLocal exists, else a `static thread_local` fixed buffer — gives each thread private scratch at zero shared-state cost.
- **Fault path** (StackWalker): must avoid heap allocation entirely (crash may be `STATUS_HEAP_CORRUPTION` / `EXCEPTION_STACK_OVERFLOW`), so a `static thread_local` fixed buffer or a caller-supplied stack buffer — never a heap/arena-grow path.

## Design

### Site 1 — Log fallback buffer (Quick Win)
**File/symbol:** `Common/Log/Log.h:82` — `static char spcLogBuffer[kiLogBufferSize] {};` inside the `Log()` template.

**Change:** Make the fallback per-thread:
```cpp
static thread_local char spcLogBuffer[kiLogBufferSize] {};
```
**Buffer strategy:** `static thread_local` fixed buffer. Each ThreadLocal-less thread gets private scratch, removing the cross-thread race at zero hot-path cost. The `gpThreadLocal != nullptr ? gpThreadLocal->mpLogBuffer : spcLogBuffer` selection at `Log.h:83` is unchanged — only the storage class of the fallback changes.

**Rationale for `thread_local` over `mWorkbuffer`:** this branch is taken precisely *because* `gpThreadLocal == nullptr`, so the Workbuffer arena is unavailable; a fixed `thread_local` is the only correct option. `thread_local` is also safe at the earliest startup on MSVC (zero-init, no dynamic init for a POD array), so there is no TLS-before-init hazard.

**Effort:** 1 (one-line storage-class change).

### Site 2 — StackWalker fault-path emit (Small)
**File/symbol:** `Common/StackWalker.h:28` — `LOG(kDefault, kError, "{} | {} | {}", rEntry.name, rEntry.lineNumber, rEntry.lineFileName);` in `LogStackWalker::OnCallstackEntry`.

**Change:** Once Site 1 makes the `LOG` fallback `thread_local`, the `OnCallstackEntry` `LOG` is automatically race-free on ThreadLocal-less faulting threads — it no longer touches a shared static. No code change is required in `StackWalker.h` for the race itself; Site 1 fixes the shared mechanism that this site exercises.

**Buffer strategy:** inherits the `static thread_local` fixed buffer from Site 1 — fault-path-appropriate (no heap allocation: `thread_local` POD array is statically allocated, `std::format_to` writes in place). The `OfstreamStackWalker::OnCallstackEntry` path (`StackWalker.h:64`) writes directly to `*mpOfstream` and never used the shared static, so it is already race-free with respect to *this* theme (its own DbgHelp-serialization concern is out of scope — see below).

**Action item:** Add a one-line comment at `StackWalker.h:28` noting the fault-path emit relies on the `thread_local` Log fallback for race-freedom on foreign threads, so a future change to Site 1 does not silently reintroduce the race.

**Effort:** 1 (comment only; correctness delivered by Site 1).

### Site 3 — WindowsUtils error-string helpers (Small)
**File/symbols:** `Common/WindowsUtils.cpp:6-11` (`LastErrorString`), `:13-18` (`HresultToString`); declarations `Common/WindowsUtils.h:6-11`, `:13-19`.

**Change:** Replace the function-local `static char spcReturn[MAX_PATH]` + returned `std::string_view` with a returned **owned `std::string`**. `FormatMessage` writes into a local stack buffer; construct and return a `std::string` from `(buffer, uiSize)`:
```cpp
std::string LastErrorString()
{
    char acReturn[MAX_PATH] {};
    DWORD uiSize = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), acReturn, static_cast<DWORD>(std::size(acReturn)) - 1, nullptr);
    return std::string(acReturn, uiSize);
}
```
…and the analogous change for `HresultToString` (same shape, `static_cast<DWORD>(hresult)` as the message id, `0` for the language id, matching the existing `:17` arguments).

**Buffer strategy:** local stack buffer to receive `FormatMessage`, return an owned `std::string`. These helpers are tools/startup callers (DataPacker, `Engine/Source/Main.cpp`), **not** the per-frame main loop, so a `std::string` return is not allocation-tracked and is the simplest correct fix (matches the codebase preference for returned values). Both verified call sites (`Main.cpp:105`, `Main.cpp:111`) already copy/consume immediately, so the return-type change from `string_view` to `string` is source-compatible there.

**Header update:** delete the "NOT THREAD-SAFE … static buffer … copy the string if needed" warnings at `WindowsUtils.h:9-10` and `:17-18`; update the `Returns:` lines from `std::string_view` to `std::string`; change the `Thread-safety:` lines to "Thread-safe — returns an owned string."

**Effort:** 2 (two functions + header doc + return-type change; verify call sites compile).

## Critical files
- `Common/Log/Log.h` — Site 1 (`spcLogBuffer` at line 82). Primary fix; also the shared mechanism Site 2 depends on.
- `Common/Log/Log.cpp` — read-only context (confirms `gLogMutex` covers emission only, `LogWrite` at line 80); no change expected.
- `Common/StackWalker.h` — Site 2 (`OnCallstackEntry` LOG at line 28); comment-only change.
- `Common/WindowsUtils.h` — Site 3 declarations + doc comments (lines 6-19).
- `Common/WindowsUtils.cpp` — Site 3 implementations (`LastErrorString` 6-11, `HresultToString` 13-18).

## Out of scope
- **Log's unbounded `std::format_to` overrun** into the fixed `kiLogBufferSize` buffer (Log.md C1) — separate bug owned by the per-file `Log.md` plan. This theme owns only the shared-static-buffer **race**, not the bounds problem. (Note: that overrun affects both the `thread_local` fallback and the per-thread `mpLogBuffer`; it is orthogonal to who owns the buffer.)
- **Log ring-buffer write-vs-dump race**, `LogPrefix` thread-id digit overrun, `strlen` recompute, `LogIndent` null-deref (Log.md H2/H3/H4/M3) — per-file `Log.md` plan.
- **StackWalker DbgHelp serialization** (CrashReport.cpp drives DbgHelp unlocked), `lineNumber > 0` frame filtering, `OnDbgHelpErr` silent swallow, `LogStackWalker`/`OfstreamStackWalker` DRY duplication, fault-path `malloc` / stack-overflow re-fault (StackWalker.md H2/M1/M2/M3/L4/L5) — per-file `StackWalker.md` plan and follow-ups; not shared-static-buffer concerns.
- **WindowsUtils HANDLE/attribute-list leak on throw** (`RunExecutable`), **`FileTimeString` `FILETIME` reinterpret_cast**, ignored `CreateProcessW`/`WaitForSingleObject` returns, CRLF trailing trim, `HresultToString` naming/DRY (WindowsUtils.md H1/H2/M1/M2/M4/M5) — per-file `WindowsUtils.md` plan. This theme touches only the `static`-buffer race in `LastErrorString`/`HresultToString`.

## Acceptance criteria
- No shared mutable `static` char buffer remains on the Log fallback path, the StackWalker fault path, or the WindowsUtils error-string path.
- Two ThreadLocal-less threads logging concurrently no longer interleave/garble into a shared array (each has private `thread_local` scratch).
- `LastErrorString()` / `HresultToString()` return owned strings; no returned reference/view can be overwritten by a concurrent or subsequent call.
- The crash/fault emit path remains **allocation-free**: StackWalker's `OnCallstackEntry` formats into a statically-allocated `thread_local` buffer, with no heap touch (safe under `STATUS_HEAP_CORRUPTION` / `EXCEPTION_STACK_OVERFLOW`).
- All call sites of the changed WindowsUtils helpers compile against the new `std::string` return.

## Notes
- "Assume params valid — no defensive input validation" applies, but these are **thread-safety/correctness** fixes (shared-state races), which the directive explicitly keeps.
- KISS/YAGNI: prefer the minimal storage-class change (`static` → `static thread_local`) for the Log fallback and an owned `std::string` for WindowsUtils over heavier mechanisms (mutex around formatting, arena plumbing into the no-ThreadLocal path).
- The Log fix is the linchpin: it simultaneously resolves Site 1 and the *mechanism* behind Site 2. Sequence Site 1 first.
- Does not contradict documented design: `Common/CLAUDE.md` Logging section describes the lockless ring buffers and "only the emission step holds a mutex" — the `thread_local` fallback preserves the zero-shared-state, lock-free-formatting property rather than adding a lock.
