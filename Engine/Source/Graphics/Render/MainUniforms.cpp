#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"
#include "Graphics/Debug/DebugRender.h"
#include "Ui/WaterWrappersBase.h"
#include "Ui/WrapperBase.h"

namespace engine
{

static void DebugRenderFrameEdges(const std::vector<GridCoord>& rActiveCoords)
{
	if constexpr (!kbDebugRender)
	{
		return;
	}

	float fZ = gBaseHeight.Get();
	constexpr XMFLOAT4A kf4EdgeColor = {0.0f, 1.0f, 1.0f, 1.0f};

	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = game::gpGame->mCoordFrames.find(rCoord);
		if (it == game::gpGame->mCoordFrames.end())
		{
			continue;
		}

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

static void DebugRenderIslandBoundaries(const std::vector<GridCoord>& rActiveCoords)
{
	if constexpr (!kbDebugRender)
	{
		return;
	}

	float fZ = gBaseHeight.Get();
	constexpr XMFLOAT4A kf4BoundaryColor = {1.0f, 0.0f, 1.0f, 1.0f};

	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = game::gpGame->mCoordFrames.find(rCoord);
		if (it == game::gpGame->mCoordFrames.end())
		{
			continue;
		}

		for (const IslandPlacement& rPlacement : it->second.staticData.islands)
		{
			const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(rPlacement.islandCrc);
			float fHalfX = 0.5f * rTemplate.mfQuadFootprintX;
			float fHalfY = 0.5f * rTemplate.mfQuadFootprintY;
			float fCos = std::cos(rPlacement.fRotation);
			float fSin = std::sin(rPlacement.fRotation);

			auto rotate = [&](float fLocalX, float fLocalY)
			{
				return XMFLOAT3A {rPlacement.f2WorldPos.x + fLocalX * fCos - fLocalY * fSin, rPlacement.f2WorldPos.y + fLocalX * fSin + fLocalY * fCos, fZ};
			};

			XMFLOAT3A f3C0 = rotate(-fHalfX, -fHalfY);
			XMFLOAT3A f3C1 = rotate( fHalfX, -fHalfY);
			XMFLOAT3A f3C2 = rotate( fHalfX,  fHalfY);
			XMFLOAT3A f3C3 = rotate(-fHalfX,  fHalfY);

			DebugRender::Line(f3C0, f3C1, kf4BoundaryColor);
			DebugRender::Line(f3C1, f3C2, kf4BoundaryColor);
			DebugRender::Line(f3C2, f3C3, kf4BoundaryColor);
			DebugRender::Line(f3C3, f3C0, kf4BoundaryColor);
		}
	}
}

static void DebugRenderIslandValidArea(const std::vector<GridCoord>& rActiveCoords)
{
	if constexpr (!kbDebugRender)
	{
		return;
	}

	// Drawn at the underwater mask threshold depth (the depth that defines the hull boundary), below
	// the magenta boundary rectangle / cyan frame edges at gBaseHeight. Debug lines are an overlay
	// (no depth test), so the underwater Z is never occluded by terrain or water.
	float fZ = common::kfUnderwaterMaskThresholdMeters;
	constexpr XMFLOAT4A kf4ValidAreaColor = {0.0f, 1.0f, 0.0f, 1.0f};

	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = game::gpGame->mCoordFrames.find(rCoord);
		if (it == game::gpGame->mCoordFrames.end())
		{
			continue;
		}

		for (const IslandPlacement& rPlacement : it->second.staticData.islands)
		{
			const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(rPlacement.islandCrc);
			if (rTemplate.mpf2ValidAreaVertices == nullptr || rTemplate.miValidAreaVertexCount < 3)
			{
				continue;
			}

			float fCos = std::cos(rPlacement.fRotation);
			float fSin = std::sin(rPlacement.fRotation);

			auto rotate = [&](const XMFLOAT2& rVert)
			{
				return XMFLOAT3A {rPlacement.f2WorldPos.x + rVert.x * fCos - rVert.y * fSin, rPlacement.f2WorldPos.y + rVert.x * fSin + rVert.y * fCos, fZ};
			};

			int32_t iCount = rTemplate.miValidAreaVertexCount;
			for (int32_t i = 0; i < iCount; ++i)
			{
				const XMFLOAT2& rA = rTemplate.mpf2ValidAreaVertices[i];
				const XMFLOAT2& rB = rTemplate.mpf2ValidAreaVertices[(i + 1) % iCount];
				DebugRender::Line(rotate(rA), rotate(rB), kf4ValidAreaColor);
			}
		}
	}
}

