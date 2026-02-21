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

**Initialization**: Constructs `ThreadLocal` with a 10MB workbuffer. Creates `common::Multithreading` worker pool sized to `(hardware cores - 2)` for parallel dispatch. Disables CRT FMA3 auto-detection via `_set_FMA3_enable(0)`, flushes denormals, and sets round-to-nearest mode for deterministic floating-point math across different CPUs. Creates managers in dependency order, sets up Windows window, configures DPI awareness, loads settings. After all managers and game objects are constructed, calls `game::gpCamera->Update()` with the initial frame interpolation data before the pre-render framebuffer loop, ensuring the camera matrices are valid for the first frame rendered to each swapchain framebuffer. Memory allocation is handled by the Memory subsystem (see Memory/CLAUDE.md).

**Main Loop**: Processes Windows messages with `PeekMessage()` during active frame processing, handles fullscreen toggling, updates input managers, delegates to game for frame updates and rendering, updates audio. Blocks on `GetMessage()` when window loses focus to reduce CPU usage.

**Shutdown**: Saves settings, destroys managers in reverse order via RAII.

**Exception Handling**: `DeviceLostException` is caught in the main loop, triggering full Graphics destruction and recreation (reset + re-construct) followed by `ResetRealTime()` to prevent time jumps. Unhandled exceptions generate crash reports with callstack and DxDiag info, saved to desktop or AppData.

**Time Reset**: `engine::ResetRealTime()` free function resets real-time clocks across AudioManager, Camera, and game TimeStep. Called when resuming from pause, loading saves, or after GPU device recreation to prevent time jumps.

### GameBase.h/cpp
Abstract base class for game implementations using fixed timestep physics.

**Purpose**: Orchestrates game loop with fixed-rate physics updates and variable-rate rendering. Manages Frame ID assignment for per-Frame UUID generation. Owns the authoritative frame counter (`miFrameCounter`) and simulation time (`mfCurrentTime`), incrementing them centrally each physics step and writing them into every active frame's Interpolate data.

**Architecture**: Map-based dual-buffered frame collections (`mCurrentFrames`/`mNextFrames` as `std::unordered_map<GridCoord, std::unique_ptr<game::Frame>>`) with swap-based updates. `CurrentFrame()`/`NextFrame()` accept an optional `GridCoord` parameter (defaulting to `kOriginCoord`). TimeStep class accumulates real-time into discrete physics steps. Each Frame receives a unique Frame ID at creation via `GenerateFrameId()`, enabling per-Frame UUID generation without atomics.

**Multi-frame Sparse Grid**: The update loop operates on a set of active grid coordinates computed by the game. Before each frame, `game::gpGame->ComputeActiveSet()` determines which grid cells are active, `EnsureNextFrames()` guarantees destination frames exist for active cells, and `BuildFrameInputs()` constructs per-coordinate FrameInput maps. Physics phases (Interpolate, PostRender, collision, destroy, spawn) iterate `mActiveCoords` rather than a single origin frame. Inactive frames are carried forward unchanged via move. After buffer swap, `EnsureNextFrames()` is called again for the next iteration.

**Flag Enums**: `MenuFlags` controls UI visibility and frame updates. `GameFlags` tracks high-level game state (quit requests, replay save/load, frame update status). Both use type-safe `common::Flags<>` wrapper.

**Access Pattern**: Engine code accesses GameBase functionality through the derived `game::gpGame` pointer (defined in Game.h), not through GameBase directly. This allows engine code to include game headers and use game-specific extensions.

**Related Free Functions**: `engine::ResetRealTime()` resets real-time clocks across AudioManager, Camera, and game TimeStep. Called when resuming from pause, loading saves, or after GPU device recreation to prevent time jumps.

**Async Rendering**: Uses `gpGraphics->mRenderFuture` (a `PersistentWorker`) to dispatch `RenderMainPresentAcquire()` asynchronously via `Wake()`, with the main thread calling `WaitForRender()` before modifying frame maps (`ComputeActiveSet()` can insert/erase entries in `mCurrentFrames`, which would cause a rehash race with the render thread reading the map). Captures the command buffer index and passes `*gpGraphics->mpFrameInterpolate` on the main thread before async dispatch. Computes a smoothly interpolated current time (`FrameInterpolate::fCurrentTime + remainder`) and passes it to `RenderGlobal()`, ensuring particles, water, and smoke get a time that advances every render frame and respects time scaling/pausing.

**Multi-Frame Rendering**: Uses `game::gpGame->mHumanGridCoord` as the camera coordinate for rendering. Always uses `MergeFramesForRender()` (from FrameGrid.h) to interpolate each visible frame and merge all collections into a single `FrameInterpolate` with grid-relative position offsets.

