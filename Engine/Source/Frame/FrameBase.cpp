#include "FrameBase.h"

#include "Frame/Collision.h"
#include "Graphics/Graphics.h"

#include "Frame/Frame.h"
#include "Frame/Player.h"
#include "Graphics/Camera.h"

namespace engine
{

FrameInterpolateBase::FrameInterpolateBase()
{
}

FramePostRenderBase::FramePostRenderBase()
: vecArea(XMVectorScale(XMLoadFloat4(&gpIslands->mf4GlobalArea), 2.0f))
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

	// Store delta time in frame
	rCurrent.fDeltaTime = fDeltaTime;

	// Load
	FrameType eFrameType = rPrevious.eFrameType;
	int64_t iFrame = rPrevious.iFrame;
	float fCurrentTime = rPrevious.fCurrentTime;

	// Update
	eFrameType = FrameType::kInterpolate;
	++iFrame;
	fCurrentTime += fDeltaTime;

	// Save
	rCurrent.eFrameType = eFrameType;
	rCurrent.iFrame = iFrame;
	rCurrent.fCurrentTime = fCurrentTime;

	// Collections
	AreaLightsInterpolate::Update(rCurrent, rPreviousFrame);
	BillboardsInterpolate::Update(rCurrent, rPreviousFrame);
	ExplosionsInterpolate::Update(rCurrent, rPreviousFrame);
	HexShieldsInterpolate::Update(rCurrent, rPreviousFrame);
	PointLightsInterpolate::Update(rCurrent, rPreviousFrame);
	PuffsInterpolate::Update(rCurrent, rPreviousFrame);
	PushersInterpolate::Update(rCurrent, rPreviousFrame);
	SoundsInterpolate::Update(rCurrent, rPreviousFrame);
	TrailsInterpolate::Update(rCurrent, rPreviousFrame);
}

void FrameInterpolateBase::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] int64_t iCommandBuffer)
{
	// Collections
	AreaLightsInterpolate::Render(rCurrent, iCommandBuffer);
	BillboardsInterpolate::Render(rCurrent, iCommandBuffer);
	ExplosionsInterpolate::Render(rCurrent, iCommandBuffer);
	HexShieldsInterpolate::Render(rCurrent, iCommandBuffer);
	PointLightsInterpolate::Render(rCurrent, iCommandBuffer);
	PuffsInterpolate::Render(rCurrent, iCommandBuffer);
	PushersInterpolate::Render(rCurrent, iCommandBuffer);
	SoundsInterpolate::Render(rCurrent, iCommandBuffer);
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

void FramePostRenderBase::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const game::FrameInput& __restrict rFrameInput)
{
	game::FramePostRender& rCurrent = rFrame.postRender;
	const game::FramePostRender& rPrevious = rPreviousFrame.postRender;

	rFrame.interpolate.eFrameType = FrameType::kPostRender;

	// Load
	common::RandomEngine randomEngine = rPrevious.randomEngine;
	XMVECTOR vecArea = rPrevious.vecArea;
	uint64_t uiNextUuid = rPrevious.uiNextUuid;
	uint16_t uiFrameId = rPrevious.uiFrameId;
	CollisionGroups collisionGroups = rPrevious.collisionGroups;

	// Save
	rCurrent.randomEngine = randomEngine;
	rCurrent.vecArea = vecArea;
	rCurrent.uiNextUuid = uiNextUuid;
	rCurrent.uiFrameId = uiFrameId;
	rCurrent.collisionGroups = collisionGroups;

	// Collections
	AreaLightsPostRender::Update(rFrame, rPreviousFrame);
	BillboardsPostRender::Update(rFrame, rPreviousFrame);
	ExplosionsPostRender::Update(rFrame, rPreviousFrame);
	HexShieldsPostRender::Update(rFrame, rPreviousFrame);
	PointLightsPostRender::Update(rFrame, rPreviousFrame);
	PuffsPostRender::Update(rFrame, rPreviousFrame);
	PushersPostRender::Update(rFrame, rPreviousFrame);
	SoundsPostRender::Update(rFrame, rPreviousFrame);
	TrailsPostRender::Update(rFrame, rPreviousFrame);

	// Setup pusher zones for spatial acceleration
	PushersInterpolate::SetupZones(rFrame);
}