static void DebugRenderNavData(const std::vector<GridCoord>& rActiveCoords)
{
	if constexpr (!kbDebugRender)
	{
		return;
	}

	float fZ = gBaseHeight.Get();
	constexpr XMFLOAT4A kf4PolygonColor = {1.0f, 1.0f, 0.0f, 1.0f};
	constexpr XMFLOAT4A kf4VertexColor = {1.0f, 0.5f, 0.0f, 1.0f};

	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = game::gpGame->mCoordFrames.find(rCoord);
		if (it == game::gpGame->mCoordFrames.end())
		{
			continue;
		}

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

// Gerstner wave phase-reduction modulus (shared by both bands).
constexpr double kdWaveTwoPi = 2.0 * 3.14159265358979323846;

// CPU-side staging + frame-invariant cache for one Gerstner wave band (`vec4` == XMFLOAT4, 16-byte
// stride — matches the mapped layout arrays exactly for a straight memcpy). All wave math reads and
// writes this cached (normal, cacheable) copy instead of the write-combined mapped uniform buffer,
// whose readbacks each stall on memory latency; the populate finishes with one memcpy per array
// region into rMainLayout. Directions (pf4WavesOne), omega (pf4WavesTwo.x), phi (pf4WavesTwo.z), and
// the clamped-but-unscaled base amplitude (pfBaseAmplitude) are frame-invariant — rebuilt only when
// the consumed tunables change. The per-frame pass rewrites only pf4WavesTwo.y (base amplitude x the
// eye-height fade scale) and pf4WavesTwo.w (the fmod phase term). Sized to the 256-entry shader maxima.
struct GerstnerWaveBandStaging
{
	XMFLOAT4 pf4WavesOne[256];
	XMFLOAT4 pf4WavesTwo[256];
	float pfBaseAmplitude[256];
};

// Consumed low/medium tunables snapshot; inequality vs last frame triggers an invariant rebuild.
// iCount defaults to -1 (never a resolved count) so the first real frame always rebuilds. The
// eye-height amplitude-fade scale is deliberately absent — it folds into the per-frame amplitude
// multiply, not a rebuild.
struct LowWaveTunables
{
	int64_t iCount = -1;
	float fAngle = 0.0f;
	float fWavelength = 0.0f;
	float fAmplitude = 0.0f;
	float fSpeed = 0.0f;
	float fAngleAdjust = 0.0f;
	float fWavelengthAdjust = 0.0f;
	float fAmplitudeAdjust = 0.0f;
	float fSpeedAdjust = 0.0f;

	bool operator==(const LowWaveTunables&) const = default;
};

struct MediumWaveTunables
{
	int64_t iCount = -1;
	float fWavelength = 0.0f;
	float fAmplitude = 0.0f;
	float fSpeed = 0.0f;
	float fAngleAdjust = 0.0f;
	float fWavelengthAdjust = 0.0f;
	float fAmplitudeAdjust = 0.0f;
	float fSpeedAdjust = 0.0f;

	bool operator==(const MediumWaveTunables&) const = default;
};

static GerstnerWaveBandStaging sLowWaveStaging {};
static GerstnerWaveBandStaging sMediumWaveStaging {};
static LowWaveTunables sLowWaveTunables {};
static MediumWaveTunables sMediumWaveTunables {};

// Rebuild the frame-invariant low-band terms (directions, omega, phi, clamped base amplitude) into
// the staging cache. Same per-wave float expression order and RandomEngine consumption sequence as
// the original inline code, so cached values are bit-identical to a per-frame recompute.
static void RebuildLowWaveInvariants(int64_t iCount)
{
	// Wave 0: fixed primary direction, no RNG draw, no amplitude clamp.
	auto vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(gWaterLowAngle.Get()));
	sLowWaveStaging.pf4WavesOne[0].x = XMVectorGetX(vecDirection);
	sLowWaveStaging.pf4WavesOne[0].y = XMVectorGetY(vecDirection);

	vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(0.0f));
	sLowWaveStaging.pf4WavesOne[0].z = XMVectorGetX(vecDirection);
	sLowWaveStaging.pf4WavesOne[0].w = XMVectorGetY(vecDirection);

	sLowWaveStaging.pf4WavesTwo[0].x = (2.0f * XM_PI) / (gWaterLowWavelength.Get()); // Omega
	sLowWaveStaging.pfBaseAmplitude[0] = gWaterLowAmplitude.Get();
	sLowWaveStaging.pf4WavesTwo[0].z = gWaterLowSpeed.Get() * sLowWaveStaging.pf4WavesTwo[0].x; // Phi

	common::RandomEngine randomEngine {};
	for (int64_t i = 1; i < iCount; ++i)
	{
		float fAngleAdjust = ((i % 2) == 0 ? 1.0f : -1.0f) * gWaterLowAngleAdjust.Get() * common::Random(randomEngine);
		vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(gWaterLowAngle.Get() + fAngleAdjust));
		sLowWaveStaging.pf4WavesOne[i].x = XMVectorGetX(vecDirection);
		sLowWaveStaging.pf4WavesOne[i].y = XMVectorGetY(vecDirection);

		vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(XM_2PI * static_cast<float>(i) / static_cast<float>(iCount)));
		sLowWaveStaging.pf4WavesOne[i].z = XMVectorGetX(vecDirection);
		sLowWaveStaging.pf4WavesOne[i].w = XMVectorGetY(vecDirection);

		float fAdjust = common::Random(randomEngine);
		float fWavelengthAdjust = fAdjust * gWaterLowWavelengthAdjust.Get();
		float fAmplitudeAdjust = (1.0f - fAdjust) * std::abs(gWaterLowAmplitudeAdjust.Get()) * common::Random(randomEngine);
		float fSpeedAdjust = fAdjust * gWaterLowSpeedAdjust.Get();
		sLowWaveStaging.pf4WavesTwo[i].x = std::abs((2.0f * XM_PI) / (gWaterLowWavelength.Get() + fWavelengthAdjust * gWaterLowWavelength.Get())); // Omega
		float fBaseAmplitude = std::abs(gWaterLowAmplitude.Get() - fAmplitudeAdjust * gWaterLowAmplitude.Get());
		fBaseAmplitude = std::min(fBaseAmplitude, 0.1f * (1.0f / sLowWaveStaging.pf4WavesTwo[i].x));
		sLowWaveStaging.pf4WavesTwo[i].z = (gWaterLowSpeed.Get() + gWaterLowSpeed.Get() * fSpeedAdjust * common::Random(randomEngine)) * sLowWaveStaging.pf4WavesTwo[i].x; // Phi

		// Thin the low-frequency band: zero every kiWaveCullModulo-th wave's amplitude below kiWaveCullLimit (tuning to reduce low-wave repetition).
		static constexpr int64_t kiWaveCullLimit = 64;
		static constexpr int64_t kiWaveCullModulo = 3;
		if (i < kiWaveCullLimit && (i % kiWaveCullModulo) == 0)
		{
			fBaseAmplitude = 0.0f;
		}
		sLowWaveStaging.pfBaseAmplitude[i] = fBaseAmplitude;
	}
}

