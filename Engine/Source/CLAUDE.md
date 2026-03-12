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
Engine entry point managing initialization, main loop, and shutdown. Initializes diagnostic file logging (`FILE_LOG_INIT(0, ...)` with build-specific filenames, output to `../../../../DiagnosticLogs/` relative to the exe), creates the multithreading worker pool, constructs managers in dependency order, sets up the Windows window (client builds), and pre-renders all swapchain framebuffers before showing the window (client builds). Deterministic floating-point math (denormals flushed, round-to-nearest) is configured per-thread by `ThreadLocal`'s constructor (see Common/CLAUDE.md).

The entry point (`wWinMain`) enforces single-instance execution via a Win32 named mutex (controlled by `kbSingleInstance` from `Pch.h`) before any initialization occurs. The main loop is intentionally minimal — network orchestration has been moved into `GameBase::UpdateClient()`/`UpdateServer()` and `GameBase::Render()`. In client builds (`BT_CLIENT`), the main loop processes Windows messages, handles fullscreen toggling, updates input, then calls: `UpdateClient()` (which internally runs `PollAndReconcileClient`, physics, and `PostTickNetworkClient`), `Render()` (which internally runs rendering and `PostRenderNetworkClient`), and audio update. In server builds (`BT_SERVER`), the main loop calls: `UpdateServer(menuInput)` (which internally runs `PreTickNetwork`, quickload/replay, server tick wait, physics with per-frame server broadcasts, and quicksave), then `UpdateServerDisplayStats()` and repaints the GDI monitoring window via `InvalidateRect()`. `DeviceLostException` triggers full Graphics destruction and recreation (client only). The shared WndProc suppresses `SC_KEYMENU` via `WM_SYSCOMMAND` to prevent the Alt key system menu from blocking the main thread. The server WndProc additionally suppresses `SC_MOVE`, `SC_SIZE`, `SC_MAXIMIZE`, and `SC_RESTORE` to prevent modal loops from stalling the simulation. The server WndProc suppresses `WM_ERASEBKGND` and delegates `WM_PAINT` to `PaintServerDisplay()` (which uses GDI double buffering) for flicker-free stats and grid visualization. Unhandled exceptions invoke crash report functionality from CrashReport.h/cpp. Shutdown saves settings and destroys managers in reverse order via RAII.

### CrashReport.h/cpp
Crash report generation extracted from Main.cpp. `HandleException()` produces a crash report file containing the callstack (via StackWalker) and DxDiag system information. `ReadDxDiag()` launches the DxDiag utility and captures its XML output into a static string (`sDxDiag`) for inclusion in crash reports. Called by Main.cpp's unhandled exception handler.

### GameBase.h/cpp
Abstract base class for game implementations orchestrating fixed-rate physics ticks with variable-rate rendering across a multi-frame sparse grid keyed by `GridCoord`.

**`CoordFrames`**: Defined in `GameBase.h`, the unified per-coord frame storage struct combining frame data with reconciliation state. Contains `current`/`next` dual-buffered `unique_ptr<Frame>` for all builds. In client builds (`#ifdef BT_CLIENT`), also contains: a fixed-size `snapshots` ring buffer (array sized by `engine::kiTickRate`, each owning a `unique_ptr<Frame>`) with `iSnapshotHead` (physical index of oldest entry), `iSnapshotCount` (number of valid entries), and `iConfirmedOffset` (logical offset from head to the confirmed snapshot, -1 = none); per-coord `serverUpdates` map; an optional `PendingFullState` (owning a `unique_ptr<Frame>` with tick number); and a `uiGeneration` counter for stale-result detection. All per-coord state (frame data + reconciliation state) is stored in `mCoordFrames` (`unordered_map<GridCoord, CoordFrames>`).

**Architecture**: `mGameFlags` (`engine::GameFlags_t`) tracks global game state such as quit, replay, and main menu mode; game implementations query these flags (e.g., `InMainMenu()`) rather than deriving state from per-frame data. Owns the `mCoordFrames` map with swap-based dual-buffered updates. `TickFrames()` is a thin orchestrator that delegates to three protected helpers per physics tick: `PrepareServerTick()` (server-only: recomputes active set and ensures next frames for newly subscribed coords), `BuildAndDispatchFrameTicks()` (resolves frame references into workbuffer-allocated `ActiveFrameRef` spans and dispatches `RunFrameTick` in parallel per-Frame via `Dispatch()`, controlled by `kbEnableFrameDispatch`), and `FinalizeFrameTick()` (server harvest transfers, client extrapolation snapshot or normal frame swap, server broadcast, clear status changes). All five physics phases (Interpolate, PostRender, Collision, Transfer, Destroy/Spawn) are unified into `RunFrameTick()`. Pusher zone globals and Collision static members are `thread_local`, enabling safe parallel execution. Manages Frame ID assignment for per-Frame UUID generation without atomics, and owns the authoritative tick counter (exposed via `TickCounter()`) and simulation time. Save/load and replay functionality is delegated to the `GameSaveLoad` class (owned as `mGameSaveLoad` member, declared as a friend). `HarvestTransfers()` is called only in server builds (`#ifdef BT_SERVER`); the client receives cross-coord transfers as StatusChanges from the server rather than locally harvesting them.

