#include "Smoke.h"

#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Graphics/Managers/TextureManager.h"
#include "Profile/ProfileManager.h"

#include "Game.h"
#include "Frame/Frame.h"

namespace engine
{

// 60 updates per second
constexpr float kfSmokeUpdateInterval = 0.0166666657;
static float sfSmokePreviousUpdateTime = 0.0f;

static XMFLOAT4 sf4SmokeArea {};

void RenderSmokeGlobal(int64_t iCommandBuffer, const game::Frame& __restrict rFrame)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.f4SmokeOne.x = kfSmokeUpdateInterval;
	rGlobalLayout.f4SmokeOne.y = gSmokeMax.Get();
	rGlobalLayout.f4SmokeOne.z = gSmokePower.Get();
	rGlobalLayout.f4SmokeOne.w = gSmokeDecay.Get();

	rGlobalLayout.f4SmokeTwo.x = gSmokeColorMin.Get();
	rGlobalLayout.f4SmokeTwo.y = gSmokeColorMultiplier.Get();
	rGlobalLayout.f4SmokeTwo.z = gSmokeTrailsFalloff.Get();
	rGlobalLayout.f4SmokeTwo.w = gSmokeDecayExtra.Get();

	rGlobalLayout.f4SmokeThree.x = gSmokeDecayExtraThreshold.Get();
	rGlobalLayout.f4SmokeThree.y = gSmokeWindNoiseScale.Get();
	rGlobalLayout.f4SmokeThree.z = gSmokeWindNoiseQuantity.Get();
	rGlobalLayout.f4SmokeThree.w = gSmokeNoiseQuantity.Get();

	rGlobalLayout.f4SmokeFour.x = gSmokeNoiseScaleOne.Get();
	rGlobalLayout.f4SmokeFour.y = gSmokeNoiseScaleTwo.Get();
	rGlobalLayout.f4SmokeFour.z = 0.0f; // (6144.0f / SmokeSimulationPixels()) * 0.75f * gSmokeSimulationArea.Get());
	rGlobalLayout.f4SmokeFour.w = 1.0f / gSmokeEdgeDecayDistance.Get();

	static bool sbSmoke = false;
	if (sbSmoke != gSmoke.Get<bool>())
	{
		sbSmoke = gSmoke.Get<bool>();
		gbSmokeClear = true;
	}

	if (gbSmokeClear)
	{
		gbSmokeClear = false;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearOne].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearTwo].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadTwo].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadOne].WriteIndirectBuffer(iCommandBuffer, 0);

	#if 0
		sfSmokePreviousUpdateTime = rFrame.interpolate.fCurrentTime;

		for (decltype(rFrame.interpolate.trails.uiMaxIndex) i = 0; i <= rFrame.interpolate.trails.uiMaxIndex; ++i)
		{
			if (!rFrame.interpolate.trails.pbUsed[i])
			{
				continue;
			}

			const engine::TrailInfo& rTrailInfo = rFrame.interpolate.trails.pObjectInfos[i];

			Trails::smpVecTrailsPositionPrevious[i] = rTrailInfo.vecPosition;
			Trails::smpVecTrailsPositionSmoothed[i] = rTrailInfo.vecPosition;
		}
	#endif

		return;
	}

	static XMFLOAT4 sf4PreviousSmokeArea {};
	if (!gbSmokeSpread || !gSmoke.Get<bool>())
	{
		rGlobalLayout.f4SmokeArea = sf4PreviousSmokeArea;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearOne].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearTwo].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadTwo].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadOne].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	gbSmokeSpread = false;

	XMFLOAT4A f4PlayerPosition {};
	XMStoreFloat4A(&f4PlayerPosition, rFrame.interpolate.player.vecPosition);
	float fAreaX = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	float fAreaY = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	rGlobalLayout.f4SmokeArea = {f4PlayerPosition.x - fAreaX, f4PlayerPosition.y + fAreaY, f4PlayerPosition.x + fAreaX, f4PlayerPosition.y - fAreaY};
	sf4SmokeArea = rGlobalLayout.f4SmokeArea;

	float fXOffset = (sf4PreviousSmokeArea.x - rGlobalLayout.f4SmokeArea.x) / (sf4PreviousSmokeArea.z - rGlobalLayout.f4SmokeArea.x);
	float fYOffset = (sf4PreviousSmokeArea.y - rGlobalLayout.f4SmokeArea.y) / (sf4PreviousSmokeArea.w - rGlobalLayout.f4SmokeArea.y);
	shaders::AxisAlignedQuadLayout& rQuad = *reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mSmokeSpreadStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	rQuad.f4VertexRect = {-1.0f + 2.0f * fXOffset, 1.0f - 2.0f * fYOffset, 2.0f, -2.0f};
	rQuad.f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
	rQuad.f4Misc = {};
	sf4PreviousSmokeArea = rGlobalLayout.f4SmokeArea;

	gpPipelineManager->mpPipelines[kPipelineSmokeClearOne].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeClearTwo].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeSpreadTwo].WriteIndirectBuffer(iCommandBuffer, 1);
	gpPipelineManager->mpPipelines[kPipelineSmokeSpreadOne].WriteIndirectBuffer(iCommandBuffer, 1);
}