// Rebuild the frame-invariant medium-band terms into the staging cache. Only pf4WavesOne.xy is
// written (the displacement shader reads only the .xy direction); .zw stay zero-initialised.
static void RebuildMediumWaveInvariants(int64_t iCount)
{
	common::RandomEngine randomEngine {};
	for (int64_t i = 0; i < iCount; ++i)
	{
		float fAngleAdjust = gWaterMediumAngleAdjust.Get() * common::Random(randomEngine);
		auto vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngleAdjust));
		sMediumWaveStaging.pf4WavesOne[i].x = XMVectorGetX(vecDirection);
		sMediumWaveStaging.pf4WavesOne[i].y = XMVectorGetY(vecDirection);

		float fWavelengthAdjust = -gWaterMediumWavelengthAdjust.Get() + 2.0f * gWaterMediumWavelengthAdjust.Get() * common::Random(randomEngine);
		float fAmplitudeAdjust = -gWaterMediumAmplitudeAdjust.Get() + 2.0f * gWaterMediumAmplitudeAdjust.Get() * common::Random(randomEngine);
		float fSpeedAdjust = -gWaterMediumSpeedAdjust.Get() + 2.0f * gWaterMediumSpeedAdjust.Get() * common::Random(randomEngine);
		sMediumWaveStaging.pf4WavesTwo[i].x = std::abs((2.0f * XM_PI) / (gWaterMediumWavelength.Get() + fWavelengthAdjust * gWaterMediumWavelength.Get())); // Omega
		float fBaseAmplitude = std::abs(gWaterMediumAmplitude.Get() + fAmplitudeAdjust * gWaterMediumAmplitude.Get());
		fBaseAmplitude = std::min(fBaseAmplitude, 0.1f * (1.0f / sMediumWaveStaging.pf4WavesTwo[i].x));
		sMediumWaveStaging.pfBaseAmplitude[i] = fBaseAmplitude;
		sMediumWaveStaging.pf4WavesTwo[i].z = (gWaterMediumSpeed.Get() + fSpeedAdjust * gWaterMediumSpeed.Get()) * sMediumWaveStaging.pf4WavesTwo[i].x; // Phi
	}
}

