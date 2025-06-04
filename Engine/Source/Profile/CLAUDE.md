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