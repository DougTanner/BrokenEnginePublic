# DeviceManager

Global: `gpDeviceManager`

Manages the logical Vulkan device, queues, and GPU memory allocation. Creates the device with required core and Vulkan 1.2 features, including extended-format storage images, 16-bit storage, non-uniform indexing, update-after-bind, partially bound descriptors, and scalar block layout. Initializes VMA and a single descriptor pool with both FREE_DESCRIPTOR_SET_BIT and UPDATE_AFTER_BIND_BIT. Manages graphics, presentation, and transfer queue handles with deduplication. Queries VK_KHR_maintenance9 for optional QFOT. Owns the shared `mOneShotVkCommandPool` and `mOneShotVkFence` used by all `OneShotCommandBuffer` instances, avoiding per-use pool/fence creation overhead. Because that pool and fence are shared, two `OneShotCommandBuffer` lifetimes must never overlap; an in-use flag asserts on overlap rather than letting the two silently corrupt each other's recording.