**Client/Server Split**: `UpdateClient()` (client-only) and `UpdateServer(const MenuInput&)` (server-only) are the top-level per-frame update methods called from Main.cpp. In client builds, `UpdateClient()` calls `gpClientSession->PollAndReconcile()` (clock correction and reconciliation wait/apply) first, early-returns when in desync debug mode (checked via `game::gpClientSession->GetDesyncTick() >= 0`), runs physics ticks, and calls `gpClientSession->PostTick()` (network poll, reconcile kick, ACK send, flush) after physics. `Render()` (client-only) performs interpolated rendering and calls `gpClientSession->PostRender()` (post-render reconcile kick) at the end. `TickFramesAndRender()` (client-only, `#ifdef BT_CLIENT`) combines `UpdateClient()` and `Render()` into a single call. During extrapolation (connected to a server), the physics loop redirects `ActiveFrameRef` pointers to the per-coord snapshot ring buffer in `CoordFrames`: `PrepareExtrapolationTick()` advances the ring (reclaiming oldest pre-confirmed slots when full), `BuildExtrapolationFrameRef()` resolves per-coord references into the ring (called inside `BuildAndDispatchFrameTicks()`), and `RecordExtrapolationSnapshot()` captures CRCs after each tick (called inside `FinalizeFrameTick()`). The frame swap is skipped during extrapolation since the snapshot ring owns the frames. For rendering, `RenderFrame()` returns the latest snapshot frame for extrapolating coords, falling back to `mCoordFrames[coord].current`. In server builds, `UpdateServer()` calls `PreTickNetwork()` before physics, handles quickload/replay via `GameSaveLoad`, waits for the server tick timer, runs physics ticks (with per-tick `PrepareServerTick()` recomputing the active set so new client subscriptions from `FinalizeNewClients` on the previous frame are picked up immediately), and calls quicksave after physics. `FinalizeFrameTick()` calls `BroadcastServerTick()` which runs the server broadcast functions (FinalizeNewClients, DetectPlayerDeaths, BroadcastStatusChanges, HandleSubscriptionUpdates) followed by `NetworkServer::Flush()` to immediately dispatch all queued packets, ensuring clients receive updates for every simulated physics tick even when multiple physics steps run per tick. Exposes setter accessors (`SetTickCounter()`, `SetCurrentTime()`, `SetNextFrameId()`) alongside existing getters so that `ClientSession` can update game state during reconciliation without being a friend class.

**Input Processing**: `ProcessInput(bool bLostFocus, MenuInput&)` orchestrates per-frame input: in client builds, calls `gpInput->UpdateMenuInput()` (which internally calls `gpRawInputManager->Update()`) then `ProcessMenuInput()`. This consolidates the input chain into a single call from Main.cpp.

**Access Pattern**: Engine code accesses GameBase through the derived `game::gpGame` pointer (defined in Game.h), not through GameBase directly. Games override virtual methods for menu handling, save/load paths, and reset behavior.

**Frame Update Flow**: See [Frame/CLAUDE.md](Frame/CLAUDE.md) for the two-phase update pipeline (Interpolate then PostRender with seven sub-phases). See [File/CLAUDE.md](File/CLAUDE.md) for DifferenceStream replay details.

### GameSaveLoad.h/cpp
Extracted save/load/replay responsibility from GameBase. Owns `mpDifferenceStreamWriter` and `mpDifferenceStreamReader` for delta-compressed deterministic recording/playback with CRC validation. Provides `Quicksave()`/`Quickload()` for grid serialization (deterministic coordinate ordering), `SaveLoadReplay()` for replay start/stop, and `SyncReplay()` for per-frame replay synchronization. `WriteGrid()`/`ReadGrid()` are private helpers for serializing/deserializing the multi-frame grid. Replay loading reads metadata (human grid coordinate, player ID, armor) from a separate `.replay.meta` file, restricts the active set to only the human's grid coordinate, skips multi-frame transfers/EnsureNextFrames, and injects pending transfer StatusChanges into FrameInput between `LoadDifference()` and `ValidateChecksum()` for deterministic cross-cell replay. `ResetStreams()` clears both stream pointers (called by Game::Reset()). Holds a reference to `GameBase` (declared as a friend in GameBase) for access to frame state during save/load operations.

