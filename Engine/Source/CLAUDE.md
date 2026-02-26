# `/Engine/Source/`

Core engine implementation with manager-based architecture. All major systems are singletons accessed via global pointers (e.g., `gpGraphics`, `gpAudioManager`).

## Global Manager Singletons

Managers created in strict dependency order (see Initialization Order below):

| Manager | Global Pointer | Purpose |
|---------|---------------|---------|
| **TextureUploadManager** | `gpTextureUploadManager` | Background GPU texture uploads via transfer queue |
| **FileManager** | `gpFileManager` | Asset loading, save/load, lazy chunk loading |
| **ProfileManager** | `gpProfileManager` | CPU/GPU performance profiling |
| **Graphics** | `gpGraphics` | Vulkan rendering orchestration |
| **AudioManager** | `gpAudioManager` | 3D spatial audio via XAudio2 |
| **NetworkManager** | `gpNetworkManager` | ENet reliable UDP networking |
| **RawInputManager** | `gpRawInputManager` | Keyboard/mouse/gamepad input |

## Core Files

### Main.cpp
Engine entry point managing initialization, main loop, and shutdown. Configures deterministic floating-point math (FMA3 disabled, denormals flushed, round-to-nearest), creates the multithreading worker pool, constructs managers in dependency order, sets up the Windows window (client builds), and pre-renders all swapchain framebuffers before showing the window (client builds).

The entry point (`wWinMain`) enforces single-instance execution via a Win32 named mutex (controlled by `kbSingleInstance` from `Pch.h`) before any initialization occurs. In client builds (`BT_CLIENT`), the main loop processes Windows messages, handles fullscreen toggling, updates input, polls the network client and runs rollback-and-replay before physics/rendering via `UpdateFramesAndRender()`, sends network input afterward, and updates audio. In server builds (`BT_SERVER`), the main loop creates Islands independently (no Graphics), creates a `NetworkServer`, and integrates networking into each tick: polls the network, handles new clients and spawn requests before `UpdateFramesOnly()`, then finalizes spawns, updates client subscriptions, and broadcasts state changes afterward. Sleeps between physics ticks using a high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` + `MsgWaitForMultipleObjects`) to yield CPU instead of busy-waiting. Repaints the GDI monitoring window every tick via `InvalidateRect()`. The server WndProc suppresses `WM_ERASEBKGND` and delegates `WM_PAINT` to `PaintServerDisplay()` (which uses GDI double buffering) for flicker-free stats and grid visualization. Blocks on `GetMessage()` when the window loses focus to reduce CPU usage (client only). `DeviceLostException` triggers full Graphics destruction and recreation (client only). Unhandled exceptions generate crash reports with callstack and DxDiag info. Shutdown saves settings and destroys managers in reverse order via RAII.

`engine::ResetRealTime()` resets real-time clocks across AudioManager and Camera (when `BT_CLIENT` is defined) and game TimeStep. Called when resuming from pause, loading saves, or after GPU device recreation to prevent time jumps.

### GameBase.h/cpp
Abstract base class for game implementations orchestrating fixed-rate physics updates with variable-rate rendering across a multi-frame sparse grid keyed by `GridCoord`.

**Architecture**: Owns map-based dual-buffered frame collections with swap-based updates. The update loop operates on active grid coordinates computed by the game, with parallelizable phases dispatched across coordinates via `Dispatch()` and collision phases running sequentially (static storage). Pre-resolves frame references into workbuffer-allocated `ActiveFrameRef` spans to avoid repeated map lookups across phases. Manages Frame ID assignment for per-Frame UUID generation without atomics, and owns the authoritative frame counter (exposed via `FrameCounter()`) and simulation time. Provides grid serialization for save/load (deterministic coordinate ordering) and a replay system using DifferenceStream for delta-compressed deterministic recording/playback with CRC validation. Replay loading reads metadata (human grid coordinate, player ID, armor) from a separate `.replay.meta` file, restricts the active set to only the human's grid coordinate, skips multi-frame transfers/EnsureNextFrames, and injects pending transfer StatusChanges into FrameInput between `LoadDifference()` and `ValidateChecksum()` for deterministic cross-cell replay.

**Client/Server Split**: `UpdateFramesAndRender()` (client-only, `#ifdef BT_CLIENT`) runs the full physics-plus-rendering loop with interpolated frames and GPU rendering. `UpdateFramesOnly()` runs physics updates only, suitable for headless server builds. Both share the same fixed-timestep update logic and frame management. In server builds, status-change draining and replay sync are guarded out (`#ifndef BT_SERVER`), since the server manages status changes through its own networking pipeline.

