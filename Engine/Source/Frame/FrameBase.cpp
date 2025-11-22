#include "FrameBase.h"

#include "Graphics/Islands.h"

#include "Frame/Frame.h"
#include "Frame/Player.h"

namespace engine
{

void FrameInterpolateBase::Update(game::FrameInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	const FrameInterpolateBase& rPrevious = rPreviousFrame.interpolate;

	// AllocateAndCopy phase
	engine::ReallocateAndCopyMetadata(rCurrent.areaLights, rPreviousFrame.interpolate.areaLights, AREA_LIGHTS_INTERPOLATE_LIST(rCurrent.areaLights));

	// Load
	float fSunAngle = rPrevious.fSunAngle;

	// Save
	rCurrent.fSunAngle = fSunAngle;

	// Children
	AreaLightsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
}

void FrameInterpolateBase::Render(int64_t iCommandBuffer) const
{
	areaLights.Render(iCommandBuffer);
}

void FramePostRenderBase::Update(game::FramePostRender& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime)
{
	const game::FramePostRender& rPrevious = rPreviousFrame.postRender;

	// AllocateAndCopy phase
	engine::ReallocateAndCopyMetadata(rCurrent.areaLights, rPreviousFrame.postRender.areaLights, AREA_LIGHTS_POST_RENDER_LIST(rCurrent.areaLights));

	// Load
	common::RandomEngine randomEngine = rPrevious.randomEngine;
	uint64_t uiNextUuid = rPrevious.uiNextUuid;

	// Save
	rCurrent.randomEngine = randomEngine;
	rCurrent.uiNextUuid = uiNextUuid;

	// Children
	AreaLightsPostRender::Update(rCurrent, rPreviousFrame, fDeltaTime);
}

void FramePostRenderBase::Spawn(game::Frame& __restrict rFrame)
{
}

void FramePostRenderBase::Destroy(game::Frame& __restrict rFrame)
{
}

FrameBase::FrameBase()
: f4GlobalArea(engine::gpIslands->mf4GlobalArea)
{
}

void FrameBase::UpdateInterpolate(FrameBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	const FrameBase& rPrevious = rPreviousFrame;

	// Load
	int64_t iFrame = rPrevious.iFrame;
	FrameType eFrameType = rPrevious.eFrameType;
	XMFLOAT4 f4GlobalArea = rPrevious.f4GlobalArea;

	// Update
	++iFrame;

	// Save
	rCurrent.iFrame = iFrame;
	rCurrent.eFrameType = eFrameType;
	rCurrent.f4GlobalArea = f4GlobalArea;

	// Children
}

float DayPercent(const game::Frame& __restrict rFrame)
{
	if (rFrame.interpolate.fSunAngle >= 0.0f && rFrame.interpolate.fSunAngle <= XM_PIDIV2)
	{
		return rFrame.interpolate.fSunAngle / XM_PIDIV2;
	}
	else if (rFrame.interpolate.fSunAngle >= XM_PIDIV2 && rFrame.interpolate.fSunAngle <= XM_PI)
	{
		return 1.0f - (rFrame.interpolate.fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

float NightPercent(const game::Frame& __restrict rFrame)
{
	if (rFrame.interpolate.fSunAngle >= XM_PI && rFrame.interpolate.fSunAngle < XM_PI + XM_PIDIV2)
	{
		return (rFrame.interpolate.fSunAngle - XM_PI) / XM_PIDIV2;
	}
	if (rFrame.interpolate.fSunAngle >= XM_PI + XM_PIDIV2)
	{
		return 1.0f - (rFrame.interpolate.fSunAngle - (XM_PI + XM_PIDIV2)) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

void FrameBase::Render(int64_t iCommandBuffer) const
{
}

void FrameBase::UpdatePostRender(FrameBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime)
{
	// Load

	// Update

	// Save

	// Children

	// Other phases
}

#if 0

#include "Frame/Frame.h"
#include "Input/Input.h"

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
	bool bEqual = common::BreakOnNotEqual(iFrame, rOther.iFrame);
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
	bool bEqual = common::BreakOnNotEqual(navmesh, rOther.navmesh);
	return bEqual;
}

#endif

} // namespace engine