### Engine.h
Single aggregation header for all engine headers, analogous to `Common/Common.h`. Included by game `Pch.h` files. Organizes includes by subsystem (Debug, Profile, Frame, File, Network, Ui, Graphics, Audio, Input, Server) with `#ifdef BT_CLIENT` / `#ifdef BT_SERVER` guards so client-only headers (Graphics, Islands, Audio, Input, NetworkClient, ClientSessionBase) and server-only headers (ServerDisplay, NetworkServer, ServerSessionBase) are conditionally included. Shared headers (Frame, File, Network core, IslandTerrain, GameBase) are always included.

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
Deterministic game state with dual-buffered frames stored in `CoordFrames` structs (sparse grid keyed by `GridCoord` in `mCoordFrames`), fixed timestep updates, and three-phase render pipeline (BeginRender/Render/EndRender, client-only). Render orchestration (uniform buffer population) has moved to `/Graphics/Render/`. IslandTerrain provides stateless terrain collision queries (GlobalElevation/GlobalNormal) shared by both client and server. Visual-only collections are `#ifdef BT_CLIENT`; server builds compile only Explosions and Pushers.
- [Frame/CLAUDE.md](Frame/CLAUDE.md)
- [Frame/Collections/CLAUDE.md](Frame/Collections/CLAUDE.md) - SOA collection structures

### `/Graphics/` - Vulkan Rendering
Multi-pass Vulkan renderer with deferred lighting, shadows, and GPU particles. Entirely client-only (`#ifdef BT_CLIENT`), including Islands (GPU terrain rendering). Contains internal managers initialized in strict dependency order and CameraBase (client-only). Render orchestration (`Render/`) populates GPU uniform buffers split by subsystem (GlobalUniforms, MainUniforms, LightingUniforms, SmokeUniforms, WindUniforms). Terrain collision queries are in `/Frame/IslandTerrain` (shared).
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md)
- [Graphics/Managers/CLAUDE.md](Graphics/Managers/CLAUDE.md) - Manager implementations
- [Graphics/Objects/CLAUDE.md](Graphics/Objects/CLAUDE.md) - RAII Vulkan wrappers

### `/Input/` - Input System
Unified input handling via Raw Input API (keyboard) and DirectXTK (mouse/gamepad).
- [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Network/` - Networking
ENet-based reliable UDP networking with singleton NetworkManager owning library lifecycle and slot-based per-coord ENet channel layout. NetworkServer and NetworkClient handle server-side and client-side communication using a custom binary protocol with client-driven coord subscriptions, per-coord LZ4-compressed delta updates, per-coord ACK tracking, full state transfers, and re-send support. NetworkDiscovery provides UDP broadcast-based LAN server auto-detection (responder on server, scanner on client). Used by both client and server builds.
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
**Client** (`BT_CLIENT`): Main.cpp's loop is minimal: processes Windows messages, handles fullscreen toggle, calls `ProcessInput()` (input), then `UpdateClient()`, `Render()`, and audio update. Network orchestration is encapsulated in `ClientSession` (accessed via `game::gpClientSession`): `UpdateClient()` internally calls `gpClientSession->PollAndReconcile()` before physics and `gpClientSession->PostTick()` after physics; `Render()` internally calls `gpClientSession->PostRender()` after rendering. Desync debug mode is handled entirely within these ClientSession methods via early returns. Audio runs unconditionally (including during desync debug mode). Fixed-rate physics at the game-defined rate (e.g., 64Hz) with variable-rate rendering and interpolated frames between physics ticks.

**Server** (`BT_SERVER`): Main.cpp's loop calls `UpdateServer(menuInput)` then `UpdateServerDisplayStats()`. Network orchestration is encapsulated inside GameBase: `UpdateServer()` internally calls `PreTickNetwork()` (which polls the NetworkServer and NetworkDiscoveryResponder, handles disconnects, new clients, and spawn requests) before physics, handles quickload/replay, waits for the server tick timer, then runs physics with per-frame server broadcasts inside the physics loop, and calls quicksave after. After `UpdateServer()`, Main.cpp calls `UpdateServerDisplayStats()` to aggregate entity counts and memory stats, then repaints the GDI monitoring window every tick.

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
- Frame state uses `CoordFrames` structs in `mCoordFrames` map with dual-buffered `current`/`next` frames per grid coordinate for deterministic updates
- Lazy loading defers texture/audio data until first use
- All Vulkan resources managed via RAII wrappers
