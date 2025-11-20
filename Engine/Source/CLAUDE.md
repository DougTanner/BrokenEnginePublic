# `/Engine/Source/`

Core engine implementation with manager-based architecture. All major systems are singletons accessed via global pointers (e.g., `gpGraphics`, `gpAudioManager`).

## Global Manager Singletons

Managers created in `Main.cpp` in strict dependency order:

| Manager | Global Pointer | Purpose |
|---------|---------------|---------|
| **FileManager** | `gpFileManager` | Asset loading, save/load, lazy chunk loading |
| **ProfileManager** | `gpProfileManager` | CPU/GPU performance profiling |
| **Graphics** | `gpGraphics` | Vulkan rendering orchestration |
| **AudioManager** | `gpAudioManager` | 3D spatial audio via XAudio2 |
| **RawInputManager** | `gpRawInputManager` | Keyboard/mouse/gamepad input |
| **UiManager** | `gpUiManager` | Immediate mode GUI system |

## Core Files

### Main.cpp
Engine entry point managing initialization, main loop, and shutdown.

**Initialization**: Creates managers in dependency order, sets up Windows window, configures DPI awareness, loads settings.

**Main Loop**: Processes Windows messages with `PeekMessage()` during active frame processing, handles fullscreen toggling, updates input managers, delegates to game for frame updates and rendering, updates audio. Blocks on `GetMessage()` when window loses focus to reduce CPU usage.

**Shutdown**: Saves settings, destroys managers in reverse order via RAII.

**Exception Handling**: Catches unhandled exceptions, generates crash reports with callstack and DxDiag info, saves to desktop or AppData.

### CameraBase.h/cpp
Abstract base camera providing view/projection matrices and frustum culling.

**Purpose**: Common camera functionality accessed via global `gpCamera` pointer. Game implementations inherit and add game-specific behavior.

**Key Features**:
- Calculates view and projection matrices from eye/target positions
- Computes visible area bounds in world space for frustum culling
- Converts screen coordinates to world space via ray-plane intersection
- Provides visibility testing for positions and objects

**Integration**: `gpCamera` global initialized in Main.cpp points to game-specific camera instance.

### GameBase.h/cpp
Abstract base class for game implementations using fixed timestep physics.

**Purpose**: Orchestrates game loop with fixed-rate physics updates and variable-rate rendering.

**Architecture**: Dual-buffered frame state (Current/Next) with swap-based updates. TimeStep class accumulates real-time into discrete physics steps.

**Frame Update Flow**:
- `UpdateFramesAndRender()` calculates required physics steps from accumulated time
- For each step: update replay streams, execute frame update phases, swap buffers
- After full steps: create interpolated frame for smooth rendering between physics ticks
- Two-phase update: Interpolate (time, positions, state) → PostRender (collision, spawn/destroy)

**Replay System**: DifferenceStream objects enable deterministic replay with validation. Records input changes with frame numbers, storing only frames where input changed for efficient storage. Captures CRCs of game state at every frame during recording. During replay, validates current state CRC against recorded values, triggering debug break on mismatch to detect non-determinism issues.

**Virtual Methods**: Games override `Reset()`, `ShouldUpdateFrame()`, and file path methods for save/load/replay functionality.

**Camera Integration**: Holds `CameraBase*` member that Main.cpp assigns to global `gpCamera`.

### Pch.cpp
Precompiled header compilation unit.

## Subsystems

### `/Audio/` - 3D Spatial Audio
XAudio2-based spatial audio system with voice pooling and lazy loading.
- [Audio/CLAUDE.md](Audio/CLAUDE.md)

### `/Debug/` - Debug Utilities
Vulkan enum-to-string conversions for error messages (conditional on `ENABLE_LOGGING`).
- [Debug/CLAUDE.md](Debug/CLAUDE.md)

### `/File/` - Asset & Save System
Centralized file I/O with eager/lazy asset loading and versioned save files.
- Eager: Fonts, glTF, Islands, Models, Shaders
- Lazy: Audio, Textures (background thread)
- [File/CLAUDE.md](File/CLAUDE.md)

### `/Frame/` - Game State Management
Deterministic game state using object pools with fixed timestep updates.
- [Frame/CLAUDE.md](Frame/CLAUDE.md)
- [Frame/Collections/](Frame/Collections/CLAUDE.md) - Spawn management
- [Frame/Pools/](Frame/Pools/CLAUDE.md) - Object pools

### `/Graphics/` - Vulkan Rendering
Multi-pass Vulkan renderer with deferred lighting, shadows, and GPU particles. Contains 10 internal managers (InstanceManager → DeviceManager → SwapchainManager → ShaderManager → TextureManager → BufferManager → PipelineManager → CommandBufferManager → ParticleManager → TextManager) that must be initialized in strict dependency order.
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md)
- [Graphics/Managers/](Graphics/Managers/CLAUDE.md) - Manager implementations
- [Graphics/Objects/](Graphics/Objects/CLAUDE.md) - RAII Vulkan wrappers

### `/Input/` - Input System
Unified input handling via Raw Input API (keyboard) and DirectXTK (mouse/gamepad).
- [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Profile/` - Performance Profiling
CPU/GPU performance tracking using Vulkan timestamp queries (conditional on `ENABLE_PROFILING`).
- [Profile/CLAUDE.md](Profile/CLAUDE.md)

### `/Ui/` - User Interface
Immediate mode GUI system with widget hierarchy, layout, and input routing.
- **Files**: UiManager.h/cpp, Widget.h/cpp, WrapperBase.h

## Architecture Overview

### Initialization Order
Managers must be created in strict dependency order:
1. FileManager → 2. ProfileManager → 3. Graphics (internal managers) → 4. AudioManager → 5. RawInputManager → 6. UiManager

### Main Loop Flow
Each frame processes Windows messages, handles fullscreen toggle, updates input systems (RawInputManager → game input conversion → UiManager), determines if frame updates needed, executes physics steps with replay handling if required, renders current/interpolated frame, updates audio.

Fixed 250Hz physics updates run via TimeStep accumulation. Each physics step updates replay streams, executes two-phase update (Interpolate → PostRender), swaps buffers. Rendering occurs at variable rate with interpolated frames between physics ticks.

### Threading Model
- **Main Thread**: Window messages, input, game logic, Vulkan command recording
- **Worker Threads**: Parallel object pool updates during physics steps
- **Background Thread**: Lazy asset loading (audio/textures)
- **GPU**: Asynchronous execution with multiple frames in flight

### Memory Patterns
- Object pools use fixed-size cache-aligned arrays
- Frame state uses dual/triple buffering for deterministic updates
- Lazy loading defers texture/audio data until first use
- All Vulkan resources managed via RAII wrappers
