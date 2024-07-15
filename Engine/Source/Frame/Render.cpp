#include "Render.h"

#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

#include "Frame/Frame.h"
#include "Input/Input.h"

using namespace DirectX;

namespace engine
{

float DayPercent(const game::Frame& __restrict rFrame)
{
	if (rFrame.fSunAngle >= 0.0f && rFrame.fSunAngle <= XM_PIDIV2)
	{
		return rFrame.fSunAngle / XM_PIDIV2;
	}
	else if (rFrame.fSunAngle >= XM_PIDIV2 && rFrame.fSunAngle <= XM_PI)
	{
		return 1.0f - (rFrame.fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

float NightPercent(const game::Frame& __restrict rFrame)
{
	if (rFrame.fSunAngle >= XM_PI && rFrame.fSunAngle < XM_PI + XM_PIDIV2)
	{
		return (rFrame.fSunAngle - XM_PI) / XM_PIDIV2;
	}
	if (rFrame.fSunAngle >= XM_PI + XM_PIDIV2)
	{
		return 1.0f - (rFrame.fSunAngle - (XM_PI + XM_PIDIV2)) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

void RenderFrameGlobal(int64_t iCommandBuffer, const game::Frame& __restrict rFrame)
{
	RenderLightingGlobal(iCommandBuffer);
	RenderSmokeGlobal(iCommandBuffer, rFrame);
	RenderGlobalList(iCommandBuffer, rFrame, UPDATE_LIST);

	float fSunAngle = rFrame.fSunAngle;
	// fSunAngle = XM_PIDIV2;
	// fSunAngle = XM_PI + XM_PIDIV2;
	// fSunAngle = 2.0f;

	// Global data
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	static int siFrame = 0;
	rGlobalLayout.i4Misc.x = static_cast<int>(gpSwapchainManager->miFramebufferIndex);
	rGlobalLayout.i4Misc.y = static_cast<int>(rFrame.iFrame);
	rGlobalLayout.i4Misc.z = static_cast<int>(siFrame++);
	rGlobalLayout.i4Misc.w = static_cast<int>(iCommandBuffer);

	rGlobalLayout.f4Misc.x = rFrame.fCurrentTime;
	rGlobalLayout.f4Misc.y = gBaseHeight.Get();
	rGlobalLayout.f4Misc.z = gpSwapchainManager->mfAspectRatio;
	rGlobalLayout.f4Misc.w = TextureManager::DetailTextureAspectRatio();

	rGlobalLayout.f4VisibleArea = gf4RenderVisibleArea;

	// Sun
	auto vecSunNormal = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	auto sunRotationMatrix = XMMatrixRotationY(-fSunAngle);
	vecSunNormal = XMVector4Normalize(XMVector4Transform(vecSunNormal, sunRotationMatrix));
	XMStoreFloat4(&rGlobalLayout.f4SunNormal, vecSunNormal);

	// Sunlight
	float fAmbientNight = std::max(kfDefaultMinimumAmbient, gMinimumAmbient.Get());
	float fAmbientMorning = std::max(0.075f, gMinimumAmbient.Get());
	XMVECTOR vecSunMorning = 0.5f * XMVectorSet(1.0f, 219.0f / 255.0f, 0.f, 1.0f);
	XMVECTOR vecAmbientMorning = XMVectorSet(fAmbientMorning, fAmbientMorning, fAmbientMorning, 1.0f);
	XMVECTOR vecSunNoon = XMVectorSet(0.8f, 0.8f, 0.8f, 1.0f);
	XMVECTOR vecAmbientNoon = XMVectorSet(0.2f, 0.2f, 0.2f, 1.0f);
	XMVECTOR vecSunEvening = 0.75f * XMVectorSet(0.8f, 0.4f, 0.4f, 1.0f);
	XMVECTOR vecAmbientEvening = XMVectorSet(fAmbientMorning, fAmbientMorning, fAmbientMorning, 0.0f);
	XMVECTOR vecSunMidnight = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR vecAmbientMidnight = XMVectorSet(fAmbientNight, fAmbientNight, fAmbientNight, 1.0f);

	XMVECTOR vecSun = vecSunMidnight;
	XMVECTOR vecAmbient = vecAmbientMidnight;

	static constexpr float kfNoonStart = XM_PIDIV8;
	static constexpr float kfNoonEnd = XM_PIDIV2 + XM_PIDIV8;
	static constexpr float kfEvening = XM_PI - XM_PIDIV16;
	static constexpr float kfNightStart = XM_PI;
	static constexpr float kfNightEnd = XM_2PI;
	static constexpr float kfMorning = XM_PIDIV16;

	if (fSunAngle >= kfMorning && fSunAngle < kfNoonStart)
	{
		float fLerp = (fSunAngle - kfMorning) / (kfNoonStart - kfMorning);
		vecSun = XMVectorLerp(vecSunMorning, vecSunNoon, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientMorning, vecAmbientNoon, fLerp);
	}
	else if (fSunAngle >= kfNoonStart && fSunAngle < kfNoonEnd)
	{
		float fLerp = (fSunAngle - kfNoonStart) / (kfEvening - kfNoonStart);
		vecSun = XMVectorLerp(vecSunNoon, vecSunNoon, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientNoon, vecAmbientNoon, fLerp);
	}
	else if (fSunAngle >= kfNoonEnd && fSunAngle < kfEvening)
	{
		float fLerp = (fSunAngle - kfNoonEnd) / (kfEvening - kfNoonEnd);
		vecSun = XMVectorLerp(vecSunNoon, vecSunEvening, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientNoon, vecAmbientEvening, fLerp);
	}
	else if (fSunAngle >= kfEvening && fSunAngle < kfNightStart)
	{
		float fLerp = (fSunAngle - kfEvening) / (kfNightStart - kfEvening);
		vecSun = XMVectorLerp(vecSunEvening, vecSunMidnight, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientEvening, vecAmbientMidnight, fLerp);
	}
	else if (fSunAngle >= kfNightStart && fSunAngle < kfNightEnd)
	{
		vecSun = vecSunMidnight;
		vecAmbient = vecAmbientMidnight;
	}
	else if (fSunAngle >= 0.0f)
	{
		float fLerp = fSunAngle / kfMorning;
		vecSun = XMVectorLerp(vecSunMidnight, vecSunMorning, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientMidnight, vecAmbientMorning, fLerp);
	}
	else
	{
		DEBUG_BREAK();
	}

	XMStoreFloat4(&rGlobalLayout.f4SunColor, vecSun);
	XMStoreFloat4(&rGlobalLayout.f4AmbientColor, vecAmbient);

	static constexpr float kfNoonFeatherEnd = XM_PIDIV8;
	float fNoonPercent = 0.0f;
	if (fSunAngle >= kfNoonFeatherEnd && fSunAngle <= XM_PIDIV2)
	{
		fNoonPercent = (fSunAngle - kfNoonFeatherEnd) / (XM_PIDIV2 - kfNoonFeatherEnd);
	}
	else if (fSunAngle > XM_PIDIV2 && fSunAngle <= (XM_PI - kfNoonFeatherEnd))
	{
		fNoonPercent = 1.0f - (fSunAngle - XM_PIDIV2) / (XM_PIDIV2 - kfNoonFeatherEnd);
	}

	float fDayPercent = DayPercent(rFrame);

	// Shadow texture
	float fShadowTextureSizeWidth = static_cast<float>(gpTextureManager->mShadowTexture.mInfo.extent.width);
	float fShadowTextureSizeHeight = static_cast<float>(gpTextureManager->mShadowTexture.mInfo.extent.height);
	float fShadowElevationTextureSizeWidth = fShadowTextureSizeWidth + 0.5f * fShadowTextureSizeWidth;

	float fShadowNoon = std::pow(fDayPercent, gShadowFeatherPower.Get());
	float fShadowEvening = 1.0f - fShadowNoon;
	float fOffsetNoon = std::pow(fNoonPercent, 2.0f);

	rGlobalLayout.f4ShadowTwo.x = 1.0f / (fShadowNoon * gShadowFeatherNoon.Get() + fShadowEvening * gShadowFeatherSunset.Get());
	rGlobalLayout.f4ShadowTwo.y = fOffsetNoon * gShadowFeatherNoonOffset.Get();
	rGlobalLayout.f4ShadowTwo.z = gShadowDistanceFallof.Get();
	rGlobalLayout.f4ShadowTwo.w = gShadowBlurSigma.Get();
	
	rGlobalLayout.f4ShadowThree.x = fDayPercent * gObjectShadowsBlurDistanceNoon.Get() + (1.0f - fDayPercent) * gObjectShadowsBlurDistanceSunset.Get();
	rGlobalLayout.f4ShadowThree.y = 0.0f;
	rGlobalLayout.f4ShadowThree.z = fDayPercent * gObjectShadowsNoon.Get() + (1.0f - fDayPercent) * gObjectShadowsSunset.Get();
	rGlobalLayout.f4ShadowThree.z *= std::pow(fDayPercent, 0.1f);
	rGlobalLayout.f4ShadowThree.w = fShadowEvening * gShadowFeatherSunsetOffset.Get();

	static constexpr float kfSunriseStretchBegin = XM_2PI - XM_PIDIV4;
	static constexpr float kfSunriseStretchEnd = XM_PIDIV2 - XM_PIDIV16;
	static constexpr float kfSunriseStretchTotal = (XM_2PI - kfSunriseStretchBegin) + kfSunriseStretchEnd;
	float fSunriseStretch = 0.0f;
	static constexpr float kfSunsetStretchBegin = XM_PIDIV2 + XM_PIDIV16;
	static constexpr float kfSunsetStretchEnd = XM_PI + XM_PIDIV4;
	static constexpr float kfSunsetStretchTotal = kfSunsetStretchEnd - kfSunsetStretchBegin;
	float fSunsetStretch = 0.0f;
	if (fSunAngle >= kfSunriseStretchBegin && fSunAngle < 0.0f)
	{
		fSunriseStretch = 1.0f - (fSunAngle - kfSunriseStretchBegin) / (kfSunriseStretchTotal);
	}
	else if (fSunAngle >= 0.0f && fSunAngle < kfSunriseStretchEnd)
	{
		fSunriseStretch = 1.0f - (fSunAngle + (XM_2PI - kfSunriseStretchBegin)) / kfSunriseStretchTotal;
	}
	else if (fSunAngle >= kfSunriseStretchEnd && fSunAngle < kfSunsetStretchBegin)
	{
	}
	else if (fSunAngle >= kfSunsetStretchBegin && fSunAngle < kfSunsetStretchEnd)
	{
		fSunsetStretch = (fSunAngle - kfSunsetStretchBegin) / kfSunsetStretchTotal;
	}
	else if (fSunAngle >= kfSunsetStretchEnd)
	{
		fSunriseStretch = 1.0f;
		fSunsetStretch = 1.0f;
	}

	rGlobalLayout.f4ShadowFour.x = fSunriseStretch * gObjectShadowsSunsetStretch.Get();
	rGlobalLayout.f4ShadowFour.y = fSunsetStretch * gObjectShadowsSunsetStretch.Get();
	rGlobalLayout.f4ShadowFour.z = std::pow(fDayPercent, 0.25f) * gShadowAffectAmbient.Get();
	rGlobalLayout.f4ShadowFour.w = 0.0f;

	rGlobalLayout.fShadowTextureSizeWidth = fShadowTextureSizeWidth;
	rGlobalLayout.fShadowTextureSizeHeight = fShadowTextureSizeHeight;
	rGlobalLayout.fShadowElevationTextureSizeWidth = fShadowElevationTextureSizeWidth;
	rGlobalLayout.fShadowElevationTextureSizeHeight = fShadowTextureSizeHeight;
	rGlobalLayout.fShadowHeightFadeTop = gShadowHeightFadeTop.Get();
	rGlobalLayout.fShadowHeightFadeBottom = gShadowHeightFadeBottom.Get();

	rGlobalLayout.i4ShadowTwo.x = gpTextureManager->mShadowTexture.mInfo.extent.width; // X pixels
	rGlobalLayout.i4ShadowTwo.y = gpTextureManager->mShadowTexture.mInfo.extent.height; // Y pixels
	rGlobalLayout.i4ShadowTwo.z = gpTextureManager->mObjectShadowsTexture.mInfo.extent.width; // X pixels
	rGlobalLayout.i4ShadowTwo.w = gpTextureManager->mObjectShadowsTexture.mInfo.extent.height; // Y pixels

	rGlobalLayout.f4VisibleAreaShadowsExtra = gf4RenderVisibleArea;
	float fQuads = (gf4RenderVisibleArea.z - gf4RenderVisibleArea.x) / gf2VisibleAreaQuadSize.x;
	if (fSunAngle >= XM_PI + XM_PIDIV2 || fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.f4VisibleAreaShadowsExtra.z += (fQuads / 2.0f) * gf2VisibleAreaQuadSize.x;

		rGlobalLayout.f4ShadowOne.x = (gf4RenderVisibleArea.z - gf4RenderVisibleArea.x) / fShadowTextureSizeWidth;
		if (fSunAngle >= 0.0f && fSunAngle < XM_PIDIV2)
		{
			rGlobalLayout.f4ShadowOne.y = fSunAngle;
		}
		else
		{
			rGlobalLayout.f4ShadowOne.y = 0.0f;
		}
		rGlobalLayout.f4ShadowOne.z = 1.0;

		rGlobalLayout.i4ShadowOne.x = static_cast<int>(fShadowElevationTextureSizeWidth); // !=
		rGlobalLayout.i4ShadowOne.y = 1; // ++
		rGlobalLayout.i4ShadowOne.z = 0; // Start offset
	}
	else
	{
		rGlobalLayout.f4VisibleAreaShadowsExtra.x -= (fQuads / 2.0f) * gf2VisibleAreaQuadSize.x;

		rGlobalLayout.f4ShadowOne.x = -(gf4RenderVisibleArea.z - gf4RenderVisibleArea.x) / fShadowTextureSizeWidth;
		rGlobalLayout.f4ShadowOne.y = fSunAngle >= XM_PI ? 0.0f : XM_PI - fSunAngle;
		rGlobalLayout.f4ShadowOne.z = -1.0;

		rGlobalLayout.i4ShadowOne.x = 0; // !=
		rGlobalLayout.i4ShadowOne.y = -1; // ++
		rGlobalLayout.i4ShadowOne.z = static_cast<int>(fShadowTextureSizeWidth / 2.0f); // Start offset
	}

	// Terrain
	rGlobalLayout.f4Terrain.x = gIslandHeight.Get();
	rGlobalLayout.f4Terrain.y = fNoonPercent * gIslandAmbientOcclusion.Get();
	rGlobalLayout.f4Terrain.z = gTerrainEarlyOut.Get();
	rGlobalLayout.f4Terrain.w = gWaterEarlyOut.Get();

	rGlobalLayout.f4TerrainTwo.x = gWaterDepth.Get();
	rGlobalLayout.f4TerrainTwo.y = std::max(0.25f, std::pow(fDayPercent, 0.25f));
	rGlobalLayout.f4TerrainTwo.z = 0.0f;
	rGlobalLayout.f4TerrainTwo.w = 0.0f;

	rGlobalLayout.fTerrainNormalXMultiplier = gpIslands->mbFlipX ? -1.0f : 1.0f;
	rGlobalLayout.fTerrainNormalYMultiplier = gpIslands->mbFlipY ? -1.0f : 1.0f;

	rGlobalLayout.fTerrainSnowMultiplier = gTerrainSnowMultiplier.Get();

	rGlobalLayout.fTerrainRockMultiplier = gTerrainRockMultiplier.Get();
	rGlobalLayout.fTerrainRockSize = gTerrainRockSize.Get();
	rGlobalLayout.fTerrainRockBlend = gTerrainRockBlend.Get();
	rGlobalLayout.fTerrainRockNormalsSizeOne = gTerrainRockNormalsSizeOne.Get();
	rGlobalLayout.fTerrainRockNormalsSizeTwo = gTerrainRockNormalsSizeTwo.Get();
	rGlobalLayout.fTerrainRockNormalsSizeThree = gTerrainRockNormalsSizeThree.Get();
	rGlobalLayout.fTerrainRockNormalsBlend = gTerrainRockNormalsBlend.Get();

	rGlobalLayout.fTerrainBeachHeight = gTerrainBeachHeight.Get();
	rGlobalLayout.fTerrainBeachSandSize = gTerrainBeachSandSize.Get();
	rGlobalLayout.fTerrainBeachSandBlend = gTerrainBeachSandBlend.Get();
	rGlobalLayout.fTerrainBeachNormalsSizeOne = gTerrainBeachNormalsSizeOne.Get();
	rGlobalLayout.fTerrainBeachNormalsSizeTwo = gTerrainBeachNormalsSizeTwo.Get();
	rGlobalLayout.fTerrainBeachNormalsSizeThree = gTerrainBeachNormalsSizeThree.Get();
	rGlobalLayout.fTerrainBeachNormalsBlend = std::max(fDayPercent * fDayPercent, 0.25f) * gTerrainBeachNormalsBlend.Get();

	// Water global
	rGlobalLayout.f4WaterOne.x = gWaterTerrainHeight.Get();
	rGlobalLayout.f4WaterOne.y = gWaterTerrainFade.Get();
	rGlobalLayout.f4WaterOne.z = gWaterNoiseFrequency.Get();
	rGlobalLayout.f4WaterOne.w = gWaterNoiseAmount.Get();

	rGlobalLayout.f4WaterTwo.x = gWaterDepthLutFeather.Get();
	rGlobalLayout.f4WaterTwo.y = gWaterDepthColorFeather.Get();
	rGlobalLayout.f4WaterTwo.z = fDayPercent * gWaterDepthReflectionFeather.Get();
	rGlobalLayout.f4WaterTwo.w = gWaterColorNoiseFrequency.Get();
	
	rGlobalLayout.f4WaterThree.x = gHighMultiplier.Get();
	rGlobalLayout.f4WaterThree.y = gHighScaleOne.Get();
	rGlobalLayout.f4WaterThree.z = gHighScaleTwo.Get();

	if (fSunAngle >= XM_PIDIV16 && fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.f4WaterThree.w = 1.0f - (fSunAngle - XM_PIDIV16) / (XM_PIDIV2 - XM_PIDIV16);
	}
	else if (fSunAngle >= XM_PIDIV2 && fSunAngle < XM_PI - XM_PIDIV16)
	{
		rGlobalLayout.f4WaterThree.w = (fSunAngle - XM_PIDIV2) / (XM_PI - XM_PIDIV16 - XM_PIDIV2);
	}
	else
	{
		rGlobalLayout.f4WaterThree.w = 1.0f;
	}
	rGlobalLayout.f4WaterThree.w = std::pow(rGlobalLayout.f4WaterThree.w, 2.0f);

	rGlobalLayout.f4WaterFour.x = std::pow(fDayPercent, 0.5f) * gWaterFresnel.Get();
	rGlobalLayout.f4WaterFour.y = gWaterColorBottom.Get();
	rGlobalLayout.f4WaterFour.z = 1.0f / gWaterColorHeight.Get();
	rGlobalLayout.f4WaterFour.w = gWaterColorNoiseAmount.Get();

	if (fSunAngle >= 0.0f && fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.f4WaterFive.w = 1.0f - (fSunAngle) / XM_PIDIV2;
	}
	else if (fSunAngle >= XM_PIDIV2 && fSunAngle < XM_PI)
	{
		rGlobalLayout.f4WaterFive.w = (fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}
	else
	{
		rGlobalLayout.f4WaterFive.w = 1.0f;
	}
	rGlobalLayout.f4WaterFive.w = std::pow(rGlobalLayout.f4WaterFive.w, 2.0f);

	rGlobalLayout.f4WaterSix.x = std::pow(fDayPercent, 0.5f) * gWaterFresnel2.Get();
	rGlobalLayout.f4WaterSix.y = gBeachDirectionalFadeBottom.Get();
	rGlobalLayout.f4WaterSix.z = 1.0f / gBeachDirectionalFadeHeight.Get();
	rGlobalLayout.f4WaterSix.w = gLowSteepness.Get();

	rGlobalLayout.f4WaterSeven.x = gMediumSteepness.Get();

	rGlobalLayout.i4Water.x = static_cast<int>(std::min(gLowCount.Get<int64_t>(), static_cast<int64_t>(gLowMax.Get())));
	rGlobalLayout.i4Water.y = static_cast<int>(gMediumCount.Get<int64_t>());
}

void RenderFrameMain(int64_t iCommandBuffer, const game::Frame& __restrict rFrame)
{
	RenderLightingMain(iCommandBuffer, rFrame);
	RenderSmokeMain(iCommandBuffer, rFrame);
	RenderMainList(iCommandBuffer, rFrame, UPDATE_LIST);

	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	// Camera shake
	float fCameraShake = std::pow(rFrame.camera.fCameraShake, 1.0f);
	constexpr float kfMaxRoll = 0.005f;
	constexpr float kfMaxPitch = 0.005f;
	constexpr float kfMaxYaw = 0.01f;
	siv::BasicPerlinNoise<float> perlinRoll {0};
	siv::BasicPerlinNoise<float> perlinPitch {1};
	siv::BasicPerlinNoise<float> perlinYaw {2};
	auto matCameraShake = XMMatrixRotationRollPitchYaw(kfMaxRoll  * fCameraShake * (-1.0f + 2.0f * perlinRoll.octave1D_01(8.0f * rFrame.fCurrentTime, 4)),
	                                                   kfMaxPitch * fCameraShake * (-1.0f + 2.0f * perlinPitch.octave1D_01(8.0f * rFrame.fCurrentTime, 4)),
	                                                   kfMaxYaw   * fCameraShake * (-1.0f + 2.0f * perlinYaw.octave1D_01(8.0f * rFrame.fCurrentTime, 4)));

	XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&rMainLayout.f4x4ViewProjection[0]), XMMatrixTranspose(XMMatrixMultiply(gMatView, XMMatrixMultiply(matCameraShake, gMatPerspective))));

	XMStoreFloat4(&rMainLayout.f4EyePosition, rFrame.camera.vecEyePosition);
	XMStoreFloat4(&rMainLayout.f4ToEyeNormal, rFrame.camera.vecToEyeNormal);

	// Water low frequency
	{
		int64_t iCount = gLowCount.Get<int64_t>();

		auto vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(gLowAngle.Get()));
		rMainLayout.pf4LowWavesOne[0].x = XMVectorGetX(vecDirection);
		rMainLayout.pf4LowWavesOne[0].y = XMVectorGetY(vecDirection);

		vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(0.0f));
		rMainLayout.pf4LowWavesOne[0].z = XMVectorGetX(vecDirection);
		rMainLayout.pf4LowWavesOne[0].w = XMVectorGetY(vecDirection);

		rMainLayout.pf4LowWavesTwo[0].x = (2.0f * XM_PI) / (gLowWavelength.Get()); // Omega
		rMainLayout.pf4LowWavesTwo[0].y = gLowAmplitude.Get();
		rMainLayout.pf4LowWavesTwo[0].z = gLowSpeed.Get() * rMainLayout.pf4LowWavesTwo[0].x; // Phi
		rMainLayout.pf4LowWavesTwo[0].w = 0.0f;

		common::RandomEngine randomEngine {};
		for (int64_t i = 1; i < iCount; ++i)
		{
			float fAngleAdjust = ((i % 2) == 0 ? 1.0f : -1.0f) * gLowAngleAdjust.Get() * common::Random(randomEngine);
			vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(gLowAngle.Get() + fAngleAdjust));
			rMainLayout.pf4LowWavesOne[i].x = XMVectorGetX(vecDirection);
			rMainLayout.pf4LowWavesOne[i].y = XMVectorGetY(vecDirection);

			vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(XM_2PI * static_cast<float>(i) / static_cast<float>(iCount)));
			rMainLayout.pf4LowWavesOne[i].z = XMVectorGetX(vecDirection);
			rMainLayout.pf4LowWavesOne[i].w = XMVectorGetY(vecDirection);

			float fAdjust = common::Random(randomEngine); // static_cast<float>(i) / static_cast<float>(iCount - 1);
			float fWavelengthAdjust = fAdjust * gLowWavelengthAdjust.Get();
			float fAmplitudeAdjust = (1.0f - fAdjust) * std::abs(gLowAmplitudeAdjust.Get()) * common::Random(randomEngine);
			float fSpeedAdjust = fAdjust * gLowSpeedAdjust.Get();
			rMainLayout.pf4LowWavesTwo[i].x = std::abs((2.0f * XM_PI) / (gLowWavelength.Get() + fWavelengthAdjust * gLowWavelength.Get())); // Omega
			rMainLayout.pf4LowWavesTwo[i].y = std::abs(gLowAmplitude.Get() - fAmplitudeAdjust * gLowAmplitude.Get());
			rMainLayout.pf4LowWavesTwo[i].y = std::min(rMainLayout.pf4LowWavesTwo[i].y, 0.1f * (1.0f / rMainLayout.pf4LowWavesTwo[i].x));
			rMainLayout.pf4LowWavesTwo[i].z = (gLowSpeed.Get() + gLowSpeed.Get() * fSpeedAdjust * common::Random(randomEngine)) * rMainLayout.pf4LowWavesTwo[i].x; // Phi
			rMainLayout.pf4LowWavesTwo[i].w = 0.0f; // common::Random(randomEngine);

			if (i < 64 && (i % 3) == 0)
			{
				rMainLayout.pf4LowWavesTwo[i].y = 0.0f;
			}
		}
	}

	// Water medium frequency
	{
		int64_t iCount = gMediumCount.Get<int64_t>();

		common::RandomEngine randomEngine {};
		for (int64_t i = 0; i < iCount; ++i)
		{
			float fAngleAdjust = gMediumAngleAdjust.Get() * common::Random(randomEngine);
			auto vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngleAdjust));
			rMainLayout.pf4MediumWavesOne[i].x = XMVectorGetX(vecDirection);
			rMainLayout.pf4MediumWavesOne[i].y = XMVectorGetY(vecDirection);

			float fWavelengthAdjust = -gMediumWavelengthAdjust.Get() + 2.0f * gMediumWavelengthAdjust.Get() * common::Random(randomEngine);
			float fAmplitudeAdjust = -gMediumAmplitudeAdjust.Get() + 2.0f * gMediumAmplitudeAdjust.Get() * common::Random(randomEngine);
			float fSpeedAdjust = -gMediumSpeedAdjust.Get() + 2.0f * gMediumSpeedAdjust.Get() * common::Random(randomEngine);
			rMainLayout.pf4MediumWavesTwo[i].x = std::abs((2.0f * XM_PI) / (gMediumWavelength.Get() + fWavelengthAdjust * gMediumWavelength.Get())); // Omega
			rMainLayout.pf4MediumWavesTwo[i].y = std::abs(gMediumAmplitude.Get() + fAmplitudeAdjust * gMediumAmplitude.Get());
			rMainLayout.pf4MediumWavesTwo[i].y = std::min(rMainLayout.pf4MediumWavesTwo[i].y, 0.1f * (1.0f / rMainLayout.pf4MediumWavesTwo[i].x));
			rMainLayout.pf4MediumWavesTwo[i].z = (gMediumSpeed.Get() + fSpeedAdjust * gMediumSpeed.Get()) * rMainLayout.pf4MediumWavesTwo[i].x; // Phi
			rMainLayout.pf4MediumWavesTwo[i].w = 0.0f; // gMediumSteepness.Get();
		}
	}

	// Hex shield
	rMainLayout.fHexShieldGrow = gHexShieldGrow.Get();
	rMainLayout.fHexShieldEdgeDistance = gHexShieldEdgeDistance.Get();
	rMainLayout.fHexShieldEdgePower = gHexShieldEdgePower.Get();
	rMainLayout.fHexShieldEdgeMultiplier = gHexShieldEdgeMultiplier.Get();

	rMainLayout.fHexShieldWaveMultiplier = gHexShieldWaveMultiplier.Get();
	rMainLayout.fHexShieldWaveDotMultiplier = gHexShieldWaveDotMultiplier.Get();
	rMainLayout.fHexShieldWaveIntensityMultiplier = gHexShieldWaveIntensityMultiplier.Get();
	rMainLayout.fHexShieldWaveIntensityPower = gHexShieldWaveIntensityPower.Get();
	rMainLayout.fHexShieldWaveFalloffPower = gHexShieldWaveFalloffPower.Get();

	rMainLayout.fHexShieldDirectionFalloffPower = gHexShieldDirectionFalloffPower.Get();
	rMainLayout.fHexShieldDirectionMultiplier = gHexShieldDirectionMultiplier.Get();
}