**Frame Update Flow**:
- `UpdateFramesAndRender()` first waits for the previous render thread to finish (`WaitForRender()`) to ensure the render thread is no longer reading `mCurrentFrames`, then calls `game::gpGame->ComputeActiveSet()`, `EnsureNextFrames()`, and `BuildFrameInputs()` to prepare the active grid coordinates and per-coordinate FrameInputs, then calculates required physics steps from accumulated time. Status changes (spawn/respawn events) are buffered persistently on the Game object and only drained into the human player's FrameInput when physics steps will actually run (`iFullUpdates > 0`), preventing event loss on render-only frames
- For each step: increment `miFrameCounter` and `mfCurrentTime`, update replay streams, execute frame update phases across all active coordinates (GameBase writes `iFrame` and `fCurrentTime` into each frame's Interpolate data), harvest transfer requests (entities that crossed frame boundaries are spawned into destination frames), swap buffers
- After full steps: create interpolated frame for smooth rendering between physics ticks
- Two-phase update per active coordinate: Interpolate (time, positions, state) → PostRender (seven sub-phases: Update, PreCollision, PostCollision, AreaDamage, Transfer, Destroy, Spawn). When multiple grid coordinates are active, parallelizable phases (Interpolate, PostRender Update, Transfer, Destroy/Spawn) use `common::gpMultithreading->Dispatch()` across active coordinates; collision phases run sequentially because they use static storage

**Grid Serialization**: `WriteGrid()`/`ReadGrid()` serialize the entire sparse grid map (all frames across all grid coordinates plus the human player's grid coordinate) for save/load. Replaces single-frame `WriteVersionedFile()`/`ReadVersionedFile()`. Writes frames in deterministic order (sorted by coordinate key). On load failure (version mismatch), falls back to creating a new game.

**Replay System**: DifferenceStream objects enable deterministic replay with validation. Records input changes with frame numbers, storing only frames where input changed for efficient storage. Captures CRCs of game state at every frame during recording. During replay, validates current state CRC against recorded values, triggering debug break on mismatch to detect non-determinism issues. Replay is only supported for single-frame mode; `SaveLoadReplay()` returns early when multiple frames exist.

**Template Methods**: `PreUpdate()` uses Template Method pattern - base handles common logic (vibration, time reset on focus loss) and calls pure virtual `ProcessMenuInput()` for game-specific menu handling.

**Virtual Methods**: Games override `Reset()`, `ShouldUpdateFrame()`, `ShouldTrapCursor()`, `ShouldUseCrosshair()`, `ProcessMenuInput()`, and file path methods for save/load/replay functionality.

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
Deterministic game state with map-based dual-buffered frames (sparse grid keyed by `GridCoord`) and fixed timestep updates.
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

Fixed-rate physics updates run via TimeStep accumulation at the game-defined rate (e.g., 64Hz). Each physics step updates replay streams, executes two-phase update (Interpolate → PostRender with Update/PreCollision/PostCollision/AreaDamage/Transfer/Destroy/Spawn sub-phases), swaps buffers. Rendering occurs at variable rate with interpolated frames between physics ticks.

### Threading Model
All async threads construct a `common::ThreadLocal` with a `Threads` enum identifier for logging and diagnostics. ThreadLocal owns its backing memory internally.
- **Main Thread**: Window messages, input, game logic, Vulkan command recording
- **Multithreading Pool** (`kThreadMultithreading`): `common::Multithreading` worker pool for parallel dispatch of data-parallel work (e.g., multi-coordinate frame updates, spaceship rendering). Workers are `PersistentWorker` threads created at startup, accessed via `common::gpMultithreading->Dispatch()`
- **Render Thread** (`kThreadRender`): `PersistentWorker` owned by Graphics (`mRenderFuture`) - `RenderMainPresentAcquire()` dispatched via `Wake()` at time-critical priority while main thread continues processing
- **Submit Global Thread** (`kThreadSubmitGlobal`): `PersistentWorker` owned by CommandBufferManager (`mSubmitGlobal`) for async global command buffer queue submission at time-critical priority
- **Submit Main Thread** (`kThreadSubmitMain`): `PersistentWorker` owned by CommandBufferManager (`mSubmitMain`) for async main command buffer queue submission at time-critical priority, waits on global submission
- **Present Thread** (`kThreadPresent`): `PersistentWorker` owned by SwapchainManager (`mPresent`) for async swapchain presentation at time-critical priority, waits on main submission
- **Screenshot Thread** (`kThreadScreenshot`): Async JPEG encoding and file save for screenshot capture
- **Disk Loading Threads**: Eager and lazy asset loading from disk via FileManager
- **Texture Upload Thread** (`kThreadTextureUpload`): GPU texture uploads via transfer queue (TextureUploadManager)
- **GPU**: Asynchronous execution with multiple frames in flight

### Memory Patterns
- Frame state uses map-based dual buffering (per-grid-coordinate) for deterministic updates
- Lazy loading defers texture/audio data until first use
- All Vulkan resources managed via RAII wrappers
