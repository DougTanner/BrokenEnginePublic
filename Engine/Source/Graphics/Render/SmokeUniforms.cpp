#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"

namespace engine
{

static XMFLOAT4 sf4SmokeArea {};

void RenderSmokeGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.fSmokeMax = gSmokeMax.Get();
	rGlobalLayout.fSmokePower = gSmokePower.Get();
	rGlobalLayout.fSmokeDecay = gSmokeDecay.Get();

	rGlobalLayout.fSmokeColorMin = gSmokeColorMin.Get();
	rGlobalLayout.fSmokeColorMultiplier = gSmokeColorMultiplier.Get();
	rGlobalLayout.fSmokeIntensityFalloff = gSmokeIntensityFalloff.Get();
	rGlobalLayout.fSmokeDecayExtra = gSmokeDecayExtra.Get();

	rGlobalLayout.fSmokeDecayExtraThreshold = gSmokeDecayExtraThreshold.Get();
	rGlobalLayout.fSmokeWindNoiseScale = gSmokeWindNoiseScale.Get();
	rGlobalLayout.fSmokeWindNoiseQuantity = gSmokeWindNoiseQuantity.Get();
	rGlobalLayout.fSmokeNoiseQuantity = gSmokeNoiseQuantity.Get();

	rGlobalLayout.fSmokeNoiseScaleOne = gSmokeNoiseScaleOne.Get();
	rGlobalLayout.fSmokeNoiseScaleTwo = gSmokeNoiseScaleTwo.Get();
	rGlobalLayout.fSmokeObjectHeightInv = 1.0f / gSmokeObjectHeight.Get();
	rGlobalLayout.fSmokeEdgeDecayDistanceInv = 1.0f / gSmokeEdgeDecayDistance.Get();
	rGlobalLayout.fSmokeNoiseInfluence = gSmokeNoiseInfluence.Get();

	static bool sbSmoke = false;
	if (sbSmoke != gSmoke.Get<bool>())
	{
		sbSmoke = gSmoke.Get<bool>();
		gbSmokeClear = true;
	}

	if (gbSmokeClear)
	{
		gbSmokeClear = false;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadB].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadA].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	static XMFLOAT4 sf4PreviousSmokeArea {};
	if (!gSmoke.Get<bool>())
	{
		rGlobalLayout.f4SmokeArea = sf4PreviousSmokeArea;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadB].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadA].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	XMFLOAT4A f4PlayerPosition {};
	XMStoreFloat4A(&f4PlayerPosition, game::gpCamera->mVecPosition);
	float fAreaX = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	float fAreaY = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	rGlobalLayout.f4SmokeArea = {f4PlayerPosition.x - fAreaX, f4PlayerPosition.y + fAreaY, f4PlayerPosition.x + fAreaX, f4PlayerPosition.y - fAreaY};
	sf4SmokeArea = rGlobalLayout.f4SmokeArea;

	float fXOffset = (sf4PreviousSmokeArea.x - rGlobalLayout.f4SmokeArea.x) / (sf4PreviousSmokeArea.z - rGlobalLayout.f4SmokeArea.x);
	float fYOffset = (sf4PreviousSmokeArea.y - rGlobalLayout.f4SmokeArea.y) / (sf4PreviousSmokeArea.w - rGlobalLayout.f4SmokeArea.y);
	shaders::AxisAlignedQuadLayout& rQuad = *reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mSmokeSpreadStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	rQuad.f4VertexRect = {-1.0f + 2.0f * fXOffset, 1.0f - 2.0f * fYOffset, 2.0f, -2.0f};
	rQuad.f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
	rQuad.f4Params = {};
	sf4PreviousSmokeArea = rGlobalLayout.f4SmokeArea;

	gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeSpreadB].WriteIndirectBuffer(iCommandBuffer, 1);
	gpPipelineManager->mpPipelines[kPipelineSmokeSpreadA].WriteIndirectBuffer(iCommandBuffer, 1);
}

} // namespace engine

#endif // BT_CLIENT
