# Engine UI - Shared Wrapper and Screen Infrastructure

Shared runtime settings, network-pending controls, and client-only curve/editor support. Engine Tweaks registration contracts live in TweaksScreen (`Screens/TweaksScreen/AGENTS.md`); game localization and wrapper affinity live in game UI (`../../../Projects/BrokenEngineSandbox/Source/Ui/AGENTS.md`).

## Wrapper Contracts

- `Wrapper` stores float, bool, or discrete values with one-consumer change tracking. `Changed<T>()` advances the previous-value state, so exactly one consumer may poll a wrapper each frame.
- Mutate through `Set` or `Reset`; assignment is disabled. `Set` leaves the prior value intact so the next poll observes a change. `Reset` updates both values and is for initialization or capability clamping that must not trigger rebuilds.
- Discrete wrappers fall back to their first allowed value when persisted or capability-derived input is invalid. Float wrappers may snap to a configured grid.
- Wrapper bounds annotated with shader invariants are correctness constraints, not UI tuning.
- Settings that alter baked render-target dimensions must participate in `Graphics::Refresh`; prefer per-frame uniforms when recreation is unnecessary.
- Wrapper headers are intentionally consumed directly rather than aggregated into `Engine.h`, limiting recompilation from tuning edits.

## Shared Types

- Height-dependent wrapper groups resolve camera-height-conditioned render values; Render (`../Graphics/Render/AGENTS.md`) owns how those resolved values reach the per-frame GPU buffers.
- Client-only curves use bounded monotone cubic interpolation and an ImPlot editor. Endpoints remain X-locked and interior control points ordered.
- The Lighting tab deliberately keeps two combine curves, `gCombineCurveOld` and `gCombineCurveNew`, behind the `gbUseCombineCurveNew` toggle so tuning can be compared live against the shipping baseline. They currently hold identical control points; that is the A/B setup, not dead duplication. Collapse to a single curve once tuning settles.
- `NetworkUiControl` disables a control while authoritative state has not resolved its request; call `Update` every frame with that authoritative state.

Most wrapper storage compiles into both builds so server-side simulation can read defaults. ImGui/ImPlot-dependent types remain `BT_CLIENT`-guarded.

## See Also

- Screens (`Screens/AGENTS.md`) - Engine screen routing
- TweaksScreen (`Screens/TweaksScreen/AGENTS.md`) - Runtime parameter UI contracts
