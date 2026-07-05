# Direct-Load Tick-Clock Retention

## Context

Surfaced by the `Save/SaveLoadTrustBoundaryHardening` session-audit (2026-07); pre-existing, NOT introduced by that session. In `GameSaveLoad::ReadGrid` the loaded tick counter / current time are set from the first frame (`GameSaveLoad.cpp` ~:491-492: `SetTickCounter` / `SetCurrentTime` from `mCoordFrames.begin()->second.pCurrent->interpolate`), but `ServerLoad` then calls `mrGameBase.Reset()` (`GameSaveLoad.cpp` ~:61), and `Game::Reset()` zeroes `miTickCounter` / `mfCurrentTime` (`Game.cpp` ~:432-433). So on a **direct** `ServerLoad` the loaded tick clock is discarded — the game resumes at tick 0.

The **replay** path does not have this issue: it re-derives tick/time after `Reset()` (`SaveLoadReplay` ~:259-264). Only the direct-load path drops the loaded clock. This may be intentional (authoritative world state lives in the per-cell frames; the tick counter is a bare wall clock with no cross-load meaning) — intent was NOT verified by the audit.

## Design

Investigate whether the loaded tick clock should survive a direct `ServerLoad`, then decide:

- **Option A (restore)** — re-apply the loaded tick counter / current time after `Reset()` on the direct-load path, mirroring the replay path's post-`Reset()` re-derivation. Makes a loaded game resume at its saved tick/time.
- **Option B (accept + document)** — confirm the reset-to-0 is intentional and add a one-line comment at `ReadGrid` / `ServerLoad` explaining that direct load deliberately resets the clock (world state is frame-carried; the tick counter carries no cross-load meaning), so the now-dead `SetTickCounter`/`SetCurrentTime` in `ReadGrid` can also be removed as they are overwritten.

Before changing anything, confirm whether any consumer depends on the loaded tick value: CRC tick tagging, replay-recording-started-from-a-loaded-state, and post-load client clock sync are the candidates to check.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — `ReadGrid` (tick/time set), `ServerLoad` (`Reset()` ordering)
- `Projects/BrokenEngineSandbox/Source/Frame/…` `Game::Reset()` (`Game.cpp`) — the tick/time zeroing

## Invariant exposure

- Server-side load-path timing. Touches the tick counter, which tags CRC'd ticks — verify whether changing the post-load tick value affects determinism / replay-from-loaded-state before choosing Option A. No wire / `kiVersion` / `.pack` layout change either way.

## Out of scope

- The trust-boundary validation already landed by `Save/SaveLoadTrustBoundaryHardening` (this plan does not revisit it).
- The `iNextGlobalId` staging (already correct — applied post-success).

## Acceptance criteria

- (Option A) A game loaded via direct `ServerLoad` resumes at its saved tick / current time rather than tick 0; all other load behavior unchanged.
- (Option B) A one-line doc note records the intentional reset, and the dead `SetTickCounter`/`SetCurrentTime` in `ReadGrid` are removed.

## Notes

- **Decision plan (present options).** Verify intent before changing — do not alter working load timing without confirmation (repo Diagnosis Discipline: never remove/alter a working feature as a "fix" without explicit confirmation).
- Touches `GameSaveLoad.cpp` (`ReadGrid` / `ServerLoad`); no other live plan currently edits this file, so refresh line cites at execution if that changes.
