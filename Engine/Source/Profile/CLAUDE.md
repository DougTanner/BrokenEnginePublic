# `/Engine/Source/Profile/`

CPU/GPU timing, boot metrics, and performance counters.

**Global**: `gpProfileManager`  
**Conditional**: Requires `ENABLE_PROFILING`

## ProfileManager

### CPU Tracking

**Counters** - Integer metrics for subsystems
- Engine: Billboards, HexShields, Lights, Smoke, Controllers, Explosions, Pushers, Sounds
- Game: Add via `CPU_COUNTERS_GAME_ENUM`

**Timers** - High-resolution timing with smoothed values
- Frame/Render (global, main)
- Input, audio, message processing
- Vulkan sync (fences, acquire, present)

### GPU Tracking

**Timers** - Vulkan timestamp queries
- Render passes: Shadow, Terrain, Smoke, Particles, Lighting, Objects, Water, Text
- Hierarchical: Global → Main → Image passes
- **Timestamp Support Validation**: Validates graphics queue family supports timestamps (`timestampValidBits > 0`) before creating query pool
- **Graceful Degradation**: If timestamps unsupported, GPU profiling is disabled (query pool remains VK_NULL_HANDLE, methods return early)
- **Query Reset Strategy**: Two-phase GPU-side reset approach:
  - **Initial Reset**: One-time reset via `OneShotCommandBuffer` in `Create()` after query pool creation
    - Puts all queries into "unavailable" (ready-to-use) state before command buffer recording
    - Required by Vulkan spec - queries must be reset before first use
  - **Per-Frame Reset**: `vkCmdResetQueryPool` in command buffer recording:
    - `ResetGlobalQueryPools()` - Resets global timer queries at start of global command buffer
    - `ResetMainQueryPools()` - Resets main timer queries at start of main command buffer
    - `ResetImageQueryPools()` - Resets image timer queries at start of image command buffer
    - Called from CommandBufferManager during command buffer recording
    - Works with "record-once, submit-many" pattern - reset executes on GPU each frame
- **Query Result Reading**: Uses `VK_QUERY_RESULT_WAIT_BIT` to ensure GPU timestamp writes have completed before reading results
- **Synchronization**: Fence wait + WAIT_BIT guarantees queries ready to read before next frame's reset

### Boot Tracking

**Timers** - One-time initialization measurements
- Vulkan managers, texture loading, command recording
- Auto-logs times >10ms

### Key Methods
- `CpuStart/Stop()` - Bracket CPU sections
- `GpuStart/Stop()` - Insert GPU queries
- `GpuRead()` - Retrieve GPU results
- `UpdateProfileText()` - Format display
- `ToggleProfileText()` - Show/hide overlay

### Usage
- `SCOPED_CPU_PROFILE` - Auto timer management
- `GPU_PROFILE_READ` - GPU timer retrieval
- Game-specific counters via `Profile/GameProfile.h`