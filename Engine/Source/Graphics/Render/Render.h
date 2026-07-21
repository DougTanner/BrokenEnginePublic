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
inline int64_t giShadowActivePixelsX = 0; // Profile GPU-screen readout: ray-marched sub-window width in texels (PopulateShadowParameters)
inline int64_t giShadowActivePixelsY = 0; // Profile GPU-screen readout: ray-marched sub-window height in texels

// Lighting
inline bool gbLightingTemporalReset = false; // Set by CreateLightingTextures; re-arms the PopulateLightingParameters first-frame guard so a recreate doesn't blend stale history for one frame
inline int64_t giLightingDepositPixelsX = 0; // Profile GPU-screen readout: full deposit texture width in texels (deposit is not windowed)
inline int64_t giLightingDepositPixelsY = 0; // Profile GPU-screen readout: full deposit texture height in texels
inline int64_t giLightingSpreadStartActivePixelsX = 0; // Profile GPU-screen readout: cropped on-screen window width in start-pass (gSpreadTextureMultiplierStart) spread texels
inline int64_t giLightingSpreadStartActivePixelsY = 0; // Profile GPU-screen readout: cropped on-screen window height in start-pass spread texels
inline int64_t giLightingSpreadEndActivePixelsX = 0; // Profile GPU-screen readout: cropped on-screen window width in end-pass (gSpreadTextureMultiplierEnd) spread texels
inline int64_t giLightingSpreadEndActivePixelsY = 0; // Profile GPU-screen readout: cropped on-screen window height in end-pass spread texels
inline int64_t giLightingDepositInstances = 0; // Light-deposit quads this frame. Reset at the top of RenderFrameMain, accumulated by the three deposit EndRender writers (AreaLights, PointLights, HexShields), consumed by RenderLightingSpreadIndirect to gate the spread chain
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
