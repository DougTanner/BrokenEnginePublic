// DT: TODO Figure out a better way to do this, this breaks intellisense
#define CPU_COUNTERS_GAME_ENUM \
kCpuCounterBlasters, \
kCpuCounterMissiles, \
kCpuCounterMissilesRendered, \
kCpuCounterSpaceships, \
kCpuCounterSpaceshipsRendered, \

#define CPU_COUNTERS_GAME \
CpuCounter {.name = "Blasters" }, \
CpuCounter {.name = "Missiles"}, \
CpuCounter {.name = "    Rendered"}, \
CpuCounter {.name = "Spaceships"}, \
CpuCounter {.name = "    Rendered"}, \

#define CPU_TIMERS_GAME_ENUM \
	kCpuTimerFrameUpdate, \
		kCpuTimerFrameMain, \
			kCpuTimerControllers, \
		kCpuTimerFramePostRender, \
			kCpuTimerPusherZones, \
			kCpuTimerPlayerDistances, \
			kCpuTimerPostRenderSpaceships, \
				kCpuTimerPostRenderSpaceshipsAvoidTerrain, \
				kCpuTimerPostRenderSpaceshipsPushers, \
		kCpuTimerCollision, \
			kCpuTimerCollisionSpaceshipsBlasters, \
			kCpuTimerCollisionSpaceshipsMissiles, \

#define CPU_TIMERS_GAME \
CpuTimer {.pcName = "Frame update"}, \
CpuTimer {.pcName = "    Main"}, \
CpuTimer {.pcName = "        Controllers"}, \
CpuTimer {.pcName = "    Post render"}, \
CpuTimer {.pcName = "        Pusher zones"}, \
CpuTimer {.pcName = "        Player distances"}, \
CpuTimer {.pcName = "        Spaceships"}, \
CpuTimer {.pcName = "            Avoid terrain"}, \
CpuTimer {.pcName = "            Pushers"}, \
CpuTimer {.pcName = "    Collision"}, \
CpuTimer {.pcName = "        Spaceships collide blasters"}, \
CpuTimer {.pcName = "        Spaceships collide missiles"}, \
