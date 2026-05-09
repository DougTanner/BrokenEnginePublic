#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"
#include "Graphics/Debug/DebugRender.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/PbrWrappersBase.h"
#include "Ui/SunMoonWrappersBase.h"
#include "Ui/WaterWrappersBase.h"

namespace engine
{

static void DebugRenderFrameEdges(const std::vector<GridCoord>& rActiveCoords)
{
	if constexpr (!kbDebugRender) return;

	float fZ = gBaseHeight.Get();
	constexpr XMFLOAT4A kf4EdgeColor = {0.0f, 1.0f, 1.0f, 1.0f};

	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = game::gpGame->mCoordFrames.find(rCoord);
		if (it == game::gpGame->mCoordFrames.end()) continue;

		// vecArea packing: x=minX, y=maxY, z=maxX, w=minY
		XMVECTOR vecArea = it->second.staticData.vecArea;
		float fMinX = XMVectorGetX(vecArea);
		float fMaxY = XMVectorGetY(vecArea);
		float fMaxX = XMVectorGetZ(vecArea);
		float fMinY = XMVectorGetW(vecArea);

		XMFLOAT3A f3MinMin = {fMinX, fMinY, fZ};
		XMFLOAT3A f3MaxMin = {fMaxX, fMinY, fZ};
		XMFLOAT3A f3MaxMax = {fMaxX, fMaxY, fZ};
		XMFLOAT3A f3MinMax = {fMinX, fMaxY, fZ};

		DebugRender::Line(f3MinMin, f3MaxMin, kf4EdgeColor);
		DebugRender::Line(f3MaxMin, f3MaxMax, kf4EdgeColor);
		DebugRender::Line(f3MaxMax, f3MinMax, kf4EdgeColor);
		DebugRender::Line(f3MinMax, f3MinMin, kf4EdgeColor);
	}
}

static void DebugRenderNavData(const std::vector<GridCoord>& rActiveCoords)
{
	if constexpr (!kbDebugRender) return;

	float fZ = gBaseHeight.Get();
	constexpr XMFLOAT4A kf4PolygonColor = {1.0f, 1.0f, 0.0f, 1.0f};
	constexpr XMFLOAT4A kf4VertexColor = {1.0f, 0.5f, 0.0f, 1.0f};

	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = game::gpGame->mCoordFrames.find(rCoord);
		if (it == game::gpGame->mCoordFrames.end()) continue;

		const NavData& rNav = it->second.staticData.navData;

		// Polygon edges
		for (int64_t iPoly = 0; iPoly < static_cast<int64_t>(rNav.polygonOffsets.size()); ++iPoly)
		{
			int64_t iStart = rNav.polygonOffsets[iPoly];
			int64_t iEnd = (iPoly + 1 < static_cast<int64_t>(rNav.polygonOffsets.size())) ? rNav.polygonOffsets[iPoly + 1] : static_cast<int64_t>(rNav.vertices.size());

			for (int64_t iVert = iStart; iVert < iEnd; ++iVert)
			{
				int64_t iNext = (iVert + 1 < iEnd) ? iVert + 1 : iStart;
				XMFLOAT3A f3A = {rNav.vertices[iVert].x, rNav.vertices[iVert].y, fZ};
				XMFLOAT3A f3B = {rNav.vertices[iNext].x, rNav.vertices[iNext].y, fZ};
				DebugRender::Line(f3A, f3B, kf4PolygonColor);
			}
		}

		// Vertex markers
		for (const XMFLOAT2& rVert : rNav.vertices)
		{
			DebugRender::Circle({rVert.x, rVert.y, fZ}, 0.75f, kf4VertexColor);
		}
	}
}

