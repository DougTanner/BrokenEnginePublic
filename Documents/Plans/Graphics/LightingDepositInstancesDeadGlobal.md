<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T00:01:00.749Z","dependsOn":[]} -->
# Delete the dead giLightingDepositInstances counter and its false comments

## Context

`engine::giLightingDepositInstances` (`Engine/Source/Graphics/Render/Render.h:92`) is written but never read:

- `Engine/Source/Frame/Collections/AreaLights/AreaLightsRender.cpp:157` — `giLightingDepositInstances += siRendered;`
- `Engine/Source/Frame/Collections/PointLights/PointLightsRender.cpp:151` — same
- `Engine/Source/Frame/Collections/HexShields/HexShieldsRender.cpp:117` — same
- `Engine/Source/Graphics/Render/MainUniforms.cpp:447` — `giLightingDepositInstances = 0;`

No read site exists anywhere in `Engine`, `Projects`, or the shaders. Verified at baseline `55beccf4` and again after the lighting/shadow dispatch windowing removal landed in this session, so it is pre-existing and unrelated to that change.

Three comments claim it drives the spread-chain gate, which is false. `RenderLightingSpreadIndirect` (`Engine/Source/Graphics/Render/LightingUniforms.cpp:383`) derives its instance and group counts solely from `sbLightingRefreshFrame` (`LightingUniforms.cpp:60`, set at `:87`), never from the counter. The false claims are at:

- `Engine/Source/Graphics/Render/Render.h:92` (trailing comment on the declaration)
- `Engine/Source/Graphics/Render/MainUniforms.cpp:443-446` (the reset's block comment, "RenderLightingSpreadIndirect consumes it")
- `Engine/Source/Graphics/Render/MainUniforms.cpp:550-551` ("that is where giLightingDepositInstances is summed")
- the per-writer trailing comments at the three `*Render.cpp` sites above

## Design

Delete the global and every write, then repair the surrounding comments so they describe the real gate. Deleting is preferred over merely correcting the comments: the repository directive is to remove obsolete code rather than keep an unused mechanism alive, and the counter has no reader to preserve behavior for. Removing a write with no reader is behavior-preserving.

Concretely: remove the `giLightingDepositInstances` declaration; remove the three `+= siRendered;` statements and the comments that exist only to explain them (leave the surrounding `WriteIndirectBuffer` calls, `siRendered`, and the HexShields note about zero-intensity shields only if it still describes live behavior — otherwise remove it with the statement it annotates); remove the reset at `MainUniforms.cpp:447` and rewrite its block comment; rewrite the `MainUniforms.cpp:550-551` ordering comment so it states the real reason `RenderLightingSpreadIndirect` must follow `EndRender` (the deposit indirect-buffer counts EndRender publishes), or delete it if EndRender ordering is already evident.

## Critical files

- `Engine/Source/Graphics/Render/Render.h`
- `Engine/Source/Graphics/Render/MainUniforms.cpp`
- `Engine/Source/Frame/Collections/AreaLights/AreaLightsRender.cpp`
- `Engine/Source/Frame/Collections/PointLights/PointLightsRender.cpp`
- `Engine/Source/Frame/Collections/HexShields/HexShieldsRender.cpp`

## In scope

- The `giLightingDepositInstances` declaration in `Render.h`
- The three `AreaLightsInterpolate::EndRender`, `PointLightsInterpolate::EndRender`, and `HexShieldsInterpolate::EndRender` accumulation statements and their attached comments
- The reset statement in `RenderFrameMain` and its block comment
- The `RenderLightingSpreadIndirect` ordering comment in `RenderFrameMain`
- Any AGENTS.md sentence that names the counter, if one exists after the code change

## Out of scope

- `sbLightingRefreshFrame` and every gating decision in `LightingUniforms.cpp`
- `RenderLightingSpreadIndirect` behavior, dispatch counts, and the spread/combine/temporal chain
- The `WriteIndirectBuffer` calls and profile counters in the three EndRender functions
- Any other unused global in `Render.h`

## Risk tier and invariants

Expected Change Workflow Tier 1 — deleting a write-only global plus comment repair, with no public signature or invariant exposure and no observable behavior change. Escalate to Tier 2 if a reader turns up that this Plan's search missed. The counter is client render-side only and is not part of the deterministic PostRender state or CRC.

## Acceptance criteria

- No occurrence of `giLightingDepositInstances` remains in the repository
- Client and server both compile
- The remaining comments in `MainUniforms.cpp` around the spread call name only mechanisms that actually gate it
