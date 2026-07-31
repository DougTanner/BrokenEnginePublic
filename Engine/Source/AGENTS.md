# Engine/Source - Core Runtime

## Overview

Shared C++23 runtime for the client and server. Managers are constructed in dependency order and destroyed in reverse; fixed-rate simulation feeds a free-running interpolated renderer.

Update Frame Update Pipeline (`../../Documents/Architecture/FrameUpdatePipeline.md`) when main-loop or frame-phase order changes.

## Hub Conventions

- Engine/game contract: Engine code may consume game types, hooks, globals, and compile-time symbols that the game is required to provide. An engine-owned shared/public member, enum value, friend, or signature must not name a game-only concept unless an existing leaf document records deliberate ownership. If one would, stop and surface the ownership decision.
- Aggregation: `Engine.h` is the engine aggregation header and is included by the game PCH. Preserve its documented include order and existing affinity spans. New single-build headers and source files still carry their own whole-file `BT_CLIENT` or `BT_SERVER` guard.
- Keep implementation internals out of the aggregation: heavy private state in a header `Engine.h` aggregates belongs behind a `std::unique_ptr` and a forward declaration, its definition header included only by the owner's TUs and never by `Engine.h` — otherwise every edit to it invalidates the PCH and rebuilds both projects. Does not apply to collection SOA storage, where layout is CRC-load-bearing and an indirection is forbidden. Precedents: `FileManager`/`PackChunks`, `AudioManager`/`StaticVoices`+`StreamingVoices`. Weigh the churn: worth it when the internals' header changes often, not when the only gain is removing one stable header at the cost of rerouting every member access through an indirection.
- Allocation: Startup and teardown may allocate; main-loop allocation follows Memory (`Memory/AGENTS.md`).
- Determinism: FMA3 is disabled and SSE4.1 is required for cross-CPU CRC matching. Simulation threads must retain the required floating-point environment.
- Reuse counter: a counter incremented each time a slot, file, or connection is reused, so stale references can be detected. It appears in code as `epoch` in some subsystems and `generation` in others.

## Startup and Main Loop

- `LaunchOptions` owns command-line parsing. `--loopback-only` is independent of `--agent-port`; `--data-directory` must resolve to an existing absolute directory; port values are validated before conversion.
- Agent launches stay minimized and suppress physical client input while preserving the close/Alt+F4 escape path. Synthetic input and command transport live in Agent (`Agent/AGENTS.md`).
- Effective fullscreen resolves in one fixed order: the agent runtime override wins, then `--windowed WxH` forces windowed, then the saved `gFullscreen` preference applies. None of the three writes the saved preference, so an agent command or launch flag never rewrites what the user chose.
- Client startup waits for terrain elevation and priority textures before renderer construction, then primes each framebuffer before showing the window.
- `TextureUploadManager` and `FileManager` outlive `MainThread` so crash handling and teardown can use them. Device loss recreates `Graphics` in place.
- Client COM uses `RO_INIT_MULTITHREADED`. Process and worker priorities, worker counts, and the single-instance policy are set in `Main.cpp`; keep agent mode non-modal.

## Frame Ownership

- Client ring indices use `SnapshotIndex`; server ticks swap dual frame buffers. ID assignment is server-authoritative.
- Client rendering interpolates only populated coord rings and never extrapolates past committed state. `ResetClientState()` is the reset point for per-coord client counters.
- Client simulation advances only through reconcile and is capped by the limit on how fast the clock-adjustment loop may correct. Server update owns network pre-tick, save/load/replay, simulation, broadcast, resend, and autosave ordering. Before an armed debug replay capture reaches its pause target, it consumes at most one accumulated tick and returns the remainder to `TimeStep`; when the target pauses the update, use the normal accumulator-clearing semantics.
- Per-coord simulation dispatch uses pre-resolved `ActiveFrameRef` entries and thread-local state. Engine session runtimes own reusable network mechanics and phase order; game wrappers own gameplay policy and persistence. See game Source (`../../Projects/BrokenEngineSandbox/Source/AGENTS.md`).

## Crash Reporting

The exception path writes from fixed buffers because it is reachable during heap corruption. Do not add allocator-dependent formatting or path construction there.

## Subsystems

- Agent (`Agent/AGENTS.md`) - Harness command transport, shared command handlers, synthetic input, and UI snapshots
- Audio (`Audio/AGENTS.md`) - XAudio2 spatial audio
- File (`File/AGENTS.md`) - Packed assets and versioned files
- Frame (`Frame/AGENTS.md`) - Base frame state, terrain, navigation, and collections
- Graphics (`Graphics/AGENTS.md`) - Vulkan renderer
- Input (`Input/AGENTS.md`) - Client input
- Memory (`Memory/AGENTS.md`) - Allocator and tracking
- Network (`Network/AGENTS.md`) - ENet transport and discovery
- Profile (`Profile/AGENTS.md`) - CPU/GPU profiling
- Ui (`Ui/AGENTS.md`) - Runtime settings and screens
