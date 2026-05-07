# Graphics - Game Camera

Game-specific camera controller for BrokenEngineSandbox. Client-only (`BT_CLIENT`). Global: `gpCamera`.

## Overview

Extends `engine::CameraBase` with smooth player tracking, static main menu positioning, damage-driven camera shake with controller vibration, and day/night cycle via sun angle modulation.

## Architecture Notes

**Hybrid Timing**: Position blend, shake decay, and internal timers advance on a display-rate accumulator (FIFO cadence). Sun angle alone advances on deterministic frame dt so day/night survives variable render rates.

**Sun Angle**: Piecewise speed with a faster band around noon to push past midday quickly; default rate elsewhere; wraps at `2*PI`; paused in main menu. UI sliders and debug ImGui can override the live value.

**Long-Distance Jump Easing**: Fixed-duration smoothstep ease triggered by distance-to-target or mid-jump target-shift exceeding threshold; re-anchors from current position without cancelling; ends on proximity or duration force-snap.

**Game/Frame Boundary**: Shake is driven by Game (armor damage on the flagship player), not Frame. Focus resolves via the client's grid cell and player index against `PlayersPostRender`, surviving spawns and cross-cell transfers. Resolution failures rate-limit diagnostic logs and fall through to extrapolation.

**Reconciliation Visual Smoothing**: Network corrections for the flagship player produce a decaying visual offset applied to the camera target only; simulation stays authoritative. See [Documents/Architecture/GameReconciliation.md](../../../../Documents/Architecture/GameReconciliation.md).

**Velocity Extrapolation**: When the focused player is absent (cross-cell transfer, late snapshot), camera extrapolates last-known position along derived velocity, clamped to a short window.

**Mouse-Wheel Zoom**: Wheel input drives a clamped eye-height target (wheel-up = zoom in); current eye height eases toward target. Per-frame scroll delta is derived from the input accumulator — see [Input](../../../../Engine/Source/Input/CLAUDE.md). Pitch is coupled to eye height: linearly tilts from oblique at default height to straight-down at 2x default.

**Async Rendering**: Supports both standard Frame-based updates and direct `FrameInterpolate` updates.

## See Also

- [Engine Graphics](../../../../Engine/Source/Graphics/CLAUDE.md)
