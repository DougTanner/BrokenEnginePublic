#include "Smoke.h"

#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

#include "Game.h"
#include "Frame/Frame.h"

namespace engine
{

// 60 updates per second
constexpr float kfSmokeUpdateInterval = 0.0166666657f;

static XMFLOAT4 sf4SmokeArea {};

void RenderSmokeGlobal(int64_t iCommandBuffer, const game::FrameInterpolate& __restrict rFrameInterpolate)
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

		return;
	}

	static XMFLOAT4 sf4PreviousSmokeArea {};
	gbSmokeSpread = true; // DT: TEMP
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
	XMStoreFloat4A(&f4PlayerPosition, rFrameInterpolate.player.vecPosition);
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

} // namespace engine
