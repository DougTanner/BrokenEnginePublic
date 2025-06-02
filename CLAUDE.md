# IMPORTANT directives

- YOU MUST follow the C++ code style guide at `/Documents/C++StyleGuide.txt` (Mandatory coding standards)
- YOU MUST only write code that is specifically required to implemented your instructions, validation and error handling will be added later
- YOU MUST Add comments before each block when adding new code, explain what the code does and why it was added
- YOU MUST search the codebase and update all usage locations after modifying code
- YOU MUST update CLAUDE.md files when making code changes
- YOU MUST update the CLAUDE.md if you learn new information inside .h or .cpp files that was not covered in the CLAUDE.md in the same folder
- Do not interact with Git

# Broken Engine Architecture Overview

This is a game engine built with a clear separation between asset processing (DataPacker), core engine functionality (Engine), game implementations (Projects), and shared utilities (Common).

# Directory Structure

## Files and directories that should not be accessed

- Ignore the following files: `LICENSE.md` `README.md`
- Ignore the following directories: `.git/` `ThirdParty` `Data/` `Build/` `Output/` `.vs/`
- Ignore everything exluded by the `.gitignore` file

## Important directories

### `/Common/` - Shared utilities and data format specifications
- **Namespace**: `common`

### `/DataPacker/Source/` - Asset Preprocessing Tool
- **Purpose**: Converts raw assets into optimized binary formats for runtime loading
- **Key Components**:
  - `/DataPacker/Source/ExportJobs/` - Individual asset processors:
    - `ExportAudio` - WAV → ADPCM compression
    - `ExportFont` - BMFont → binary font data
    - `ExportGltf` - glTF → PBR materials and animations
    - `ExportIsland` - Terrain → heightmaps and navigation data
    - `ExportModel` - OBJ/glTF → optimized vertex buffers
    - `ExportShader` - HLSL → SPIR-V compilation
    - `ExportTexture` - PNG/TGA → BC4/BC7 compression
  - `FileManager.cpp` - Orchestrates two-phase export process
- **Output**: Files are output to `\Projects\BrokenEngineSandbox\Platforms\VisualStudio2022\Output\Data`
  - `.manifest` files contain offsets of Chunk data in the .pack file
  - `.pack` files contain the packed binary data
  - `.h` files contain crc_t asset IDs
- **Dependencies**: 
  - Uses `/Common/` for data formats and utilities

### `/Engine/Source/` - Core Game Engine
- **Purpose**: Runtime systems for graphics, audio, input, and game state management
- **Namespace**: `engine`
- **Architecture**: Manager-based with global singletons

#### `/Engine/Source/Audio/`
- **Purpose**: Playback of sound and music
- `AudioManager` - 3D spatial audio using XAudio2
- Features: voice pooling, distance attenuation, doppler effects, music crossfading
- **Dependencies**: DirectXTK AudioEngine

#### `/Engine/Source/File/`
- **Purpose**: Manage access to the filesystem
- `FileManager` - Loads assets from DataPacker-generated binaries
  - Background loading thread processes lazy load requests
- `DifferenceStream` - Save/load system with state recording
- **Dependencies**: Uses CRC system from `/Common/`

#### `/Engine/Source/Frame/`
- **Purpose**: Game state management with deterministic simulation
- Triple-buffered frames (Previous/Current/Next) for interpolation
- Fixed timestep at 250Hz (4ms per frame)
- **Subdirectories**:
  - `/Collections/` - Spawn request management templates
  - `/Pools/` - Fixed-size object pools (Areas, Billboards, Explosions, etc.)
- `UpdateList.h` - Parallel update system for worker threads

#### `/Engine/Source/Graphics/`
- **Purpose**: Vulkan-based renderer with manager architecture
- **Key Managers** (`/Managers/`):
  - `DeviceManager` - Vulkan device and queue management
  - `SwapchainManager` - Presentation and frame synchronization
  - `BufferManager` - Vertex/index/uniform buffer allocation
  - `TextureManager` - Texture loading and sampling
  - `ShaderManager` - SPIR-V shader loading
  - `PipelineManager` - Render pipeline states
  - `CommandBufferManager` - Multi-threaded command recording
  - `ParticleManager` - GPU compute particle systems
  - `TextManager` - Text rendering with SDF fonts
  - `InstanceManager` - Instanced rendering
- **Objects** (`/Objects/`): RAII wrappers for Vulkan resources

#### `/Engine/Source/Input/`
- `RawInputManager` - Keyboard/mouse via Win32 Raw Input API
- Gamepad support via DirectXTK
- **Dependencies**: Windows Raw Input API, DirectXTK GamePad

#### `/Engine/Source/Profile/`
- `ProfileManager` - CPU/GPU performance profiling
- Uses Vulkan timestamp queries for GPU timing

