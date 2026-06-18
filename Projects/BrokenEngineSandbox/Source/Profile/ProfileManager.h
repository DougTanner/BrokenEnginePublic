#pragma once

namespace game
{

enum GameCpuCounters : int64_t
{
	kCpuCounterPlayers = engine::kEngineCpuCounterCount,
	kCpuCounterBlasters,
		kCpuCounterBlastersRendered,
	kCpuCounterMissiles,
		kCpuCounterMissilesRendered,
	kCpuCounterSpaceships,
		kCpuCounterSpaceshipsRendered,
	kCpuCounterTargets,

	kGameCpuCounterCount
};

enum GameCpuTimers : int64_t
{
	kCpuTimerFrameUpdate = engine::kEngineCpuTimerCount,
		kCpuTimerFrameInterpolate,
			kCpuTimerInterpolateAllocateAndCopy,
				kCpuTimerInterpolateAllocateAndCopySpaceships,
			kCpuTimerInterpolateUpdate,
				kCpuTimerInterpolateUpdateSpaceships,
		kCpuTimerFramePostRender,
			kCpuTimerPostRenderAllocateAndCopy,
			kCpuTimerPostRenderUpdate,
				kCpuTimerPostRenderUpdateNavQuery,
				kCpuTimerPostRenderUpdateSpaceships,
			kCpuTimerPostRenderPreCollision,
			kCpuTimerPostRenderCollide,
			kCpuTimerPostRenderPostCollision,
			kCpuTimerPostRenderAreaDamage,
			kCpuTimerPostRenderDestroy,
			kCpuTimerPostRenderSpawn,

	kCpuTimerRender,
		kCpuTimerRenderPlayer,
		kCpuTimerRenderSpaceships,
			kCpuTimerRenderSpaceshipsAnimate,

	kGameCpuTimerCount
};

// One entry per game enumerator, in order; static_assert guards a dropped/extra name (which would otherwise
// silently misalign the overlay rows). Mirrors the engine kXxx*Names tables — see engine ProfileManagerBase.h.
inline constexpr std::string_view kGameCpuCounterNames[]
{
	"Players",
	"Blasters",
	"    Rendered",
	"Missiles",
	"    Rendered",
	"Spaceships",
	"    Rendered",
	"Targets",
};
static_assert(std::size(kGameCpuCounterNames) == static_cast<size_t>(kGameCpuCounterCount - engine::kEngineCpuCounterCount));

inline constexpr std::string_view kGameCpuTimerNames[]
{
	"Frame update",
	"    Interpolate",
	"        AllocateAndCopy",
	"            Spaceships",
	"        Update",
	"            Spaceships",
	"    PostRender",
	"        AllocateAndCopy",
	"        Update",
	"            NavQuery",
	"            Spaceships",
	"        PreCollision",
	"        Collide",
	"        PostCollision",
	"        AreaDamage",
	"        Destroy",
	"        Spawn",
	"Render",
	"    Player",
	"    Spaceships",
	"        Animate",
};
static_assert(std::size(kGameCpuTimerNames) == static_cast<size_t>(kGameCpuTimerCount - engine::kEngineCpuTimerCount));

class ProfileManager : public engine::ProfileManagerBase
{
public:

	ProfileManager();
	~ProfileManager() override;

	engine::CpuCounter& GetCpuCounter(int64_t iIndex) override;
	engine::CpuTimer& GetCpuTimer(int64_t iIndex) override;
	std::string_view GetCpuCounterName(int64_t iIndex) override;
	std::string_view GetCpuTimerName(int64_t iIndex) override;
	int64_t GetCpuCounterCount() const override;
	int64_t GetCpuTimerCount() const override;

#if defined(BT_CLIENT)
	void FormatGameScreens(common::Workbuffer& rWorkbuffer) override;
	void RenderImPlotGraphs() override;

	void SetClockCorrection(int64_t iOffset, int64_t iTargetBehind, int64_t iError);
	void SetReconcileCounters(int64_t iCrcValidated, int64_t iAssumed, int64_t iCrcFastPath, int64_t iStatusChangeReplay, int64_t iKnockOnReplay);
#endif

private:

	// Names live in the kGameCpu*Names tables above (static_assert-guarded); these arrays carry only per-row runtime state.
	engine::CpuCounter mGameCpuCounters[static_cast<int64_t>(kGameCpuCounterCount) - static_cast<int64_t>(engine::kEngineCpuCounterCount)];

	engine::CpuTimer mGameCpuTimers[static_cast<int64_t>(kGameCpuTimerCount) - static_cast<int64_t>(engine::kEngineCpuTimerCount)];

#if defined(BT_CLIENT)
	common::Smoothed<int64_t> mSmoothedRtt;
	common::Smoothed<int64_t> mSmoothedJitter;
	common::Smoothed<int64_t> mSmoothedClockOffset;
	common::Smoothed<int64_t> mSmoothedClockTarget;
	common::Smoothed<int64_t> mSmoothedClockError;
	common::Smoothed<int64_t> mSmoothedRollback;
	common::Smoothed<int64_t> mSmoothedBuffer;
	common::Smoothed<int64_t> mSmoothedRecv;

	common::InTheLastSecond mCrcValidatedTicksPerSecond;
	common::InTheLastSecond mAssumedTicksPerSecond;
	common::InTheLastSecond mCrcFastPathEventsPerSecond;
	common::InTheLastSecond mStatusChangeReplayTicksPerSecond;
	common::InTheLastSecond mKnockOnReplayTicksPerSecond;
#endif
};

extern ProfileManager* gpProfileManager;

} // namespace game

using game::gpProfileManager;
