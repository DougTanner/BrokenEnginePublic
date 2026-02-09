# `/Engine/Source/ThirdParty/`

Third-party library integration files using unity build compilation pattern.

## Integration Files

| File | Library | Purpose |
|------|---------|---------|
| **DirectXTK.cpp** | DirectX Toolkit | XAudio2 audio engine and XINPUT-based gamepad/mouse input (Steam Deck compatible) |
| **ImGui.cpp** | Dear ImGui | Immediate-mode GUI for menus, HUD, and debug screens with Win32 and Vulkan backends |
| **StackWalker.cpp** | StackWalker | Stack trace generation for crash reports |
| **Mimalloc.cpp** | mimalloc | General-purpose allocator replacing CRT malloc/new with per-thread heaps |
| **Stb.cpp** | stb_image_write | JPEG image writing for screenshots |
| **Vma.cpp** | Vulkan Memory Allocator | GPU memory allocation with suballocation and defragmentation |
| **Volk.cpp** | Volk | Vulkan meta-loader for runtime API function loading |

## Unity Build Pattern

Each file `#include`s third-party source directly rather than linking a library. This keeps third-party code isolated, allows centralized warning suppression, and avoids modifying upstream sources.

Source code location: `/ThirdParty/` at repository root.
