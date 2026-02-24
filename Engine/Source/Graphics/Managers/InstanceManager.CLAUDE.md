# InstanceManager

**Global**: `gpInstanceManager`

Manages Vulkan instance and physical device selection. Creates instance with Vulkan 1.2 requirement, selects GPU, creates Win32 surface. Configurable validation layers with GPU-assisted validation and Debug Printf support. Retries without validation if SDK not installed. Disables validation under RenderDoc.
