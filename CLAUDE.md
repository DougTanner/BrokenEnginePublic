# IMPORTANT directives

- YOU MUST follow the C++ code style guide at `/Documents/C++StyleGuide.txt` (mandatory coding standards)
- YOU MUST Add comments before each block when adding new code, explaining what the code does and why it was added
- YOU MUST search the codebase and update all usage locations after modifying code
- YOU MUST update CLAUDE.md files when making code changes
- YOU MUST update the CLAUDE.md if you learn new information inside .h or .cpp files that was not covered in the CLAUDE.md in the same folder
- Do not interact with Git
- Do not add error handling or validation, assume any parameters or members are valid and can be used without checking
- Don't test anything, only write the code
- **Language**: C++23 standard

# Broken Engine Architecture Overview

This is a game engine built with a clear separation between asset pre-processing (DataPacker executable), core engine functionality (Engine), game implementations (Projects), and shared utilities (Common).

**Note**: Each major directory contains its own CLAUDE.md file with detailed subsystem documentation.

# Directory Structure

## Files and directories that should not be accessed

- Ignore the following files: `LICENSE.md` `README.md`
- Ignore the following directories: `.git/` `ThirdParty` `Data/` `Build/` `Output/` `.vs/`
- Ignore everything exluded by the `.gitignore` file

## Important directories

### `/Common/` - Shared utilities and data format specifications
- **Namespace**: `common`

### `/DataPacker/` - Asset Preprocessing Tool
- **Purpose**: Converts raw assets into optimized binary formats for runtime loading
- **Usage**: Is compiled to a standalone executable, is triggered automatically by a pre-build event in the Project
- **Asset Types**: Audio, Font, Gltf, Islands, Model, Shader, Texture
- **Output**: Files are output to `/Projects/*/Platforms/VisualStudio2022/Output/Data`
  - `.manifest` files contain CRC → chunk location mapping
  - `.pack` files contain binary data headers and chunks
  - `.h` files contain compile-time CRC constants
- **Dependencies**: Uses `/Common/` for data formats and utilities

### `/Engine/` - Core Game Engine
- **Purpose**: Runtime systems for graphics, audio, input, and game state management
- **Usage**: Used as an abstraction layer by Projects to access lower level APIs
- **Namespace**: `engine`
- **Manager Pattern**: All managers are singletons accessed via global pointers (e.g., `gpGraphics`, `gpAudioManager`)
- **Dependencies**: Uses `/Common/` for data formats and utilities

### `/Projects/` - Game Implementations
- **Purpose**: Actual games built using the Engine
- **Namespace**: `game`
- **Current Projects**:
  - `/Projects/BrokenEngineSandbox/` - Sample game testing all engine features
- **Dependencies**: 
  - Uses `/Engine/`
  - Uses `/Common/` for utilities

### `/ThirdParty/`
- External libraries, generally ignore unless specifically needed