// Geometric "low" wave band: writes the pf4LowWaves* Gerstner terms, or zeroes the count when faded out.
static void PopulateGerstnerLowWaves(shaders::MainLayout& rMainLayout, shaders::GlobalLayout& rGlobalLayout, double dWaveTime, double dWaveCameraX, double dWaveCameraY, float fLowAmplitudeScale)
{
	if (fLowAmplitudeScale <= 0.0f)
	{
		rGlobalLayout.iWaterLowCount = 0;
		return;
	}

	int64_t iCount = std::min(gWaterLowCount.Get<int64_t>(), static_cast<int64_t>(gWaterLowMax.Get()));

	LowWaveTunables tunables {};
	tunables.iCount = iCount;
	tunables.fAngle = gWaterLowAngle.Get();
	tunables.fWavelength = gWaterLowWavelength.Get();
	tunables.fAmplitude = gWaterLowAmplitude.Get();
	tunables.fSpeed = gWaterLowSpeed.Get();
	tunables.fAngleAdjust = gWaterLowAngleAdjust.Get();
	tunables.fWavelengthAdjust = gWaterLowWavelengthAdjust.Get();
	tunables.fAmplitudeAdjust = gWaterLowAmplitudeAdjust.Get();
	tunables.fSpeedAdjust = gWaterLowSpeedAdjust.Get();
	if (tunables != sLowWaveTunables)
	{
		sLowWaveTunables = tunables;
		RebuildLowWaveInvariants(iCount);
	}

	// Per-frame: eye-height-scaled amplitude + camera/time phase. Wave 0 uses (dWaveTime + 0.0) == dWaveTime.
	for (int64_t i = 0; i < iCount; ++i)
	{
		sLowWaveStaging.pf4WavesTwo[i].y = sLowWaveStaging.pfBaseAmplitude[i] * fLowAmplitudeScale;
		double dDirX = static_cast<double>(sLowWaveStaging.pf4WavesOne[i].x);
		double dDirY = static_cast<double>(sLowWaveStaging.pf4WavesOne[i].y);
		double dOmega = static_cast<double>(sLowWaveStaging.pf4WavesTwo[i].x);
		double dPhi = static_cast<double>(sLowWaveStaging.pf4WavesTwo[i].z);
		sLowWaveStaging.pf4WavesTwo[i].w = static_cast<float>(std::fmod((dDirX * dWaveCameraX + dDirY * dWaveCameraY) * dOmega + dPhi * (dWaveTime + static_cast<double>(i)), kdWaveTwoPi));
	}

	std::memcpy(rMainLayout.pf4LowWavesOne, sLowWaveStaging.pf4WavesOne, static_cast<size_t>(iCount) * sizeof(rMainLayout.pf4LowWavesOne[0]));
	std::memcpy(rMainLayout.pf4LowWavesTwo, sLowWaveStaging.pf4WavesTwo, static_cast<size_t>(iCount) * sizeof(rMainLayout.pf4LowWavesTwo[0]));
}

