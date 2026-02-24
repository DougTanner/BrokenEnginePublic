# `/Engine/Source/Graphics/Managers/`

Manager classes for the Vulkan renderer. All managers are singletons with global pointers initialized during Graphics construction in strict dependency order.

## Architecture Patterns

**Manager Lifecycle**:
- Created in strict dependency order during Graphics construction
- Never individually destroyed - only during Graphics destruction
- Support resource recreation for window resize and device changes
- Global pointer access (e.g., `gpTextureManager`) for cross-system communication

**Vulkan Resource Management**:
- Managers own and manage Vulkan objects with proper cleanup
- Resource updates only after fence synchronization
- Descriptor set management for dynamic resource binding
- Pipeline state object caching and reuse
- VMA (Vulkan Memory Allocator) handles all GPU memory allocation

## Manager Initialization Order

1. InstanceManager -> 2. DeviceManager -> 3. SwapchainManager -> 4. CommandBufferManager -> 5. BufferManager -> 6. TextureManager -> 7. TextManager -> 8. PipelineManager -> 9. ParticleManager -> 10. ImGuiManager

## Managers

| Manager | Global | Purpose | Docs |
|---------|--------|---------|------|
| BufferManager | `gpBufferManager` | GPU buffers (vertex, uniform, storage) | [BufferManager.CLAUDE.md](BufferManager.CLAUDE.md) |
| CommandBufferManager | `gpCommandBufferManager` | Command recording/submission | [CommandBufferManager.CLAUDE.md](CommandBufferManager.CLAUDE.md) |
| DeviceManager | `gpDeviceManager` | Logical device, queues, VMA | [DeviceManager.CLAUDE.md](DeviceManager.CLAUDE.md) |
| ImGuiManager | `gpImGuiManager` | Dear ImGui UI rendering | [ImGuiManager.CLAUDE.md](ImGuiManager.CLAUDE.md) |
| InstanceManager | `gpInstanceManager` | Vulkan instance, GPU selection | [InstanceManager.CLAUDE.md](InstanceManager.CLAUDE.md) |
| ParticleManager | `gpParticleManager` | GPU particle system | [ParticleManager.CLAUDE.md](ParticleManager.CLAUDE.md) |
| PipelineManager | `gpPipelineManager` | Shader loading, graphics/compute pipelines | [PipelineManager.CLAUDE.md](PipelineManager.CLAUDE.md) |
| SwapchainManager | `gpSwapchainManager` | Swapchain, framebuffers, sync | [SwapchainManager.CLAUDE.md](SwapchainManager.CLAUDE.md) |
| TextManager | `gpTextManager` | Font rendering, text layout | [TextManager.CLAUDE.md](TextManager.CLAUDE.md) |
| TextureManager | `gpTextureManager` | Textures, samplers, render targets | [TextureManager.CLAUDE.md](TextureManager.CLAUDE.md) |
| TextureUploadManager | `gpTextureUploadManager` | Background GPU texture uploads | [TextureUploadManager.CLAUDE.md](TextureUploadManager.CLAUDE.md) |

## Vulkan-Specific Patterns

### Resource Synchronization
- Fence wait required before all GPU resource updates
- Semaphore chain: Image acquisition -> Global -> Main -> ImGui -> Presentation
- Per-framebuffer resource duplication enables parallel frame processing

### Descriptor Management
- Single descriptor pool in DeviceManager serves all pipelines
- Global Set 0 owned by TextureManager, shared by all non-compute graphics pipelines
- Each pipeline manages its own Sets 1 and 2

### Pipeline State
- Pipelines are immutable after creation
- Dynamic state for viewport and scissor
- Pipeline creation crashes if required shader not found
