# DeviceManager

**Global**: `gpDeviceManager`

Manages the logical Vulkan device, queues, and GPU memory allocation. Creates device with Vulkan 1.2 features (16-bit storage, non-uniform indexing, update-after-bind, partially bound descriptors, scalar block layout). Initializes VMA and a single descriptor pool with both FREE_DESCRIPTOR_SET_BIT and UPDATE_AFTER_BIND_BIT. Manages graphics, presentation, and transfer queue handles with deduplication. Queries VK_KHR_maintenance9 for optional QFOT.
