# InstanceManager

Global: `gpInstanceManager`

Manages Vulkan instance and physical device selection. Creates instance with Vulkan 1.2 requirement, selects GPU, creates Win32 surface. Capability validation runs against the finally selected device before logical-device creation and covers selected required core/Vulkan 1.2 features plus format-specific optimal-tiling support; `DeviceManager` must enable every feature required here. Configurable validation layers with GPU-assisted validation and Debug Printf support. Under RenderDoc (`renderdoc.dll` loaded), the validation layer, its `VK_EXT_layer_settings` extension, and the `VkLayerSettingsCreateInfoEXT` `pNext` chain are all dropped together (enabling only surface, win32 surface, portability enumeration, and debug utils). Retry path on a clean machine without the Vulkan SDK drops the same `pNext` plus the portability flag to stay spec-valid.

Exposes a RenderDoc in-app API pointer whenever the build opts in (`kbRenderDoc`) and `renderdoc.dll` is loaded — lets other systems drive programmatic captures, and configures the capture-file path template under `%TEMP%\RenderDoc`. The `--renderdoc` launch option force-loads `renderdoc.dll` before instance creation so RenderDoc's "Attach to running instance" works without launching through its UI, at the cost of validation layers.
