# BrokenEngineSandbox - Sample Game Implementation

## Overview

Tech demo that demonstrates and stress-tests engine features — not a game: no human player or win condition. AI-driven spaceship fleets (flagship + wingmen) fight continuously spawning enemies over the island ocean. All game code lives in the `game` namespace.

## Frame Purity Constraint

Frame update code is purely functional: it relies only on explicit function parameters and never queries Game (`gpGame`). Client-only render code under `Frame/` is exempt. The Frame does not privilege any player index — all players are AI-driven; one is the **flagship** that others follow. Flagship-tracking responsibilities (camera shake, death transitions, respawn orchestration) are Game-level, not Frame.

## Key Classes/Systems

- **Game** (`Game.h`/`Game.cpp`) - Central coordinator inheriting from `engine::GameBase`, accessed via `gpGame`. Owns lifecycle, multi-frame grid orchestration, cross-grid entity transfers, the `ClientSession`/`ServerSession`, and (server-only) the `GameSaveLoad` save/load/replay subsystem; holds a `FleetSelection` member and forwards its fleet-navigation API.
- **FleetSelection** (`FleetSelection.h`/`.cpp`, client-only) - Owns the client fleet list and focus state; drives which grid cell the camera follows.
- **Fleet** (`Fleet.h`) - Client-side grouping of player entities for focus/selection and shared navigation intent. Each fleet carries a server-minted random 128-bit identifier that is stable across disconnect/reconnect/save-load and used by clients to refer to fleets persistently.
- **Settings/transfer free functions** - Client-only persisted-settings save/load/reset lives in `ClientSettings.{h,cpp}` (`game::` free functions); the shared spawn-side of cross-grid transfers is the `game::SpawnTransfer` free function in `SpawnTransfer.{h,cpp}`.
- **ClientSession** / **ServerSession** - Per-side networking orchestration owned by `Game`. See [Network/AGENTS.md](Network/AGENTS.md).

## Architecture Notes

- **Deterministic floating-point**: `/fp:strict` in vcxproj ensures cross-hardware determinism for CRC-based reconciliation. See [Documents/FloatingPointDeterminism.txt](../../../Documents/FloatingPointDeterminism.txt).
- **Client-driven subscriptions**: Client computes its own active set (current cell + any cells in the immediate 3x3 ring whose footprint overlaps the camera's zoom-dependent `f4LargeVisibleArea`); server only sees the resulting subscription list. Local-only frames outside the active set are evicted; confirmed frames are kept. The 3x3 clamp is explicit — at extreme zoom-out the VisibleArea can extend beyond the ring and would otherwise add cells the server doesn't authorize. Write the client grid coord only through `Game::SetClientGridCoord()` — the visible-neighbor cache stores absolute coords and the setter invalidates it.
- **Cross-grid transfers**: Entity hand-off between grid cells is expressed as transfer `StatusChange`s carrying fully-serialized spawn state, dispatched per collection type and stripped from `FrameInput` before the normal Spawn phase.
- **Alignments**: `playerAlignment` / `enemyAlignment` are owned by `Game` and copied onto each new `Frame::postRender`. Frame code reads them from `postRender`, never from `gpGame`.
- **Engine dependency direction**: Engine code may read or call game symbols by contract; do not decouple sanctioned Engine→game access. Do not introduce game-specific concepts into engine-owned types. See [Engine Source](../../../Engine/Source/AGENTS.md).
- **Versioned persisted settings**: Sound/graphics/tweaks settings plus per-client UI/session state (focused fleet identifier, focused ship global ID, camera zoom target) are client-only POD structs with an embedded `kiVersion`, round-tripped through `engine::{Write,Read}VersionedFile` into `kAppDataDirectory`. Bump the struct version on layout change. Session state is captured each frame and re-persisted only when the diff against the last-written copy changes.
- **Save/load/replay** (`Save/GameSaveLoad`, server-only, driven by the engine server loop): grid saves are atomic and coord-key sorted. Externally reported saves consume the write result; replay start requires its grid snapshot, while replay stop attempts every component and reports success only when all persist. Replay records per-coord `engine::DifferenceStream`s and validates each tick's checksum against resimulation. Nav/elevation data is derived, not persisted; clients receive server-built nav data. Fresh-game paths reset the fleet manager explicitly. Save/replay reads are all-or-nothing trust boundaries: corrupt or truncated input falls back to a clean fresh game.
- **Versioning**: the save/replay format is gated by `game::Frame::kiVersion` (composition documented in [Frame/AGENTS.md](Frame/AGENTS.md)) — collection version bumps propagate automatically. `Version.h`'s `kiGameVersion` is informational only (startup log, crash report, Vulkan application version); it gates nothing.
- **Compile-time toggles**: `Pch.h` holds `inline constexpr` flags for frame dispatch parallelism, desync recovery, render thread, network simulation, the agent command layer (`kbAgent`), and per-configuration debug/profile knobs. It also defines per-category compile-floor log-level overrides before including `Common.h` (runtime thresholds default `kInfo`; see [Common/AGENTS.md](../../../Common/AGENTS.md) Logging).
- **Pch.h-provided headers (do not re-`#include`)**: `Pch.h` force-includes `ExternalHeaders.h`, `Log/LogTypes.h`, `Common.h`, `Shaders/ShaderLayouts.h`, `Ui/HexShieldWrappers.h`, `Ui/WindDepositsWrappers.h`, `Frame/Frame.h`, and `Engine.h`, so their symbols are visible in every client/server TU — consumer TUs must not redundantly include them. DataPacker uses a separate, leaner PCH (`ExternalHeaders.h`/`Log/LogTypes.h`/`Common.h` only), so its sources are unaffected by the game-Pch-only headers.
- Detailed networking flow: [Game Reconciliation](../../../Documents/Architecture/GameReconciliation.md) and [Network Architecture](../../../Documents/Architecture/Network.md). Update the affected document when reconciliation structure or protocol flow changes.

## Subdirectories

- [Agent/](Agent/) - JSON command dispatcher, compiled on both sides and activated by `kbAgent`. Shared commands cover process/log control. Client commands drive capture, UI/input automation, scene queries, and settled window state through engine agent facilities; server commands drive sim/save/replay control, StatusChange injection, and Frame queries. Server save/load `file` values are appdata-relative trust boundaries: require a non-empty bare filename with no embedded NUL, separators, `..`, `:`, or Windows reserved device basename. Client capture `path` values deliberately accept full paths. Replay commands throw when `kbDebugInput` is disabled.
- [Frame/](Frame/AGENTS.md) - Game state, physics tick pipeline, SOA collections
- [Graphics/](Graphics/AGENTS.md) - Camera controller
- [Input/](Input/AGENTS.md) - Keyboard/mouse and gamepad input pipeline
- [Network/](Network/AGENTS.md) - Sessions, packet types, serialization
- [Profile/](Profile/AGENTS.md) - Game CPU profiling counters
- [Save/](Save/) - Save/load/replay (`game::GameSaveLoad`, server-only)
- [Server/](Server/AGENTS.md) - GDI monitoring window (server-only)
- [Ui/](Ui/AGENTS.md) - ImGui HUD, menus, settings

## See Also

- [Engine Source](../../../Engine/Source/AGENTS.md)
- [Common Utilities](../../../Common/AGENTS.md)
