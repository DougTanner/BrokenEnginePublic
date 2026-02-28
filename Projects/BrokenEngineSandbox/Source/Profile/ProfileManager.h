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

class ProfileManager : public engine::ProfileManagerBase
{
public:

	ProfileManager();
	~ProfileManager() override;

	engine::CpuCounter& GetCpuCounter(int64_t iIndex) override;
	engine::CpuTimer& GetCpuTimer(int64_t iIndex) override;
	int64_t GetCpuCounterCount() const override;
	int64_t GetCpuTimerCount() const override;

private:

	engine::CpuCounter mGameCpuCounters[static_cast<int64_t>(kGameCpuCounterCount) - static_cast<int64_t>(engine::kEngineCpuCounterCount)]
	{
		{.name = "Players"},
		{.name = "Blasters"},
		{.name = "    Rendered"},
		{.name = "Missiles"},
		{.name = "    Rendered"},
		{.name = "Spaceships"},
		{.name = "    Rendered"},
		{.name = "Targets"},
	};

	engine::CpuTimer mGameCpuTimers[static_cast<int64_t>(kGameCpuTimerCount) - static_cast<int64_t>(engine::kEngineCpuTimerCount)]
	{
		{.name = "Frame update"},
		{.name = "    Interpolate"},
		{.name = "        AllocateAndCopy"},
		{.name = "            Spaceships"},
		{.name = "        Update"},
		{.name = "            Spaceships"},
		{.name = "    PostRender"},
		{.name = "        AllocateAndCopy"},
		{.name = "        Update"},
		{.name = "            Spaceships"},
		{.name = "        PreCollision"},
		{.name = "        Collide"},
		{.name = "        PostCollision"},
		{.name = "        AreaDamage"},
		{.name = "        Destroy"},
		{.name = "        Spawn"},
		{.name = "Render"},
		{.name = "    Player"},
		{.name = "    Spaceships"},
		{.name = "        Animate"},
	};
};

extern ProfileManager* gpProfileManager;

} // namespace game

using game::gpProfileManager;
