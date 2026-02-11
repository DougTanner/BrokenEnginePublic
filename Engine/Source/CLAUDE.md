# `/Engine/Source/`

Core engine implementation with manager-based architecture. All major systems are singletons accessed via global pointers (e.g., `gpGraphics`, `gpAudioManager`).

## Global Manager Singletons

Managers created in `Main.cpp` in strict dependency order:

| Manager | Global Pointer | Purpose |
|---------|---------------|---------|
| **TextureUploadManager** | `gpTextureUploadManager` | Background GPU texture uploads via transfer queue |
| **FileManager** | `gpFileManager` | Asset loading, save/load, lazy chunk loading |
| **ProfileManager** | `gpProfileManager` | CPU/GPU performance profiling |
| **Graphics** | `gpGraphics` | Vulkan rendering orchestration |
| **AudioManager** | `gpAudioManager` | 3D spatial audio via XAudio2 |
| **RawInputManager** | `gpRawInputManager` | Keyboard/mouse/gamepad input |

## Core Files

### Main.cpp
Engine entry point managing initialization, main loop, and shutdown.

**Initialization**: Creates stack-local log buffer and workbuffer memory, constructs `ThreadLocal` from these references. Creates managers in dependency order, sets up Windows window, configures DPI awareness, loads settings. Memory allocation is handled by the Memory subsystem (see Memory/CLAUDE.md).

**Main Loop**: Processes Windows messages with `PeekMessage()` during active frame processing, handles fullscreen toggling, updates input managers, delegates to game for frame updates and rendering, updates audio. Blocks on `GetMessage()` when window loses focus to reduce CPU usage.

**Shutdown**: Saves settings, destroys managers in reverse order via RAII.

**Exception Handling**: Catches unhandled exceptions, generates crash reports with callstack and DxDiag info, saves to desktop or AppData.

**Time Reset**: `engine::ResetRealTime()` free function resets real-time clocks across AudioManager, Camera, and game TimeStep. Called when resuming from pause or loading saves to prevent time jumps.

### GameBase.h/cpp
Abstract base class for game implementations using fixed timestep physics.

**Purpose**: Orchestrates game loop with fixed-rate physics updates and variable-rate rendering. Manages Frame ID assignment for per-Frame UUID generation.

**Architecture**: Dual-buffered frame state (Current/Next) with swap-based updates. TimeStep class accumulates real-time into discrete physics steps. Each Frame receives a unique Frame ID at creation via `GenerateFrameId()`, enabling per-Frame UUID generation without atomics.

**Flag Enums**: `MenuFlags` controls UI visibility and frame updates. `GameFlags` tracks high-level game state (quit requests, replay save/load, frame update status). Both use type-safe `common::Flags<>` wrapper.

**Access Pattern**: Engine code accesses GameBase functionality through the derived `game::gpGame` pointer (defined in Game.h), not through GameBase directly. This allows engine code to include game headers and use game-specific extensions.

**Related Free Functions**: `engine::ResetRealTime()` resets real-time clocks across AudioManager, Camera, and game TimeStep. Called when resuming from pause, loading saves, or after GPU device recreation to prevent time jumps.

**Async Rendering**: Uses `gpGraphics->mRenderFuture` (a `PersistentWorker`) to dispatch `RenderMainPresentAcquire()` asynchronously via `Wake()`, with the main thread calling `WaitForRender()` (which calls `Wait()`) before the next frame's global rendering begins. Captures the command buffer index and passes `*gpGraphics->mpFrameInterpolate` on the main thread before async dispatch.

**Frame Update Flow**:
- `UpdateFramesAndRender()` calculates required physics steps from accumulated time
- For each step: update replay streams, execute frame update phases, swap buffers
- After full steps: create interpolated frame for smooth rendering between physics ticks
- Two-phase update: Interpolate (time, positions, state) → PostRender (six sub-phases: Update, PreCollision, PostCollision, AreaDamage, Destroy, Spawn)

**Replay System**: DifferenceStream objects enable deterministic replay with validation. Records input changes with frame numbers, storing only frames where input changed for efficient storage. Captures CRCs of game state at every frame during recording. During replay, validates current state CRC against recorded values, triggering debug break on mismatch to detect non-determinism issues.

**Template Methods**: `PreUpdate()` uses Template Method pattern - base handles common logic (vibration, time reset on focus loss) and calls pure virtual `ProcessMenuInput()` for game-specific menu handling.

**Virtual Methods**: Games override `Reset()`, `ShouldUpdateFrame()`, `ProcessMenuInput()`, and file path methods for save/load/replay functionality.

### Pch.cpp
Precompiled header compilation unit.

## Subsystems

### `/Audio/` - 3D Spatial Audio
XAudio2-based spatial audio system with voice pooling and lazy loading.
- [Audio/CLAUDE.md](Audio/CLAUDE.md)