**Access Pattern**: Engine code accesses GameBase through the derived `game::gpGame` pointer (defined in Game.h), not through GameBase directly. Games override virtual methods for menu handling, save/load paths, frame update control, and reset behavior.

**Frame Update Flow**: See [Frame/CLAUDE.md](Frame/CLAUDE.md) for the two-phase update pipeline (Interpolate then PostRender with seven sub-phases). See [File/CLAUDE.md](File/CLAUDE.md) for DifferenceStream replay details.

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
Deterministic game state with map-based dual-buffered frames (sparse grid keyed by `GridCoord`), fixed timestep updates, and three-phase render pipeline (BeginRender/Render/EndRender, client-only). Visual-only collections are `#ifdef BT_CLIENT`; server builds compile only Explosions and Pushers.
- [Frame/CLAUDE.md](Frame/CLAUDE.md)
- [Frame/Collections/CLAUDE.md](Frame/Collections/CLAUDE.md) - SOA collection structures

### `/Graphics/` - Vulkan Rendering
Multi-pass Vulkan renderer with deferred lighting, shadows, and GPU particles. Client-only (`#ifdef BT_CLIENT`) except Islands (terrain heightmap system, shared with server for collision). Contains internal managers initialized in strict dependency order and CameraBase (client-only).
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md)
- [Graphics/Managers/CLAUDE.md](Graphics/Managers/CLAUDE.md) - Manager implementations
- [Graphics/Objects/CLAUDE.md](Graphics/Objects/CLAUDE.md) - RAII Vulkan wrappers

### `/Input/` - Input System
Unified input handling via Raw Input API (keyboard) and DirectXTK (mouse/gamepad).
- [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Network/` - Networking
ENet-based reliable UDP networking with singleton NetworkManager owning library lifecycle. NetworkServer and NetworkClient handle server-side and client-side communication using a custom binary protocol with LZ4-compressed delta updates filtered per client's active grid region, full state transfers, and re-send support. Used by both client and server builds.
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
1. TextureUploadManager (client-only) -> 2. FileManager -> 3. ProfileManager -> 4. AudioManager (client-only) -> 5. NetworkManager -> 6. RawInputManager (client-only) -> 7. Graphics (internal managers, client-only)

### Main Loop Flow
**Client** (`BT_CLIENT`): Each frame processes Windows messages, handles fullscreen toggle, updates input (RawInputManager then game input conversion), polls the network client and performs rollback-and-replay (restoring confirmed state and re-simulating with server updates), runs fixed-rate physics steps with replay handling, renders current/interpolated frame, sends network input, and updates audio. Fixed-rate physics at the game-defined rate (e.g., 64Hz) with variable-rate rendering and interpolated frames between physics ticks.

**Server** (`BT_SERVER`): Runs headless without graphics or audio. Each tick: polls `NetworkServer`, calls Game server methods to handle new clients and spawn requests, runs `UpdateFramesOnly()` for physics, then finalizes spawns, updates client subscriptions for grid transfers, and broadcasts delta updates to all clients. Sleeps between ticks via a high-resolution waitable timer to yield CPU while remaining responsive to window messages. Displays a GDI monitoring window showing simulation stats (including connected client count) and a grid map with client position highlighting, repainted every tick.

### Threading Model
All async threads construct a `common::ThreadLocal` with a `Threads` enum identifier. ThreadLocal owns its backing memory internally.
- **Main Thread**: Window messages, input, game logic, Vulkan command recording
- **Multithreading Pool**: Worker pool for parallel dispatch of data-parallel work across active grid coordinates
- **Render Thread**: Async `RenderMainPresentAcquire()` at time-critical priority while main thread continues
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
