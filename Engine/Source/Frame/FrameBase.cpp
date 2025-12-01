#include "FrameBase.h"

#include "Frame/Collision.h"
#include "Graphics/Graphics.h"

#include "Frame/Frame.h"
#include "Frame/Player.h"

namespace engine
{

void FrameInterpolateBase::Update([[maybe_unused]] game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	const FrameInterpolateBase& rPrevious = rPreviousFrame.interpolate;

	// Load
	int64_t iFrame = rPrevious.iFrame;
	float fSunAngle = rPrevious.fSunAngle;
	float fCurrentTime = rPrevious.fCurrentTime;

	// Update
	++iFrame;
	fCurrentTime += fDeltaTime;

	// Save
	rCurrent.iFrame = iFrame;
	rCurrent.fSunAngle = fSunAngle;
	rCurrent.fCurrentTime = fCurrentTime;

	// Update collections
	AreaLightsInterpolate::Update(rCurrent.areaLights, rPrevious.areaLights);
	BillboardsInterpolate::Update(rCurrent.billboards, rPrevious.billboards);
	ExplosionsInterpolate::Update(rCurrent.explosions, rPrevious.explosions, fCurrentTime);
	PointLightsInterpolate::Update(rCurrent.pointLights, rPrevious.pointLights, fCurrentTime);
	PuffsInterpolate::Update(rCurrent.puffs, rPrevious.puffs, fCurrentTime);
	PushersInterpolate::Update(rCurrent.pushers, rPrevious.pushers, fDeltaTime);
	TrailsInterpolate::Update(rCurrent.trails, rPrevious.trails, fCurrentTime);
}

void FrameInterpolateBase::Sync([[maybe_unused]] game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	AreaLightsInterpolate::Sync(rCurrent.areaLights, rPreviousFrame, fDeltaTime);
	BillboardsInterpolate::Sync(rCurrent.billboards, rPreviousFrame, fDeltaTime);
	ExplosionsInterpolate::Sync(rCurrent, rPreviousFrame, fDeltaTime);
	PointLightsInterpolate::Sync(rCurrent.pointLights, rPreviousFrame, fDeltaTime);
	PuffsInterpolate::Sync(rCurrent.puffs, rPreviousFrame, fDeltaTime);
	TrailsInterpolate::Sync(rCurrent.trails, rPreviousFrame, fDeltaTime);
}

void FrameInterpolateBase::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	AreaLightsInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	BillboardsInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	PointLightsInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	PuffsInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	TrailsInterpolate::Render(rFrameInterpolate, iCommandBuffer);
}

void FramePostRenderBase::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime, [[maybe_unused]] const game::FrameInput& __restrict rFrameInput)
{
	game::FramePostRender& rCurrent = rFrame.postRender;
	const game::FramePostRender& rPrevious = rPreviousFrame.postRender;

	// Load
	common::RandomEngine randomEngine = rPrevious.randomEngine;
	int64_t iNextUuid = rPrevious.iNextUuid;

	// Save
	rCurrent.randomEngine = randomEngine;
	rCurrent.iNextUuid = iNextUuid;

	// Update
	AreaLightsPostRender::Update(rCurrent.areaLights, rPrevious.areaLights);
	BillboardsPostRender::Update(rCurrent.billboards, rPrevious.billboards);
	ExplosionsPostRender::Update(rFrame, rPreviousFrame);
	PointLightsPostRender::Update(rCurrent.pointLights, rPrevious.pointLights);
	PuffsPostRender::Update(rCurrent.puffs, rPrevious.puffs);
	PushersPostRender::Update(rCurrent.pushers, rPrevious.pushers);
	TrailsPostRender::Update(rCurrent.trails, rPrevious.trails);

	// Setup pusher zones for spatial acceleration (needs player position from interpolate)
	PushersInterpolate::SetupZones(rFrame);
}

void FramePostRenderBase::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void FramePostRenderBase::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void FramePostRenderBase::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void FramePostRenderBase::Destroy([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	ExplosionsPostRender::Destroy(rFrame, rFrame.interpolate.fCurrentTime);
	PointLightsPostRender::Destroy(rFrame, rFrame.interpolate.fCurrentTime);
	PuffsPostRender::Destroy(rFrame, rFrame.interpolate.fCurrentTime);
	// Trails don't auto-destroy - external code removes them via Remove()
}

FrameBase::FrameBase()
: f4GlobalArea(engine::gpIslands->mf4GlobalArea)
{
}

void FrameBase::Register()
{
}

void FrameBase::AllocateGraphicsResources()
{
	engine::AreaLightsInterpolate::AllocatePipelines();
	engine::BillboardsInterpolate::AllocatePipelines();
	engine::PointLightsInterpolate::AllocatePipelines();
	engine::PuffsInterpolate::AllocatePipelines();
	engine::TrailsInterpolate::AllocatePipelines();
}

void FrameBase::InterpolateUpdate([[maybe_unused]] FrameBase& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	const FrameBase& rPrevious = rPreviousFrame;

	// Load
	FrameType eFrameType = rPrevious.eFrameType;
	XMFLOAT4 f4GlobalArea = rPrevious.f4GlobalArea;

	// Save
	rCurrent.eFrameType = eFrameType;
	rCurrent.f4GlobalArea = f4GlobalArea;

	// Children
}

void FrameBase::InterpolateSync([[maybe_unused]] FrameBase& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

float DayPercent(const game::FrameInterpolate& __restrict rFrameInterpolate)
{
	if (rFrameInterpolate.fSunAngle >= 0.0f && rFrameInterpolate.fSunAngle <= XM_PIDIV2)
	{
		return rFrameInterpolate.fSunAngle / XM_PIDIV2;
	}
	else if (rFrameInterpolate.fSunAngle >= XM_PIDIV2 && rFrameInterpolate.fSunAngle <= XM_PI)
	{
		return 1.0f - (rFrameInterpolate.fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

float NightPercent(const game::FrameInterpolate& __restrict rFrameInterpolate)
{
	if (rFrameInterpolate.fSunAngle >= XM_PI && rFrameInterpolate.fSunAngle < XM_PI + XM_PIDIV2)
	{
		return (rFrameInterpolate.fSunAngle - XM_PI) / XM_PIDIV2;
	}
	if (rFrameInterpolate.fSunAngle >= XM_PI + XM_PIDIV2)
	{
		return 1.0f - (rFrameInterpolate.fSunAngle - (XM_PI + XM_PIDIV2)) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

void FrameBase::Render([[maybe_unused]] const game::Frame& __restrict rFrame, [[maybe_unused]] int64_t iCommandBuffer)
{
}

void FrameBase::PostRenderUpdate([[maybe_unused]] FrameBase& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime, [[maybe_unused]] const game::FrameInput& __restrict rFrameInput)
{
	// Load

	// Update

	// Save

	// Children
}

void FrameBase::PostRenderPreCollision([[maybe_unused]] FrameBase& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Children
}

void FrameBase::PostRenderCollide()
{
	Collision::Collide();
}

void FrameBase::PostRenderPostCollision([[maybe_unused]] FrameBase& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Children
}

void FrameBase::PostRenderSpawn([[maybe_unused]] FrameBase& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Children
}

void FrameBase::PostRenderDestroy([[maybe_unused]] FrameBase& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Children
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