XMVECTOR XM_CALLCONV ScreenToWorld(FXMVECTOR vecScreenPos, float fHeight)
{
	XMVECTOR vecPlane = XMPlaneFromPointNormal(XMVectorSet(0.0f, 0.0f, fHeight, 1.0f), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));

	float fViewportWidth = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	float fViewportHeight = static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	auto vecWorldPos = XMVectorMultiply(XMVectorSet(fViewportWidth, fViewportHeight, 1.0f, 1.0f), vecScreenPos);
	vecWorldPos = XMVectorSetZ(vecWorldPos, fHeight);

	vecWorldPos = XMVectorSetZ(vecWorldPos, 0.0f);
	auto vecRayStart = XMVector3Unproject(vecWorldPos, 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, XMMatrixIdentity());
	vecWorldPos = XMVectorSetZ(vecWorldPos, 1.0f);
	auto vecRayEnd = XMVector3Unproject(vecWorldPos, 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, XMMatrixIdentity());

	return XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
}

void CalculateMatricesAndVisibleArea(const game::Frame& __restrict rFrame, bool bWriteVisibleArea)
{
	auto vecToEyeNormal = XMVector3Normalize(XMVectorSubtract(rFrame.camera.vecEyePosition, rFrame.camera.vecPosition));
	auto vecUp = XMVector3Cross(vecToEyeNormal, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
	gMatView = XMMatrixLookAtRH(rFrame.camera.vecEyePosition, rFrame.camera.vecPosition, vecUp);

	static constexpr float kfNearClip = 1.0f;
	static constexpr float kfFarClip = 400.0f;
	float fViewportWidth = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	float fViewportHeight = static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	float fAspectRatio = gpSwapchainManager->mfAspectRatio;
	gMatPerspective = XMMatrixPerspectiveFovRH(XMConvertToRadians(gFov.Get() / fAspectRatio), fAspectRatio, kfNearClip, kfFarClip);

	if (!bWriteVisibleArea)
	{
		return;
	}

	XMFLOAT3 f3Origin{ 0.0f, 0.0f, 0.0f };
	XMFLOAT3 f3Normal{ 0.0f, 0.0f, 1.0f };
	XMVECTOR vecPlane = XMPlaneFromPointNormal(XMLoadFloat3(&f3Origin), XMLoadFloat3(&f3Normal));

	XMMATRIX matIdentity = XMMatrixIdentity();

	XMVECTOR vecRayStart{};
	XMVECTOR vecRayEnd{};
	XMVECTOR vecIntersectPlane{};

	XMFLOAT3 f3ScreenPos{ 0.0f, 0.0f, 0.0f };

	// Top left
	f3ScreenPos.x = 0.0f;
	f3ScreenPos.y = 0.0f;

	f3ScreenPos.z = 0.0f;
	vecRayStart = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, matIdentity);
	f3ScreenPos.z = 1.0f;
	vecRayEnd = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, matIdentity);

	vecIntersectPlane = XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
	XMStoreFloat4(&gf4VisibleTopLeft, vecIntersectPlane);

	// Top right
	f3ScreenPos.x = fViewportWidth;
	f3ScreenPos.y = 0.0f;

	f3ScreenPos.z = 0.0f;
	vecRayStart = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, matIdentity);
	f3ScreenPos.z = 1.0f;
	vecRayEnd = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, matIdentity);

	vecIntersectPlane = XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
	XMStoreFloat4(&gf4VisibleTopRight, vecIntersectPlane);

	// Bottom left
	f3ScreenPos.x = 0.0f;
	f3ScreenPos.y = fViewportHeight;

	f3ScreenPos.z = 0.0f;
	vecRayStart = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, matIdentity);
	f3ScreenPos.z = 1.0f;
	vecRayEnd = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, matIdentity);

	vecIntersectPlane = XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
	XMStoreFloat4(&gf4VisibleBottomLeft, vecIntersectPlane);

	// Bottom right
	f3ScreenPos.x = fViewportWidth;
	f3ScreenPos.y = fViewportHeight;

	f3ScreenPos.z = 0.0f;
	vecRayStart = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, matIdentity);
	f3ScreenPos.z = 1.0f;
	vecRayEnd = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, gMatPerspective, gMatView, matIdentity);

	vecIntersectPlane = XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
	XMStoreFloat4(&gf4VisibleBottomRight, vecIntersectPlane);

	gf4LargeVisibleArea = XMFLOAT4 {gf4VisibleTopLeft.x, gf4VisibleTopLeft.y, gf4VisibleTopRight.x, gf4VisibleBottomRight.y};

	if (gpGraphics->mFramebufferExtent2D.width > gpGraphics->mFramebufferExtent2D.height) [[likely]]
	{
		gf4RenderVisibleArea = gf4LargeVisibleArea;
		gf4RenderVisibleArea.x -= gVisibleAreaExtraTop.Get() * (gf4RenderVisibleArea.y - gf4RenderVisibleArea.w);
		gf4RenderVisibleArea.y += gVisibleAreaExtraTop.Get() * (gf4RenderVisibleArea.y - gf4RenderVisibleArea.w);
		gf4RenderVisibleArea.z += gVisibleAreaExtraTop.Get() * (gf4RenderVisibleArea.y - gf4RenderVisibleArea.w);
		gf4RenderVisibleArea.w -= gVisibleAreaExtraBottom.Get() * (gf4RenderVisibleArea.y - gf4RenderVisibleArea.w);
	}
	else [[unlikely]] 
	{
		gf4RenderVisibleArea = gf4LargeVisibleArea;
	}

	// Adjust visible area in world space to align with terrain and water polygon grid
	auto [iTerrainQuadX, iTerrainQuadY] = gpTextureManager->DetailTextureSize(gWorldDetail.Get());
	float fQuadsX = static_cast<float>(iTerrainQuadX);
	float fQuadsY = static_cast<float>(iTerrainQuadY);

	gf2VisibleAreaQuadSize.x = common::RoundDown((gf4RenderVisibleArea.z - gf4RenderVisibleArea.x) / fQuadsX + 0.01f, 0.01f);
	gf2VisibleAreaQuadSize.y = common::RoundDown((gf4RenderVisibleArea.y - gf4RenderVisibleArea.w) / fQuadsY + 0.01f, 0.01f);
	gf4RenderVisibleArea.x = common::RoundDown(gf4RenderVisibleArea.x, gf2VisibleAreaQuadSize.x);
	gf4RenderVisibleArea.y = common::RoundDown(gf4RenderVisibleArea.y + gf2VisibleAreaQuadSize.y, gf2VisibleAreaQuadSize.y);
	gf4RenderVisibleArea.z = gf4RenderVisibleArea.x + fQuadsX * gf2VisibleAreaQuadSize.x;
	gf4RenderVisibleArea.w = gf4RenderVisibleArea.y - fQuadsY * gf2VisibleAreaQuadSize.y;
}