### `/Projects/` - Game Implementations
- **Purpose**: Actual games built using the Engine
- **Namespace**: `game`
- **Current Projects**:
  - `/Projects/BrokenEngineSandbox/Source/` - Sample game testing all engine features
    - Spaceship combat with AI
    - Weapon systems (blasters, missiles, shields)
    - Complete UI with menus and HUD
    - Third-person camera system
- **Dependencies**: 
  - Uses all Engine systems
  - Inherits from `GameBase` class
  - Must implement frame update and rendering

### `/ThirdParty/`
- External libraries, generally ignore unless specifically needed

## Key Architectural Patterns

### 1. Manager Pattern
Most subsystems use global singleton managers accessible via `gp` prefixed pointers.

### 2. Frame-Based State Management
- Triple-buffering enables deterministic simulation and smooth interpolation
- All game objects exist in Previous/Current/Next frame states
- Fixed timestep ensures reproducible behavior

### 3. Object Pooling
- Fixed-size pools prevent runtime allocations
- O(1) allocation/deallocation
- Thread-safe with mutex protection

### 4. CRC-Based Asset System
- All assets indexed by compile-time CRC32
- Fast runtime lookup
- Type-safe asset references

### 5. Two-Phase Processing
- DataPacker pre-exports dependencies before main export
- Ensures all referenced assets are available

### 6. Thread Architecture
- Main thread: Game logic, input, Windows messages
- Render thread: Command buffer recording (optional)
- Worker threads: Parallel updates via UpdateList
- Audio thread: Managed by XAudio2

## Dependencies & Interrelations

### Build-Time Dependencies
```
Common/ (no dependencies)
   ↓
DataPacker/ (uses Common/)
   ↓
[Generates .manifest/.pack files]
   ↓
Engine/ (uses Common/, loads .pack files)
   ↓
Projects/ (uses Engine/ and Common/)
```

### Runtime Dependencies
- **Graphics**: Vulkan API, DirectX Math
- **Audio**: XAudio2 (via DirectXTK)
- **Input**: Win32 Raw Input, DirectXTK GamePad

### Detailed Data Flow

### Asset Pipeline Flow
```
Raw Assets (PNG, WAV, HLSL, glTF, etc.)
    ↓
DataPacker (pre-build step)
    ├── Phase 1: Pre-export (glTF, Islands - can create new assets)
    └── Phase 2: Main export (Audio, Fonts, Models, Shaders, Textures)
    ↓
Binary Files + Headers (per asset type)
    ├── {Type}.manifest files (CRC → chunk location mapping)
    ├── {Type}.pack files (compressed binary data)
    └── {Type}.h files (compile-time CRC constants)
    Where {Type} = Audio, Font, Gltf, Islands, Model, Shader, Texture
    ↓
FileManager (runtime loading)
    ├── Loads manifests into memory
    ├── Memory-maps pack files
    └── Provides CRC-based chunk access
    ↓
Engine Managers
    ├── TextureManager: Loads BC4/BC7 textures
    ├── ShaderManager: Loads SPIR-V shaders
    ├── BufferManager: Loads vertex/index data
    ├── AudioManager: Loads ADPCM audio
    └── TextManager: Loads font data
```

### Runtime Game Loop Flow
```
Windows Message Loop (Main.cpp)
    ↓
Input Processing (RawInputManager)
    ├── Keyboard/Mouse via Raw Input API
    └── Gamepad via DirectXTK
    ↓
Game Update (fixed 250Hz)
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

### Save/Load Data Flow
```
Game State → DifferenceStream → Compressed Save File
    ↑                                    ↓
Frame Previous/Current/Next ← DifferenceStream ← Load
```

### Manager Communication Flow
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

## Code Change Impact Areas

When modifying `/Common/`:
- Update DataPacker, Engine, and all Projects

When modifying `/Engine/`:
- All Projects must be updated
- Frame structure changes affect save/load compatibility
- Manager interface changes require Project code updates

When modifying asset formats in `/DataPacker/`:
- Update Engine loading code if format changed
- Update any Project-specific asset handling

## Code Style & Standards

- **Language**: C++23 standard
- **Style Guide**: `/Documents/C++StyleGuide.txt` (mandatory)
- **Headers**: System headers in `/Common/ExternalHeaders.h`
- **APIs**:
  - Rendering: Vulkan
  - Math: DirectX Math
  - Audio: DirectXTK AudioEngine
  - Input: Win32 Raw Input + DirectXTK GamePad

## Important Build Defines

- `BT_DEBUG` - Debug build configuration
- `BT_PROFILE` - Profile build configuration  
- `BT_RELEASE` - Release build configuration
- `BT_ENGINE` - Engine-specific code