### `/Debug/` - Debug Utilities
Vulkan enum-to-string conversions for error messages (uses `if constexpr (kbEnableLogging)`).
- [Debug/CLAUDE.md](Debug/CLAUDE.md)

### `/File/` - Asset & Save System
Centralized file I/O with eager/lazy asset loading and versioned save files.
- Eager: Fonts, Scenes, Islands, Models, Shaders
- Lazy: Audio, Textures (background thread)
- [File/CLAUDE.md](File/CLAUDE.md)

### `/Frame/` - Game State Management
Deterministic game state with dual-buffered frames and fixed timestep updates.
- [Frame/CLAUDE.md](Frame/CLAUDE.md)
- [Frame/Collections/CLAUDE.md](Frame/Collections/CLAUDE.md) - SOA collection structures

### `/Graphics/` - Vulkan Rendering
Multi-pass Vulkan renderer with deferred lighting, shadows, and GPU particles. Contains internal managers that must be initialized in strict dependency order. Also contains CameraBase (abstract camera with view/projection matrices and frustum culling) and Islands (terrain heightmap system).
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md)
- [Graphics/Managers/CLAUDE.md](Graphics/Managers/CLAUDE.md) - Manager implementations
- [Graphics/Objects/CLAUDE.md](Graphics/Objects/CLAUDE.md) - RAII Vulkan wrappers

### `/Input/` - Input System
Unified input handling via Raw Input API (keyboard) and DirectXTK (mouse/gamepad).
- [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Memory/` - Memory Allocation
Global mimalloc allocator (or CRT debug heap) with operator new/delete overloads, per-frame allocation profiling, and `ScopedSuppressAllocationTracking` for excluding expected allocation noise from profiling.
- [Memory/CLAUDE.md](Memory/CLAUDE.md)

### `/Profile/` - Performance Profiling
CPU/GPU performance tracking using Vulkan timestamp queries. ProfileManager is always instantiated; methods use `if constexpr (kbEnableProfiling)` for compile-time elimination.
- [Profile/CLAUDE.md](Profile/CLAUDE.md)

### `/ThirdParty/` - External Library Integrations
Compilation units for third-party libraries: DirectXTK (mouse/gamepad), StackWalker (callstacks), Volk (Vulkan loader).
- [ThirdParty/CLAUDE.md](ThirdParty/CLAUDE.md)

### `/Ui/` - User Interface
Runtime-adjustable parameter wrappers for graphics, audio, and gameplay settings.
- [Ui/CLAUDE.md](Ui/CLAUDE.md)

## Architecture Overview

### Initialization Order
Managers must be created in strict dependency order:
1. TextureUploadManager → 2. FileManager → 3. ProfileManager → 4. Graphics (internal managers) → 5. AudioManager → 6. RawInputManager

### Main Loop Flow
Each frame processes Windows messages, handles fullscreen toggle, updates input systems (RawInputManager → game input conversion), determines if frame updates needed, executes physics steps with replay handling if required, renders current/interpolated frame, updates audio.

Fixed 250Hz physics updates run via TimeStep accumulation. Each physics step updates replay streams, executes two-phase update (Interpolate → PostRender with Update/PreCollision/PostCollision/AreaDamage/Destroy/Spawn sub-phases), swaps buffers. Rendering occurs at variable rate with interpolated frames between physics ticks.

### Threading Model
All async threads allocate their own log buffer and workbuffer memory (as stack-local or static variables) and construct a `common::ThreadLocal` from these references with a `Threads` enum identifier for logging and diagnostics.
- **Main Thread**: Window messages, input, game logic, Vulkan command recording
- **Render Thread** (`kThreadRender`): `PersistentWorker` owned by Graphics (`mRenderFuture`) - `RenderMainPresentAcquire()` dispatched via `Wake()` at time-critical priority while main thread continues processing
- **Submit Global Thread** (`kThreadSubmitGlobal`): `PersistentWorker` owned by CommandBufferManager (`mSubmitGlobal`) for async global command buffer queue submission at time-critical priority
- **Submit Main Thread** (`kThreadSubmitMain`): `PersistentWorker` owned by CommandBufferManager (`mSubmitMain`) for async main command buffer queue submission at time-critical priority, waits on global submission
- **Present Thread** (`kThreadPresent`): `PersistentWorker` owned by SwapchainManager (`mPresent`) for async swapchain presentation at time-critical priority, waits on main submission
- **Screenshot Thread** (`kThreadScreenshot`): Async JPEG encoding and file save for screenshot capture
- **Disk Loading Threads**: Eager and lazy asset loading from disk via FileManager
- **Texture Upload Thread** (`kThreadTextureUpload`): GPU texture uploads via transfer queue (TextureUploadManager)
- **GPU**: Asynchronous execution with multiple frames in flight

### Memory Patterns
- Frame state uses dual buffering for deterministic updates
- Lazy loading defers texture/audio data until first use
- All Vulkan resources managed via RAII wrappers
