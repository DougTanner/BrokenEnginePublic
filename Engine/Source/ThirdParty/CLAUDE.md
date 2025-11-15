# `/Engine/Source/ThirdParty/`

Third-party library integration files that compile external dependencies directly into the engine using unity build compilation.

## Integration Files

### DirectXTK.cpp
Compiles DirectX Toolkit components for audio and input handling.

**Components Included**:
- AudioEngine, SoundCommon - XAudio2-based audio playback
- GamePad - Xbox controller input via XINPUT
- Mouse - Mouse input handling

**Platform Support**: Configured to use XINPUT for Steam Deck compatibility.

### StackWalker.cpp
Compiles StackWalker library for generating stack traces during crashes and error conditions.

**Features**: Extensive compiler warning suppression for clean third-party integration without modifying upstream code.

### Volk.cpp
Compiles Volk, a Vulkan meta-loader that enables runtime loading of Vulkan API functions.

**Purpose**: Allows dynamic Vulkan function loading without linking directly to vulkan-1.dll, providing flexibility for different driver implementations and versions.

## Pattern

All files use the unity build pattern - single .cpp files that `#include` the actual third-party source. This approach:
- Keeps third-party code isolated in `/ThirdParty/` directory
- Allows centralized warning suppression per library
- Enables full optimization of third-party code
- Avoids modifying upstream sources

## Source Locations

The actual third-party source code lives in `/ThirdParty/` at the repository root:
- `/ThirdParty/DirectXTK/` - DirectX Toolkit
- `/ThirdParty/StackWalker/` - StackWalker
- `/ThirdParty/volk/` - Volk meta-loader
