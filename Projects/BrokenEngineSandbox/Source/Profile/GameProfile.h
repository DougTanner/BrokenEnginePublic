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
				kCpuTimerInterpolateAllocateAndCopySpaceships, \
			kCpuTimerInterpolateUpdate, \
				kCpuTimerInterpolateUpdateSpaceships, \
		kCpuTimerFramePostRender, \
			kCpuTimerPostRenderAllocateAndCopy, \
			kCpuTimerPostRenderUpdate, \
				kCpuTimerPostRenderUpdateSpaceships, \
			kCpuTimerPostRenderPreCollision, \
			kCpuTimerPostRenderCollide, \
			kCpuTimerPostRenderPostCollision, \
			kCpuTimerPostRenderAreaDamage, \
			kCpuTimerPostRenderDestroy, \
			kCpuTimerPostRenderSpawn, \

#define CPU_TIMERS_GAME \
CpuTimer {.name = "Frame update"}, \
CpuTimer {.name = "    Interpolate"}, \
CpuTimer {.name = "        AllocateAndCopy"}, \
CpuTimer {.name = "            Spaceships"}, \
CpuTimer {.name = "        Update"}, \
CpuTimer {.name = "            Spaceships"}, \
CpuTimer {.name = "    PostRender"}, \
CpuTimer {.name = "        AllocateAndCopy"}, \
CpuTimer {.name = "        Update"}, \
CpuTimer {.name = "            Spaceships"}, \
CpuTimer {.name = "        PreCollision"}, \
CpuTimer {.name = "        Collide"}, \
CpuTimer {.name = "        PostCollision"}, \
CpuTimer {.name = "        AreaDamage"}, \
CpuTimer {.name = "        Destroy"}, \
CpuTimer {.name = "        Spawn"}, \
