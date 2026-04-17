# Refactor: CoordScratch Booleans → common::Flags

Source: /external-refactor-clean on Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.h

`CoordScratch` (lines 50-54) has 5 per-coord per-reconcile bool outcome flags: `bCrcFastPath`, `bReplayed`, `bShrunkRollback`, `bReSimOccurred`, `bSuppressRepeatLogs`. These are the canonical "related flags that travel together" shape the root `CLAUDE.md` names as the `common::Flags<EnumType>` use case.

## Changes

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.h
- Add enum class next to `CoordScratch`:
  ```cpp
  enum class ReconcileScratchFlags : uint8_t {
      kCrcFastPath, kReplayed, kShrunkRollback, kReSimOccurred, kSuppressRepeatLogs,
  };
  ```
- Replace the 5 `bool` members with one `common::Flags<ReconcileScratchFlags> flags;`. Memory goes from 5 bytes to 1. [~15m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.cpp
- Line ~153: the logging-condition predicate `(!scratch.bSuppressRepeatLogs && (scratch.bReplayed || scratch.bReSimOccurred))` becomes `!scratch.flags.Has(kSuppressRepeatLogs) && scratch.flags.HasAny(kReplayed, kReSimOccurred)`. [~10m]
- All write sites (set/clear) migrate from `scratch.bCrcFastPath = true;` to `scratch.flags.Set(kCrcFastPath);`. Grep for each and migrate. [~30m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp
- Same migration at every `scratch.b*` read or write site. [~30m]

## Verification
- Rebuild client config.
- Run a reconciliation-heavy session; confirm `LOG(kNetwork, kVerbose, ...)` output is identical to pre-refactor.

## Verification Notes
Verified — `ClientReconciler.h:50-54` has the 5 listed `bool` members (`bCrcFastPath`, `bReplayed`, `bShrunkRollback`, `bReSimOccurred`, `bSuppressRepeatLogs`). Migration to `common::Flags<EnumType>` is the canonical pattern per root CLAUDE.md ("Flags over booleans").
