# `/Engine/Source/ThirdParty/`

Third-party library integration files using unity build compilation pattern.

## Integration Files

| File | Library | Purpose |
|------|---------|---------|
| **DirectXTK.cpp** | DirectX Toolkit | XAudio2 audio engine and XINPUT-based gamepad/mouse input (Steam Deck compatible) |
| **StackWalker.cpp** | StackWalker | Stack trace generation for crash reports |
| **Volk.cpp** | Volk | Vulkan meta-loader for runtime API function loading |

## Unity Build Pattern

Each file `#include`s third-party source directly rather than linking a library. This keeps third-party code isolated, allows centralized warning suppression, and avoids modifying upstream sources.

Source code location: `/ThirdParty/` at repository root.
