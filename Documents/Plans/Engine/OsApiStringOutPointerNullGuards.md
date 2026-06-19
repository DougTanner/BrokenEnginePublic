# OS/COM String Out-Pointer Null Guards (boot + crash paths)

## Context

A sibling-pattern sweep surfaced a small bug family: an OS/COM string out-pointer is consumed
(assigned, copied, or dereferenced) **without checking the API's result code or null-guarding the
out-pointer**, so a real (if rare) OS failure yields a null pointer that flows straight into UB. These
are trust-boundary inputs — the OS/COM call result is opaque to our code — so per project policy
(root [CLAUDE.md](../../../CLAUDE.md)) they warrant graceful guards (check result, null-guard, log +
fall back), not defensive validation between our own functions. This is the same discipline as the
just-landed Audio `GetId` guard.

Three verified sites (all paths run at boot or during crash handling — no determinism/CRC/`.pack`/wire
exposure):

1. **`FileManager` ctor — `Engine/Source/File/FileManager.cpp:23-26`.**
   `SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &pWideChar)` (`:24`) — HRESULT
   discarded — then `mAppDataDirectory = pWideChar;` (`:25`) assigns the raw `PWSTR` to a
   `std::filesystem::path`. Per MSDN, on failure `SHGetKnownFolderPath` returns a non-`S_OK` HRESULT and
   sets `pWideChar` to null; constructing a path from a null pointer is UB. `CoTaskMemFree(pWideChar)`
   (`:26`) is already present and correct (documented no-op on null — keep it).

2. **`HandleException` — `Engine/Source/CrashReport.cpp:27-30`.** Same shape: `SHGetKnownFolderPath(...,
   &pWideChar)` (`:28`, HRESULT discarded) then `wcscpy_s(spcPath, std::size(spcPath), pWideChar)`
   (`:29`). A null `pWideChar` source trips the CRT invalid-parameter handler / UB. `CoTaskMemFree`
   (`:30`) is already present (keep). **Critical constraint:** this handler is reachable from the
   `SIGABRT` path during heap corruption and **must not re-enter the allocator** — it uses fixed `wchar`
   buffers (`spcPath` is `static wchar_t[MAX_PATH + 1]`), never `std::string`/`std::wstring`. Confirmed
   against `Engine/Source/CLAUDE.md` "Crash Reporting". Any fix here stays allocator-free: leave
   `spcPath` in a safe defined state and continue.

3. **`ReadDxDiag` BSTR read — `Engine/Source/CrashReport.cpp:124,129`** (RELATED, secondary).
   `pChild->GetProp(pcPropName, &variant)` (`:124`) — HRESULT discarded — guarded by `variant.vt ==
   VT_BSTR` (`:125`), then `common::ToString(variant.bstrVal)` (`:129`). `common::ToString` takes a
   `std::wstring_view` (`Common/StringUtils.h:28`); a null `BSTR` constructs `std::wstring_view(nullptr)`,
   which is UB **in the view constructor** (`char_traits::length(nullptr)`) — *before* `ToString`'s
   existing `.empty()` early-out can run. `VT_BSTR` with a null `bstrVal` is technically legal (some COM
   producers represent an empty BSTR as null), so the existing type-check does not by itself make the
   pointer non-null. This site is inside `ReadDxDiag`'s existing try/catch (`CHECK_HRESULT` throws are
   caught at `:136`), so it is lower-stakes than #1/#2, and `ReadDxDiag` runs on a normal worker thread
   with its own `ThreadLocal` — allocator-free is **not** required here (`sDxDiag` is a `std::string`).

## Design

Each site gets its own minimal guard (no shared helper — YAGNI; the three contexts differ in
allocation/return constraints):

1. **`FileManager` ctor (`FileManager.cpp:23-26`).** Capture the HRESULT, and on
   `FAILED(hresult) || pWideChar == nullptr`: `LOG(kLoading, kError, ...)` and fall back to a defined
   default. Minimal recovery: skip the `mAppDataDirectory = pWideChar;` assignment so the path stays
   default-constructed (empty); the subsequent `mAppDataDirectory.append(game::kGameName)` +
   `create_directory` then operate on a relative path beside the working directory rather than
   dereferencing null. Boot path runs before the main loop, so logging/`std::filesystem` allocations are
   free here. Keep `CoTaskMemFree(pWideChar)` unconditional (handles the null no-op).

