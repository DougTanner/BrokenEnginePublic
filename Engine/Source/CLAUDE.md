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

### GameBase.h/cpp
- **Purpose**: Abstract base class for game implementations
- **Key Features**:
  - Fixed 250Hz (4ms) timestep with frame interpolation
  - Triple-buffered frame state (Previous/Current/Next)
  - Save/load with DifferenceStream delta compression
  - Debug replay functionality
  - Worker thread management
  - Split update flow: Global (time/camera) -> Visible area calculation -> Interpolation -> Full update (collision/spawn/destroy)

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
- **Key Classes**: FrameBase, Render, Navmesh, UpdateList
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
Windows Message Loop (Main.cpp)
    ↓
Game Update @ 250Hz
    ├── [PAUSED PATH]
    │   ├── Input Processing (Game::Update)
    │   │   ├── RawInputManager::ProcessRawInput() directly
    │   │   ├── Returns MenuInput for pause menu
    │   │   └── Discards FrameInput (not used when paused)
    │   ├── UI/Menu Processing (Game::Update)
    │   └── Return early (skip game logic)
    │
    └── [NOT PAUSED PATH]
        ├── Input Processing (GameBase::Update)
        │   ├── RawInputManager::ProcessRawInput()
        │   ├── Returns MenuInput for UI
        │   └── Fills FrameInput by reference for game logic
        ├── Process previous frame input
        ├── Advance frame state (Prev→Curr→Next)
        ├── UpdateFrameGlobal: Time-based systems and camera
        ├── Calculate visible area from camera
        ├── CopyVisibleAreaToFrameInput: Store visible area in FrameInput
        ├── RenderGlobal: Initial rendering pass
        ├── UI/Menu Processing (Game::Update)
        │   └── Uses MenuInput from GameBase::Update
        └── Continue to frame updates below
    ↓
System Updates (Frame) - Continues @ 250Hz
    ├── UpdateFrameInterpolate: Position/rotation smoothing
    └── UpdateFrameFull: PostRender, collision, spawning, destruction
    └── Parallel pool updates (workers)
    ↓
System Updates (Frame)
    ├── Object pool physics/logic
    ├── Collision detection
    ├── Navmesh pathfinding
    └── Audio source updates
    ↓
Rendering @ Variable Rate (Graphics)
    ├── Interpolate positions
    ├── Update matrices
    ├── Record command buffers
    │   ├── Shadow passes
    │   ├── Opaque geometry
    │   ├── Transparent objects
    │   ├── Post-processing
    │   └── UI overlay
    └── Submit to GPU
    ↓
Present to Screen
```

**Input Processing Pattern**:
- ProcessRawInput() is called exactly once per frame to prevent input loss
- When PAUSED: Game::Update() processes input and handles pause menu
- When NOT PAUSED: GameBase::Update() processes input, Game::Update() handles UI
- MenuInput is passed from GameBase to Game for UI processing when not paused

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
