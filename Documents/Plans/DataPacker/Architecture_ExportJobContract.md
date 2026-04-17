# Architecture: Formalize ExportJob Contract

Source: /external-architecture-review on DataPacker/Source/

Goal: `RunExportJobs<T>` duck-types through a static contract (`T::Handles`, `T::kName`, `T::GetVersion()`) that is documented only in `CLAUDE.md`, not expressed in C++. Formalize it, remove the speculative per-type hook, and deduplicate `GetVersion()` bookkeeping.

## Changes

### DataPacker/Source/ExportJobs/ExportJob.h
- Add `static constexpr int64_t Version(int64_t iRaw) { return iRaw + sizeof(common::ChunkHeader); }` on `ExportJob`. Rewrite each subclass `GetVersion()` to `return Version(N);` — eliminates the `+ sizeof(common::ChunkHeader)` repetition across subclasses [~15m]
- Lines 3-8: delete the dead `namespace utils { struct ChunkHeader; }` forward declaration — the real type lives in `common::`, nothing in the file references `utils::ChunkHeader` [~5m]
- Add a C++20 `concept IsExportJob<T>` requiring `T::Handles`, `T::kName`, `T::GetVersion()` static/member accessibility. Apply as a template constraint to `RunExportJobs<T>` so a malformed subclass fails at instantiation with a readable diagnostic rather than deep template errors [~20m]

### DataPacker/Source/Main.cpp
- Line 143: remove the `if constexpr (std::is_same_v<T, ExportTexture>)` per-type header-hook branch. The hook is currently a no-op (`ExportTexture::AddToHeader` at `ExportTexture.cpp:319-321` is an empty function with `/*commented out*/` parameter names). Delete the branch, the declaration at `ExportTexture.h:13`, and the empty definition at `ExportTexture.cpp:319-321` [~10m]

### DataPacker/Source/ExportJobs/ExportJob.cpp
- Lines 38-72: replace hand-written move constructor and move-assignment operator with `= default`. All members are move-friendly (`std::vector`, `std::filesystem::path`, `std::future`, scalars), so defaulted versions behave identically today and stay correct if future members are added [~5m]

## Expected Outcome

- `GetVersion()` arithmetic lives in one place
- `RunExportJobs<T>` no longer closes over `ExportTexture` — adding a new ExportJob does not require touching `Main.cpp`
- Concept constraint turns duck-typing mistakes into readable compile errors
- Dead stub removed

## Verification Notes

- Verified `ExportTexture::AddToHeader` at `ExportTexture.cpp:319-321` is truly an empty-body stub with commented-out parameter names. Collapsed the two-option `AddToHeader` recommendation to a single concrete decision: delete it.
- REMOVED the "`CheckDirty` dual-output issue" bullet. Investigation showed `mbDirty` is persistent object state consumed later by `RunExport()` at `ExportJob.cpp:174` (`if (!mbDirty) ...`), while the return value is used by `Main.cpp:57` to aggregate across jobs (`bDirty |= rpExportJob->CheckDirty(...)`). The two outputs serve distinct callers — not a dual-output bug.
- All remaining line numbers verified. The dead `namespace utils { struct ChunkHeader; }` forward declaration spans lines 3-8 (not just 6-8) since the namespace braces themselves are dead.
