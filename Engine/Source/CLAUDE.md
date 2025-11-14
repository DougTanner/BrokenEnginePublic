# `/Engine/Source/`

Core engine implementation with manager-based architecture. The engine uses a singleton pattern where all major systems are accessed via global pointers (e.g., `gpGraphics`, `gpAudioManager`).

## Global Manager Singletons

All managers are created in `Main.cpp` and accessed globally throughout the engine:

| Manager | Global Pointer | Purpose | Key Dependencies |
|---------|---------------|---------|-------------------|
| **FileManager** | `gpFileManager` | Asset loading, save/load, lazy chunk loading | None (must init first) |
| **ProfileManager** | `gpProfileManager` | CPU/GPU performance profiling | Graphics managers |
| **Graphics** | `gpGraphics` | Vulkan rendering orchestration | FileManager |
| **AudioManager** | `gpAudioManager` | 3D spatial audio (XAudio2) | FileManager |
| **RawInputManager** | `gpRawInputManager` | Keyboard/mouse/gamepad input | None |
| **UiManager** | `gpUiManager` | Immediate mode GUI system | Graphics, TextManager |

## Core Files

### Main.cpp
- **Purpose**: Engine entry point, window creation, manager initialization
- **Key Features**:
  - Creates all manager singletons in critical dependency order
  - Windows message loop with WM_SIZE, WM_DISPLAYCHANGE handling  
  - Fullscreen toggling (F11) and DPI awareness
  - Clean shutdown sequence for all managers

### CameraBase.h/cpp

Base camera class providing view and projection matrix calculation with frustum culling support.

**Purpose**: Provides common camera functionality for all game implementations with global access via `gpCamera` pointer.

**Key Responsibilities**:
- Calculates view and projection matrices from eye and target positions
- Computes visible area bounds for frustum culling
- Converts screen coordinates to world space for mouse interaction
- Provides visibility testing for objects and positions

**Architecture**: Game implementations inherit from CameraBase and add game-specific camera behavior. The engine accesses camera state through the global `gpCamera` pointer, which is initialized in Main.cpp.

### GameBase.h/cpp

Abstract base class that game implementations inherit to integrate with the engine's fixed timestep physics system.

**Purpose**: Orchestrates the core game loop by managing frame state updates at a fixed timestep while allowing rendering at variable rates.

**Architecture**: Uses dual-buffered frame state (Current/Next) with swap-based updates. Relies on TimeStep class for time accumulation and supports deterministic replay via separate difference stream recording for held and pressed input.

**Key Responsibilities**:
- Converts real-time into discrete physics steps via TimeStep
- Runs two-phase frame updates (Interpolate → PostRender) for each physics step
- Creates interpolated frames for smooth rendering between physics steps
- Manages frame state swapping and replay recording/playback via separate held/pressed streams
- Provides virtual hooks for game-specific behavior (Reset, ShouldUpdateFrame, ProcessSavesAndReplays)
- Provides CameraBase pointer member for game-specific camera implementation

**Replay System**: Maintains four DifferenceStream objects (writer/reader pairs for held and pressed input) that enable deterministic replay. During recording, each frame's input is captured separately. During playback, input is reconstructed and injected before frame updates.

**Input Processing**: Game-specific input conversion happens via `ProcessSavesAndReplays()`, which transforms raw input into held/pressed frame input and handles save/load/replay operations. The replay system intercepts input via templated `UpdateDifferenceStream()` calls before frame updates.

**Update Flow**: `UpdateFramesAndRender()` calculates needed physics steps from TimeStep, executes full updates for each step with input replay and frame swaps, then creates a partial interpolated frame for rendering. This decouples physics simulation rate from rendering framerate.

**Camera Integration**: GameBase holds a `pCamera` member pointer of type `CameraBase*`. Main.cpp initializes the global `gpCamera` pointer to reference the game's camera instance, allowing universal access throughout the engine.

### Pch.cpp
- Precompiled header for build performance

## Subsystems

### `/Audio/` - 3D Spatial Audio
- **Manager**: `gpAudioManager` - XAudio2-based spatial audio
- **Features**: Voice pooling, 3D positioning, music crossfading, lazy loading
- **See**: [Audio/CLAUDE.md](Audio/CLAUDE.md)

### `/Debug/` - Debug Utilities  
- **Purpose**: Vulkan enum-to-string conversions for error messages
- **Features**: Conditional compilation with `ENABLE_LOGGING`
- **See**: [Debug/CLAUDE.md](Debug/CLAUDE.md)

### `/File/` - Asset & Save System
- **Manager**: `gpFileManager` - Centralized file I/O and asset loading
- **Features**: 
  - Eager loading (startup): Font, Gltf, Islands, Model, Shader
  - Lazy loading (on-demand): Audio, Texture via background thread
  - Versioned save files with DifferenceStream compression
- **See**: [File/CLAUDE.md](File/CLAUDE.md)

### `/Frame/` - Game State Management
- **Purpose**: Deterministic game state with object pools
- **Features**: Triple-buffering, fixed timestep, parallel updates
- **Key Classes**: FrameBase, Render, Navmesh, UpdateList, TimeStep
- **See**: [Frame/CLAUDE.md](Frame/CLAUDE.md)
  - [Collections/CLAUDE.md](Frame/Collections/CLAUDE.md) - Spawn management
  - [Pools/CLAUDE.md](Frame/Pools/CLAUDE.md) - Object pools