void RenderFrameMain(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord)
{
	if (rActiveCoords.empty())
	{
		return;
	}

	const game::FrameInterpolate& rCameraInterpolate = rRenderInterpolates.at(cameraCoord);

	RenderLightingMain(iCommandBuffer);
	gpBufferManager->ResetSkinningAllocations(iCommandBuffer);

	// Per-frame visible-area LOD draw params for terrain and water. The pipelines bind a
	// single concat mesh buffer holding all LODs; per-frame we tell vkCmdDrawIndexedIndirect
	// which LOD's index range and vertex base to draw. CameraBase computes miVisibleAreaLod
	// from eye distance with 4× hysteresis bands; mesh density and snap-grid are in lockstep.
	int iLod = std::clamp(game::gpCamera->miVisibleAreaLod, 0, BufferManager::kiVisibleAreaLodCount - 1);
	const auto& rTerrainLod = gpBufferManager->mTerrainMeshLods[iLod];
	const auto& rWaterLod   = gpBufferManager->mWaterMeshLods[iLod];
	gpPipelineManager->mpPipelines[kPipelineTerrain].WriteIndirectBuffer(iCommandBuffer, 1, rTerrainLod.iIndexCount, rTerrainLod.iIndexOffset, rTerrainLod.iVertexOffset);
	gpPipelineManager->mpPipelines[kPipelineWater].WriteIndirectBuffer(iCommandBuffer, 1, rWaterLod.iIndexCount, rWaterLod.iIndexOffset, rWaterLod.iVertexOffset);

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

	// Phase 4: Game-specific debug rendering (per-coord, positions from fully-interpolated frame)
	if constexpr (kbDebugRender)
	{
		for (const GridCoord& rCoord : rActiveCoords)
		{
			auto it = rRenderInterpolates.find(rCoord);
			if (it != rRenderInterpolates.end())
			{
				game::FrameInterpolate::DebugRender(it->second, rCoord);
			}
		}
	}

	DebugRenderNavData(rActiveCoords);
	DebugRenderFrameEdges(rActiveCoords);

	DebugRender::BeginRender(iCommandBuffer);
	DebugRender::EndRender(iCommandBuffer);

	// Post-render MainLayout setup (camera matrices, wave params, hex shields, camera shake)
	const game::FrameInterpolate& rFrameInterpolate = rCameraInterpolate;
	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	static int32_t siRenderCount = 0;
	rMainLayout.iFrameNumber = static_cast<int>(game::gpCamera->miFrame);
	rMainLayout.iRenderNumber = ++siRenderCount;

	// Camera shake
	float fCameraShake = game::gpCamera->mfShake;
	static constexpr float kfMaxRoll = 0.005f;
	static constexpr float kfMaxPitch = 0.005f;
	static constexpr float kfMaxYaw = 0.01f;
	siv::BasicPerlinNoise<float> perlinRoll(0);
	siv::BasicPerlinNoise<float> perlinPitch(1);
	siv::BasicPerlinNoise<float> perlinYaw(2);
	auto matCameraShake = XMMatrixRotationRollPitchYaw(kfMaxRoll * fCameraShake * (-1.0f + 2.0f * perlinRoll.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)), kfMaxPitch * fCameraShake * (-1.0f + 2.0f * perlinPitch.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)), kfMaxYaw * fCameraShake * (-1.0f + 2.0f * perlinYaw.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)));

	XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&rMainLayout.f4x4ViewProjection[0]), XMMatrixTranspose(XMMatrixMultiply(game::gpCamera->mMatView, XMMatrixMultiply(matCameraShake, game::gpCamera->mMatPerspective))));

	XMStoreFloat4(&rMainLayout.f4EyePosition, game::gpCamera->mVecEyePosition);
	XMStoreFloat4(&rMainLayout.f4ToEyeNormal, game::gpCamera->mVecToEyeNormal);

	// Wave phase reduction: read elapsed time from already-populated global layout
	const shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<const shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	double dWaveTime = static_cast<double>(rGlobalLayout.fElapsedTime);
	XMFLOAT4A f4WaveCameraPos {};
	XMStoreFloat4A(&f4WaveCameraPos, game::gpCamera->mVecPosition);
	double dWaveCameraX = static_cast<double>(f4WaveCameraPos.x);
	double dWaveCameraY = static_cast<double>(f4WaveCameraPos.y);
	constexpr double kdTwoPi = 2.0 * 3.14159265358979323846;

	// Fade geometric wave amplitude as the camera zooms out: 1.0 at default eye height, 0.0 at 2x default
	static constexpr float kfWaveFadeEnd = 2.0f * game::Camera::kfCameraEyeHeightDefault;
	float fWaveAmplitudeScale = std::clamp((kfWaveFadeEnd - game::gpCamera->mfCameraEyeHeight) / (kfWaveFadeEnd - game::Camera::kfCameraEyeHeightDefault), 0.0f, 1.0f);

	// Water low frequency
	{
		int64_t iCount = gWaterLowCount.Get<int64_t>();

		auto vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(gWaterLowAngle.Get()));
		rMainLayout.pf4LowWavesOne[0].x = XMVectorGetX(vecDirection);
		rMainLayout.pf4LowWavesOne[0].y = XMVectorGetY(vecDirection);

		vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(0.0f));
		rMainLayout.pf4LowWavesOne[0].z = XMVectorGetX(vecDirection);
		rMainLayout.pf4LowWavesOne[0].w = XMVectorGetY(vecDirection);

		rMainLayout.pf4LowWavesTwo[0].x = (2.0f * XM_PI) / (gWaterLowWavelength.Get()); // Omega
		rMainLayout.pf4LowWavesTwo[0].y = gWaterLowAmplitude.Get() * fWaveAmplitudeScale;
		rMainLayout.pf4LowWavesTwo[0].z = gWaterLowSpeed.Get() * rMainLayout.pf4LowWavesTwo[0].x; // Phi
		{
			double dDirX = static_cast<double>(rMainLayout.pf4LowWavesOne[0].x);
			double dDirY = static_cast<double>(rMainLayout.pf4LowWavesOne[0].y);
			double dOmega = static_cast<double>(rMainLayout.pf4LowWavesTwo[0].x);
			double dPhi = static_cast<double>(rMainLayout.pf4LowWavesTwo[0].z);
			rMainLayout.pf4LowWavesTwo[0].w = static_cast<float>(std::fmod((dDirX * dWaveCameraX + dDirY * dWaveCameraY) * dOmega + dPhi * dWaveTime, kdTwoPi));
		}

		common::RandomEngine randomEngine {};
		for (int64_t i = 1; i < iCount; ++i)
		{
			float fAngleAdjust = ((i % 2) == 0 ? 1.0f : -1.0f) * gWaterLowAngleAdjust.Get() * common::Random(randomEngine);
			vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(gWaterLowAngle.Get() + fAngleAdjust));
			rMainLayout.pf4LowWavesOne[i].x = XMVectorGetX(vecDirection);
			rMainLayout.pf4LowWavesOne[i].y = XMVectorGetY(vecDirection);

			vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(XM_2PI * static_cast<float>(i) / static_cast<float>(iCount)));
			rMainLayout.pf4LowWavesOne[i].z = XMVectorGetX(vecDirection);
			rMainLayout.pf4LowWavesOne[i].w = XMVectorGetY(vecDirection);

			float fAdjust = common::Random(randomEngine);
			float fWavelengthAdjust = fAdjust * gWaterLowWavelengthAdjust.Get();
			float fAmplitudeAdjust = (1.0f - fAdjust) * std::abs(gWaterLowAmplitudeAdjust.Get()) * common::Random(randomEngine);
			float fSpeedAdjust = fAdjust * gWaterLowSpeedAdjust.Get();
			rMainLayout.pf4LowWavesTwo[i].x = std::abs((2.0f * XM_PI) / (gWaterLowWavelength.Get() + fWavelengthAdjust * gWaterLowWavelength.Get())); // Omega
			rMainLayout.pf4LowWavesTwo[i].y = std::abs(gWaterLowAmplitude.Get() - fAmplitudeAdjust * gWaterLowAmplitude.Get());
			rMainLayout.pf4LowWavesTwo[i].y = std::min(rMainLayout.pf4LowWavesTwo[i].y, 0.1f * (1.0f / rMainLayout.pf4LowWavesTwo[i].x));
			rMainLayout.pf4LowWavesTwo[i].y *= fWaveAmplitudeScale;
			rMainLayout.pf4LowWavesTwo[i].z = (gWaterLowSpeed.Get() + gWaterLowSpeed.Get() * fSpeedAdjust * common::Random(randomEngine)) * rMainLayout.pf4LowWavesTwo[i].x; // Phi
			{
				double dDirX = static_cast<double>(rMainLayout.pf4LowWavesOne[i].x);
				double dDirY = static_cast<double>(rMainLayout.pf4LowWavesOne[i].y);
				double dOmega = static_cast<double>(rMainLayout.pf4LowWavesTwo[i].x);
				double dPhi = static_cast<double>(rMainLayout.pf4LowWavesTwo[i].z);
				rMainLayout.pf4LowWavesTwo[i].w = static_cast<float>(std::fmod((dDirX * dWaveCameraX + dDirY * dWaveCameraY) * dOmega + dPhi * (dWaveTime + static_cast<double>(i)), kdTwoPi));
			}

			if (i < 64 && (i % 3) == 0)
			{
				rMainLayout.pf4LowWavesTwo[i].y = 0.0f;
			}
		}
	}

	// Water medium frequency
	{
		int64_t iCount = gWaterMediumCount.Get<int64_t>();

		common::RandomEngine randomEngine {};
		for (int64_t i = 0; i < iCount; ++i)
		{
			float fAngleAdjust = gWaterMediumAngleAdjust.Get() * common::Random(randomEngine);
			auto vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngleAdjust));
			rMainLayout.pf4MediumWavesOne[i].x = XMVectorGetX(vecDirection);
			rMainLayout.pf4MediumWavesOne[i].y = XMVectorGetY(vecDirection);

			float fWavelengthAdjust = -gWaterMediumWavelengthAdjust.Get() + 2.0f * gWaterMediumWavelengthAdjust.Get() * common::Random(randomEngine);
			float fAmplitudeAdjust = -gWaterMediumAmplitudeAdjust.Get() + 2.0f * gWaterMediumAmplitudeAdjust.Get() * common::Random(randomEngine);
			float fSpeedAdjust = -gWaterMediumSpeedAdjust.Get() + 2.0f * gWaterMediumSpeedAdjust.Get() * common::Random(randomEngine);
			rMainLayout.pf4MediumWavesTwo[i].x = std::abs((2.0f * XM_PI) / (gWaterMediumWavelength.Get() + fWavelengthAdjust * gWaterMediumWavelength.Get())); // Omega
			rMainLayout.pf4MediumWavesTwo[i].y = std::abs(gWaterMediumAmplitude.Get() + fAmplitudeAdjust * gWaterMediumAmplitude.Get());
			rMainLayout.pf4MediumWavesTwo[i].y = std::min(rMainLayout.pf4MediumWavesTwo[i].y, 0.1f * (1.0f / rMainLayout.pf4MediumWavesTwo[i].x));
			rMainLayout.pf4MediumWavesTwo[i].y *= fWaveAmplitudeScale;
			rMainLayout.pf4MediumWavesTwo[i].z = (gWaterMediumSpeed.Get() + fSpeedAdjust * gWaterMediumSpeed.Get()) * rMainLayout.pf4MediumWavesTwo[i].x; // Phi
			double dDirX = static_cast<double>(rMainLayout.pf4MediumWavesOne[i].x);
			double dDirY = static_cast<double>(rMainLayout.pf4MediumWavesOne[i].y);
			double dOmega = static_cast<double>(rMainLayout.pf4MediumWavesTwo[i].x);
			double dPhi = static_cast<double>(rMainLayout.pf4MediumWavesTwo[i].z);
			rMainLayout.pf4MediumWavesTwo[i].w = static_cast<float>(std::fmod((dDirX * dWaveCameraX + dDirY * dWaveCameraY) * dOmega + dPhi * dWaveTime, kdTwoPi));
		}
	}

	// Hex shield
	rMainLayout.fHexShieldGrow = game::gHexShieldGrow.Get();
	rMainLayout.fHexShieldEdgeDistance = game::gHexShieldEdgeDistance.Get();
	rMainLayout.fHexShieldEdgePower = game::gHexShieldEdgePower.Get();
	rMainLayout.fHexShieldEdgeMultiplier = game::gHexShieldEdgeMultiplier.Get();

	rMainLayout.fHexShieldWaveMultiplier = game::gHexShieldWaveMultiplier.Get();
	rMainLayout.fHexShieldWaveDotMultiplier = game::gHexShieldWaveDotMultiplier.Get();
	rMainLayout.fHexShieldWaveIntensityMultiplier = game::gHexShieldWaveIntensityMultiplier.Get();
	rMainLayout.fHexShieldWaveIntensityPower = game::gHexShieldWaveIntensityPower.Get();
	rMainLayout.fHexShieldWaveFalloffPower = game::gHexShieldWaveFalloffPower.Get();

	rMainLayout.fHexShieldDirectionFalloffPower = game::gHexShieldDirectionFalloffPower.Get();
	rMainLayout.fHexShieldDirectionMultiplier = game::gHexShieldDirectionMultiplier.Get();
}

} // namespace engine

#endif // BT_CLIENT
