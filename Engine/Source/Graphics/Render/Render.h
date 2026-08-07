#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct FrameInterpolate;

}

namespace engine
{

struct WorldSizedTexelArea
{
	XMFLOAT4 f4Area {};
	float fAspect = 0.0f;
	float fTanHalfFov = 0.0f;
	float fWorldTexelX = 0.0f;
	float fWorldTexelY = 0.0f;
	float fFullWidth = 0.0f;
	float fFullHeight = 0.0f;

	XMFLOAT2 ComputeVisibleArea(float fEyeHeight) const
	{
		return {2.0f * fEyeHeight * fAspect * fTanHalfFov, 2.0f * fEyeHeight * fTanHalfFov};
	}
};

inline WorldSizedTexelArea XM_CALLCONV ComputeWorldSizedTexelArea(float fHeadroomMultiplier, float fTexelEyeHeight, float fTextureWidth, float fTextureHeight, float fAspect, float fFov, FXMVECTOR vecCameraPosition)
{
	float fTanHalfFov = std::tan(0.5f * XMConvertToRadians(fFov / fAspect));
	float fWorldTexelX = (2.0f * fHeadroomMultiplier * fAspect * fTanHalfFov / fTextureWidth) * fTexelEyeHeight;
	float fWorldTexelY = (2.0f * fHeadroomMultiplier * fTanHalfFov / fTextureHeight) * fTexelEyeHeight;
	float fFullWidth = fTextureWidth * fWorldTexelX;
	float fFullHeight = fTextureHeight * fWorldTexelY;
	XMFLOAT4A f4CameraPosition {};
	XMStoreFloat4A(&f4CameraPosition, vecCameraPosition);
	int64_t iLeftTexel = static_cast<int64_t>(std::floor((f4CameraPosition.x - fFullWidth * 0.5f) / fWorldTexelX));
	int64_t iTopTexel = static_cast<int64_t>(std::floor((f4CameraPosition.y + fFullHeight * 0.5f) / fWorldTexelY));
	float fLeft = static_cast<float>(iLeftTexel) * fWorldTexelX;
	float fTop = static_cast<float>(iTopTexel) * fWorldTexelY;
	return WorldSizedTexelArea {
		.f4Area = {fLeft, fTop, fLeft + fFullWidth, fTop - fFullHeight},
		.fAspect = fAspect,
		.fTanHalfFov = fTanHalfFov,
		.fWorldTexelX = fWorldTexelX,
		.fWorldTexelY = fWorldTexelY,
		.fFullWidth = fFullWidth,
		.fFullHeight = fFullHeight,
	};
}

struct TemporalAreaLatch
{
	bool bInitialized = false;
	XMFLOAT4 f4PreviousArea {};

	float Update(const XMFLOAT4& rf4CurrentArea, bool& rbReset, float fBlend, XMFLOAT4& rf4PreviousArea)
	{
		if (rbReset)
		{
			rbReset = false;
			bInitialized = false;
		}

		float fResolvedBlend = fBlend;
		if (!bInitialized)
		{
			f4PreviousArea = rf4CurrentArea;
			bInitialized = true;
			fResolvedBlend = 1.0f;
		}

		rf4PreviousArea = f4PreviousArea;
		f4PreviousArea = rf4CurrentArea;
		return fResolvedBlend;
	}
};

void RenderFrameGlobal(int64_t iCommandBuffer, float fCurrentTime);
void RenderFrameMain(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord);

// Shadow
inline bool gbShadowTemporalReset = false; // Set by CreateShadowTextures; re-arms the PopulateShadowParameters first-frame guard so a recreate doesn't blend stale history for one frame

// Lighting
// Set by CreateLightingTextures and by Camera on every frame with outward live-eye movement. PopulateLightingParameters
// consumes it to force pure-current output and re-seed history; any lighting refresh cadence must refresh while it is pending.
inline bool gbLightingTemporalReset = false;
inline int64_t giLightingDepositInstances = 0; // Light-deposit quads this frame, reset and accumulated by the deposit EndRender writers; refreshes always process the spread chain so empty deposits publish fresh zeroes.
void RenderLightingGlobal(int64_t iCommandBuffer);
void RenderLightingMain(int64_t iCommandBuffer);
void RenderLightingSpreadIndirect(int64_t iCommandBuffer);

// Smoke
inline bool gbSmokeClear = true;

void RenderSmokeGlobal(int64_t iCommandBuffer);

// Wind
inline int64_t giWindTextureIndex = 0; // 0 = write TextureOne, 1 = write TextureTwo

void RenderWindGlobal(int64_t iCommandBuffer);

// Water
void PopulateWaterParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent);

} // namespace engine

#endif // defined(BT_CLIENT)
