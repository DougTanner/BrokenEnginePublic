# Code style

- Use the C++23 standard
- YOU MUST follow the C++ code style guide in `/Documents/C++StyleGuide.txt`
- C++ system header includes, and includes for other external APIs and SDKs, are located in `/Common/ExternalHeaders.h`
- 3D rendering is handled by the Vulkan API
- Vector/Matrix and 3D math is handled by "DirectX Math"
- Audio playback is handled in AudioManager by DirectXTK's AudioEngine API
- Mouse and keyboard input is handled in RawInputManager by Win32's "Raw Input" API
- Gamepad input is handled by DirectXTK's GamePad API


## Important Defines

- `BT_DEBUG` - Debug build configuration
- `BT_PROFILE` - Profile build configuration  
- `BT_RELEASE` - Release build configuration
- `BT_ENGINE` - Engine-specific code



# Key Directories

- `/Common/` - Utilities used by DataPacker/Engine/Projects, and specification for the data files
- `/DataPacker/` preprocesses raw assets into .bin data files that are loaded by the Engine
	- Ignore everything in `DataPacker/Source/ThirdParty`
- `/Documents/` contains the C++ code style
- `/Engine/` Shared code used by the game implementations, loads data from binary files and calls 3D/Input/Audio APIs
	- Ignore everything in `/Engine/Data/`
	- Ignore everything in `/Engine/Source/ThirdParty/`
- `/Projects/` Game implementations that use the Engine code
	- Ignore everything in /Data/ folders under `/Projects/`
- Ignore everything in `/ThirdParty/` unless instructed otherwise

- Ignore everything in `/.git/`
- Ignore everything exluded by the `.gitignore` file
- Ignore all `LICENSE.md` files
- Ignore all `README.md` files
- Ignore everything in `Build/` folders
- Ignore everything in `Output/` folders
- Ignore everything in `.vs/` folders



# Instructions

- Do not add or stage files to Git, do not interact with Git at all.
- When you make code changes, remove code, or add new code, update the CLAUDE.md file in the same directory as the modified file. Consider updating CLAUDE.md files in parent directories if the changes are far-reaching enough.
- When making changes to any shared code, make sure the all the locations the code is used are updated. For example, code in `/Common/` can be referenced in `/DataPacker/` or `/Engine/` or `/Projects/`. And `/Engine/` code can be used in multiple places inside `/Projects/`.



# Architecture Overview


## DataPacker

- Compiles HLSL shaders to SPIR-V
- Packs models (.gltf/.obj) into optimized vertex buffers
- Compresses textures to BC4/BC7 format
- Converts audio to ADPCM format
- Outputs single Data.bin file with CRC-indexed chunks


## Engine

### Manager-Based Architecture, the engine uses global singleton managers for major subsystems

- `gpGraphics` - Central graphics orchestrator containing all rendering managers
- `gpAudioManager` - 3D spatial audio system using XAudio2
- `gpFileManager` - Resource loading with CRC-based chunk system
- `gpRawInputManager` - Keyboard, mouse, and gamepad input handling

### Vulkan-based renderer with
- Pre-recorded command buffers
- Particle systems (GPU compute)

### Threading Model

- Main thread: Game logic, input, Windows messages
- Render thread: Command buffer recording (when enabled)
- Worker threads: Parallel updates via UpdateList pattern
- Audio thread: XAudio2 managed

### Frame-Based State Management

Game state organized into Frame structures:
- Fixed timestep simulation at 250Hz (4ms per frame) with interpolation
- Previous/Current/Next frame pattern enables interpolation
- Supports save states and deterministic replay

1. Process input
2. Update frame state (if timestep elapsed)
3. Interpolate between frames
4. Render (uses interpolated positions)
5. Present


## Projects

- `Projects\BrokenEngineSandbox` is a sample game designed to test all the features of the Engine
