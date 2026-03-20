#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"

namespace engine
{

void RenderSmokeGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.fSmokeMax = gSmokeMax.Get();
	rGlobalLayout.fSmokePower = gSmokePower.Get();
	rGlobalLayout.fSmokeDecay = gSmokeDecay.Get();

	rGlobalLayout.fSmokeColorMin = gSmokeColorMin.Get();
	rGlobalLayout.fSmokeColorMultiplier = gSmokeColorMultiplier.Get();
	rGlobalLayout.fSmokeIntensityFalloff = gSmokeIntensityFalloff.Get();
	rGlobalLayout.fSmokeWindNoiseScale = gSmokeWindNoiseScale.Get();
	rGlobalLayout.fSmokeWindNoiseQuantity = gSmokeWindNoiseQuantity.Get();
	rGlobalLayout.fSmokeNoiseQuantity = gSmokeNoiseQuantity.Get();

	rGlobalLayout.fSmokeNoiseScaleOne = gSmokeNoiseScaleOne.Get();
	rGlobalLayout.fSmokeNoiseScaleTwo = gSmokeNoiseScaleTwo.Get();
	rGlobalLayout.fSmokeObjectHeightInv = 1.0f / gSmokeObjectHeight.Get();
	rGlobalLayout.fSmokeEdgeDecayDistanceInv = 1.0f / gSmokeEdgeDecayDistance.Get();
	rGlobalLayout.fSmokeNoiseInfluence = gSmokeNoiseInfluence.Get();

	uint32_t uiTextureOneWidth = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.width;
	uint32_t uiMaxWidth = std::max(uiTextureOneWidth, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.width);
	rGlobalLayout.uiSmokeTilesX = (uiMaxWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
	rGlobalLayout.fSmokeDepositTileScale = static_cast<float>(uiMaxWidth) / static_cast<float>(uiTextureOneWidth);

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

		return;
	}

	static XMFLOAT4 sf4PreviousSmokeArea {};
	if (!gSmoke.Get<bool>())
	{
		rGlobalLayout.f4SmokeArea = sf4PreviousSmokeArea;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	XMFLOAT4A f4PlayerPosition {};
	XMStoreFloat4A(&f4PlayerPosition, game::gpCamera->mVecPosition);
	float fAreaX = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	float fAreaY = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	rGlobalLayout.f4SmokeArea = {f4PlayerPosition.x - fAreaX, f4PlayerPosition.y + fAreaY, f4PlayerPosition.x + fAreaX, f4PlayerPosition.y - fAreaY};

	shaders::AxisAlignedQuadLayout& rQuad = *reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mSmokeSpreadStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	WriteSpreadQuad(sf4PreviousSmokeArea, rGlobalLayout.f4SmokeArea, rQuad);
	sf4PreviousSmokeArea = rGlobalLayout.f4SmokeArea;

	gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 0);
}

} // namespace engine

#endif // defined(BT_CLIENT)
