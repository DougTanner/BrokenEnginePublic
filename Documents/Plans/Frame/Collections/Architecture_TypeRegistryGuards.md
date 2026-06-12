# Architecture: Type Registry Index Guards and Threading Contract

## Context

Source: /external-architecture-review on `Engine/Source/Frame/Collections` (non-recursive). Two small hardening gaps in the registry mixins:

1. `TypeRegistry::RegisterType` (`Collection.h:411-415`) and `ControllerTypeRegistry::RegisterControllerType` (`Collection.h:382-387`) compute the new index as `static_cast<uint8_t>(sTypes.size())` with no size check. The 256th registration silently aliases index 0, and index 255 collides with the `kuiInvalidControllerType` sentinel (`Collection.h:298`), which `DestroyExpiredControlled` treats as "no controller" (`Collection.h:752`) — a registered type at index 255 would never animate or expire. Far from current usage; zero-cost to assert.
2. The registry storage (`static inline std::vector` at `Collection.h:380` and `:409`) is mutated by `Register*` and read via `.at()` from parallel frame ticks, safe only because registration happens during single-threaded startup before `Dispatch()` fan-out — a load-bearing convention documented nowhere in the header.

## Design

### Engine/Source/Frame/Collections/Collection.h
- In `RegisterControllerType` (lines 382-387) and `RegisterType` (lines 411-415): `ASSERT(sControllerTypes.size() < kuiInvalidControllerType)` / `ASSERT(sTypes.size() < 0xFF)` before the cast, so the sentinel value and wraparound are unreachable [~5m]
- On `TypeRegistry` / `ControllerTypeRegistry` (lines 374-380, 403-409): one comment documenting the threading contract — registration is startup-only (single-threaded, before `Dispatch()` workers), reads from parallel ticks are safe because the vectors are immutable afterwards [~5m]

## Critical files
- `Engine/Source/Frame/Collections/Collection.h`

## Out of scope
- Synchronizing the registries (unnecessary under the documented startup-only convention)
- Widening the `uint8_t` index type (YAGNI — current registries hold a handful of types)
- The asset-I/O side effects of `RegisterType` (documented behavior, Collections CLAUDE.md)

## Notes
- Assert-plus-comment only; no determinism/CRC/serialization exposure, no behavior change on any reachable path (`ASSERT` throws in all builds, so the guard is real if ever hit).

## Verification Notes (2026-06-11)
- All cites exact: `RegisterControllerType` (`Collection.h:382-387`), `RegisterType` (:411-415 for the assert/cast/push_back), `kuiInvalidControllerType = 0xFF` (:298), sentinel skip in `DestroyExpiredControlled` (:752), `static inline std::vector` storage (:380, :409), `.at()` reads (:391, :442).
- Wraparound/sentinel collision is real: `static_cast<uint8_t>(sTypes.size())` aliases index 0 at the 256th registration, and index 255 collides with both the controller sentinel and the `ruiIndex == 0xFF` unregistered marker the existing asserts rely on — `< 0xFF` bounds are correct for both registries.
- Startup-only registration verified: the single registration root is the `GameBase` ctor (`GameBase.cpp:19-22`) → `game::FrameInterpolate::Register()` → `FrameInterpolateBase::Register()` (`FrameBase.cpp:153`) plus leaf `Register()`s — main thread, before the loop/`Dispatch()` fan-out; registries are register-once (asserted), so the threading-contract comment documents an invariant that genuinely holds.
- `ASSERT` confirmed active in all builds (`Common/ErrorUtils.h:12` — no NDEBUG gating; `DEBUG_BREAK` + throw). The new asserts follow the file's existing invariant-assert pattern (e.g. `AddElement`'s capacity assert), not the banned internal defensive-validation class. No corrections needed.
