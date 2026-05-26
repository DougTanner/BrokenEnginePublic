# Architecture: Engine → game::gpCamera Coupling Sweep

## Context

`game::Camera` derives from `engine::CameraBase` (`Projects/BrokenEngineSandbox/Source/Graphics/Camera.h:11`), and `game::gpCamera` is a game-layer global parallel to `game::gpGame` (`Camera.h:61`). Across the engine, render / uniform / collection-render / audio code reads `engine::CameraBase` members *through* this game-layer global — the same letter-compliant-but-intent-breaking shape as the network layer-violations plans.

This is the **broadest of the deferred layer-violation plans** (>10 read sites across ~13 TUs). Frame it honestly as **tightening the seam by injecting `CameraBase`, not removing the dependency**: the renderer is client-only and inherently camera-coupled, so the camera dependency is legitimate — what is wrong is reaching it via the *game* global instead of an engine-typed reference. The value is debatable; size and risk are real (>10 sites, all on the per-frame render path).

A sweep of `Engine/Source/**/*.cpp` for `gpCamera` finds 42 occurrences across 14 files. Excluding the two camera-*driver* `game::gpCamera->Update(...)` mutation calls (`Main.cpp:217`, `GameBase.cpp:405` — these legitimately drive the camera from the game layer and are out of scope), the member-read sites to convert are:

- **`Engine/Source/Graphics/Render/GlobalUniforms.cpp`** (most sites): `mfLodStableWidth`/`mfLodStableHeight` (`:246-247, :517-518`), `mVecPosition` (`:254, :421, :506, :529`), `f4RenderVisibleArea` (`:263, :509`), `mfCameraEyeHeight` (`:430`), `SunAngle()` (`:496`).
- **`Engine/Source/Graphics/Render/MainUniforms.cpp`**: `miVisibleAreaLod` (`:139`), `miFrame` (`:206`), `mfShake` (`:210`), `mMatView`/`mMatPerspective` (`:219`), `mVecEyePosition` (`:221`), `mVecToEyeNormal` (`:222`), `mVecPosition` (`:233`), `mfCameraEyeHeight` (`:239`).
- **`Engine/Source/Graphics/Render/LightingUniforms.cpp`**: `mfCameraEyeHeight` (`:71, :118`).
- **`Engine/Source/Graphics/Render/SmokeUniforms.cpp`**: `f4RenderVisibleArea` (`:43`).
- **`Engine/Source/Graphics/GraphicsUtils.cpp`**: `f4RenderVisibleArea` (`:79`), `mVecEyePosition` (`:85`).
- **`Engine/Source/Graphics/Managers/ParticleManager.cpp`**: `f4RenderVisibleArea` (`:38`).
- **`Engine/Source/Audio/StaticVoices.cpp`**: `mVecEyePosition` (`:486`), `mVecPosition` (`:487`), `mfCameraEyeHeight` (`:501`).
- **`Engine/Source/Frame/Collections/PointLights/PointLightsRender.cpp`**: `mMatView` rows (`:61-62`), `mfLodStableWidth`/`mfLodStableHeight` (`:69-70`).
- **`Engine/Source/Frame/Collections/AreaLights/AreaLightsRender.cpp`**: `mfLodStableWidth`/`mfLodStableHeight` (`:67-68`), `AabbIntersectsVisibleArea(...)` + `f4RenderVisibleArea` (`:102`).
- **`Engine/Source/Frame/Collections/Billboards/BillboardsRender.cpp`**: `mMatView`/`mMatPerspective` (`:65`).
- **`Engine/Source/Frame/Collections/HexShields/HexShieldsRender.cpp`**: `InVisibleArea(...)` + `f4RenderVisibleArea` (`:64`).

Members/methods read: `mVecEyePosition`, `mVecPosition`, `mVecToEyeNormal`, `f4RenderVisibleArea`, `mMatView`, `mMatPerspective`, `mfShake`, `miFrame`, `miVisibleAreaLod`, `mfLodStableWidth`/`mfLodStableHeight`, `mfCameraEyeHeight`, `SunAngle()`, `InVisibleArea(...)`, `AabbIntersectsVisibleArea(...)`. All are declared on `engine::CameraBase` (confirm during the grill that none of the read members live on `game::Camera` only).