void FramePostRenderBase::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	// Collections
	AreaLightsPostRender::PreCollision(rFrame, rPreviousFrame);
	BillboardsPostRender::PreCollision(rFrame, rPreviousFrame);
	ExplosionsPostRender::PreCollision(rFrame, rPreviousFrame);
	HexShieldsPostRender::PreCollision(rFrame, rPreviousFrame);
	PointLightsPostRender::PreCollision(rFrame, rPreviousFrame);
	PuffsPostRender::PreCollision(rFrame, rPreviousFrame);
	PushersPostRender::PreCollision(rFrame, rPreviousFrame);
	SoundsPostRender::PreCollision(rFrame, rPreviousFrame);
	TrailsPostRender::PreCollision(rFrame, rPreviousFrame);
}

void FramePostRenderBase::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	// Collections
	AreaLightsPostRender::PostCollision(rFrame, rPreviousFrame);
	BillboardsPostRender::PostCollision(rFrame, rPreviousFrame);
	ExplosionsPostRender::PostCollision(rFrame, rPreviousFrame);
	HexShieldsPostRender::PostCollision(rFrame, rPreviousFrame);
	PointLightsPostRender::PostCollision(rFrame, rPreviousFrame);
	PuffsPostRender::PostCollision(rFrame, rPreviousFrame);
	PushersPostRender::PostCollision(rFrame, rPreviousFrame);
	SoundsPostRender::PostCollision(rFrame, rPreviousFrame);
	TrailsPostRender::PostCollision(rFrame, rPreviousFrame);
}

void FramePostRenderBase::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	// Collections
	AreaLightsPostRender::AreaDamage(rFrame, rPreviousFrame);
	BillboardsPostRender::AreaDamage(rFrame, rPreviousFrame);
	ExplosionsPostRender::AreaDamage(rFrame, rPreviousFrame);
	HexShieldsPostRender::AreaDamage(rFrame, rPreviousFrame);
	PointLightsPostRender::AreaDamage(rFrame, rPreviousFrame);
	PuffsPostRender::AreaDamage(rFrame, rPreviousFrame);
	PushersPostRender::AreaDamage(rFrame, rPreviousFrame);
	SoundsPostRender::AreaDamage(rFrame, rPreviousFrame);
	TrailsPostRender::AreaDamage(rFrame, rPreviousFrame);
}

void FramePostRenderBase::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Collections
	AreaLightsPostRender::Destroy(rFrame);
	BillboardsPostRender::Destroy(rFrame);
	ExplosionsPostRender::Destroy(rFrame);
	HexShieldsPostRender::Destroy(rFrame);
	PointLightsPostRender::Destroy(rFrame);
	PuffsPostRender::Destroy(rFrame);
	PushersPostRender::Destroy(rFrame);
	SoundsPostRender::Destroy(rFrame);
	TrailsPostRender::Destroy(rFrame);
}

void FramePostRenderBase::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Collections
	AreaLightsPostRender::Spawn(rFrame);
	BillboardsPostRender::Spawn(rFrame);
	ExplosionsPostRender::Spawn(rFrame);
	HexShieldsPostRender::Spawn(rFrame);
	PointLightsPostRender::Spawn(rFrame);
	PuffsPostRender::Spawn(rFrame);
	PushersPostRender::Spawn(rFrame);
	SoundsPostRender::Spawn(rFrame);
	TrailsPostRender::Spawn(rFrame);
}

float DayPercent()
{
	float fSunAngle = game::gpCamera->mfSunAngle;
	if (fSunAngle >= 0.0f && fSunAngle <= XM_PIDIV2)
	{
		return fSunAngle / XM_PIDIV2;
	}
	else if (fSunAngle >= XM_PIDIV2 && fSunAngle <= XM_PI)
	{
		return 1.0f - (fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

float NightPercent()
{
	float fSunAngle = game::gpCamera->mfSunAngle;
	if (fSunAngle >= XM_PI && fSunAngle < XM_PI + XM_PIDIV2)
	{
		return (fSunAngle - XM_PI) / XM_PIDIV2;
	}
	if (fSunAngle >= XM_PI + XM_PIDIV2)
	{
		return 1.0f - (fSunAngle - (XM_PI + XM_PIDIV2)) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

} // namespace engine
