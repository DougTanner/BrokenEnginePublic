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
	ForEachRegister(InterpolateTypes{});
}

void FrameInterpolateBase::GraphicsResources()
{
	ForEachGraphicsResources(InterpolateTypes{});
}

void FrameInterpolateBase::AllocateAndCopy([[maybe_unused]] game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] const game::FrameInterpolate& __restrict rPrevious)
{
	FrameInterpolateBase& rCurrentBase = rCurrent;
	const FrameInterpolateBase& rPreviousBase = rPrevious;
	AllocateAndCopyCollections(rCurrentBase.Collections(), rPreviousBase.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(rCurrentBase.Collections())>>{});
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

	ForEachInterpolateUpdate(InterpolateTypes{}, rCurrent, rPreviousFrame);
}

void FrameInterpolateBase::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] int64_t iCommandBuffer)
{
	ForEachInterpolateRender(InterpolateTypes{}, rCurrent, iCommandBuffer);
}

void FramePostRenderBase::AllocateAndCopy([[maybe_unused]] game::FramePostRender& __restrict rCurrent, [[maybe_unused]] const game::FramePostRender& __restrict rPrevious)
{
	FramePostRenderBase& rCurrentBase = rCurrent;
	const FramePostRenderBase& rPreviousBase = rPrevious;
	AllocateAndCopyCollections(rCurrentBase.Collections(), rPreviousBase.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(rCurrentBase.Collections())>>{});
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
	Alignments alignments = rPrevious.alignments;

	// Save
	rCurrent.randomEngine = randomEngine;
	rCurrent.vecArea = vecArea;
	rCurrent.uiNextUuid = uiNextUuid;
	rCurrent.uiFrameId = uiFrameId;
	rCurrent.alignments = alignments;

	ForEachPostRenderUpdate(PostRenderBaseTypes{}, rFrame, rPreviousFrame);

	// Setup pusher zones for spatial acceleration
	PushersInterpolate::SetupZones(rFrame);
}

void FramePostRenderBase::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	ForEachPostRenderPreCollision(PostRenderBaseTypes{}, rFrame, rPreviousFrame);
}

void FramePostRenderBase::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	ForEachPostRenderPostCollision(PostRenderBaseTypes{}, rFrame, rPreviousFrame);
}

void FramePostRenderBase::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	ForEachPostRenderAreaDamage(PostRenderBaseTypes{}, rFrame, rPreviousFrame);
}

void FramePostRenderBase::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
	ForEachPostRenderDestroy(PostRenderBaseTypes{}, rFrame);
}

void FramePostRenderBase::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
	ForEachPostRenderSpawn(PostRenderBaseTypes{}, rFrame);
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
