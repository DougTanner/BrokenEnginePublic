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

void FrameInterpolateBase::Allocate([[maybe_unused]] game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] const game::FrameInterpolate& __restrict rPrevious)
{
	// Collections
	engine::Allocate(rCurrent.areaLights, rPrevious.areaLights, rCurrent.areaLights.Members());
	engine::Allocate(rCurrent.billboards, rPrevious.billboards, rCurrent.billboards.Members());
	engine::Allocate(rCurrent.explosions, rPrevious.explosions, rCurrent.explosions.Members());
	engine::Allocate(rCurrent.hexShields, rPrevious.hexShields, rCurrent.hexShields.Members());
	engine::Allocate(rCurrent.pointLights, rPrevious.pointLights, rCurrent.pointLights.Members());
	engine::Allocate(rCurrent.puffs, rPrevious.puffs, rCurrent.puffs.Members());
	engine::Allocate(rCurrent.pushers, rPrevious.pushers, rCurrent.pushers.Members());
	engine::Allocate(rCurrent.sounds, rPrevious.sounds, rCurrent.sounds.Members());
	engine::Allocate(rCurrent.trails, rPrevious.trails, rCurrent.trails.Members());
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
	AreaLightsInterpolate::Update(rCurrent.areaLights, rPreviousFrame, fDeltaTime);
	BillboardsInterpolate::Update(rCurrent.billboards, rPreviousFrame, fDeltaTime);
	ExplosionsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	HexShieldsInterpolate::Update(rCurrent.hexShields, rPreviousFrame, fDeltaTime);
	PointLightsInterpolate::Update(rCurrent.pointLights, rPreviousFrame, fDeltaTime);
	PuffsInterpolate::Update(rCurrent.puffs, rPreviousFrame, fDeltaTime);
	PushersInterpolate::Update(rCurrent.pushers, rPreviousFrame, fDeltaTime);
	SoundsInterpolate::Update(rCurrent.sounds, rPreviousFrame, fDeltaTime);
	TrailsInterpolate::Update(rCurrent.trails, rPreviousFrame, fDeltaTime);
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

void FramePostRenderBase::Allocate([[maybe_unused]] game::FramePostRender& __restrict rCurrent, [[maybe_unused]] const game::FramePostRender& __restrict rPrevious)
{
	// Collections
	engine::Allocate(rCurrent.areaLights, rPrevious.areaLights, rCurrent.areaLights.Members());
	engine::Allocate(rCurrent.billboards, rPrevious.billboards, rCurrent.billboards.Members());
	engine::Allocate(rCurrent.explosions, rPrevious.explosions, rCurrent.explosions.Members());
	engine::Allocate(rCurrent.hexShields, rPrevious.hexShields, rCurrent.hexShields.Members());
	engine::Allocate(rCurrent.pointLights, rPrevious.pointLights, rCurrent.pointLights.Members());
	engine::Allocate(rCurrent.puffs, rPrevious.puffs, rCurrent.puffs.Members());
	engine::Allocate(rCurrent.pushers, rPrevious.pushers, rCurrent.pushers.Members());
	engine::Allocate(rCurrent.sounds, rPrevious.sounds, rCurrent.sounds.Members());
	engine::Allocate(rCurrent.trails, rPrevious.trails, rCurrent.trails.Members());
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
	AreaLightsPostRender::Update(rCurrent.areaLights, rPrevious.areaLights, fDeltaTime);
	BillboardsPostRender::Update(rCurrent.billboards, rPrevious.billboards, fDeltaTime);
	ExplosionsPostRender::Update(rCurrent.explosions, rPreviousFrame, fDeltaTime);
	HexShieldsPostRender::Update(rCurrent.hexShields, rPrevious.hexShields, fDeltaTime);
	PointLightsPostRender::Update(rCurrent.pointLights, rPrevious.pointLights, fDeltaTime);
	PuffsPostRender::Update(rCurrent.puffs, rPrevious.puffs, fDeltaTime);
	PushersPostRender::Update(rCurrent.pushers, rPrevious.pushers, fDeltaTime);
	SoundsPostRender::Update(rCurrent.sounds, rPrevious.sounds, fDeltaTime);
	TrailsPostRender::Update(rCurrent.trails, rPrevious.trails, fDeltaTime);

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