void XM_CALLCONV RenderObjects(shaders::ObjectLayout* pLayouts, int64_t iCommandBuffer, int64_t iCount, const XMVECTOR* pVecPositions, const XMVECTOR* pVecDirections, FXMMATRIX matScale, CXMMATRIX matRotation, [[maybe_unused]] CpuCounters eCounter, Pipelines ePipeline, Pipelines ePipelineShadow)
{
	PROFILE_SET_COUNT(eCounter, iCount);

	int64_t iRendered = 0;
	for (int64_t i = 0; i < iCount; ++i)
	{
		auto& rVecPosition = pVecPositions[i];

		XMFLOAT4A f4Position{};
		XMStoreFloat4A(&f4Position, rVecPosition);
		if (f4Position.x < gf4RenderVisibleArea.x || f4Position.x > gf4RenderVisibleArea.z || f4Position.y > gf4RenderVisibleArea.y || f4Position.y < gf4RenderVisibleArea.w)
		{
			continue;
		}

		auto vecDirection = XMVector3Normalize(pVecDirections[i]);
		auto matRotationFinal = matRotation * common::RotationMatrixFromDirection(XMVectorNegate(vecDirection), XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));
		auto matTranslation = XMMatrixTranslationFromVector(rVecPosition);
		auto matTransform = matScale * matRotationFinal * matTranslation;

		shaders::ObjectLayout& rObjectLayout = pLayouts[iRendered];
		rObjectLayout.ui4Misc = { 0xFFFFFFFF, 0, 0, 0 };
		rObjectLayout.f4Position = f4Position;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rObjectLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rObjectLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));

		++iRendered;
	}
	PROFILE_SET_COUNT(eCounter + 1, iRendered);

	gpPipelineManager->mpPipelines[ePipeline].WriteIndirectBuffer(iCommandBuffer, iRendered);
	if (ePipelineShadow != kPipelineCount)
	{
		gpPipelineManager->mpPipelines[ePipelineShadow].WriteIndirectBuffer(iCommandBuffer, iRendered);
	}
}

}
