#include "FrameBase.h"

#include "Frame/Collision.h"
#include "Graphics/Graphics.h"

#include "Frame/Frame.h"
#include "Frame/Player.h"

namespace engine
{

FrameInterpolateBase::FrameInterpolateBase()
: f4GlobalArea(gpIslands->mf4GlobalArea)
{
}

void FrameInterpolateBase::Register()
{
	// Collections
	AreaLightsInterpolate::Register();
	BillboardsInterpolate::Register();
	ExplosionsInterpolate::Register();
	HexShieldsInterpolate::Register();
	PointLightsInterpolate::Register();
	PuffsInterpolate::Register();
	PushersInterpolate::Register();
	SoundsInterpolate::Register();
	TrailsInterpolate::Register();
}

void FrameInterpolateBase::GraphicsResources()
{
	// Collections
	AreaLightsInterpolate::GraphicsResources();
	BillboardsInterpolate::GraphicsResources();
	ExplosionsInterpolate::GraphicsResources();
	HexShieldsInterpolate::GraphicsResources();
	PointLightsInterpolate::GraphicsResources();
	PuffsInterpolate::GraphicsResources();
	PushersInterpolate::GraphicsResources();
	SoundsInterpolate::GraphicsResources();
	TrailsInterpolate::GraphicsResources();
}

void FrameInterpolateBase::AllocateAndCopy([[maybe_unused]] game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] const game::FrameInterpolate& __restrict rPrevious)
{
	// Collections
	AreaLightsInterpolate::AllocateAndCopy(rCurrent.areaLights, rPrevious.areaLights);
	BillboardsInterpolate::AllocateAndCopy(rCurrent.billboards, rPrevious.billboards);
	ExplosionsInterpolate::AllocateAndCopy(rCurrent.explosions, rPrevious.explosions);
	HexShieldsInterpolate::AllocateAndCopy(rCurrent.hexShields, rPrevious.hexShields);
	PointLightsInterpolate::AllocateAndCopy(rCurrent.pointLights, rPrevious.pointLights);
	PuffsInterpolate::AllocateAndCopy(rCurrent.puffs, rPrevious.puffs);
	PushersInterpolate::AllocateAndCopy(rCurrent.pushers, rPrevious.pushers);
	SoundsInterpolate::AllocateAndCopy(rCurrent.sounds, rPrevious.sounds);
	TrailsInterpolate::AllocateAndCopy(rCurrent.trails, rPrevious.trails);
}

void FrameInterpolateBase::Update([[maybe_unused]] game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	const FrameInterpolateBase& rPrevious = rPreviousFrame.interpolate;

	// Load
	FrameType eFrameType = rPrevious.eFrameType;
	int64_t iFrame = rPrevious.iFrame;
	float fSunAngle = rPrevious.fSunAngle;
	float fCurrentTime = rPrevious.fCurrentTime;
	XMFLOAT4 f4GlobalArea = rPrevious.f4GlobalArea;

	// Update
	eFrameType = FrameType::kInterpolate;
	++iFrame;
	fCurrentTime += fDeltaTime;

	// Save
	rCurrent.eFrameType = eFrameType;
	rCurrent.iFrame = iFrame;
	rCurrent.fSunAngle = fSunAngle;
	rCurrent.fCurrentTime = fCurrentTime;
	rCurrent.f4GlobalArea = f4GlobalArea;

	// Collections
	AreaLightsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	BillboardsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	ExplosionsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	HexShieldsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	PointLightsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	PuffsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	PushersInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	SoundsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	TrailsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
}

void FrameInterpolateBase::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] int64_t iCommandBuffer)
{
	// Collections
	AreaLightsInterpolate::Render(rCurrent, iCommandBuffer);
	BillboardsInterpolate::Render(rCurrent, iCommandBuffer);
	HexShieldsInterpolate::Render(rCurrent, iCommandBuffer);
	PointLightsInterpolate::Render(rCurrent, iCommandBuffer);
	PuffsInterpolate::Render(rCurrent, iCommandBuffer);
	TrailsInterpolate::Render(rCurrent, iCommandBuffer);
}

void FramePostRenderBase::AllocateAndCopy([[maybe_unused]] game::FramePostRender& __restrict rCurrent, [[maybe_unused]] const game::FramePostRender& __restrict rPrevious)
{
	// Collections
	AreaLightsPostRender::AllocateAndCopy(rCurrent.areaLights, rPrevious.areaLights);
	BillboardsPostRender::AllocateAndCopy(rCurrent.billboards, rPrevious.billboards);
	ExplosionsPostRender::AllocateAndCopy(rCurrent.explosions, rPrevious.explosions);
	HexShieldsPostRender::AllocateAndCopy(rCurrent.hexShields, rPrevious.hexShields);
	PointLightsPostRender::AllocateAndCopy(rCurrent.pointLights, rPrevious.pointLights);
	PuffsPostRender::AllocateAndCopy(rCurrent.puffs, rPrevious.puffs);
	PushersPostRender::AllocateAndCopy(rCurrent.pushers, rPrevious.pushers);
	SoundsPostRender::AllocateAndCopy(rCurrent.sounds, rPrevious.sounds);
	TrailsPostRender::AllocateAndCopy(rCurrent.trails, rPrevious.trails);
}

void FramePostRenderBase::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime, [[maybe_unused]] const game::FrameInput& __restrict rFrameInput)
{
	game::FramePostRender& rCurrent = rFrame.postRender;
	const game::FramePostRender& rPrevious = rPreviousFrame.postRender;

	rFrame.interpolate.eFrameType = FrameType::kPostRender;

	// Load
	common::RandomEngine randomEngine = rPrevious.randomEngine;
	int64_t iNextUuid = rPrevious.iNextUuid;

	// Save
	rCurrent.randomEngine = randomEngine;
	rCurrent.iNextUuid = iNextUuid;

	// Collections
	AreaLightsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	BillboardsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	ExplosionsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	HexShieldsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	PointLightsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	PuffsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	PushersPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	SoundsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	TrailsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);

	// Setup pusher zones for spatial acceleration
	// DT: TODO Should this be here?
	PushersInterpolate::SetupZones(rFrame);
}

void FramePostRenderBase::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Collections
	AreaLightsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	BillboardsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	ExplosionsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	HexShieldsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	PointLightsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	PuffsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	PushersPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	SoundsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	TrailsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
}

void FramePostRenderBase::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void FramePostRenderBase::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
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

} // namespace engine
