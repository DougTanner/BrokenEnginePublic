// DT: TODO Figure out a better way to do this, this breaks intellisense
#define CPU_COUNTERS_GAME_ENUM \
kCpuCounterBlasters, \
kCpuCounterMissiles, \
kCpuCounterMissilesRendered, \
kCpuCounterSpaceships, \
kCpuCounterSpaceshipsRendered, \
kCpuCounterTargets, \

#define CPU_COUNTERS_GAME \
CpuCounter {.name = "Blasters" }, \
CpuCounter {.name = "Missiles"}, \
CpuCounter {.name = "    Rendered"}, \
CpuCounter {.name = "Spaceships"}, \
CpuCounter {.name = "    Rendered"}, \
CpuCounter {.name = "Targets"}, \

#define CPU_TIMERS_GAME_ENUM \
	kCpuTimerFrameUpdate, \
		kCpuTimerFrameInterpolate, \
			kCpuTimerInterpolateAllocateAndCopy, \
			kCpuTimerInterpolateUpdate, \
		kCpuTimerFramePostRender, \
			kCpuTimerPostRenderAllocateAndCopy, \
			kCpuTimerPostRenderUpdate, \
			kCpuTimerPostRenderPreCollision, \
			kCpuTimerPostRenderCollide, \
			kCpuTimerPostRenderPostCollision, \
			kCpuTimerPostRenderAreaDamage, \
			kCpuTimerPostRenderDestroy, \
			kCpuTimerPostRenderSpawn, \

#define CPU_TIMERS_GAME \
CpuTimer {.pcName = "Frame update"}, \
CpuTimer {.pcName = "    Interpolate"}, \
CpuTimer {.pcName = "        AllocateAndCopy"}, \
CpuTimer {.pcName = "        Update"}, \
CpuTimer {.pcName = "    PostRender"}, \
CpuTimer {.pcName = "        AllocateAndCopy"}, \
CpuTimer {.pcName = "        Update"}, \
CpuTimer {.pcName = "        PreCollision"}, \
CpuTimer {.pcName = "        Collide"}, \
CpuTimer {.pcName = "        PostCollision"}, \
CpuTimer {.pcName = "        AreaDamage"}, \
CpuTimer {.pcName = "        Destroy"}, \
CpuTimer {.pcName = "        Spawn"}, \
