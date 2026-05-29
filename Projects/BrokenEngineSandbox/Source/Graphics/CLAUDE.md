# Graphics - Game Camera

Game-specific camera controller for BrokenEngineSandbox. Client-only (`BT_CLIENT`). Global: `gpCamera`.

## Overview

Extends `engine::CameraBase` with smooth player tracking, static main menu positioning, damage-driven camera shake with controller vibration, and day/night cycle via sun angle modulation.

## Architecture Notes

**Hybrid Timing**: Position blend, shake decay, and internal timers advance on the wall-clock render delta the engine just measured for player interpolation (`gpGame->mfLastRenderFrameSeconds`), keeping the camera in sync with the interpolated player across vsync misses. Sun angle alone advances on the interpolate-frame's deterministic dt so day/night survives variable render rates.

**Sun Angle**: Piecewise advance rate (distinct speeds for the noon band, the night band, and elsewhere); wraps at `2*PI`; paused in main menu. UI sliders and debug ImGui can override the live value.

**Long-Distance Jump Easing**: Fixed-duration smoothstep ease triggered by distance-to-target or mid-jump target-shift exceeding threshold; re-anchors from current position without cancelling; ends on proximity or duration force-snap.

**Game/Frame Boundary**: Shake is driven by Game (armor damage on the flagship player), not Frame. Focus resolves via the client's grid cell and player index against `PlayersPostRender`, surviving spawns and cross-cell transfers. Resolution failures rate-limit diagnostic logs and fall through to extrapolation.

**Reconciliation Visual Smoothing**: Network corrections for the flagship player produce a decaying visual offset applied to the camera target only; simulation stays authoritative. See [Documents/Architecture/GameReconciliation.md](../../../../Documents/Architecture/GameReconciliation.md).

**Velocity Extrapolation**: When the focused player is absent (cross-cell transfer, late snapshot), camera extrapolates last-known position along derived velocity, clamped to a short window.

**Mouse-Wheel Zoom**: Wheel input drives a clamped eye-height target (wheel-up = zoom in), with the per-tick delta scaled by `sqrt(height/default)` so high altitudes don't take oversized steps. `kfEyeHeightMaxReference` is the eye height the shadow texture is pre-sized to cover and the clamp ceiling for the shadow-texel world size; Release pins `kfEyeHeightMax` to it so shadow coverage always fills the frame, while dev builds allow a higher max where the coverage crops on screen. A separate rate-limited reference height (zeroed by `Game::Reset` so each session re-snaps) ramps toward the live eye height once per frame at a tunable meters-per-second cap, scaling the shadow texel world size: fixed at a settled height (the grid snaps cleanly under pan), rescaling only while it tracks a zoom — when zoomed out the coarser texels let the shadow pass shrink its ray-marched window back toward the default-height cost. The lighting deposit/spread/combine path carries an independent parallel reference height (its own meters-per-second ramp cap, also zeroed by `Game::Reset`, clamped to a lighting-specific coverage/headroom anchor) driving its world-sized-texel grid the same way. Current eye height eases toward target on a cubic-Hermite curve that re-anchors position *and* velocity each tick the target moves, avoiding mid-flight stepping. Per-frame scroll delta is derived from the input accumulator — see [Input](../../../../Engine/Source/Input/CLAUDE.md). The eye sits straight down (+Z) above the target; the position chase rate scales linearly with eye height (loose close up, tight when zoomed out).

**Free Camera**: Debug-only (`kbFreeCamera`), main-menu-only — WASD bypasses target/blend to fly the camera directly.

**Async Rendering**: Supports both standard Frame-based updates and direct `FrameInterpolate` updates.

## See Also

- [Engine Graphics](../../../../Engine/Source/Graphics/CLAUDE.md)