2. **`HandleException` (`CrashReport.cpp:27-30`).** Capture the HRESULT; on failure/null, do **not**
   `wcscpy_s` from the null source. Leave `spcPath` in a safe defined state — it is already
   zero-initialized (`static wchar_t spcPath[MAX_PATH + 1] {}`), so the simplest allocator-free recovery
   is to skip the copy (and, to keep the report writable rather than landing at a bare drive root, the
   existing `SHGetSpecialFolderPathW` desktop branch at `:23` is a reasonable fallback target — but the
   minimal change is just to guard the copy and continue; the later `wcscat_s` calls then build the path
   under the empty/desktop base). No `std::string`/heap; `LOG` is acceptable (the log buffer is a fixed
   ring), but keep the guard body allocator-free to honor the crash-path contract. Keep `CoTaskMemFree`
   unconditional.

3. **`ReadDxDiag` (`CrashReport.cpp:124,129`).** Add a `variant.bstrVal != nullptr` guard alongside the
   existing `variant.vt == VT_BSTR` check before calling `common::ToString(variant.bstrVal)` (i.e. widen
   the `:125` condition or short-circuit the body). Optionally capture the `GetProp` HRESULT and skip the
   property on failure, but the null-`bstrVal` guard is the load-bearing fix (the existing `.vt` check
   already discriminates non-BSTR/`VT_EMPTY` results). `VariantClear(&variant)` (`:132`) stays
   unconditional. This is the secondary item — keep it minimal.

## Critical files

- `Engine/Source/File/FileManager.cpp` — `FileManager::FileManager()` ctor, `SHGetKnownFolderPath` /
  `mAppDataDirectory` assignment (`:23-26`).
- `Engine/Source/CrashReport.cpp` — `HandleException` `SHGetKnownFolderPath` / `wcscpy_s` (`:27-30`);
  `ReadDxDiag` `GetProp` / `ToString(variant.bstrVal)` (`:124-129`).
- `Common/StringUtils.h:28` / `Common/StringUtils.cpp:6-15` — `common::ToString(std::wstring_view)`
  reference only (the empty-guard runs *after* view construction, so it does not cover null
  `bstrVal`); not modified.

## Out of scope

- **No shared helper / refactor** of the three sites into a common guard function (YAGNI — they differ in
  allocation and return constraints).
- The already-landed Audio `AudioManager.cpp` `GetId` guards — not touched.
- The `CHECK_HRESULT`-wrapped COM calls in `ReadDxDiag` (`:85-113`) — already result-checked; not
  touched.
- The `DataPacker/Source/FileManager.cpp` file — a separate offline-tool file, not this engine one.
- Any change to `common::ToString`'s signature or body, or to `SHGetSpecialFolderPathW` /
  `GetTempPathW` / `GetModuleFileNameW` call sites (out of this family's scope; those return success
  flags into fixed buffers, not allocate-and-out-pointer).
- Converting these guards into `ASSERT(p != nullptr)` — explicitly rejected; the fixes are graceful
  recovery, not asserts.

## Acceptance criteria

- All three sites check the API result and/or null-guard the out-pointer before consuming it; on
  failure each logs (`kError`) and falls back to a defined-safe state instead of dereferencing/copying
  null.
- `CoTaskMemFree(pWideChar)` remains unconditional at both `SHGetKnownFolderPath` sites.
- `HandleException`'s guard introduces no `std::string`/`std::wstring`/heap allocation (crash-path
  allocator-free contract preserved); `spcPath` is never copied-from-null.
- Client and server builds compile clean (both files are in both vcxprojs — see Notes).

## Notes

**Invariant exposure:**

- **Client/server:** Both files are compiled into **client and server** —
  `BrokenEngineSandbox.vcxproj` (`:507` FileManager, `:596` CrashReport) and
  `BrokenEngineSandboxServer.vcxproj` (`:433` FileManager, `:457` CrashReport). No `#ifdef
  BT_CLIENT`/`BT_SERVER` wrapping is involved; the edited code is shared. Build/verify in **both**
  configurations.
- **Determinism/CRC/`kiVersion`/`.pack`/wire:** None. These are boot (`FileManager` ctor) and
  crash-handling paths — they touch no simulation state, no CRC, no serialized layout, no network. No
  `kiVersion` bump.
- **Allocation tracking:** The `FileManager` ctor (#1) runs in `wWinMain` **before** the main loop, so
  the allocation tracker is not yet armed — logging and `std::filesystem` allocations in the guard are
  free. The crash path (#2, `HandleException`) is reachable from `SIGABRT` during heap corruption and
  **must stay allocator-free** (fixed `wchar` buffers only) — the guard body must not introduce
  `std::string`/heap. `ReadDxDiag` (#3) runs on a normal worker thread with the tracker not armed there
  and already builds a `std::string` (`sDxDiag`), so its guard has no allocation constraint.
- No single open architectural decision requires `/external-grill-plan` adjudication — the recovery
  shape for each site is determined by its allocation/return constraints above. The one judgment call
  (whether site #3's existing `VT_BSTR` type-check already makes `bstrVal` non-null) is resolved in
  Context: it does not (null is a legal empty-BSTR encoding), so the guard is warranted.
