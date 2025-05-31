# /Engine/Source/Profile/

The `/Engine/Source/Profile/` directory contains the engine's performance profiling system that tracks CPU timers, GPU timers, boot timers, and various performance counters. The profiling system is conditionally compiled based on the `ENABLE_PROFILING` define.

## Core Files

### ProfileManager.h & ProfileManager.cpp

### Core Functionality
- **ProfileManager** - Central singleton manager (`gpProfileManager`) for all profiling operations
- Integrates with Vulkan query pools for GPU timing
- Thread-aware CPU timing with high-resolution clocks
- Real-time performance text display via TextManager
- Uses smoothed values for stable display of timing data

### CPU Performance Tracking

#### CpuCounter Structure
- Tracks integer counters for various engine subsystems
- Predefined counters include: Billboards, HexShields, Lights, Smoke, Controllers, Explosions, Pushers, Sounds
- Game-specific counters can be added via `CPU_COUNTERS_GAME_ENUM`

#### CpuTimer Structure  
- High-resolution timing for CPU operations
- Tracks total frame time, thread count, and smoothed microsecond values
- Key timers include:
  - Frame/Render timing (global, main)
  - Input and message processing
  - Audio updates
  - Vulkan synchronization (fences, image acquisition, presentation)
  - Profile text updates

### GPU Performance Tracking

#### GpuTimer Structure
- Uses Vulkan timestamp queries
- Tracks rendering passes: Shadow, Terrain layers, Smoke, Particles, Lighting, Objects, Water, Text
- Organized hierarchically (Global → Main → Image render passes)

### Boot Performance Tracking

#### BootTimer Structure
- One-time measurements during engine initialization
- Tracks initialization of: Vulkan managers, texture loading, command buffer recording
- Logs boot times over 10ms threshold

### Key Methods
- `CpuStart/CpuStop()` - Bracket CPU timing sections
- `GpuStart/GpuStop()` - Insert GPU timestamp queries  
- `GpuRead()` - Retrieve GPU timing results
- `UpdateProfileText()` - Format and display performance metrics
- `ToggleProfileText()` - Show/hide performance overlay
- `LogTimers()` - Output all timers to log

### Integration Points
- Requires Graphics managers (Device, CommandBuffer, Text, Instance)
- Includes game-specific profile header via `Profile/GameProfile.h`
- Uses `SCOPED_CPU_PROFILE` macro for automatic timer management
- Provides `GPU_PROFILE_READ` macro for GPU timer retrieval