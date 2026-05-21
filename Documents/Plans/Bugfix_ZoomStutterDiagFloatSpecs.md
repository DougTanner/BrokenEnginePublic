# Bugfix: Zoom-Stutter Diagnostic Violates Float-Format LOG Rule

## Context

`Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp` contains a TEMP zoom-stutter diagnostic block (marked "TEMP zoom-stutter diagnostic — remove once root cause is identified.") that emits per-frame CSV rows via `FILE_LOG(...)` using float format specs `{:.6f}` and `{:.3f}` (around line 287).

Project CLAUDE.md states this is a **NEVER**:

> NEVER use float format specs (`{:.Nf}`, `{:f}`, `{:e}`, etc.) in `LOG(...)` — they heap-allocate and trip the main-loop allocation tracker. Wrap each float arg with `common::Wb(value, precision)` and each `XMVECTOR` arg with `common::WbV2(vec, precision)`; the placeholder stays `{}`.

The diagnostic runs every frame in the main loop, so the heap allocations from `{:.Nf}` will trip the allocation tracker (`DEBUG_BREAK()`).

## Options

- **Option A (recommended)**: Remove the TEMP diagnostic block once the user confirms the root cause is identified or it is no longer needed.
	- Pros: Eliminates dead/temporary code, removes the violation, restores zero-allocation main loop.
	- Cons: Loses the diagnostic capability if the stutter resurfaces (can be re-added later).
- **Option B**: Keep diagnostic; wrap every float arg with `common::Wb(value, 6)` / `common::Wb(value, 3)` and change format specs to `{}`.
	- Pros: Preserves diagnostic; complies with allocation-tracker rule.
	- Cons: Keeps TEMP code path in shipping codebase; still per-frame file I/O cost.

## Recommendation

**Option A** — remove the TEMP diagnostic when the user confirms it is no longer needed. If the stutter is still under investigation, apply Option B as a stopgap.

## Files / Lines Touched for Option A

All in `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp`:

- Lines 12–14: namespace block containing `gZoomStutterDiag` / `gbZoomStutterHeaderWritten`
- Lines 63–68: local diagnostic state declarations
- Lines 153–155: `vecPlayerPosLogged` / `bHasPlayerLogged` assignment
- Line 225: `fJumpTLogged = fT;`
- Lines 236–238: `fAdaptiveBlendLogged` / `fBlendLogged` assignments
- Lines 287–305: the `FILE_LOG(...)` CSV-emission block

Also remove any now-unused includes or helper variables introduced solely for the diagnostic (verify after deletion).
