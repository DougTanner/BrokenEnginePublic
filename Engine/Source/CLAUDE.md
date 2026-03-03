# `/Engine/Source/`

See also: [System Overview](../../Documents/Architecture/SystemOverview.md)

Core engine implementation with manager-based architecture. All major systems are singletons accessed via global pointers (e.g., `gpGraphics`, `gpAudioManager`).

## Global Manager Singletons

Managers created in strict dependency order (see Initialization Order below):

| Manager | Global Pointer | Purpose |
|---------|---------------|---------|
| **TextureUploadManager** | `gpTextureUploadManager` | Background GPU texture uploads via transfer queue |
| **FileManager** | `gpFileManager` | Asset loading, save/load, lazy chunk loading |
| **IslandTerrain** | `gpIslandTerrain` | CPU terrain collision (shared client/server) |
| **ProfileManager** | `gpProfileManager` | CPU/GPU performance profiling |
| **Graphics** | `gpGraphics` | Vulkan rendering orchestration |
| **AudioManager** | `gpAudioManager` | 3D spatial audio via XAudio2 |
| **NetworkManager** | `gpNetworkManager` | ENet reliable UDP networking |
| **RawInputManager** | `gpRawInputManager` | Keyboard/mouse/gamepad input |

## Core Files

### Main.cpp
Engine entry point managing initialization, main loop, and shutdown. Initializes diagnostic file logging (`FILE_LOG_INIT(0, ...)` with build-specific filename, and `FILE_LOG_INIT(1, ...)` for position tracking in client builds, output to `../../../../DiagnosticLogs/` relative to the exe), creates the multithreading worker pool, constructs managers in dependency order, sets up the Windows window (client builds), and pre-renders all swapchain framebuffers before showing the window (client builds). Deterministic floating-point math (denormals flushed, round-to-nearest) is configured per-thread by `ThreadLocal`'s constructor (see Common/CLAUDE.md).

The entry point (`wWinMain`) enforces single-instance execution via a Win32 named mutex (controlled by `kbSingleInstance` from `Pch.h`) before any initialization occurs. In client builds (`BT_CLIENT`), the main loop processes Windows messages, handles fullscreen toggling, updates input, captures local input, then checks for desync debug mode (`GetDesyncFrame() >= 0`): if active, the loop only polls the network client, flushes, and renders (skipping reconciliation, physics, input sending, and audio) to keep the window responsive while waiting for the server's debug frame response. Otherwise, the normal path waits for and applies the async reconciliation result via `WaitForReconcile()` with soft clock correction computed first (`ComputeClockCorrectionNs()` adjusts `mUpdateRemainderNs` to keep the client behind the server by RTT/2 + 1 frames), then frame deficit compensation (the frame counter delta from reconciliation, reduced by the clock error so rollbacks help correction converge rather than fighting it, is added to `mUpdateRemainderNs` so the physics loop doesn't re-advance past the reconciled position, and `miSkipSnapshotSteps` is set to the adjusted deficit so compensation frames don't store snapshots), runs physics via `UpdateFrames()`, polls the network client via `PollNetworkClient()` and kicks the next reconciliation via `TryKickReconcile()` (between physics and render, so the async reconcile worker gets the freshest network data and operates on the latest extrapolated snapshots), sends input to the server and flushes the network client (after physics and poll/kick, so the input packet carries the most up-to-date ACK state), renders via `Render()`, then updates audio. `UpdateFramesAndRender()` still exists as a convenience that calls `UpdateFrames()` then `Render()`, but Main.cpp calls them separately to insert poll+kick+send between them. In server builds (`BT_SERVER`), the main loop creates a `NetworkServer`, and integrates networking into each tick: polls the network, handles disconnects, new clients, and spawn requests before `UpdateFrames()`, which runs physics and per-frame server broadcasts internally. Sleeps between physics ticks using a high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` + `MsgWaitForMultipleObjects`) to yield CPU instead of busy-waiting. After physics, calls `UpdateServerDisplayStats()` to aggregate entity counts and memory stats, then repaints the GDI monitoring window every tick via `InvalidateRect()`. The server WndProc suppresses `WM_ERASEBKGND` and delegates `WM_PAINT` to `PaintServerDisplay()` (which uses GDI double buffering) for flicker-free stats and grid visualization. When `bIgnoreFocus` is false, blocks on `GetMessage()` when the window loses focus to reduce CPU usage; when `bIgnoreFocus` is true, returns `!sbHasFocus` without blocking (client only). In network mode, `ProcessMessages()` is called with `bIgnoreFocus=true` so the client continues running when alt-tabbed. `DeviceLostException` triggers full Graphics destruction and recreation (client only). Unhandled exceptions generate crash reports with callstack and DxDiag info. Shutdown saves settings and destroys managers in reverse order via RAII.

