#include "FrameBase.h"

#include "Graphics/Islands.h"

#include "Frame/Frame.h"
#include "Input/Input.h"

using namespace DirectX;

namespace engine
{

FrameBase::FrameBase(IslandsFlip eInitialIslandsFlip)
{
	eIslandsFlip = eInitialIslandsFlip;
	gpIslands->SetIslandsFlip(eInitialIslandsFlip);

	f4GlobalArea = gpIslands->mf4GlobalArea;

	Navmesh::SetupGrid(f4GlobalArea, navmesh);
}

void UpdateFrameBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime, FrameType eFrameType)
{
	SCOPED_CPU_PROFILE(kCpuTimerFrameUpdate);

	ASSERT(rPreviousFrame.eFrameType == FrameType::kFull);

#if defined(BT_DEBUG)
	common::ScopedLambda resetCurrentFrameTypeProcessing([&]()
	{
		gCurrentFrameTypeProcessing = FrameType::kFull;
	});
#endif

	bool bEpsilon = fDeltaTime <= kfEpsilon;
	if (bEpsilon)
	{
		LOG("fDeltaTime <= kfEpsilon");
		memcpy(&rFrame, &rPreviousFrame, sizeof(rFrame));
	}

	{
		SCOPED_CPU_PROFILE(kCpuTimerFrameGlobal);

#if defined(BT_DEBUG)
		gCurrentFrameTypeProcessing = FrameType::kGlobal;
#endif

		rFrame.iFrame = eFrameType == FrameType::kFull ? rPreviousFrame.iFrame + 1 : rPreviousFrame.iFrame;
		rFrame.eFrameType = eFrameType;
		rFrame.eIslandsFlip = rPreviousFrame.eIslandsFlip;
		engine::gpIslands->SetIslandsFlip(rFrame.eIslandsFlip);

		if (bEpsilon)
		{
			return;
		}

		rFrame.fCurrentTime = rPreviousFrame.fCurrentTime + fDeltaTime;
		rFrame.randomEngine = rPreviousFrame.randomEngine;

		FrameGlobal(rFrame, rPreviousFrame, rFrameInput.held, fDeltaTime);
		GlobalList(rFrame, rPreviousFrame, rFrameInput.held, fDeltaTime, UPDATE_LIST);
	}

	{
		if (eFrameType == FrameType::kGlobal)
		{
			return;
		}

		SCOPED_CPU_PROFILE(kCpuTimerFrameMain);

#if defined(BT_DEBUG)
		gCurrentFrameTypeProcessing = FrameType::kMain;
#endif

		Areas::Copy(rFrame.enemyAreas, rPreviousFrame.enemyAreas);
		Areas::Copy(rFrame.playerAreas, rPreviousFrame.playerAreas);
		AreaLights::Copy(rFrame.areaLights, rPreviousFrame.areaLights);
		Billboards::Copy(rFrame.billboards, rPreviousFrame.billboards);
		Explosions::Copy(rFrame.explosions, rPreviousFrame.explosions);
		HexShields::Copy(rFrame.hexShields, rPreviousFrame.hexShields);
		PointLights::Copy(rFrame.pointLights, rPreviousFrame.pointLights);
			rFrame.pointLightControllers2.UpdateMain(rFrame.pointLightControllers2, rPreviousFrame.pointLightControllers2, rFrame.pointLights, rFrame.fCurrentTime);
			rFrame.pointLightControllers3.UpdateMain(rFrame.pointLightControllers3, rPreviousFrame.pointLightControllers3, rFrame.pointLights, rFrame.fCurrentTime);
		Puffs::Copy(rFrame.puffs, rPreviousFrame.puffs);
			rFrame.puffControllers2.UpdateMain(rFrame.puffControllers2, rPreviousFrame.puffControllers2, rFrame.puffs, rFrame.fCurrentTime);
			rFrame.puffControllers3.UpdateMain(rFrame.puffControllers3, rPreviousFrame.puffControllers3, rFrame.puffs, rFrame.fCurrentTime);
		Pullers::Copy(rFrame.pullers, rPreviousFrame.pullers);
		Pushers::Copy(rFrame.pushers, rPreviousFrame.pushers);
		Sounds::Copy(rFrame.sounds, rPreviousFrame.sounds);
		Splashes::Copy(rFrame.splashes, rPreviousFrame.splashes);
		Targets::Copy(rFrame.targets, rPreviousFrame.targets);
		Trails::Copy(rFrame.trails, rPreviousFrame.trails);

		FrameInterpolate(rFrame, rPreviousFrame, rFrameInput.held, fDeltaTime);
		InterpolateList(rFrame, rPreviousFrame, rFrameInput.held, fDeltaTime, UPDATE_LIST);
		Explosions::Interpolate(rFrame);
		Targets::Interpolate(rFrame);
	}

	{
		if (eFrameType == FrameType::kMain)
		{
			return;
		}

		SCOPED_CPU_PROFILE(kCpuTimerFramePostRender);

#if defined(BT_DEBUG)
		gCurrentFrameTypeProcessing = FrameType::kFull;
#endif

		rFrame.navmesh.SetupPlayerDistances(rFrame, rPreviousFrame);
		rFrame.pushers.SetupZones(rFrame);

		FramePostRender(rFrame, rPreviousFrame, rFrameInput, fDeltaTime);
		PostRenderList(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, UPDATE_LIST);
		Splashes::PostRender(rFrame, fDeltaTime);

		CollideList(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, UPDATE_LIST);

		// Spawn second to last, because a spawned object has no information in the previous frame
		FrameSpawn(rFrame, rPreviousFrame, rFrameInput, fDeltaTime);
		SpawnList(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, UPDATE_LIST);

		// Destroy last, because this desynchronizes indices from previous frame
		FrameDestroy(rFrame, rPreviousFrame, rFrameInput, fDeltaTime);
		DestroyList(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, UPDATE_LIST);
	}
}

bool XM_CALLCONV InsideVisibleArea(const game::FrameInput& rFrameInput, FXMVECTOR vecPosition, float fAdjustLeft, float fAdjustRight, float fAdjustTop, float fAdjustBottom)
{
	return InVisibleArea(rFrameInput.f4LargeVisibleArea, vecPosition, fAdjustLeft, fAdjustRight, fAdjustTop, fAdjustBottom);
}

bool XM_CALLCONV OutsideVisibleArea(const game::FrameInput& rFrameInput, FXMVECTOR vecPosition, float fAdjustLeft, float fAdjustRight, float fAdjustTop, float fAdjustBottom)
{
	return !InVisibleArea(rFrameInput.f4LargeVisibleArea, vecPosition, fAdjustLeft, fAdjustRight, fAdjustTop, fAdjustBottom);
}

XMFLOAT4 XM_CALLCONV VisibleDistances(const game::FrameInput& __restrict rFrameInput, DirectX::FXMVECTOR vecPosition)
{
	DirectX::XMFLOAT4A f4 {};
	DirectX::XMStoreFloat4A(&f4, vecPosition);
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