// Geometric "medium" wave band: writes the pf4MediumWaves* Gerstner terms, or zeroes the count when faded out.
static void PopulateGerstnerMediumWaves(shaders::MainLayout& rMainLayout, shaders::GlobalLayout& rGlobalLayout, double dWaveTime, double dWaveCameraX, double dWaveCameraY, float fMediumAmplitudeScale)
{
	if (fMediumAmplitudeScale <= 0.0f)
	{
		rGlobalLayout.iWaterMediumCount = 0;
		return;
	}

	int64_t iCount = gWaterMediumCount.Get<int64_t>();

	MediumWaveTunables tunables {};
	tunables.iCount = iCount;
	tunables.fWavelength = gWaterMediumWavelength.Get();
	tunables.fAmplitude = gWaterMediumAmplitude.Get();
	tunables.fSpeed = gWaterMediumSpeed.Get();
	tunables.fAngleAdjust = gWaterMediumAngleAdjust.Get();
	tunables.fWavelengthAdjust = gWaterMediumWavelengthAdjust.Get();
	tunables.fAmplitudeAdjust = gWaterMediumAmplitudeAdjust.Get();
	tunables.fSpeedAdjust = gWaterMediumSpeedAdjust.Get();
	if (tunables != sMediumWaveTunables)
	{
		sMediumWaveTunables = tunables;
		RebuildMediumWaveInvariants(iCount);
	}

	// Per-frame: eye-height-scaled amplitude + camera/time phase.
	for (int64_t i = 0; i < iCount; ++i)
	{
		sMediumWaveStaging.pf4WavesTwo[i].y = sMediumWaveStaging.pfBaseAmplitude[i] * fMediumAmplitudeScale;
		double dDirX = static_cast<double>(sMediumWaveStaging.pf4WavesOne[i].x);
		double dDirY = static_cast<double>(sMediumWaveStaging.pf4WavesOne[i].y);
		double dOmega = static_cast<double>(sMediumWaveStaging.pf4WavesTwo[i].x);
		double dPhi = static_cast<double>(sMediumWaveStaging.pf4WavesTwo[i].z);
		sMediumWaveStaging.pf4WavesTwo[i].w = static_cast<float>(std::fmod((dDirX * dWaveCameraX + dDirY * dWaveCameraY) * dOmega + dPhi * dWaveTime, kdWaveTwoPi));
	}

	std::memcpy(rMainLayout.pf4MediumWavesOne, sMediumWaveStaging.pf4WavesOne, static_cast<size_t>(iCount) * sizeof(rMainLayout.pf4MediumWavesOne[0]));
	std::memcpy(rMainLayout.pf4MediumWavesTwo, sMediumWaveStaging.pf4WavesTwo, static_cast<size_t>(iCount) * sizeof(rMainLayout.pf4MediumWavesTwo[0]));
}

