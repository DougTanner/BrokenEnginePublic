#include "FrameBase.h"

#include "Graphics/Islands.h"

#include "Frame/Frame.h"
#include "Input/Input.h"


namespace engine
{

// Interpolation phase: Copy pools from previous frame and interpolate positions/rotations
void WriteFrameInterpolateBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime)
{
	SCOPED_CPU_PROFILE(kCpuTimerFrameInterpolate);

	game::FrameInterpolate& rInterpolate = rFrame.interpolate;
	const game::FrameInterpolate& rPreviousInterpolate = rPreviousFrame.interpolate;

	ASSERT(rPreviousInterpolate.eFrameType == FrameType::kPostRender);
	gCurrentFrameTypeProcessing = FrameType::kInterpolate;

	rInterpolate.fDeltaTime = fDeltaTime;
	rInterpolate.iFrame = rPreviousInterpolate.iFrame + 1;
	rInterpolate.eFrameType = FrameType::kInterpolate;
	rInterpolate.fCurrentTime = rPreviousInterpolate.fCurrentTime + rInterpolate.fDeltaTime;
	rInterpolate.randomEngine = rPreviousInterpolate.randomEngine;

	WriteFrameInterpolate(rFrame, rPreviousFrame, rFrameInputHeld, rInterpolate.fDeltaTime);

	Areas::Copy(rInterpolate.enemyAreas, rPreviousInterpolate.enemyAreas);
	Areas::Copy(rInterpolate.playerAreas, rPreviousInterpolate.playerAreas);
	AreaLights::Copy(rInterpolate.areaLights, rPreviousInterpolate.areaLights);
	Billboards::Copy(rInterpolate.billboards, rPreviousInterpolate.billboards);
	Explosions::Copy(rInterpolate.explosions, rPreviousInterpolate.explosions);
	HexShields::Copy(rInterpolate.hexShields, rPreviousInterpolate.hexShields);
	PointLights::Copy(rInterpolate.pointLights, rPreviousInterpolate.pointLights);
		rInterpolate.pointLightControllers2.UpdateMain(rInterpolate.pointLightControllers2, rPreviousInterpolate.pointLightControllers2, rInterpolate.pointLights, rInterpolate.fCurrentTime);
		rInterpolate.pointLightControllers3.UpdateMain(rInterpolate.pointLightControllers3, rPreviousInterpolate.pointLightControllers3, rInterpolate.pointLights, rInterpolate.fCurrentTime);
	Puffs::Copy(rInterpolate.puffs, rPreviousInterpolate.puffs);
		rInterpolate.puffControllers2.UpdateMain(rInterpolate.puffControllers2, rPreviousInterpolate.puffControllers2, rInterpolate.puffs, rInterpolate.fCurrentTime);
		rInterpolate.puffControllers3.UpdateMain(rInterpolate.puffControllers3, rPreviousInterpolate.puffControllers3, rInterpolate.puffs, rInterpolate.fCurrentTime);
	Pullers::Copy(rInterpolate.pullers, rPreviousInterpolate.pullers);
	Pushers::Copy(rInterpolate.pushers, rPreviousInterpolate.pushers);
	Sounds::Copy(rInterpolate.sounds, rPreviousInterpolate.sounds);
	Splashes::Copy(rInterpolate.splashes, rPreviousInterpolate.splashes);
	Targets::Copy(rInterpolate.targets, rPreviousInterpolate.targets);
	Trails::Copy(rInterpolate.trails, rPreviousInterpolate.trails);

	InterpolateList(rFrame, rPreviousFrame, rFrameInputHeld, rInterpolate.fDeltaTime, UPDATE_LIST);

	Explosions::Interpolate(rFrame);
	Targets::Interpolate(rFrame);
}

