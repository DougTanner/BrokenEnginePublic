#if defined(BT_CLIENT)

#include "Render.h"

#include "Graphics/Camera.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/SmokeWrappersBase.h"

namespace engine
{

// Reciprocal of the previous smoke area's signed extents (Smoke/Wind OccupancyDilate remap divisor). Preserves
// the shader's component order exactly: X = 1/(z-x) (width), Y = 1/(w-y) (negative — max-Y is .y, min-Y is .w).
static void PopulatePreviousSmokeAreaSizeInv(shaders::GlobalLayout& rGlobalLayout, const XMFLOAT4& rArea)
{
	rGlobalLayout.f2PreviousSmokeAreaSizeInv.x = 1.0f / (rArea.z - rArea.x);
	rGlobalLayout.f2PreviousSmokeAreaSizeInv.y = 1.0f / (rArea.w - rArea.y);
}

void RenderSmokeGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.fSmokeMax = gSmokeMax.Get();
	rGlobalLayout.fSmokePower = gSmokePower.Get();
	rGlobalLayout.fSmokeDecay = gSmokeDecay.Get();

	rGlobalLayout.fSmokeColorMin = gSmokeColorMin.Get();
	rGlobalLayout.fSmokeColorMultiplier = gSmokeColorMultiplier.Get();
	rGlobalLayout.fSmokeLightingMultiplier = gSmokeLightingMultiplier.Get();
	rGlobalLayout.fSmokeIntensityFalloff = gSmokeIntensityFalloff.Get();
	rGlobalLayout.fSmokeWindNoiseScale = gSmokeWindNoiseScale.Get();
	rGlobalLayout.fSmokeWindNoiseQuantity = gSmokeWindNoiseQuantity.Get();
	rGlobalLayout.fSmokeNoiseQuantity = gSmokeNoiseQuantity.Get();

	rGlobalLayout.fSmokeNoiseScaleOne = gSmokeNoiseScaleOne.Get();
	rGlobalLayout.fSmokeNoiseScaleTwo = gSmokeNoiseScaleTwo.Get();
	rGlobalLayout.fSmokeObjectHeightInv = 1.0f / gSmokeObjectHeight.Get();
	rGlobalLayout.fSmokeEdgeDecayDistanceInv = 1.0f / gSmokeEdgeDecayDistance.Get();

	uint32_t uiTextureOneWidth = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.width;
	uint32_t uiTextureOneHeight = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.height;
	uint32_t uiMaxWidth = std::max(uiTextureOneWidth, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.width);
	uint32_t uiMaxHeight = std::max(uiTextureOneHeight, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.height);
	rGlobalLayout.uiSmokeTilesX = TileCount(uiMaxWidth);
	rGlobalLayout.uiSmokeTilesY = TileCount(uiMaxHeight);
	rGlobalLayout.fSmokeDepositTileScale = static_cast<float>(uiMaxWidth) / static_cast<float>(uiTextureOneWidth);

	// World-area follows the visible area each frame: aspect inherits from the framebuffer,
	// size grows with camera zoom-out. gSmokeSimulationArea acts as a margin multiplier.
	const XMFLOAT4& rVisible = game::gpCamera->f4RenderVisibleArea;
	float fCenterX = 0.5f * (rVisible.x + rVisible.z);
	float fCenterY = 0.5f * (rVisible.y + rVisible.w);
	float fHalfWidth = 0.5f * (rVisible.z - rVisible.x) * gSmokeSimulationArea.Get();
	float fHalfHeight = 0.5f * (rVisible.y - rVisible.w) * gSmokeSimulationArea.Get();
	XMFLOAT4 f4CurrentSmokeArea {fCenterX - fHalfWidth, fCenterY + fHalfHeight, fCenterX + fHalfWidth, fCenterY - fHalfHeight};

	static bool sbSmoke = false;
	if (sbSmoke != gSmokeEnabled.Get<bool>())
	{
		sbSmoke = gSmokeEnabled.Get<bool>();
		gbSmokeClear = true;
	}

	// Persisted across frames so the next frame's spread starts with previous == current
	// after a clear/disabled span — avoids divide-by-zero in WorldToSmokeTexcoord(zero, ...)
	static bool sbPreviousAreaInitialized = false;
	static XMFLOAT4 sf4PreviousSmokeArea {};
	if (!sbPreviousAreaInitialized)
	{
		sf4PreviousSmokeArea = f4CurrentSmokeArea;
		sbPreviousAreaInitialized = true;
	}

	if (gbSmokeClear)
	{
		gbSmokeClear = false;

		rGlobalLayout.f4SmokeArea = f4CurrentSmokeArea;
		rGlobalLayout.f4PreviousSmokeArea = f4CurrentSmokeArea;
		PopulatePreviousSmokeAreaSizeInv(rGlobalLayout, f4CurrentSmokeArea);
		sf4PreviousSmokeArea = f4CurrentSmokeArea;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 1);

		return;
	}

	if (!gSmokeEnabled.Get<bool>())
	{
		rGlobalLayout.f4SmokeArea = sf4PreviousSmokeArea;
		rGlobalLayout.f4PreviousSmokeArea = sf4PreviousSmokeArea;
		PopulatePreviousSmokeAreaSizeInv(rGlobalLayout, sf4PreviousSmokeArea);

		gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	rGlobalLayout.f4SmokeArea = f4CurrentSmokeArea;
	rGlobalLayout.f4PreviousSmokeArea = sf4PreviousSmokeArea;
	PopulatePreviousSmokeAreaSizeInv(rGlobalLayout, sf4PreviousSmokeArea);
	sf4PreviousSmokeArea = f4CurrentSmokeArea;

	gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 0);
}

} // namespace engine

#endif // defined(BT_CLIENT)