`engine::ResetRealTime()` resets real-time clocks across AudioManager and Graphics render-frame timer (when `BT_CLIENT` is defined) and game TimeStep. Called when resuming from pause, loading saves, or after GPU device recreation to prevent time jumps.

### GameBase.h/cpp
Abstract base class for game implementations orchestrating fixed-rate physics updates with variable-rate rendering across a multi-frame sparse grid keyed by `GridCoord`.

**Architecture**: Owns map-based dual-buffered frame collections with swap-based updates. The update loop operates on active grid coordinates computed by the game: Interpolate, Transfer, and Destroy/Spawn phases are parallelized via `Dispatch()`; PostRender::Update runs sequentially (pusher zone static storage); Collision phases run sequentially (static storage). Pre-resolves frame references into workbuffer-allocated `ActiveFrameRef` spans to avoid repeated map lookups across phases. Manages Frame ID assignment for per-Frame UUID generation without atomics, and owns the authoritative frame counter (exposed via `FrameCounter()`) and simulation time. Provides grid serialization for save/load (deterministic coordinate ordering) and a replay system using DifferenceStream for delta-compressed deterministic recording/playback with CRC validation. Replay loading reads metadata (human grid coordinate, player ID, armor) from a separate `.replay.meta` file, restricts the active set to only the human's grid coordinate, skips multi-frame transfers/EnsureNextFrames, and injects pending transfer StatusChanges into FrameInput between `LoadDifference()` and `ValidateChecksum()` for deterministic cross-cell replay.

**Client/Server Split**: `UpdateFramesAndRender()` (client-only, `#ifdef BT_CLIENT`) runs the full physics-plus-rendering loop with interpolated frames and GPU rendering. In network mode, after each physics frame it calls `StoreExtrapolatedSnapshot()` to capture per-frame CRCs and serialized state for the reconciliation CRC fast-path; snapshot storage is suppressed during frame deficit compensation frames (via `miSkipSnapshotSteps`) to prevent CRC mismatches during subsequent reconciliations. `UpdateFrames()` runs physics updates only, suitable for headless server builds. Both share the same fixed-timestep update logic and frame management. In server builds, status-change draining and replay sync are guarded out (`#ifndef BT_SERVER`), since the server manages status changes through its own networking pipeline. In server builds, `UpdateFrames()` calls the server broadcast functions (FinalizeNewClientsServer, BroadcastStatusChangesServer, HandleSubscriptionUpdatesServer) inside the physics loop after each frame's updates, followed by `NetworkServer::Flush()` to immediately dispatch all queued packets, ensuring clients receive updates for every simulated physics frame even when multiple physics steps run per tick.

**Access Pattern**: Engine code accesses GameBase through the derived `game::gpGame` pointer (defined in Game.h), not through GameBase directly. Games override virtual methods for menu handling, save/load paths, frame update control, and reset behavior.

**Frame Update Flow**: See [Frame/CLAUDE.md](Frame/CLAUDE.md) for the two-phase update pipeline (Interpolate then PostRender with seven sub-phases). See [File/CLAUDE.md](File/CLAUDE.md) for DifferenceStream replay details.

### Engine.h
Single aggregation header for all engine headers, analogous to `Common/Common.h`. Included by game `Pch.h` files. Organizes includes by subsystem (Debug, Profile, Frame, File, Network, Ui, Graphics, Audio, Input, Server) with `#ifdef BT_CLIENT` / `#ifdef BT_SERVER` guards so client-only headers (Graphics, Islands, Audio, Input, NetworkClient) and server-only headers (ServerDisplay, NetworkServer) are conditionally included. Shared headers (Frame, File, Network core, IslandTerrain, GameBase) are always included.

