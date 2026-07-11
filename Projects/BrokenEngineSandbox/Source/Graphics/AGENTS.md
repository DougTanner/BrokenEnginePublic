# Graphics - Game Camera

Game-specific camera controller for BrokenEngineSandbox. Client-only (whole-file `BT_CLIENT` wrap — client vcxproj only). Global: `gpCamera`.

## Overview

Extends `engine::CameraBase`, which owns the matrices, snapped visible areas, LOD latching, and the texel-grid rendering mechanism — see [Engine Graphics](../../../../Engine/Source/Graphics/AGENTS.md). This class owns where the camera points and how it moves: player tracking, jump easing, zoom, shake-to-vibration, and the day/night sun angle.

## Architecture Notes

**Update Entry**: The engine calls `Update(const FrameInterpolate&)` once per render frame (`GameBase`, before `RenderGlobal`) plus once at boot; the `Frame&` overload is a thin forward with no current callers.

**Hybrid Timing**: Position blend, shake decay, and internal timers advance on the wall-clock render delta the engine just measured for player interpolation (`gpGame->mfLastRenderFrameSeconds`), keeping the camera in sync with the interpolated player across vsync misses. Sun angle alone advances on the interpolate-frame's deterministic dt so day/night survives variable render rates.

**Sun Angle**: Two-rate piecewise advance (faster through the night half of the cycle); wraps at `2*PI`; paused in main menu. UI sliders and debug ImGui can override the live value.

**Game/Frame Boundary**: Shake is driven by Game (armor damage on the flagship player), not Frame; the camera only decays it and converts it to controller vibration. Focus resolves via the client's grid cell and player index against `PlayersPostRender`, surviving spawns and cross-cell transfers. Resolution failures rate-limit diagnostic logs and extrapolate the last-known position along derived velocity, clamped to a short window; no valid client player ID at all falls back to the canonical main-menu pose instead of stranding at a stale gameplay position.

**Reconciliation Visual Smoothing**: Network corrections for the flagship player produce a decaying visual offset applied to the camera target only; simulation stays authoritative. See [Documents/Architecture/GameReconciliation.md](../../../../Documents/Architecture/GameReconciliation.md).

**Long-Distance Jump Easing**: Fixed-duration smoothstep ease triggered by distance-to-target or mid-jump target-shift exceeding threshold; re-anchors from current position without cancelling; ends on proximity or duration force-snap.

**Mouse-Wheel Zoom**: Wheel input nudges a clamped eye-height target, the per-tick delta sqrt-scaled by altitude so high heights don't build oversized easing velocity. Current height eases toward the target on a cubic Hermite that re-anchors position *and* velocity each tick the target moves — no mid-flight stepping while the user keeps scrolling. The Release clamp ceiling is purely the gameplay zoom-out limit, not a texel anchor (texels coarsen with zoom; coverage never crops). Per-frame scroll delta is derived in the game input layer — see [Input](../Input/AGENTS.md). The eye sits straight down (+Z) above the target; the position chase rate scales linearly with eye height (loose close up, tight zoomed out).

**Texel Eye Heights**: The camera ramps two independent rate-limited heights (shadow, lighting) toward the live eye height — unclamped, tunable meters-per-second caps, with a zero-sentinel "snap on first frame" reset by `Game::Reset` so state never leaks across sessions. The renderer maps them to world-sized texel grids; mechanism documented in [Engine Graphics — CameraBase](../../../../Engine/Source/Graphics/AGENTS.md).

**Persistence**: Every `Update` ends with the diff-checked client session-state capture (`gpGame->CaptureClientStateAndSaveIfChanged()`) — the one hook guaranteed to run each render frame; the zoom target is restored from persisted client settings at boot.

**Free Camera**: Debug-only (`kbFreeCamera`), main-menu-only — WASD bypasses target/blend to fly the camera directly.

## See Also

- [Engine Graphics](../../../../Engine/Source/Graphics/AGENTS.md)
