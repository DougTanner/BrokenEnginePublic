#include "FrameBase.h"

#include "Frame/Collision.h"
#include "Graphics/Graphics.h"

#include "Frame/Frame.h"
#include "Frame/Player.h"

namespace engine
{

void FrameInterpolateBase::Allocate([[maybe_unused]] game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	const game::FrameInterpolate& __restrict rPrevious = rPreviousFrame.interpolate;
	engine::Allocate(rCurrent.areaLights, rPrevious.areaLights, rCurrent.areaLights.Members());
	engine::Allocate(rCurrent.billboards, rPrevious.billboards, rCurrent.billboards.Members());
	engine::Allocate(rCurrent.explosions, rPrevious.explosions, rCurrent.explosions.Members());
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

	// Update collections (merged with Sync)
	AreaLightsInterpolate::Update(rCurrent.areaLights, rPreviousFrame, fDeltaTime);
	BillboardsInterpolate::Update(rCurrent.billboards, rPreviousFrame, fDeltaTime);
	ExplosionsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	PointLightsInterpolate::Update(rCurrent.pointLights, rPreviousFrame, fDeltaTime);
	PuffsInterpolate::Update(rCurrent.puffs, rPreviousFrame, fDeltaTime);
	PushersInterpolate::Update(rCurrent.pushers, rPreviousFrame, fDeltaTime);
	SoundsInterpolate::Update(rCurrent.sounds, rPreviousFrame, fDeltaTime);
	TrailsInterpolate::Update(rCurrent.trails, rPreviousFrame, fDeltaTime);
}

void FrameInterpolateBase::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] int64_t iCommandBuffer)
{
	AreaLightsInterpolate::Render(rCurrent, iCommandBuffer);
	BillboardsInterpolate::Render(rCurrent, iCommandBuffer);
	PointLightsInterpolate::Render(rCurrent, iCommandBuffer);
	PuffsInterpolate::Render(rCurrent, iCommandBuffer);
	TrailsInterpolate::Render(rCurrent, iCommandBuffer);
}

void FramePostRenderBase::Allocate([[maybe_unused]] game::FramePostRender& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	const game::FramePostRender& __restrict rPrevious = rPreviousFrame.postRender;
	engine::Allocate(rCurrent.areaLights, rPrevious.areaLights, rCurrent.areaLights.Members());
	engine::Allocate(rCurrent.billboards, rPrevious.billboards, rCurrent.billboards.Members());
	engine::Allocate(rCurrent.explosions, rPrevious.explosions, rCurrent.explosions.Members());
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
	SoundsPostRender::Update(rCurrent.sounds, rPrevious.sounds);
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
	// Trails don't auto-destroy - external code removes them via Remove()
}

FrameBase::FrameBase()
: f4GlobalArea(engine::gpIslands->mf4GlobalArea)
{
}

void FrameBase::Register()
{
	ExplosionsPostRender::Register();
}

void FrameBase::AllocateGraphicsResources()
{
	engine::AreaLightsInterpolate::AllocatePipelines();
	engine::BillboardsInterpolate::AllocatePipelines();
	engine::PointLightsInterpolate::AllocatePipelines();
	engine::PuffsInterpolate::AllocatePipelines();
	engine::TrailsInterpolate::AllocatePipelines();
}

void FrameBase::InterpolateAllocate([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	FrameInterpolateBase::Allocate(rFrame.interpolate, rPreviousFrame, fDeltaTime);
}

void FrameBase::InterpolateUpdate([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Load
	FrameType eFrameType = rPreviousFrame.eFrameType;
	XMFLOAT4 f4GlobalArea = rPreviousFrame.f4GlobalArea;

	// Save
	rFrame.eFrameType = eFrameType;
	rFrame.f4GlobalArea = f4GlobalArea;

	// Children
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

void FrameBase::PostRenderAllocate([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	FramePostRenderBase::Allocate(rFrame.postRender, rPreviousFrame);
}

void FrameBase::PostRenderUpdate([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime, [[maybe_unused]] const game::FrameInput& __restrict rFrameInput)
{
	// Load

	// Update

	// Save

	// Children
}

void FrameBase::PostRenderPreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Children
}

void FrameBase::PostRenderCollide()
{
	Collision::Collide();
}

void FrameBase::PostRenderPostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Children
}

void FrameBase::PostRenderAreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Children
}

void FrameBase::PostRenderSpawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Children
}

void FrameBase::PostRenderDestroy([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Children
}

} // namespace engine