// Wave phase reduction: read elapsed time from already-populated global layout.
// Non-const: when a per-stack camera-eye-height fade clamps amplitude to zero, the band helpers zero
// the matching iWater*Count so the WaterDisplacement.comp Gerstner loop short-circuits to no work.
static void PopulateGerstnerWaves(shaders::MainLayout& rMainLayout, shaders::GlobalLayout& rGlobalLayout)
{
	double dWaveTime = static_cast<double>(rGlobalLayout.fElapsedTime);
	XMFLOAT4A f4WaveCameraPos {};
	XMStoreFloat4A(&f4WaveCameraPos, game::gpCamera->mVecPosition);
	double dWaveCameraX = static_cast<double>(f4WaveCameraPos.x);
	double dWaveCameraY = static_cast<double>(f4WaveCameraPos.y);

	// Fade geometric wave amplitudes by camera eye height — per-stack Start/End sliders (1.0 at ≤ Start, 0.0 at ≥ End, linear between).
	float fCameraEyeHeight = game::gpCamera->mfCameraEyeHeight;
	float fLowFadeStart = gWaterLowAmplitudeFadeStart.Get();
	float fLowFadeEnd = gWaterLowAmplitudeFadeEnd.Get();
	float fLowAmplitudeScale = std::clamp((fLowFadeEnd - fCameraEyeHeight) / std::max(fLowFadeEnd - fLowFadeStart, 1e-3f), 0.0f, 1.0f);
	float fMediumFadeStart = gWaterMediumAmplitudeFadeStart.Get();
	float fMediumFadeEnd = gWaterMediumAmplitudeFadeEnd.Get();
	float fMediumAmplitudeScale = std::clamp((fMediumFadeEnd - fCameraEyeHeight) / std::max(fMediumFadeEnd - fMediumFadeStart, 1e-3f), 0.0f, 1.0f);

	PopulateGerstnerLowWaves(rMainLayout, rGlobalLayout, dWaveTime, dWaveCameraX, dWaveCameraY, fLowAmplitudeScale);

	PopulateGerstnerMediumWaves(rMainLayout, rGlobalLayout, dWaveTime, dWaveCameraX, dWaveCameraY, fMediumAmplitudeScale);
}

