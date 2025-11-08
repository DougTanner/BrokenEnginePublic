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

// Global phase: Update time-based systems and camera position
void UpdateFrameGlobal(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame)
{
	SCOPED_CPU_PROFILE(kCpuTimerFrameGlobal);

	auto& rGlobal = rFrame.global;
	const auto& rPreviousGlobal = rPreviousFrame.global;

	ASSERT(rPreviousGlobal.eFrameType == FrameType::kFull);
	gCurrentFrameTypeProcessing = FrameType::kGlobal;

	bool bEpsilon = rGlobal.fDeltaTime <= kfEpsilon;
	if (bEpsilon)
	{
		LOG("fDeltaTime <= kfEpsilon");
		memcpy(&rFrame, &rPreviousFrame, sizeof(rFrame));
		return;
	}

	rGlobal.iFrame = rPreviousGlobal.iFrame + 1;
	rGlobal.eFrameType = FrameType::kGlobal;
	rGlobal.eIslandsFlip = rPreviousGlobal.eIslandsFlip;
	engine::gpIslands->SetIslandsFlip(rGlobal.eIslandsFlip);

	rGlobal.fCurrentTime = rPreviousGlobal.fCurrentTime + rGlobal.fDeltaTime;
	rGlobal.randomEngine = rPreviousGlobal.randomEngine;

	WriteFrameGlobal(rFrame, rPreviousFrame, game::FrameInputHeld(), rGlobal.fDeltaTime);
	GlobalList(rFrame, rPreviousFrame, game::FrameInputHeld(), rGlobal.fDeltaTime, UPDATE_LIST);
}

// Interpolation phase: Copy pools from previous frame and interpolate positions/rotations
void UpdateFrameInterpolate(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput)
{
	SCOPED_CPU_PROFILE(kCpuTimerFrameInterpolate);

	const auto& rGlobal = rFrame.global;
	auto& rInterpolate = rFrame.interpolate;
	const auto& rPreviousInterpolate = rPreviousFrame.interpolate;

	ASSERT(rGlobal.eFrameType == FrameType::kGlobal);
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

// Full phase: PostRender, collision, spawning, and destruction
void UpdateFrameFull(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput)
{
	SCOPED_CPU_PROFILE(kCpuTimerFrameFull);

	const auto& rGlobal = rFrame.global;
	auto& rInterpolate = rFrame.interpolate;
	auto& rFull = rFrame.full;

	ASSERT(rGlobal.eFrameType == FrameType::kInterpolate);
	gCurrentFrameTypeProcessing = FrameType::kFull;
	rFrame.global.eFrameType = FrameType::kFull;

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
}

bool XM_CALLCONV InsideVisibleArea(const game::FrameInput& rFrameInput, FXMVECTOR vecPosition, float fAdjustLeft, float fAdjustRight, float fAdjustTop, float fAdjustBottom)
{
	return InVisibleArea(rFrameInput.f4LargeVisibleArea, vecPosition, fAdjustLeft, fAdjustRight, fAdjustTop, fAdjustBottom);
}

bool XM_CALLCONV OutsideVisibleArea(const game::FrameInput& rFrameInput, FXMVECTOR vecPosition, float fAdjustLeft, float fAdjustRight, float fAdjustTop, float fAdjustBottom)
{
	return !InVisibleArea(rFrameInput.f4LargeVisibleArea, vecPosition, fAdjustLeft, fAdjustRight, fAdjustTop, fAdjustBottom);
}

XMFLOAT4 XM_CALLCONV VisibleDistances(const game::FrameInput& __restrict rFrameInput, FXMVECTOR vecPosition)
{
	XMFLOAT4A f4 {};
	XMStoreFloat4A(&f4, vecPosition);
	auto vecTopLeft = XMVectorSetZ(XMLoadFloat4(&rFrameInput.f4VisibleTopLeft), engine::gBaseHeight.Get());
	auto vecTopRight = XMVectorSetZ(XMLoadFloat4(&rFrameInput.f4VisibleTopRight), engine::gBaseHeight.Get());
	auto vecBottomLeft = XMVectorSetZ(XMLoadFloat4(&rFrameInput.f4VisibleBottomLeft), engine::gBaseHeight.Get());
	auto vecBottomRight = XMVectorSetZ(XMLoadFloat4(&rFrameInput.f4VisibleBottomRight), engine::gBaseHeight.Get());

	auto vecDistanceToLeft = XMVector3LinePointDistance(vecTopLeft, vecBottomLeft, vecPosition);
	float fToLeft = XMVectorGetX(vecDistanceToLeft);
	if (f4.x <= rFrameInput.f4VisibleTopLeft.x)
	{
		fToLeft = -fToLeft;
	}

	auto vecDistanceToRight = XMVector3LinePointDistance(vecTopRight, vecBottomRight, vecPosition);
	float fToRight = XMVectorGetX(vecDistanceToRight);
	if (f4.x >= rFrameInput.f4VisibleTopRight.x)
	{
		fToRight = -fToRight;
	}

	auto vecDistanceToTop = XMVector3LinePointDistance(vecTopLeft, vecTopRight, vecPosition);
	float fToTop = XMVectorGetX(vecDistanceToTop);
	if (f4.y >= rFrameInput.f4VisibleTopLeft.y)
	{
		fToTop = -fToTop;
	}

	auto vecDistanceToBottom = XMVector3LinePointDistance(vecBottomLeft, vecBottomRight, vecPosition);
	float fToBottom = XMVectorGetX(vecDistanceToBottom);
	if (f4.y <= rFrameInput.f4VisibleBottomLeft.y)
	{
		fToBottom = -fToBottom;
	}

	return XMFLOAT4 {fToLeft, fToRight, fToTop, fToBottom};
}

} // namespace engine
