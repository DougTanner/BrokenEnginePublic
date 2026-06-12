# Refactor: Profile Row Flags

## Context

Root CLAUDE.md "Flags over booleans" mandates `common::Flags<EnumType>` instead of multiple `bool` members. The three profile-row structs in `Engine/Source/Profile/ProfileManagerBase.h` now violate it:

- `CpuTimer` carries **two** bools — `bVisible` (visibility-cadence state) and `bSmoothAtStop` (per-timer smoothing latch, added this session alongside the landed `SmoothNowDoubleLatch` fix).
- `CpuCounter` and `GpuTimer` each carry **one** bool — `bVisible`.

`CpuTimer` is the structural trigger (2 bools → the rule applies), but converting only it leaves the two sibling rows inconsistent for the same shared `bVisible` concept. Treat the three together.

This is the struct-**member** companion to `Profile/Refactor_InFunctionCleanups.md`, which separately converts `CpuStop`'s two positional **parameters** (`bSmoothNow`/`bCrossThread`) to `common::Flags<CpuStopFlags>`. Distinct surfaces (member layout vs call-site signature), same Flags-over-bools rule, same files/session. Precedent for filing a struct-member Flags conversion as its own row: `Input/Refactor_InputButtonFlags.md`, `Network/Refactor_ClientStateFlags.md`.

## Design

### Shared enum

Add one namespace-scoped flags enum beside the other profile enums in `ProfileManagerBase.h`, unsigned underlying type (`common::Flags` `static_asserts std::is_unsigned_v` — `Flags.h:10`):

```cpp
enum class ProfileRowFlags : uint8_t
{
	kVisible      = 1 << 0,
	kSmoothAtStop = 1 << 1,
};
```

One shared enum serves all three structs — `CpuCounter`/`GpuTimer` simply never set `kSmoothAtStop`. (Alternative considered: a visibility-only enum for the siblings + a separate two-flag enum for `CpuTimer`. Rejected as more types for no benefit — an unused flag value costs nothing; see `## Notes` for the grill pre-stage.)

### Struct members

Replace the bool members with `common::Flags<ProfileRowFlags> flags {};` on `CpuTimer`, `CpuCounter`, `GpuTimer`. The existing `{.name = ...}` designated-initializer arrays in `ProfileManagerBase.cpp` need no change (the flags member default-constructs empty).

### Readers / writers

Rewrite the member accesses (all in the same Profile session's files):

- **`bSmoothAtStop` write** — `ProfileManagerBase::CpuStop` (`ProfileManagerBase.cpp`, the `if (bSmoothNow)` block): `rCpuTimer.bSmoothAtStop = true` → `rCpuTimer.flags.Set(ProfileRowFlags::kSmoothAtStop)`.
- **`bSmoothAtStop` read** — `ProfileManagerBase::SmoothCpuTimers` (`ProfileManagerBase.cpp`): `if (!rCpuTimer.bSmoothAtStop)` → `if (!(rCpuTimer.flags & ProfileRowFlags::kSmoothAtStop))`.
- **`CpuTimer::bVisible`** — `FormatCpuTimersText` (`ProfileScreens.cpp`): the `bReevaluate` write and the `if (!...bVisible) continue;` read → `flags.Set(kVisible, <predicate>)` / `if (!(flags & kVisible))`.
- **`CpuCounter::bVisible`** — `FormatCpuCountersText` (`ProfileScreens.cpp`): same pattern.
- **`GpuTimer::bVisible`** — `FormatGpuScreen` (`ProfileScreens.cpp`, the GPU-timer-rows block): same pattern.

`Flags::Set(eFlag, bool)` (`Flags.h:39`) takes the predicate directly, so the `bReevaluate ? rCpuTimer.bVisible = <expr>` lines become `flags.Set(ProfileRowFlags::kVisible, <expr>)`.

Note `bSmoothAtStop` is only ever set to `true` (never cleared) — preserve that; do not add a clear.

## Critical files

- `Engine/Source/Profile/ProfileManagerBase.h` — the `ProfileRowFlags` enum + the three struct member swaps.
- `Engine/Source/Profile/ProfileManagerBase.cpp` — `CpuStop` write, `SmoothCpuTimers` read.
- `Engine/Source/Profile/ProfileScreens.cpp` — `FormatCpuTimersText`, `FormatCpuCountersText`, `FormatGpuScreen` visibility read/writes.

## Out of scope

- `CpuStop`'s positional-parameter bools (`bSmoothNow`/`bCrossThread`) → `CpuStopFlags` — owned by `Profile/Refactor_InFunctionCleanups.md` (co-schedule; same session).
- `FormatGpuScreen` decomposition, empty ctor/dtor `= default` — also `Profile/Refactor_InFunctionCleanups.md`.
- Visibility-cadence semantics / the ~2s sticky re-evaluation in `TickVisibilityCadence` — unchanged; this plan only swaps the storage of the cached result.
- `bSmoothAtStop` latch semantics — already landed (`SmoothNowDoubleLatch`); not revisited.
- Server-side guard width / GPU-struct client-gating — `Profile/Architecture_ClientServerGuardScope.md`.

## Notes

- No determinism/CRC/network/`kiVersion` exposure — `common::Flags` is serializable/CRC-hashable but these structs are profiling-display-only, never serialized or CRC'd. Compile-checked in both client and server builds (`CpuCounter`/`CpuTimer` and their readers compile server-side; `GpuTimer`/`FormatGpuScreen` are `BT_CLIENT`-only).
- `bSmoothAtStop` is server-relevant too (`SmoothCpuTimers` runs on the server's GDI display path), so the `CpuTimer` conversion and the `CpuStop`/`SmoothCpuTimers` rewrites must stay build-agnostic.
- Grill decision (pre-staged): one shared `ProfileRowFlags { kVisible, kSmoothAtStop }` enum for all three structs (recommended — fewer types, unused flag is free) vs a visibility-only enum for `CpuCounter`/`GpuTimer` plus a two-flag enum for `CpuTimer`.
