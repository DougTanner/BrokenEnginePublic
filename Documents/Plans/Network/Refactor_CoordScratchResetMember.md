# CoordScratch Reset Member

## Context

`game::CoordScratch` (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.h`) is the per-coord, per-frame scratch struct used by `ClientReconciler::Run` to drive rollback-and-replay. Each `Run()` call resets every field on `CoordScratch` while preserving the `replayStack`'s allocated capacity (so the per-frame scratch reset doesn't churn the heap).

Today that reset is an 18-line field-by-field block inline in `ClientReconciler::Run` (`ClientReconciler.cpp`, the per-coord reset loop). When a contributor adds a new field to `CoordScratch`, nothing forces them to update the reset block — the only protection is the comment `// Reset scratch fields but preserve replayStack capacity` next to the call site. A missed reset would silently leak state across frames within the same coord, with debug-symptom-only manifestations (CRC drift, repeated mismatches).

## Design

Move the reset into a `Reset()` member on `CoordScratch` itself:

```cpp
struct CoordScratch
{
    // ... existing field declarations ...

    void Reset()
    {
        replayStack.clear();
        iReplayStackCount = 0;
        iReplayWriteHead = 0;
        // ...etc, every other field set to its declared default...
    }
};
```

Replace the 18-line block in `ClientReconciler::Run` with `rWork.scratch.Reset();`.

The benefit is co-location: a contributor adding a field to `CoordScratch` sees `Reset()` directly under the field they just added, in the same file. The existing fragility (forgetting to update the reset code) doesn't disappear — it's just made dramatically easier to notice.

### Consider but reject

A `*this = CoordScratch{};` swap idiom (preserving `replayStack` via move-and-restore) was considered. It would auto-reset future fields via default member initializers, removing the manual update step entirely. Rejected because:

1. It defeats the capacity-preservation goal unless wrapped in awkward move-and-restore boilerplate.
2. It silently re-initializes `pDesyncClientFrame` (`unique_ptr`) — fine in current code, but the swap idiom hides ownership transitions that a future field's destructor side-effect could break.
3. The straightforward field-by-field `Reset()` is KISS; the swap idiom is clever-but-fragile.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.h` (add `Reset()` member to `CoordScratch`)
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.cpp` (replace the inline reset block in `ClientReconciler::Run` with `rWork.scratch.Reset()`)

## Out of scope

- Changing the reset semantics. The new `Reset()` must clear exactly the same fields the existing block clears, with the same defaults — including `pDesyncClientFrame.reset()` and `profiling = {}`.
- Touching `CoordWork::coord` or `CoordWork::pFrames` — those are reassigned in the same loop, not reset, and stay in the call site.
- Renaming or restructuring `CoordScratch` itself.
- Applying the same pattern to other scratch structs (`ConfirmedClientState`, `ReconcileProfiling`, `CoordWork`) unless they exhibit the same fragility — they currently do not.

## Acceptance criteria

- A new field added to `CoordScratch` with a default member initializer requires updating exactly one place (`Reset()` in the .h) instead of two (the struct declaration plus the .cpp reset block).
- BrokenEngineSandbox client builds clean.
- Replay/desync behavior is bit-for-bit identical to before the refactor — no CRC differences in a deterministic replay run.