// Hex shield
static void PopulateHexShield(shaders::MainLayout& rMainLayout)
{
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

void RenderFrameMain(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord)
{
	// Reset the light-deposit accumulator before anything can return: the three deposit EndRender writers add
	// into it and RenderLightingSpreadIndirect consumes it, both below. It lives here rather than in
	// RenderLightingMain because the all-rings-empty path below returns before RenderLightingMain ever runs,
	// which would leave a previous frame's count standing and hold the spread gate open on an empty frame.
	giLightingDepositInstances = 0;

	// Never-empty invariant: client mActiveCoords always contains mClientGridCoord (Game::ComputeActiveSet
	// gameplay and main-menu branches, Game::Reset re-seed) and the boot prerender passes {kOriginCoord}, so
	// this return is unreachable. It exists only to keep rRenderInterpolates.at(cameraCoord) below from throwing
	// if the invariant ever breaks. If it does, this skips every per-frame indirect-count write while the
	// record-once Main CB still submits unconditionally (Graphics::RenderMainPresentAcquire), re-submitting each
	// command buffer's last-written counts (framebuffer-count frames stale) for as long as the skip persists.
	// Only the per-entity/effect + debug instance counts ghost-draw when stale (a leftover nonzero count draws
	// phantom entities) — those are what an empty path added here must flush before returning. The water LOD
	// WriteIndirectBuffer / WaterDisplacement WriteIndirectComputeBuffer params are deliberately left at their
	// last-written per-framebuffer values: water is an always-draw fixed reference mesh (instanceCount = 1,
	// never reallocated), so its stale-but-self-consistent params keep the frozen-camera ocean rendering and
	// need no flush. The lighting spread chain's per-pass WriteIndirectBuffer (RenderLightingSpreadIndirect) is
	// left stale for the same reason: its slots hold instanceCount 0 or 1 against a deposit that is equally
	// stale, so the pair stays self-consistent and can only re-spread the last frame's light, never ghost-draw.
	// See the reachable cameraCoord-not-found flush immediately below (all-rings-empty frame, e.g. a failed
	// reconnect), which zeroes exactly the entity/effect + debug counts via FrameInterpolate::BeginRender/
	// EndRender + DebugRender::BeginRender/EndRender.
	if (rActiveCoords.empty())
	{
		return;
	}

	if (rRenderInterpolates.find(cameraCoord) == rRenderInterpolates.end())
	{
		// All-rings-empty frame (rActiveCoords non-empty but no active coord is renderable — e.g. a failed
		// reconnect to a dead local server). GameBase::Render leaves mRenderInterpolates renderable-only, so a
		// missing cameraCoord entry means the whole map is empty. Flush the record-once Main CB's indirect counts
		// to zero (the SAME Begin/EndRender the normal path calls, but with no per-coord Render between them, so
		// every collection's counter — reset at the top of BeginRender — is written as 0 by EndRender) so nothing
		// ghost-draws, then bail before touching cameraCoord's absent interpolate. Both BeginRender and EndRender
		// tolerate the empty map: every collection BeginRender iterates rActiveCoords with an rRenderInterpolates
		// find-guard (or is a no-op), never .at().
		game::FrameInterpolate::BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords);
		game::FrameInterpolate::EndRender(iCommandBuffer);
		RenderLightingSpreadIndirect(iCommandBuffer);
		if constexpr (kbDebugRender)
		{
			DebugRender::BeginRender(iCommandBuffer);
			DebugRender::EndRender(iCommandBuffer);
		}
		return;
	}

	const game::FrameInterpolate& rCameraInterpolate = rRenderInterpolates.at(cameraCoord);
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	RenderLightingMain(iCommandBuffer);
	gpBufferManager->ResetSkinningAllocations(iCommandBuffer);

	// Per-frame visible-area LOD draw params for water. The water pipeline binds a single concat
	// mesh buffer holding all LODs; per-frame we tell vkCmdDrawIndexedIndirect which LOD's index
	// range and vertex base to draw. CameraBase computes miVisibleAreaLod from eye distance with 4×
	// hysteresis bands; mesh density and snap-grid are in lockstep. Terrain draws via one
	// vkCmdDrawIndexedIndirect per island template in CommandBufferRecordMain.cpp.
	int64_t iLevelOfDetail = game::gpCamera->miVisibleAreaLod;
	const BufferManager::VisibleAreaMeshLod& rWaterLevelOfDetail = gpBufferManager->mWaterMeshLods[iLevelOfDetail];
	gpPipelineManager->mpPipelines[kPipelineWater].WriteIndirectBuffer(iCommandBuffer, 1, rWaterLevelOfDetail.iIndexCount, rWaterLevelOfDetail.iIndexOffset, rWaterLevelOfDetail.iVertexOffset);

	// Active LOD's vertex-grid dims (iQuadCount* == iMeshX/Y - 1). Read by:
	//   1) WaterDisplacement.comp — bounds-checks each thread, only writes the top-left rectangle.
	//   2) Water.vert — scales f2InTexcoord to the matching texel index via texelFetch.
	// Both shaders must read the SAME values; populating once here keeps them in lockstep.
	rGlobalLayout.iWaterActiveQuadX = static_cast<int32_t>(rWaterLevelOfDetail.iQuadCountX);
	rGlobalLayout.iWaterActiveQuadY = static_cast<int32_t>(rWaterLevelOfDetail.iQuadCountY);

	// Indirect dispatch dims for kPipelineWaterDisplacement: one workgroup per kiComputeTileSize block over the
	// (iQuadCount + 1) active-LOD texel rectangle the compute shader writes — the live sub-region only, not the
	// full LOD0 grid. Written per framebuffer to match RecordComputeIndirect's slot indexing.
	gpPipelineManager->mpPipelines[kPipelineWaterDisplacement].WriteIndirectComputeBuffer(iCommandBuffer, (rWaterLevelOfDetail.iQuadCountX + 1 + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (rWaterLevelOfDetail.iQuadCountY + 1 + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, 1);

	// Phase 1: BeginRender — compute total capacities, resize GPU buffers, reset counters
	game::FrameInterpolate::BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords);

	// Phase 2: Render per-frame (camera first for index 0 stability)
	auto renderFrame = [&](const GridCoord& rCoord)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it == rRenderInterpolates.end())
		{
			return;
		}
		const game::FrameInterpolate& rInterp = it->second;
		// Automatically dispatched main collections
		game::FrameInterpolate::Render(rInterp, iCommandBuffer);
		// Manually rendered trail collections
		SmokeTrailsInterpolate::Render(rInterp, iCommandBuffer);
		WindTrailsInterpolate::Render(rInterp, iCommandBuffer);
	};
	renderFrame(cameraCoord);
	for (const GridCoord& rCoord : rActiveCoords)
	{
		if (rCoord == cameraCoord)
		{
			continue;
		}
		renderFrame(rCoord);
	}

	// Phase 3: EndRender — write indirect draw buffer counts
	game::FrameInterpolate::EndRender(iCommandBuffer);

	// Spread-chain gate, paired with the deposit counts EndRender just published. Must follow EndRender — that
	// is where giLightingDepositInstances is summed.
	RenderLightingSpreadIndirect(iCommandBuffer);

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
	DebugRenderIslandBoundaries(rActiveCoords);
	DebugRenderIslandValidArea(rActiveCoords);

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
	// Fixed-seed permutation tables are identical every frame — construct once (each ctor reshuffles a 256-entry table).
	static const siv::BasicPerlinNoise<float> sPerlinRoll(0);
	static const siv::BasicPerlinNoise<float> sPerlinPitch(1);
	static const siv::BasicPerlinNoise<float> sPerlinYaw(2);
	auto matCameraShake = XMMatrixRotationRollPitchYaw(kfMaxRoll * fCameraShake * (-1.0f + 2.0f * sPerlinRoll.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)), kfMaxPitch * fCameraShake * (-1.0f + 2.0f * sPerlinPitch.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)), kfMaxYaw * fCameraShake * (-1.0f + 2.0f * sPerlinYaw.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)));

	XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&rMainLayout.f4x4ViewProjection[0]), XMMatrixTranspose(XMMatrixMultiply(game::gpCamera->mMatView, XMMatrixMultiply(matCameraShake, game::gpCamera->mMatPerspective))));

	XMStoreFloat4(&rMainLayout.f4EyePosition, game::gpCamera->mVecEyePosition);
	XMVECTOR vecToEyeNormal = game::gpCamera->mVecToEyeNormal;
	XMStoreFloat4(&rMainLayout.f4ToEyeNormal, vecToEyeNormal);
	// Camera-facing billboard basis (DebugRenderBillboard.vert): fold the shader's per-vertex worldUp select +
	// cross chain CPU-side since f4ToEyeNormal is invocation-invariant. Mirror the shader exactly — 0.999 z
	// threshold, +X fallback, right normalized, up = cross(forward, right) left unnormalized. Forward stays f4ToEyeNormal.
	XMVECTOR vecBillboardWorldUp = std::abs(XMVectorGetZ(vecToEyeNormal)) < 0.999f ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	XMVECTOR vecBillboardRight = XMVector3Normalize(XMVector3Cross(vecBillboardWorldUp, vecToEyeNormal));
	XMStoreFloat4(&rMainLayout.f4BillboardRight, vecBillboardRight);
	XMStoreFloat4(&rMainLayout.f4BillboardUp, XMVector3Cross(vecToEyeNormal, vecBillboardRight));

	PopulateGerstnerWaves(rMainLayout, rGlobalLayout);

	PopulateHexShield(rMainLayout);
}

} // namespace engine

#endif // defined(BT_CLIENT)
