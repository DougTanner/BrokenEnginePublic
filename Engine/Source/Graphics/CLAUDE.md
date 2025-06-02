# /Engine/Source/Graphics/

The `/Engine/Source/Graphics/` directory contains the Vulkan-based rendering system.

## Core Files

### Graphics.h & Graphics.cpp
**Global Access**: `gpGraphics`  
**Purpose**: Central orchestrator containing all rendering managers and subsystems  
- Initializes and manages all graphics managers (Device, Instance, Swapchain, etc.)
- Handles frame synchronization with multiple frames in flight
- Coordinates the rendering pipeline from initialization to presentation
- Provides high-level rendering interface for game systems
- Processes pending texture loads from lazy loading system after fence wait

### Islands.h & Islands.cpp
**Purpose**: Island-based terrain rendering system  
- Loads and renders terrain chunks from island data files
- Height-based terrain rendering with ambient occlusion
- Terrain mesh generation and level-of-detail support
- Integration with global terrain rendering pipeline

### OneShotCommandBuffer.h & OneShotCommandBuffer.cpp
**Purpose**: Utility for immediate GPU operations  
- Executes single-use command buffers for immediate operations
- Buffer uploads and image layout transitions
- Mipmap generation and texture operations
- Synchronous GPU command execution

### Screenshot.h & Screenshot.cpp
**Purpose**: Frame capture functionality  
- GPU-to-CPU image transfer for screenshot capture
- PNG file writing via async thread
- Frame buffer reading and format conversion
- Handles swap chain image capture with proper synchronization

## Manager Dependencies & Initialization Order

**Initialization Order** (critical for proper startup):
1. **InstanceManager** - Creates Vulkan instance and selects physical device (no dependencies)
2. **DeviceManager** - Creates logical device (depends on: InstanceManager)
3. **SwapchainManager** - Creates swap chain and framebuffers (depends on: DeviceManager, InstanceManager)
4. **ShaderManager** - Loads shader modules (depends on: DeviceManager, FileManager)
5. **TextureManager** - Creates textures and render targets (depends on: DeviceManager, SwapchainManager, FileManager)
6. **BufferManager** - Allocates GPU buffers (depends on: DeviceManager, FileManager for model data)
7. **PipelineManager** - Creates render pipelines (depends on: DeviceManager, ShaderManager, SwapchainManager)
8. **CommandBufferManager** - Allocates command buffers (depends on: DeviceManager, SwapchainManager)
9. **ParticleManager** - Initializes particle systems (depends on: DeviceManager, BufferManager, PipelineManager)
10. **TextManager** - Initializes text rendering (depends on: DeviceManager, TextureManager, FileManager for fonts)

**Runtime Dependencies**:
- **Graphics** (main orchestrator) depends on: All managers, FileManager (for Islands loading)
- **CommandBufferManager** depends on: All other managers during command recording
- **BufferManager** depends on: FileManager (loads model vertex data)
- **TextureManager** depends on: FileManager (loads texture data)
- **TextManager** depends on: FileManager (loads font data)

**External System Dependencies**:
- **FileManager** (from `/Engine/Source/File/`) - Required for loading all assets
- **Frame System** (from `/Engine/Source/Frame/`) - Provides game state for rendering
- **UI System** (from `/Engine/Source/Ui/`) - Uses TextManager for widget text rendering

## See Also
- Managers: [Managers/CLAUDE.md](Managers/CLAUDE.md) - High-level resource managers for Vulkan rendering (buffers, textures, pipelines, etc.)
- Objects: [Objects/CLAUDE.md](Objects/CLAUDE.md) - RAII wrappers for low-level Vulkan resources (VkBuffer, VkPipeline, VkImage, etc.)