void RenderSmokeMain(int64_t iCommandBuffer, const game::Frame& __restrict rFrame)
{
	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	rMainLayout.fSmokeShadowIntensity = gSmokeShadowIntensity.Get();

	static common::RandomEngine sRandomEngine;

#if 0
	if (rFrame.iFrame <= 3 || rFrame.interpolate.fCurrentTime < sfSmokePreviousUpdateTime + kfSmokeUpdateInterval || !gSmoke.Get<bool>())
	{
		gpPipelineManager->mpPipelines[kPipelineSmokePuffs].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeTrails].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}
#endif

	int64_t iPuffCount = 0;
	int64_t iPuffsRendered = 0;
	int64_t iTrailCount = 0;
	int64_t iTrailsRendered = 0;

#if 0
	gbSmokeSpread = true;
	sfSmokePreviousUpdateTime += kfSmokeUpdateInterval;

	auto pPuffLayouts = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mSmokePuffsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	for (decltype(rFrame.interpolate.puffs.uiMaxIndex) i = 0; i <= rFrame.interpolate.puffs.uiMaxIndex; ++i)
	{
		if (!rFrame.interpolate.puffs.pbUsed[i])
		{
			continue;
		}

		const engine::PuffInfo& rPuffInfo = rFrame.interpolate.puffs.pObjectInfos[i];
		
		++iPuffCount;

		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rPuffInfo.vecPosition);
		if (f4Position.x < sf4SmokeArea.x || f4Position.x > sf4SmokeArea.z || f4Position.y > sf4SmokeArea.y || f4Position.y < sf4SmokeArea.w)
		{
			continue;
		}

		float fElevation = gpIslands->GlobalElevation(rPuffInfo.vecPosition);
		auto vecBaseAreaPosition = common::ToBaseHeight(rPuffInfo.vecPosition, game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));
		XMStoreFloat4A(&f4Position, vecBaseAreaPosition);

		shaders::AxisAlignedQuadLayout& rLayout = pPuffLayouts[iPuffsRendered];
		rLayout.f4VertexRect = {f4Position.x - rPuffInfo.fArea, f4Position.y + rPuffInfo.fArea, 2.0f * rPuffInfo.fArea, -2.0f * rPuffInfo.fArea};
		rLayout.f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
		rLayout.f4Misc = {rPuffInfo.fIntensity, 1.0f, rPuffInfo.fCookie, common::Random<XM_2PI>(sRandomEngine)};

		++iPuffsRendered;
	}

	auto pTrailLayouts = reinterpret_cast<shaders::QuadLayout*>(gpBufferManager->mSmokeTrailsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	for (decltype(rFrame.interpolate.trails.uiMaxIndex) i = 0; i <= rFrame.interpolate.trails.uiMaxIndex; ++i)
	{
		if (!rFrame.interpolate.trails.pbUsed[i])
		{
			continue;
		}

		const engine::TrailInfo& rTrailInfo = rFrame.interpolate.trails.pObjectInfos[i];
		const engine::Trail& rTrail = rFrame.interpolate.trails.pObjects[i];
		
		++iTrailCount;

		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rTrailInfo.vecPosition);
		if (f4Position.x < sf4SmokeArea.x || f4Position.x > sf4SmokeArea.z || f4Position.y > sf4SmokeArea.y || f4Position.y < sf4SmokeArea.w)
		{
			continue;
		}

		float fJitterOne = gSmokeTrailsSideJitter.Get() * common::Random(sRandomEngine);
		fJitterOne = fJitterOne * fJitterOne;
		float fJitterTwo = gSmokeTrailsSideJitter.Get() * common::Random(sRandomEngine);
		fJitterTwo = fJitterTwo * fJitterTwo;

		float fElevation = gpIslands->GlobalElevation(rTrailInfo.vecPosition);
		auto vecBaseAreaPosition = common::ToBaseHeight(rTrailInfo.vecPosition, game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));
		fElevation = gpIslands->GlobalElevation(Trails::smpVecTrailsPositionPrevious[i]);
		auto vecBaseAreaPreviousPosition = common::ToBaseHeight(Trails::smpVecTrailsPositionPrevious[i], game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));

		auto vecToPrevious = vecBaseAreaPosition - vecBaseAreaPreviousPosition;
		float fLengthScale = XMVectorGetX(XMVector3Length(vecToPrevious));
		if (fLengthScale <= 0.01f)
		{
			continue;
		}

		auto vecToSmoothed = vecBaseAreaPosition - Trails::smpVecTrailsPositionSmoothed[i];
		if (XMVectorGetX(XMVector3Length(vecToSmoothed)) <= 0.01f)
		{
			vecToSmoothed = vecToPrevious;
		}
		auto vecToSmoothedNormal = XMVector3Normalize(vecToSmoothed);

		auto vecToPreviousNormal = XMVector3Normalize(vecToPrevious);
		auto vecLeftNormal = XMVector3Normalize(XMVector3Cross(vecToSmoothedNormal, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
		auto vecPointOne = vecBaseAreaPosition + gSmokeTrailsWidthCurrent.Get() * rTrailInfo.fWidth *  vecLeftNormal;
		auto vecPointTwo = vecBaseAreaPosition + gSmokeTrailsWidthCurrent.Get() * rTrailInfo.fWidth * -vecLeftNormal;
		float fLength = gSmokeTrailsLength.Get() + gSmokeTrailsLengthJitter.Get() * common::Random(sRandomEngine);
		if (rFrame.interpolate.fCurrentTime - rTrail.fStartTime < 0.05f)
		{
			fLength = 0.0f;
		}
		auto vecPointThree = vecBaseAreaPreviousPosition + gSmokeTrailsWidthPrevious.Get() * fJitterOne * vecLeftNormal - fLength * fLengthScale * vecToSmoothedNormal;
		auto vecPointFour = vecBaseAreaPreviousPosition + gSmokeTrailsWidthPrevious.Get() * fJitterTwo * -vecLeftNormal - fLength * fLengthScale * vecToSmoothedNormal;

		XMStoreFloat4A(&f4Position, vecPointOne);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[0] = {f4Position.x, f4Position.y, 0.0f, 0.0f};
		XMStoreFloat4A(&f4Position, vecPointTwo);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[1] = {f4Position.x, f4Position.y, 1.0f, 0.0f};
		XMStoreFloat4A(&f4Position, vecPointThree);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[2] = {f4Position.x, f4Position.y, 0.0f, 1.0f};
		XMStoreFloat4A(&f4Position, vecPointFour);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[3] = {f4Position.x, f4Position.y, 1.0f, 1.0f};

		float fQuantity = rTrailInfo.fIntensity * gSmokeTrailsQuantity.Get() / fLengthScale;
		ASSERT(!XMISNAN(fQuantity) && !XMISINF(fQuantity));
		pTrailLayouts[iTrailsRendered].pf4Misc[0] = {fQuantity, 1.0f, 0.0f, 0.0f};
		pTrailLayouts[iTrailsRendered].pf4Misc[1] = {fQuantity, 1.0f, 0.0f, 0.0f};
		pTrailLayouts[iTrailsRendered].pf4Misc[2] = {fQuantity, 0.0f, 0.0f, 0.0f};
		pTrailLayouts[iTrailsRendered].pf4Misc[3] = {fQuantity, 0.0f, 0.0f, 0.0f};

		++iTrailsRendered;

		float fPercent = gSmokeTrailsFollow.Get();
		Trails::smpVecTrailsPositionPrevious[i] = rTrailInfo.vecPosition;
		Trails::smpVecTrailsPositionSmoothed[i] = fPercent * Trails::smpVecTrailsPositionSmoothed[i] + (1.0f - fPercent) * rTrailInfo.vecPosition;
	}
#endif

	PROFILE_SET_COUNT(kCpuCounterSmokePuffs, iPuffCount);
	PROFILE_SET_COUNT(kCpuCounterSmokePuffsRendered, iPuffsRendered);
	gpPipelineManager->mpPipelines[kPipelineSmokePuffs].WriteIndirectBuffer(iCommandBuffer, iPuffsRendered);
	PROFILE_SET_COUNT(kCpuCounterSmokeTrails, iTrailCount);
	PROFILE_SET_COUNT(kCpuCounterSmokeTrailsRendered, iTrailsRendered);
	gpPipelineManager->mpPipelines[kPipelineSmokeTrails].WriteIndirectBuffer(iCommandBuffer, iTrailsRendered);
}

} // namespace engine
