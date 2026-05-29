# WindowsUtils Resource-Safety and Conversion Fixes

## Context

`Common/WindowsUtils.h` / `Common/WindowsUtils.cpp` are thin Win32 wrappers used
almost exclusively by tools/startup (DataPacker bake tools, `Engine/Source/Main.cpp`),
not the per-frame main loop. A validation pass against the source confirmed three
genuine defects that are NOT defensive-input-validation and NOT owned by the
shared-static-buffer thread-safety theme:

1. `RunExecutable` leaks kernel HANDLEs and a Win32 attribute-list allocation on
   any mid-function `VERIFY_SUCCESS` throw (every guarded call between the pipe
   creation and the cleanup block can unwind past the manual `CloseHandle` /
   `DeleteProcThreadAttributeList` calls).
2. `FileTimeString` `reinterpret_cast`s a `std::filesystem::file_time_type*` to a
   `FILETIME*` — undefined behavior; the two types are not guaranteed
   layout/epoch compatible (works only by MSVC-STL coincidence today).
3. The system-message strings returned by `LastErrorString` / `HresultToString`
   carry the trailing `\r\n` (and often a `.`) that `FormatMessage` appends, and
   the returned `string_view` length includes it — producing embedded CRLF /
   blank lines in single-line log output.

`VERIFY_SUCCESS(x)` expands to `ASSERT(x)` which on failure logs, `DEBUG_BREAK()`s,
then `throw`s — so it unwinds the stack and skips any manual cleanup that sits
below it. That is the mechanism behind finding 1.

## Design

### 1. RAII the HANDLE and attribute-list lifetimes in `RunExecutable` (`WindowsUtils.cpp:112-178`) — effort: M

The four pipe handles (`hStdInPipeRead/Write`, `hStdOutPipeRead/Write`,
`WindowsUtils.cpp:121-126`), the process/thread handles populated by
`CreateProcessW` (`WindowsUtils.cpp:152-153`), and the initialized
`PROC_THREAD_ATTRIBUTE_LIST` (`WindowsUtils.cpp:140`) are all acquired before, or
between, throwing `VERIFY_SUCCESS` calls, yet only cleaned up by the manual
`CloseHandle` block (`WindowsUtils.cpp:156-157, 172-175`) and
`DeleteProcThreadAttributeList` (`WindowsUtils.cpp:154`) at the bottom. A throw at
`CreatePipe` #2 (126), `SetHandleInformation` (128-129),
`UpdateProcThreadAttribute` (142), or `CreateProcessW` (153) leaks them.

- Wrap each raw `HANDLE` in a small RAII holder so destruction closes it on the
  throw path. Options: a `common::ScopedLambda` per handle (see
  `Common/CLAUDE.md` "ScopedLambda"), or a single reusable
  `unique_ptr<void, HandleDeleter>`-style wrapper. Pick the simplest (KISS) — a
  local move-only handle wrapper that calls `CloseHandle` in its dtor when the
  value is non-null/non-`INVALID_HANDLE_VALUE`.
- Wrap the attribute list in a scope guard (`ScopedLambda`) that calls
  `DeleteProcThreadAttributeList` after `InitializeProcThreadAttributeList`
  succeeds (`WindowsUtils.cpp:140`). The backing `attributeListBuffer`
  (`make_unique`) already frees its memory; the guard only needs to run the
  Win32 teardown.
- The existing manual `CloseHandle` / `DeleteProcThreadAttributeList` /
  early-close-of-child-ends (156-157) logic must be preserved as the normal-path
  behavior — `hStdOutPipeWrite` / `hStdInPipeRead` are intentionally closed early
  (before the read loop) so `ReadFile` sees EOF; ensure the RAII design still
  closes those two at that point, not only at scope exit.

No new defensive validation — the holders just make the existing cleanup
exception-safe.

### 2. Replace the `FILETIME` cast in `FileTimeString` (`WindowsUtils.cpp:97`) — effort: S