Two sites also read the **game-specific constant** `game::Camera::kfCameraEyeHeightDefault`: `GlobalUniforms.cpp:429` (`2.0f * game::Camera::kfCameraEyeHeightDefault`) and `LightingUniforms.cpp:117-118`. These are not `CameraBase` members — they need either a `CameraBase` static/virtual seam or to be passed in alongside the camera reference.

## Design

Inject an immutable camera view into the render / uniform / collection-render / audio functions instead of dereferencing the game global:

- Pass `const engine::CameraBase&` (the simplest seam) into each affected function, threaded from the engine render entry points (`GlobalUniforms` / `MainUniforms` / `LightingUniforms` / `SmokeUniforms` population, the per-collection `*Render` functions, `ParticleManager` culling, `StaticVoices` listener update). The entry points already run after `game::gpCamera->Update(...)`, so passing `*game::gpCamera` (sliced to `CameraBase&`) at the top of the render path is a one-line bridge per entry point.
- **Alternative — an immutable camera snapshot struct.** If threading a reference through many signatures is unwieldy, define an engine `CameraSnapshot` (POD of the read members) built once per frame from `CameraBase` and passed by const-ref. Decide reference-vs-snapshot in the grill; reference is simpler (KISS) unless a snapshot is needed for the collection-render fan-out / threading model.
- For `kfCameraEyeHeightDefault`: expose it on `CameraBase` (static or virtual accessor) so the two uniform sites stop naming `game::Camera`, OR pass the resolved default through the camera reference's accessor. The grill must pick one since this constant is genuinely game-defined.

The `game::gpCamera->Update(...)` calls at `Main.cpp:217` and `GameBase.cpp:405` stay as-is (the game layer owns camera driving).

## Critical files

- `Engine/Source/Graphics/Render/GlobalUniforms.cpp`, `MainUniforms.cpp`, `LightingUniforms.cpp`, `SmokeUniforms.cpp` — uniform population.
- `Engine/Source/Graphics/GraphicsUtils.cpp`, `Engine/Source/Graphics/Managers/ParticleManager.cpp` — visible-area helpers / culling.
- `Engine/Source/Audio/StaticVoices.cpp` — 3D listener placement.
- `Engine/Source/Frame/Collections/{PointLights,AreaLights,Billboards,HexShields}/*Render.cpp` — per-collection render culling / billboard orientation.
- `Engine/Source/Graphics/CameraBase.h` (+ `.cpp`) — add `kfCameraEyeHeightDefault` seam if chosen; confirm all read members live here.
- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.h` — `game::Camera`, `gpCamera`, `kfCameraEyeHeightDefault` source of truth.
- Engine render entry points that invoke the above (where `*game::gpCamera` is bridged to `CameraBase&`).

## Out of scope

- **The `game::gpCamera->Update(...)` driver calls** at `Main.cpp:217` and `GameBase.cpp:405` — camera driving is legitimately game-owned; only member *reads* are converted.
- **Removing the camera dependency from the renderer.** The renderer is inherently camera-coupled; this plan only swaps the *access path* (game global → engine reference), it does not decouple rendering from the camera.
- **Moving any read member that turns out to be `game::Camera`-only** up to `CameraBase` — if the grill finds such a member, scope it out and note it as a follow-up rather than relocating it here.
- **The `mbShowImGui` / `meUiState` UI-state reads** — separate plan `Graphics/ImGuiManagerGameStateReads.md`.

## Acceptance criteria

- No `engine::` TU dereferences `game::gpCamera` for member *reads* (the two `Update(...)` driver calls excepted); each affected function takes a `const engine::CameraBase&` (or `CameraSnapshot`) instead.
- Neither `game::Camera::kfCameraEyeHeightDefault` is named from `engine::` code after the change.
- Rendered output, audio listener positioning, and collection culling are visually/behaviorally identical to before (no shadow-area, lighting-LOD, billboard-orientation, or visible-area-cull regressions across zoom levels).