// PostRender phase: PostRender, collision, spawning, and destruction
void WriteFramePostRenderBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, const game::FrameInputPressed& __restrict rFrameInputPressed)
{
	SCOPED_CPU_PROFILE(kCpuTimerFramePostRender);

	game::FrameInterpolate& rInterpolate = rFrame.interpolate;
	game::FramePostRender& rPostRender = rFrame.postRender;

	ASSERT(rInterpolate.eFrameType == FrameType::kInterpolate);
	gCurrentFrameTypeProcessing = FrameType::kPostRender;
	rFrame.interpolate.eFrameType = FrameType::kPostRender;

	rPostRender.navmesh.SetupPlayerDistances(rFrame, rPreviousFrame);
	rInterpolate.pushers.SetupZones(rFrame);

	WriteFramePostRender(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rInterpolate.fDeltaTime);

	PostRenderList(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rInterpolate.fDeltaTime, UPDATE_LIST);
	Splashes::PostRender(rFrame, rInterpolate.fDeltaTime);

	CollideList(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rInterpolate.fDeltaTime, UPDATE_LIST);

	// Spawn second to last, because a spawned object has no information in the previous frame
	WriteFramePostRenderSpawn(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rInterpolate.fDeltaTime);
	SpawnList(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rInterpolate.fDeltaTime, UPDATE_LIST);

	// Destroy last, because this desynchronizes indices from previous frame
	WriteFramePostRenderDestroy(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rInterpolate.fDeltaTime);
	DestroyList(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rInterpolate.fDeltaTime, UPDATE_LIST);
}

bool FrameBaseInterpolate::operator==(const FrameBaseInterpolate& rOther) const
{
	bool bEqual = true;

	bEqual &= common::BreakOnNotEqual(iFrame, rOther.iFrame);
	bEqual &= common::BreakOnNotEqual(eFrameType, rOther.eFrameType);
	bEqual &= common::BreakOnNotEqual(randomEngine, rOther.randomEngine);
	bEqual &= common::BreakOnNotEqual(fCurrentTime, rOther.fCurrentTime);
	bEqual &= common::BreakOnNotEqual(fSunAngle, rOther.fSunAngle);
	bEqual &= common::BreakOnNotEqual(f4GlobalArea, rOther.f4GlobalArea);

	bEqual &= common::BreakOnNotEqual(enemyAreas, rOther.enemyAreas);
	bEqual &= common::BreakOnNotEqual(playerAreas, rOther.playerAreas);
	bEqual &= common::BreakOnNotEqual(areaLights, rOther.areaLights);
	bEqual &= common::BreakOnNotEqual(billboards, rOther.billboards);
	bEqual &= common::BreakOnNotEqual(explosions, rOther.explosions);
	bEqual &= common::BreakOnNotEqual(hexShields, rOther.hexShields);
	bEqual &= common::BreakOnNotEqual(pointLights, rOther.pointLights);
	bEqual &= common::BreakOnNotEqual(pointLightControllers2, rOther.pointLightControllers2);
	bEqual &= common::BreakOnNotEqual(pointLightControllers3, rOther.pointLightControllers3);
	bEqual &= common::BreakOnNotEqual(puffs, rOther.puffs);
	bEqual &= common::BreakOnNotEqual(puffControllers2, rOther.puffControllers2);
	bEqual &= common::BreakOnNotEqual(puffControllers3, rOther.puffControllers3);
	bEqual &= common::BreakOnNotEqual(pullers, rOther.pullers);
	bEqual &= common::BreakOnNotEqual(pushers, rOther.pushers);
	bEqual &= common::BreakOnNotEqual(sounds, rOther.sounds);
	bEqual &= common::BreakOnNotEqual(splashes, rOther.splashes);
	bEqual &= common::BreakOnNotEqual(targets, rOther.targets);
	bEqual &= common::BreakOnNotEqual(trails, rOther.trails);

	return bEqual;
}

bool FrameBasePostRender::operator==(const FrameBasePostRender& rOther) const
{
	bool bEqual = true;

	bEqual &= common::BreakOnNotEqual(navmesh, rOther.navmesh);

	return bEqual;
}

} // namespace engine
