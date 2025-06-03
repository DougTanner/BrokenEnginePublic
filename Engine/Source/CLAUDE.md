# /Engine/Source/

Core engine implementation with manager-based architecture and subsystem organization.

## Core Files in `/Engine/Source/`

### Main.cpp
- Engine entry point and window management
- Creates all manager singletons in critical dependency order
- Windows message loop with WM_SIZE, WM_DISPLAYCHANGE handling
- Fullscreen toggling and DPI awareness

### GameBase.h/cpp
- Abstract base class for game implementations
- Frame timing management (250Hz fixed timestep)
- Previous/Current/Next frame triple buffering
- Save/load state management with DifferenceStream
- Debug replay functionality

### Pch.cpp
- Precompiled header

## Subdirectories

### `/Audio/` - 3D Spatial Audio System
- XAudio2-based audio engine with voice pooling and effects
- See [Audio/CLAUDE.md](Audio/CLAUDE.md)

### `/Debug/` - Debugging Utilities
- Debug utilities and enum string conversions
- See [Debug/CLAUDE.md](Debug/CLAUDE.md)

### `/File/` - Asset and Save System
- Binary asset loading and save state management
- See [File/CLAUDE.md](File/CLAUDE.md)

### `/Frame/` - Game State Management
- Triple-buffered game state and object pools
- See [Frame/CLAUDE.md](Frame/CLAUDE.md)
  - [Collections/CLAUDE.md](Frame/Collections/CLAUDE.md) - Spawn request management templates
  - [Pools/CLAUDE.md](Frame/Pools/CLAUDE.md) - Fixed-size object pool implementations

### `/Graphics/` - Vulkan Rendering Pipeline
- Vulkan rendering pipeline and resource management
- See [Graphics/CLAUDE.md](Graphics/CLAUDE.md)
  - [Managers/CLAUDE.md](Graphics/Managers/CLAUDE.md) - GPU resource manager implementations
  - [Objects/CLAUDE.md](Graphics/Objects/CLAUDE.md) - RAII wrappers for Vulkan resources

### `/Input/` - Input Handling
- Keyboard, mouse, and gamepad input handling
- See [Input/CLAUDE.md](Input/CLAUDE.md)

### `/Profile/` - Performance Profiling
- Performance profiling with GPU timestamp queries
- See [Profile/CLAUDE.md](Profile/CLAUDE.md)

### `/Ui/` - User Interface System
- Immediate mode GUI with widget hierarchy
- UiManager.h/cpp - Central UI management and input routing
- Widget.h/cpp - Base widget class with layout and rendering
- WrapperBase.h - Type-safe widget wrapper template

## Manager Initialization Order

Critical creation order in `Main.cpp` due to interdependencies:

1. **FileManager** (`gpFileManager`) - Must be first, loads all assets
2. **ProfileManager** (`gpProfileManager`) - Early init for performance tracking
3. **Graphics** (`gpGraphics`) - Complex internal initialization order
4. **AudioManager** (`gpAudioManager`) - Depends on FileManager for audio assets
5. **RawInputManager** (`gpRawInputManager`) - Independent initialization
6. **UiManager** (`gpUiManager`) - Depends on Graphics/TextManager for rendering

## Manager Dependencies

### Asset Loading Flow
FileManager → All Managers (provides CRC-indexed asset access from multiple .pack files)

### Runtime Update Flow
RawInputManager → Game/UI → Frame → Audio/Graphics

### Rendering Flow
Frame (interpolation) → Graphics (command recording) → GPU

### Audio Flow
Frame (3D positions) → AudioManager → XAudio2 voices

## Runtime Game Loop Flow
```
Windows Message Loop (Main.cpp)
    ↓
Input Processing (RawInputManager)
    ├── Keyboard/Mouse via Raw Input API
    └── Gamepad via DirectXTK
    ↓
Game Update (fixed 250Hz in GameBase)
    ├── Process input from previous frame
    ├── Update Frame state (Previous → Current → Next)
    ├── Spawn/destroy objects via Collections
    └── Parallel updates via worker threads
    ↓
Frame System Updates
    ├── Object pools update positions/states
    ├── Collision detection
    ├── Navmesh pathfinding
    └── Audio source positioning
    ↓
Rendering (variable rate with interpolation)
    ├── Calculate interpolated positions
    ├── Update view/projection matrices
    ├── Record command buffers
    │   ├── Shadow passes
    │   ├── Geometry passes
    │   ├── Transparent passes
    │   ├── Post-processing
    │   └── UI rendering
    └── Submit to GPU queue
    ↓
Presentation
    └── Swap chain present
```

## Manager Communication Flow
```
FileManager ←→ All Managers (asset loading)
    ↓
Graphics ←→ Frame (object rendering)
    ↓        ↓
    ↓     AudioManager ← Frame (3D positioning)
    ↓
UiManager → TextManager (text rendering)
    ↑
RawInputManager (UI input handling)
```

## Important Build Defines for the Engine

- `BT_DEBUG` - Debug build configuration
- `BT_PROFILE` - Profile build configuration  
- `BT_RELEASE` - Release build configuration
- `BT_ENGINE` - Engine-specific code
