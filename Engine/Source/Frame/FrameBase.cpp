#include "FrameBase.h"

#include "Frame/Collision.h"
#include "Graphics/Islands.h"

#include "Frame/Frame.h"
#include "Frame/Player.h"
#ifdef BT_CLIENT
#include "Graphics/Camera.h"
#endif

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

#ifdef BT_CLIENT
void FrameInterpolateBase::GraphicsResources()
{
	ForEachGraphicsResources(InterpolateTypes{});
}
#endif

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
	FrameFlags_t frameFlags = rPrevious.frameFlags;

	// Update
	frameFlags.Clear({FrameFlags::kInterpolate, FrameFlags::kPostRender});
	frameFlags.Set(FrameFlags::kInterpolate);

	// Save
	rCurrent.frameFlags = frameFlags;

	ForEachInterpolateUpdate(InterpolateTypes{}, rCurrent, rPreviousFrame);
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

	rFrame.interpolate.frameFlags.Clear({FrameFlags::kInterpolate, FrameFlags::kPostRender});
	rFrame.interpolate.frameFlags.Set(FrameFlags::kPostRender);

	// Load
	common::RandomEngine randomEngine = rPrevious.randomEngine;
	XMVECTOR vecArea = rPrevious.vecArea;
	uint64_t uiNextUuid = rPrevious.uiNextUuid;
#ifdef BT_CLIENT
	uint64_t uiNextSoundUuid = rPrevious.uiNextSoundUuid;
	uint64_t uiNextVisualUuid = rPrevious.uiNextVisualUuid;
#endif
	uint16_t uiFrameId = rPrevious.uiFrameId;
	IslandsFlip eIslandsFlip = rPrevious.eIslandsFlip;

	// Save
	rCurrent.randomEngine = randomEngine;
	rCurrent.vecArea = vecArea;
	rCurrent.uiNextUuid = uiNextUuid;
#ifdef BT_CLIENT
	rCurrent.uiNextSoundUuid = uiNextSoundUuid;
	rCurrent.uiNextVisualUuid = uiNextVisualUuid;
#endif
	rCurrent.uiFrameId = uiFrameId;
	rCurrent.eIslandsFlip = eIslandsFlip;
	rCurrent.alignments.CopyFrom(rPrevious.alignments);

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

void FramePostRenderBase::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	ForEachPostRenderTransfer(PostRenderBaseTypes{}, rFrame);
}

void FramePostRenderBase::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
	ForEachPostRenderDestroy(PostRenderBaseTypes{}, rFrame);
}

void FramePostRenderBase::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
	ForEachPostRenderSpawn(PostRenderBaseTypes{}, rFrame);
}

#ifdef BT_CLIENT
float DayPercent()
{
	float fSunAngle = game::gpCamera->SunAngle();
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
	float fSunAngle = game::gpCamera->SunAngle();
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

void FrameInterpolateBase::BeginRender([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, [[maybe_unused]] const std::vector<GridCoord>& rActiveCoords)
{
	ForEachBeginRender(InterpolateTypes{}, iCommandBuffer, rRenderInterpolates, rActiveCoords);
}

void FrameInterpolateBase::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] int64_t iCommandBuffer)
{
	ForEachInterpolateRender(InterpolateRenderTypes{}, rCurrent, iCommandBuffer);
}

void FrameInterpolateBase::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	ForEachEndRender(InterpolateTypes{}, iCommandBuffer);
}
#endif // BT_CLIENT

} // namespace engine
