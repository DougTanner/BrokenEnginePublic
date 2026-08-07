# Graphics - Game Camera

Client-only `Camera` derives from `engine::CameraBase`. The engine base owns matrices, visible-area snapping, LOD latching, and texel-grid projection; this layer owns game focus, motion, zoom, shake, sun angle, and persisted camera state.

## Timing and Ownership

- Camera position, easing, shake, and visual timers advance from render wall-clock delta so they track the interpolated player across variable display frames. Sun angle advances from the interpolate frame's delta and pauses in the main menu.
- Network reconciliation contributes a decaying camera-target offset only; authoritative simulation state is unchanged. Keep this integration consistent with `../../../../Documents/Architecture/GameReconciliation.md`.
- Player focus resolves through the current client coord and `PlayersPostRender`, surviving spawn and transfer. Brief lookup failures extrapolate the last focus within a bounded window; an absent player identity uses the main-menu pose.
- `Camera::Update` has two overloads, but only the `FrameInterpolate` one does work: both callers pass an interpolate frame, and the `Frame` overload is a one-line forward nothing calls. New camera update work goes in the `FrameInterpolate` overload.
- Damage-driven shake decays here and drives controller vibration. Debug free-camera motion is main-menu-only.

## Movement and Rendering Contracts

- Long-distance target changes use a bounded smoothstep jump that may re-anchor from the current position. Normal tracking tightens with camera height.
- Mouse wheel input updates a persisted, clamped eye-height target. Height and velocity re-anchor while scrolling so repeated input remains smooth; the gameplay zoom ceiling is not a texel-grid boundary.
- Shadow and lighting camera-height references expand immediately with outward live-eye motion and contract independently at their existing rates inward. Their zero sentinel initializes directly to live height, and each initialized reference remains at or above live height before the engine renderer converts it to a world texel grid.
- Every frame whose post-zoom live eye height exceeds its pre-zoom height marks lighting temporal history to be re-seeded from current values only by the next `RenderGlobal`; shadow temporal history needs no such mark, because its temporal pass rejects newly exposed history wherever the reprojected UV falls outside the previous footprint.
- Camera update captures diff-checked client state each render frame. Preserve the moment that state becomes visible to readers when changing zoom persistence or reset behavior.

## See Also

- `../../../../Engine/Source/Graphics/AGENTS.md` - CameraBase and renderer grids
- Game Input (`../Input/AGENTS.md`) - Per-frame scroll delta