### Pch.cpp
Precompiled header compilation unit.

## Subsystems

### `/Audio/` - 3D Spatial Audio
XAudio2-based spatial audio system with voice pooling and lazy loading. Client-only (`#ifdef BT_CLIENT`).
- [Audio/CLAUDE.md](Audio/CLAUDE.md)

### `/Debug/` - Debug Utilities
Vulkan enum-to-string conversions for error messages (uses `if constexpr (kbEnableLogging)`).
- [Debug/CLAUDE.md](Debug/CLAUDE.md)

### `/File/` - Asset & Save System
Centralized file I/O with eager/lazy asset loading, versioned save files, and DifferenceStream replay.
- [File/CLAUDE.md](File/CLAUDE.md)

### `/Frame/` - Game State Management
Deterministic game state with map-based dual-buffered frames (sparse grid keyed by `GridCoord`), fixed timestep updates, and three-phase render pipeline (BeginRender/Render/EndRender, client-only). IslandTerrain provides stateless terrain collision queries (GlobalElevation/GlobalNormal) shared by both client and server. Visual-only collections are `#ifdef BT_CLIENT`; server builds compile only Explosions and Pushers.
- [Frame/CLAUDE.md](Frame/CLAUDE.md)
- [Frame/Collections/CLAUDE.md](Frame/Collections/CLAUDE.md) - SOA collection structures

### `/Graphics/` - Vulkan Rendering
Multi-pass Vulkan renderer with deferred lighting, shadows, and GPU particles. Entirely client-only (`#ifdef BT_CLIENT`), including Islands (GPU terrain rendering). Contains internal managers initialized in strict dependency order and CameraBase (client-only). Terrain collision queries are in `/Frame/IslandTerrain` (shared).
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md)
- [Graphics/Managers/CLAUDE.md](Graphics/Managers/CLAUDE.md) - Manager implementations
- [Graphics/Objects/CLAUDE.md](Graphics/Objects/CLAUDE.md) - RAII Vulkan wrappers

