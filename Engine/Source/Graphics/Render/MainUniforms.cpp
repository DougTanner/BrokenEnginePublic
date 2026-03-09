#ifdef BT_CLIENT

#include "Render.h"

#include "Profile/ProfileManager.h"

#include "Game.h"

namespace engine
{

void RenderFrameMain(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord)
{
	const game::FrameInterpolate& rCameraInterpolate = rRenderInterpolates.at(cameraCoord);

	// One-time setup (preserved from original RenderFrameMain)
	RenderLightingMain(iCommandBuffer, rCameraInterpolate);
	gpBufferManager->ResetSkinningAllocations(iCommandBuffer);

	// Phase 1: BeginRender — compute total capacities, resize GPU buffers, reset counters
	game::FrameInterpolate::BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords);

	// Phase 2: Render per-frame (camera first for index 0 stability)
	auto renderFrame = [&](const GridCoord& rCoord)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it == rRenderInterpolates.end()) return;
		const game::FrameInterpolate& rInterp = it->second;
		uint16_t uiFrameId = game::gpGame->RenderFrame(rCoord).postRender.uiFrameId;
		// Main collections via Render (no uiFrameId needed)
		game::FrameInterpolate::Render(rInterp, iCommandBuffer);
		// SmokeTrails/WindTrails called separately with uiFrameId
		SmokeTrailsInterpolate::Render(rInterp, iCommandBuffer, uiFrameId);
		WindTrailsInterpolate::Render(rInterp, iCommandBuffer, uiFrameId);
	};
	renderFrame(cameraCoord);
	for (const GridCoord& rCoord : rActiveCoords)
	{
		if (rCoord == cameraCoord) continue;
		renderFrame(rCoord);
	}

	// Phase 3: EndRender — write indirect draw buffer counts
	game::FrameInterpolate::EndRender(iCommandBuffer);

	// Post-render MainLayout setup (camera matrices, wave params, hex shields, camera shake)
	const game::FrameInterpolate& rFrameInterpolate = rCameraInterpolate;
	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	static int siRenderCount = 0;
	rMainLayout.iFrameNumber = static_cast<int>(game::gpCamera->miFrame);
	rMainLayout.iRenderNumber = ++siRenderCount;

	// Camera shake
	float fCameraShake = std::pow(game::gpCamera->mfShake, 1.0f);
	static constexpr float kfMaxRoll = 0.005f;
	static constexpr float kfMaxPitch = 0.005f;
	static constexpr float kfMaxYaw = 0.01f;
	siv::BasicPerlinNoise<float> perlinRoll {0};
	siv::BasicPerlinNoise<float> perlinPitch {1};
	siv::BasicPerlinNoise<float> perlinYaw {2};
	auto matCameraShake = XMMatrixRotationRollPitchYaw(kfMaxRoll * fCameraShake * (-1.0f + 2.0f * perlinRoll.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)), kfMaxPitch * fCameraShake * (-1.0f + 2.0f * perlinPitch.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)), kfMaxYaw * fCameraShake * (-1.0f + 2.0f * perlinYaw.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)));

	XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&rMainLayout.f4x4ViewProjection[0]), XMMatrixTranspose(XMMatrixMultiply(game::gpCamera->mMatView, XMMatrixMultiply(matCameraShake, game::gpCamera->mMatPerspective))));

	XMStoreFloat4(&rMainLayout.f4EyePosition, game::gpCamera->mVecEyePosition);
	XMStoreFloat4(&rMainLayout.f4ToEyeNormal, game::gpCamera->mVecToEyeNormal);

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
			rMainLayout.pf4LowWavesTwo[i].w = 0.0f;

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
			rMainLayout.pf4MediumWavesTwo[i].w = 0.0f;
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

} // namespace engine

#endif // BT_CLIENT