### `/Graphics/` - Vulkan Rendering
- **Manager**: `gpGraphics` - Rendering pipeline orchestration
- **Features**: Multi-frame in flight, deferred rendering, GPU particles
- **Internal Managers** (initialization order critical):
  1. InstanceManager - Vulkan instance
  2. DeviceManager - Logical device  
  3. SwapchainManager - Swap chain
  4. ShaderManager - Shader modules
  5. TextureManager - Textures/render targets
  6. BufferManager - Vertex/index buffers
  7. PipelineManager - Render pipelines
  8. CommandBufferManager - Command recording
  9. ParticleManager - GPU particles
  10. TextManager - Font rendering
- **See**: [Graphics/CLAUDE.md](Graphics/CLAUDE.md)
  - [Managers/CLAUDE.md](Graphics/Managers/CLAUDE.md) - Manager details
  - [Objects/CLAUDE.md](Graphics/Objects/CLAUDE.md) - RAII wrappers

### `/Input/` - Input System
- **Manager**: `gpRawInputManager` - Unified input handling
- **Features**: Raw Input API (keyboard), DirectXTK (mouse/gamepad)
- **See**: [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Profile/` - Performance Profiling
- **Manager**: `gpProfileManager` - CPU/GPU performance tracking
- **Features**: Vulkan timestamp queries, smoothed timing display
- **Conditional**: Only with `ENABLE_PROFILING` define
- **See**: [Profile/CLAUDE.md](Profile/CLAUDE.md)

### `/Ui/` - User Interface
- **Manager**: `gpUiManager` - Immediate mode GUI
- **Features**: Widget hierarchy, layout system, input routing
- **Files**: UiManager.h/cpp, Widget.h/cpp, WrapperBase.h

## System Flow Diagrams

### Initialization Order (Critical)
```
1. FileManager (loads assets)
    ↓
2. ProfileManager (performance tracking)
    ↓
3. Graphics (complex internal init - see Graphics/CLAUDE.md)
    ↓
4. AudioManager (needs FileManager)
    ↓
5. RawInputManager (independent)
    ↓
6. UiManager (needs Graphics/TextManager)
```

### Runtime Game Loop
```
Windows Message Loop (Main.cpp MainThread)
    ↓
Main Loop (every frame)
    ├── 1. Fullscreen Toggle
    ├── 2. Process Windows Messages
    ├── 3. Lost Focus Handling
    ├── 4. Input Processing
    │   ├── RawInputManager::Update() → RawInput
    │   ├── game::ProcessRawInput(RawInput) → MenuInput + FrameInput
    │   ├── UiManager::Update(MenuInput)
    │   └── game::PreUpdate(MenuInput, FrameInput, bLostFocus) → bUpdateFrames
    ├── 5. Frame Updates and Rendering
    │   ├── IF bUpdateFrames:
    │   │   └── GameBase::UpdateFramesAndRender(FrameInput, bLostFocus)
    │   │       ├── TimeStep::UpdateRealtime() → physics step count
    │   │       ├── RenderGlobal (shadows, particles) if partial frame
    │   │       ├── FOR EACH PHYSICS STEP:
    │   │       │   ├── HandleReplay (record/playback input)
    │   │       │   ├── WriteFrameInterpolateBase (time, positions, game state)
    │   │       │   ├── WriteFramePostRenderBase (collision, spawn, destroy)
    │   │       │   └── Swap frames (Current ↔ Next)
    │   │       └── Create interpolated frame:
    │   │           ├── WriteFrameInterpolateBase (partial step)
    │   │           └── RenderMainImagePresentAcquire
    │   └── ELSE: RenderMainImagePresentAcquire(CurrentFrame)
    ├── 6. Quit Detection
    ├── 7. Update Cursor Visual
    └── 8. Audio Update
    ↓
Physics Updates (inside WriteFrame*Base) @ 250Hz
    ├── WriteFrameInterpolateBase: Time accumulation, copy pools, interpolate positions
    └── WriteFramePostRenderBase: PostRender, collision, spawning, destruction
        └── Parallel pool updates (worker threads)
    ↓
Rendering @ Variable Rate (Graphics)
    ├── RenderGlobal: Shadow passes, particles (called before interpolation)
    ├── RenderMainImagePresentAcquire: Main rendering pass
    │   ├── Calculate matrices and visible area
    │   ├── Record command buffers
    │   │   ├── Opaque geometry
    │   │   ├── Transparent objects
    │   │   ├── Post-processing
    │   │   └── UI overlay
    │   └→ Submit to GPU and present
    ↓
Present to Screen
```

### Data Flow Between Systems
```
FileManager (Assets)
    ├→ Graphics (textures, models, shaders)
    ├→ Audio (sounds, music)
    └→ Frame (islands, save data)

Frame State (Game Logic)
    ├→ Graphics (object positions, visibility)
    ├→ Audio (3D positions, velocities)
    └→ UI (game state display)

Input (User Actions)
    ├→ Game (player control)
    └→ UI (menu navigation)
```

## Key Constants & Defines

### Timing
- `kUpdateStepNs = 4'000'000ns` - Fixed timestep (250Hz)
- `kfDeltaTime = 0.004f` - Delta time in seconds

### Build Configuration
- `BT_DEBUG` - Debug build
- `BT_PROFILE` - Profile build with timing
- `BT_RELEASE` - Release build
- `BT_ENGINE` - Engine compilation flag
- `ENABLE_LOGGING` - Enable debug logging
- `ENABLE_PROFILING` - Enable performance profiling

## Memory & Threading

### Memory Patterns
- **Object Pools**: Fixed-size arrays with `alignas(64)` for cache optimization
- **Triple Buffering**: Previous/Current/Next frame states
- **Lazy Loading**: Background thread loads audio/textures on demand

### Threading Model
- **Main Thread**: Window messages, input, game logic, command recording
- **Worker Threads**: Parallel object pool updates (count from std::thread::hardware_concurrency)
- **Background Thread**: Lazy asset loading (FileManager)
- **GPU**: Asynchronous command execution with multiple frames in flight