### `/Input/` - Input System
Unified input handling via Raw Input API (keyboard) and DirectXTK (mouse/gamepad).
- [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Network/` - Networking
ENet-based reliable UDP networking with singleton NetworkManager owning library lifecycle. NetworkServer and NetworkClient handle server-side and client-side communication using a custom binary protocol with LZ4-compressed delta updates filtered per client's active grid region, full state transfers, and re-send support. NetworkDiscovery provides UDP broadcast-based LAN server auto-detection (responder on server, scanner on client). Used by both client and server builds.
- [Network/CLAUDE.md](Network/CLAUDE.md)

### `/Memory/` - Memory Allocation
Global mimalloc allocator (or CRT debug heap) with operator new/delete overloads, per-frame allocation profiling, and `ScopedSuppressAllocationTracking` for expected allocations.
- [Memory/CLAUDE.md](Memory/CLAUDE.md)

### `/Profile/` - Performance Profiling
CPU/GPU performance tracking. Always instantiated; methods use `if constexpr (kbEnableProfiling)` for compile-time elimination. GPU timing via Vulkan timestamp queries and the profile text overlay are client-only (`#ifdef BT_CLIENT`); CPU timers, counters, and boot timers compile in both builds.
- [Profile/CLAUDE.md](Profile/CLAUDE.md)

### `/Server/` - Server Display
GDI-based monitoring window for headless server builds, showing simulation stats and an auto-scaling grid map. Server-only (`#ifdef BT_SERVER`).
- [Server/CLAUDE.md](Server/CLAUDE.md)

### `/ThirdParty/` - External Library Integrations
Compilation units for third-party libraries: DirectXTK (mouse/gamepad), StackWalker (callstacks), Volk (Vulkan loader).

### `/Ui/` - User Interface
Runtime-adjustable parameter wrappers for graphics, audio, and gameplay settings.
- [Ui/CLAUDE.md](Ui/CLAUDE.md)

## Architecture Overview

### Initialization Order
Managers must be created in strict dependency order. Client builds create all managers; server builds skip Graphics, AudioManager, and RawInputManager (all `#ifdef BT_CLIENT`):
1. TextureUploadManager (client-only) -> 2. FileManager -> 3. IslandTerrain (shared, before Graphics) -> 4. ProfileManager -> 5. AudioManager (client-only) -> 6. NetworkManager -> 7. RawInputManager (client-only) -> 8. Graphics (internal managers including Islands, client-only)

### Main Loop Flow
**Client** (`BT_CLIENT`): Each frame processes Windows messages (in network mode, passes `bIgnoreFocus=true` to avoid blocking on `GetMessage()` when alt-tabbed), handles fullscreen toggle, updates input (RawInputManager then game input conversion), captures local input, then checks for desync debug mode: if `GetDesyncFrame() >= 0`, the loop only polls the network client, flushes, and renders (skipping reconciliation, physics, input sending, and audio) to keep the window responsive while awaiting the server's debug frame response. Otherwise, the normal path waits for and applies the async reconciliation result via `WaitForReconcile()` with soft clock correction computed first (proportionally adjusts `mUpdateRemainderNs` to keep the client behind the server by RTT/2 + 1 frames, enabling the CRC fast-path to succeed routinely), then frame deficit compensation (adds the number of frames rewound by reconciliation, reduced by the clock error so rollbacks help correction converge, to `mUpdateRemainderNs` so the subsequent physics loop does not re-advance past the reconciled position, and sets `miSkipSnapshotSteps` to the adjusted deficit to suppress snapshot storage during those compensation frames), runs fixed-rate physics steps with replay handling via `UpdateFrames()`, polls the network client via `PollNetworkClient()` and kicks the next reconciliation via `TryKickReconcile()` (between physics and render, so the async worker gets the freshest network data and latest extrapolated snapshots), sends input to the server via `SendNetworkInput()` and flushes the network client (`NetworkClient::Flush()`) to immediately dispatch queued packets (after physics and poll/kick, so the input packet carries the most up-to-date ACK state), renders current/interpolated frame via `Render()`, then updates audio. `UpdateFramesAndRender()` still exists as a convenience that calls `UpdateFrames()` then `Render()`, but Main.cpp calls them separately to insert poll+kick+send between physics and render. Two dedicated CPU timers (`kCpuTimerNetworkPollReconcile` and `kCpuTimerNetworkSend`) bracket the network phases. Fixed-rate physics at the game-defined rate (e.g., 64Hz) with variable-rate rendering and interpolated frames between physics ticks.

**Server** (`BT_SERVER`): Runs headless without graphics or audio. Each tick: polls `NetworkServer` and the `NetworkDiscoveryResponder` (for LAN discovery), calls Game server methods to handle disconnects, new clients, and spawn requests, then runs `UpdateFrames()` which executes physics and per-frame server broadcasts (finalize spawns, update subscriptions, broadcast delta updates) inside the physics loop for each simulated frame. Sleeps between ticks via a high-resolution waitable timer to yield CPU while remaining responsive to window messages. After physics, calls `UpdateServerDisplayStats()` to aggregate entity counts and memory stats, then displays a GDI monitoring window showing simulation stats (including connected client count) and a grid map with client position highlighting, repainted every tick.

### Threading Model
All async threads construct a `common::ThreadLocal` with a `Threads` enum identifier. ThreadLocal owns its backing memory internally.
- **Main Thread**: Window messages, input, game logic, Vulkan command recording, main rendering
- **Multithreading Pool**: Worker pool for parallel dispatch of data-parallel work across active grid coordinates
- **Submit Threads** (Global + Main): Async command buffer queue submission at time-critical priority
- **Present Thread**: Async swapchain presentation at time-critical priority, waits on main submission
- **Screenshot Thread**: Async JPEG encoding and file save
- **Disk Loading Threads**: Eager and lazy asset loading from disk via FileManager
- **Texture Upload Thread**: GPU texture uploads via transfer queue
- **GPU**: Asynchronous execution with multiple frames in flight

### Memory Patterns
- Frame state uses map-based dual buffering (per-grid-coordinate) for deterministic updates
- Lazy loading defers texture/audio data until first use
- All Vulkan resources managed via RAII wrappers
