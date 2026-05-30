# Wrapper::GetIndex Fail-Loud on Out-of-Set Value

## Context

`Wrapper::GetIndex() const` (`Engine/Source/Ui/WrapperBase.h:172-187`) linear-scans `mAllowed` for the current value and returns its index; on no match it does `DEBUG_BREAK(); return 0;`. Per `Engine/Source/Ui/CLAUDE.md`, the discrete-enum `Wrapper` flavor "`DEBUG_BREAK`s on out-of-set values" — i.e. reaching the fall-through is documented as a bug. But `DEBUG_BREAK()` is a no-op without a debugger attached (`Common/ErrorUtils.h:11`), so in a release build an out-of-set value silently returns index `0` (the first allowed value), masking the bug.

`GetIndex()` is not just a query — it is also invoked from `Set()` (`WrapperBase.h:154`) on every discrete-enum set, so the fall-through sits on the settings-mutation path, not only on read.

This is a sibling of the fail-soft-`default:` hardening pass that converted four sites (`common::SizeInBytes`, `FileManager::GetFilePath`, `ExportScene::ToVkFilter`/`ToVkSamplerAddressMode`) from `DEBUG_BREAK(); return <plausible value>;` to `ASSERT(false); return <value>;`. `GetIndex` was surfaced by that session's affected-locations sweep but deferred here because it lives on a **runtime UI/settings path** with a larger blast radius than the offline DataPacker mappers — promoting it to a throw needs a reachability check first.

## Design

1. **Reachability audit (do this first; it decides the rest).** Determine whether a discrete-enum `Wrapper` can legitimately hold a value not in `mAllowed` during a normal run. Key inflows to check:
   - `Set()` / `SetIndex()` callers on discrete-enum wrappers (`WrapperBase.h:149-155, 189-192`).
   - The settings persistence/load path (`Projects/BrokenEngineSandbox/Source/Ui/ClientSettings.cpp` and any engine-side load) — can a stale or older-version persisted value flow into a discrete-enum `Wrapper::Set()`?
   - Whether the float-snap-step path or any clamp can produce a value off the `mAllowed` grid.
2. **If bug-only** (matches the four already-hardened sites — the fall-through is unreachable on valid input): replace `DEBUG_BREAK(); return 0;` with `ASSERT(false); return 0;`. Keep the trailing `return` — `common::Assert` is not `[[noreturn]]`, so it is required to avoid C4715; it is unreachable at runtime because `Assert(false, ...)` throws unconditionally. This makes an out-of-set value fail loud in release instead of silently snapping to index 0.
3. **If legitimately reachable** (e.g. a stale persisted setting): do **not** throw. Clamp/snap the incoming value into `mAllowed` at the `Set`/load boundary (or formally document the index-0 soft-fall as intentional). Decide between these in the grill.

## Critical files

- `Engine/Source/Ui/WrapperBase.h` — `GetIndex()` fall-through `:185-186`; `Set()` (calls `GetIndex`) `:149-155`; `SetIndex()` `:189-192`.
- `Projects/BrokenEngineSandbox/Source/Ui/ClientSettings.cpp` — settings load/persist callers (audit target for step 1; likely read-only).

## Out of scope

- The `float` and `bool` `Wrapper` flavors — `GetIndex` is meaningful only for the discrete-enum flavor.
- `Engine/Source/Debug/EnumToString.h:60-61` (`DEBUG_BREAK(); return "...UNKNOWN_VK_ENUM";`) — **assessed working-as-intended and explicitly excluded.** A diagnostic enum→string converter must never throw while formatting a debug log just because the Vulkan SDK added an enumerator; the `"UNKNOWN_VK_ENUM"` fallback is the correct behavior, not a fail-soft bug. Do not convert it to `ASSERT(false)`.
- The four sites already hardened in the originating session (`SizeInBytes`, `GetFilePath`, `ToVkFilter`, `ToVkSamplerAddressMode`).
- Any broader `Wrapper` refactor, change-detection rework, or `mAllowed` storage change.
- `WrapperBase.cpp` literal-constant edits owned by `Frame/ScaleEngineToMeters.md` — that plan touches `WrapperBase.cpp` (tunable literals); this plan touches `WrapperBase.h` (`GetIndex` method). Different file, different symbols — no overlap.

## Acceptance criteria

- The reachability question (step 1) is answered with concrete evidence (the `Set`/`SetIndex`/settings-load audit), recorded in the implementation.
- If bug-only: an out-of-set value triggers `ASSERT(false)` in both debug and release; no silent index-0 snap remains; the function still compiles clean (trailing `return` kept).
- If reachable: the value is clamped/snapped at the boundary (or the soft-fall is documented as intentional) so a legitimate stale setting does not spuriously throw on load.
- Client and server compile with no new warnings.

## Notes

- The change itself is one line; the work is the reachability audit and the branch decision, hence Quick Win effort with Moderate risk (it sits on the runtime settings `Set` path — a wrong "bug-only" call would throw on settings load).
- Found by the affected-locations sweep during the fail-soft-`default:` hardening session; both header-resident siblings (`WrapperBase.h`, `EnumToString.h`) were missed by the original content sweep because they live in `.h` files.
