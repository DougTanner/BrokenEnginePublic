#include "FrameBase.h"

#include "Graphics/Islands.h"

#include "Frame/Frame.h"
#include "Input/Input.h"


namespace engine
{

FrameBase::FrameBase(IslandsFlip eInitialIslandsFlip)
{
	global.eIslandsFlip = eInitialIslandsFlip;
	gpIslands->SetIslandsFlip(eInitialIslandsFlip);

	global.f4GlobalArea = gpIslands->mf4GlobalArea;

	Navmesh::SetupGrid(global.f4GlobalArea, full.navmesh);
}

void WriteFrameGlobalBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	SCOPED_CPU_PROFILE(kCpuTimerFrameCamera);

	auto& rGlobal = rFrame.global;
	const auto& rPreviousGlobal = rPreviousFrame.global;

	ASSERT(rPreviousGlobal.eFrameType == FrameType::kPostRender);
	gCurrentFrameTypeProcessing = FrameType::kCamera;

	rGlobal.fDeltaTime = fDeltaTime;

	rGlobal.iFrame = rPreviousGlobal.iFrame + 1;
	rGlobal.eFrameType = FrameType::kCamera;
	rGlobal.eIslandsFlip = rPreviousGlobal.eIslandsFlip;
	engine::gpIslands->SetIslandsFlip(rGlobal.eIslandsFlip); // DT: TODO This shouldn't be here

	rGlobal.fCurrentTime = rPreviousGlobal.fCurrentTime + rGlobal.fDeltaTime;
	rGlobal.randomEngine = rPreviousGlobal.randomEngine;

	WriteFrameGlobal(rFrame, rPreviousFrame, game::FrameInputHeld(), rGlobal.fDeltaTime);

	GlobalList(rFrame, rPreviousFrame, game::FrameInputHeld(), rGlobal.fDeltaTime, UPDATE_LIST);
}

// Interpolation phase: Copy pools from previous frame and interpolate positions/rotations
void WriteFrameInterpolateBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput)
{
	SCOPED_CPU_PROFILE(kCpuTimerFrameInterpolate);

	const auto& rGlobal = rFrame.global;
	auto& rInterpolate = rFrame.interpolate;
	const auto& rPreviousInterpolate = rPreviousFrame.interpolate;

	ASSERT(rGlobal.eFrameType == FrameType::kCamera);
	gCurrentFrameTypeProcessing = FrameType::kInterpolate;
	rFrame.global.eFrameType = FrameType::kInterpolate;

	Areas::Copy(rInterpolate.enemyAreas, rPreviousInterpolate.enemyAreas);
	Areas::Copy(rInterpolate.playerAreas, rPreviousInterpolate.playerAreas);
	AreaLights::Copy(rInterpolate.areaLights, rPreviousInterpolate.areaLights);
	Billboards::Copy(rInterpolate.billboards, rPreviousInterpolate.billboards);
	Explosions::Copy(rInterpolate.explosions, rPreviousInterpolate.explosions);
	HexShields::Copy(rInterpolate.hexShields, rPreviousInterpolate.hexShields);
	PointLights::Copy(rInterpolate.pointLights, rPreviousInterpolate.pointLights);
		rInterpolate.pointLightControllers2.UpdateMain(rInterpolate.pointLightControllers2, rPreviousInterpolate.pointLightControllers2, rInterpolate.pointLights, rGlobal.fCurrentTime);
		rInterpolate.pointLightControllers3.UpdateMain(rInterpolate.pointLightControllers3, rPreviousInterpolate.pointLightControllers3, rInterpolate.pointLights, rGlobal.fCurrentTime);
	Puffs::Copy(rInterpolate.puffs, rPreviousInterpolate.puffs);
		rInterpolate.puffControllers2.UpdateMain(rInterpolate.puffControllers2, rPreviousInterpolate.puffControllers2, rInterpolate.puffs, rGlobal.fCurrentTime);
		rInterpolate.puffControllers3.UpdateMain(rInterpolate.puffControllers3, rPreviousInterpolate.puffControllers3, rInterpolate.puffs, rGlobal.fCurrentTime);
	Pullers::Copy(rInterpolate.pullers, rPreviousInterpolate.pullers);
	Pushers::Copy(rInterpolate.pushers, rPreviousInterpolate.pushers);
	Sounds::Copy(rInterpolate.sounds, rPreviousInterpolate.sounds);
	Splashes::Copy(rInterpolate.splashes, rPreviousInterpolate.splashes);
	Targets::Copy(rInterpolate.targets, rPreviousInterpolate.targets);
	Trails::Copy(rInterpolate.trails, rPreviousInterpolate.trails);

	WriteFrameInterpolate(rFrame, rPreviousFrame, rFrameInput.held, rGlobal.fDeltaTime);

	InterpolateList(rFrame, rPreviousFrame, rFrameInput.held, rGlobal.fDeltaTime, UPDATE_LIST);

	Explosions::Interpolate(rFrame);
	Targets::Interpolate(rFrame);
}

// PostRender phase: PostRender, collision, spawning, and destruction
void WriteFrameFullBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput)
{
	SCOPED_CPU_PROFILE(kCpuTimerFramePostRender);

	const auto& rGlobal = rFrame.global;
	auto& rInterpolate = rFrame.interpolate;
	auto& rFull = rFrame.full;

	ASSERT(rGlobal.eFrameType == FrameType::kInterpolate);
	gCurrentFrameTypeProcessing = FrameType::kPostRender;
	rFrame.global.eFrameType = FrameType::kPostRender;

	rFull.navmesh.SetupPlayerDistances(rFrame, rPreviousFrame);
	rInterpolate.pushers.SetupZones(rFrame);

	WriteFrameFull(rFrame, rPreviousFrame, rFrameInput, rGlobal.fDeltaTime);

	PostRenderList(rFrame, rPreviousFrame, rFrameInput, rGlobal.fDeltaTime, UPDATE_LIST);
	Splashes::PostRender(rFrame, rGlobal.fDeltaTime);

	CollideList(rFrame, rPreviousFrame, rFrameInput, rGlobal.fDeltaTime, UPDATE_LIST);

	// Spawn second to last, because a spawned object has no information in the previous frame
	WriteFrameFullSpawn(rFrame, rPreviousFrame, rFrameInput, rGlobal.fDeltaTime);
	SpawnList(rFrame, rPreviousFrame, rFrameInput, rGlobal.fDeltaTime, UPDATE_LIST);

	// Destroy last, because this desynchronizes indices from previous frame
	WriteFrameFullDestroy(rFrame, rPreviousFrame, rFrameInput, rGlobal.fDeltaTime);
	DestroyList(rFrame, rPreviousFrame, rFrameInput, rGlobal.fDeltaTime, UPDATE_LIST);

#if defined(ENABLE_PROFILING)
	gpProfileManager->mUpdatesInTheLastSecond.Set();
#endif
}

} // namespace engine