`reinterpret_cast<const FILETIME*>(&rFileTime)` aliases a
`std::chrono::time_point` through `FILETIME`. Convert through a supported path
instead:

- Preferred: `std::chrono::clock_cast` from `file_time_type::clock` to
  `std::chrono::system_clock` (or `utc_clock`), then build the `FILETIME` from
  the resulting duration since the 1601 epoch; or compute the 100 ns tick count
  from `rFileTime.time_since_epoch()` explicitly and store into
  `FILETIME.{dwLowDateTime,dwHighDateTime}` before `FileTimeToSystemTime`.
- At minimum, if the explicit conversion is deferred, add a `static_assert`
  pinning the assumed MSVC `_File_time_clock` representation plus a comment that
  the cast is MSVC-specific — but the proper conversion is the intended fix.

### 3. Trim trailing `\r\n.` from the formatted message strings (`WindowsUtils.cpp:10, 17`) — effort: S

`FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, ...)` returns a length that includes
the trailing `\r\n` (and frequently a period). Both `LastErrorString` and
`HresultToString` build the returned `std::string_view` from that raw length.

- After the `FormatMessage` call, decrement the length while the last character
  is one of `'\r'`, `'\n'`, `'.'`, or `' '`, then construct the `string_view`
  with the trimmed length.
- Keep the existing buffer/encoding behavior otherwise. (Do NOT change the
  static-buffer storage model here — see Notes.)

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\WindowsUtils.cpp` — all three fixes.
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\WindowsUtils.h` — touch only if a new
  RAII handle-wrapper type needs declaring (prefer keeping it `.cpp`-local).

## Out of scope

- The shared-static-buffer thread-safety / dangling-view footgun in
  `LastErrorString` / `HresultToString` — owned by
  `ThreadSafety_SharedStaticBuffers.md` (see Notes). Do not change the
  `static char[MAX_PATH]` storage model in this plan.
- `LaunchExecutable` swallowing `CreateProcessW` failure and `RunExecutable` /
  `RunExecutableInNewConsole` not checking `WaitForSingleObject` /
  `GetExitCodeProcess` — these are defensive checks for cases that "won't fail
  under valid params," excluded by the assume-valid-params directive.
- Renaming `HresultToString` / collapsing it into `LastErrorString` (API/DRY
  polish, not a bug).
- Dynamic `GetProcAddress(GetLogicalProcessorInformation)` legacy ceremony and
  the >64-logical-processor multi-group under-count in `HardwareCoreCount`
  (Low; edge hardware; safe fallback exists).
- Exit-code unsigned→signed widening, `BOOL`/`FALSE` idiom, `ExecutableResult`
  empty-output ergonomics, `_Analysis_assume_` note — Low cosmetic/style.
- Do NOT add a Verification section. Do NOT add unit tests.

## Acceptance criteria

- A `VERIFY_SUCCESS` throw at any guarded call in `RunExecutable` releases every
  already-acquired HANDLE and runs `DeleteProcThreadAttributeList` on unwind (no
  leaked kernel handles or attribute list on the throw path), while the
  normal-path early close of the child pipe ends before the read loop is
  preserved.
- `FileTimeString` no longer `reinterpret_cast`s `file_time_type*` to
  `FILETIME*`; the conversion goes through a documented/standard path and still
  produces the same `yyyy-MM-dd` / `h:mm tt` output for current timestamps.
- `LastErrorString` and `HresultToString` return messages with no trailing
  `\r`, `\n`, `.`, or space, so logged output stays on a single line.
- DataPacker and the affected projects build clean (LNK errors ignorable per
  build notes).

## Notes

- The shared-static-buffer thread-safety / re-entrancy issue for
  `LastErrorString` / `HresultToString` (function-local `static char[MAX_PATH]`
  returned as a `string_view`) is intentionally NOT addressed here — it is owned
  by the theme plan `ThreadSafety_SharedStaticBuffers.md`. The trailing-whitespace
  trim in fix 3 operates within the existing static-buffer model and does not
  pre-empt that theme.
