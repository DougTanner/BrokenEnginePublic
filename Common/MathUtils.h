#pragma once

#include "Random.h"

namespace common
{

struct AreaVertices
{
	XMVECTOR vecTopLeft {};
	XMVECTOR vecTopRight {};
	XMVECTOR vecBottomLeft {};
	XMVECTOR vecBottomRight {};

	bool operator==(const AreaVertices& rOther) const = default;
};

// Validate an XMVECTOR at a spawn / transfer invariant boundary: all 4 lanes finite, and
// W == 1.0 for positions (IS_POSITION=true) or W == 0.0 for directions and direction-like
// vectors, e.g. velocities (IS_POSITION=false). `XMVectorMultiplyAdd` propagates all 4 lanes,
// so velocity.W != 0 would drift into position.W; `XMVector3Normalize` divides by 3D length
// only, so a W-lane NaN survives and silently poisons downstream 4D consumers.
template<bool IS_POSITION>
inline void XM_CALLCONV ValidateVector(FXMVECTOR vec)
{
	ASSERT(std::isfinite(XMVectorGetX(vec))
		&& std::isfinite(XMVectorGetY(vec))
		&& std::isfinite(XMVectorGetZ(vec))
		&& std::isfinite(XMVectorGetW(vec)));
	if constexpr (IS_POSITION) { ASSERT(XMVectorGetW(vec) == 1.0f); }
	else                       { ASSERT(XMVectorGetW(vec) == 0.0f); }
}

// Converts packed RGBA uint32_t to XMVECTOR with normalized [0.0, 1.0] components
// Parameters: uiColor - Packed RGBA color (0xRRGGBBAA)
// Returns: XMVECTOR with RGBA components in [0.0, 1.0] range
XMVECTOR XM_CALLCONV ColorToVector(uint32_t uiColor);

// Converts XMVECTOR color to packed RGBA uint32_t (inverse of ColorToVector)
// Components are clamped to [0.0, 1.0] range before packing
// Parameters: vecColor - XMVECTOR color with normalized components
// Returns: Packed RGBA color (0xRRGGBBAA)
uint32_t XM_CALLCONV ColorToUint(FXMVECTOR vecColor);

// Linear interpolation between two packed RGBA colors by a given percentage
// Parameters: uiA - Start color, uiB - End color, fPercent - Interpolation factor [0.0, 1.0]
// Returns: Interpolated color
uint32_t ColorLerp(uint32_t uiA, uint32_t uiB, float fPercent);

XMVECTOR XM_CALLCONV ToBaseHeight(FXMVECTOR vecPosition, FXMVECTOR vecEyePosition, float fBaseHeight);
float RotationFromPosition(FXMVECTOR vecPosition);
XMVECTOR XM_CALLCONV QuaternionFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
AreaVertices XM_CALLCONV CalculateArea(FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fForward, float fBack, float fWidth);
XMVECTOR XM_CALLCONV RotateTowardsPercent(FXMVECTOR vecDirection, FXMVECTOR vecTowards, float fPercent);
XMVECTOR XM_CALLCONV RandomAngleJitter(FXMVECTOR vecDirection, float fMaxJitter, RandomEngine& rRandomEngine);

// Generate random XY offset in range [-JITTER, +JITTER] for each component
// Compile-time jitter value for constexpr cases
template<float JITTER>
inline XMVECTOR XM_CALLCONV RandomXYJitter(RandomEngine& rRandomEngine)
{
	return XMVectorSet(-JITTER + Random(2.0f * JITTER, rRandomEngine), -JITTER + Random(2.0f * JITTER, rRandomEngine), 0.0f, 0.0f);
}

// Runtime jitter value (for dynamic values like rType.fParticlePositionJitter)
inline XMVECTOR XM_CALLCONV RandomXYJitter(float fJitter, RandomEngine& rRandomEngine)
{
	return XMVectorSet(-fJitter + Random<1.0f>(rRandomEngine) * 2.0f * fJitter, -fJitter + Random<1.0f>(rRandomEngine) * 2.0f * fJitter, 0.0f, 0.0f);
}

// Add random XY jitter to a position (no normalization)
template<float JITTER>
inline XMVECTOR XM_CALLCONV RandomPositionJitter(FXMVECTOR vecPosition, RandomEngine& rRandomEngine)
{
	return XMVectorAdd(vecPosition, RandomXYJitter<JITTER>(rRandomEngine));
}

// Runtime version for dynamic jitter values
inline XMVECTOR XM_CALLCONV RandomPositionJitter(FXMVECTOR vecPosition, float fJitter, RandomEngine& rRandomEngine)
{
	return XMVectorAdd(vecPosition, RandomXYJitter(fJitter, rRandomEngine));
}

// Add random XY jitter to a direction and normalize
template<float JITTER>
inline XMVECTOR XM_CALLCONV RandomDirectionJitter(FXMVECTOR vecDirection, RandomEngine& rRandomEngine)
{
	return XMVector3Normalize(XMVectorAdd(vecDirection, RandomXYJitter<JITTER>(rRandomEngine)));
}

XMMATRIX XM_CALLCONV RotationMatrixFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));

float XM_CALLCONV Distance(FXMVECTOR vecOne, FXMVECTOR vecTwo);

XMVECTOR XM_CALLCONV DirectionTo(FXMVECTOR vecFrom, FXMVECTOR vecTo);

// Compute lead position to intercept a moving target with a constant-speed projectile.
// Solves the quadratic ||(T - S) + Vt*t|| = vp*t for the smallest positive t and returns T + Vt*t.
// Falls back to vecTargetPosition when no positive intercept exists (target outruns projectile,
// degenerate, etc.). Assumes the projectile does NOT inherit shooter velocity.
XMVECTOR XM_CALLCONV ComputeLeadPosition(FXMVECTOR vecShooterPosition, FXMVECTOR vecTargetPosition, FXMVECTOR vecTargetVelocity, float fProjectileSpeed);

template<std::integral T>
constexpr inline T RoundUp(T iToRound, T iMultiple)
{
	return ((iToRound + iMultiple - 1) / iMultiple) * iMultiple;
}

template<std::integral T, T MULTIPLE>
constexpr inline T RoundUp(T iToRound)
{
	static_assert(MULTIPLE > 0, "Multiple must be positive");

	if constexpr (std::has_single_bit(static_cast<std::make_unsigned_t<T>>(MULTIPLE)))
	{
		return (iToRound + MULTIPLE - 1) & ~(MULTIPLE - 1);
	}
	else
	{
		return ((iToRound + MULTIPLE - 1) / MULTIPLE) * MULTIPLE;
	}
}

template<std::integral T>
constexpr inline T RoundDown(T iToRound, T iMultiple)
{
	return (iToRound / iMultiple) * iMultiple;
}

template<std::floating_point T>
constexpr inline T RoundDown(T fToRound, T fMultiple)
{
	T fInv = static_cast<T>(1.0) / fMultiple;
	return std::floor(fToRound * fInv) / fInv;
}

template <typename T>
inline T FloatToUnorm(float fValue)
{
	ASSERT(fValue >= 0.0f && fValue <= 1.0f);
	return static_cast<T>(static_cast<float>(std::numeric_limits<T>::max()) * fValue);
}

template <typename T>
inline float UnormToFloat(T uiValue)
{
	return static_cast<float>(uiValue) / static_cast<float>(std::numeric_limits<T>::max());
}

float FromGamma(float fGamma);

// Frame-rate independent exponential decay factor using Padé (1,1) approximation
// Approximates exp(-fDecayRate * fDeltaTime) for consistent behavior at any timestep
// Fast (no transcendentals), stable (never negative), accurate (~1% for x < 1.0)
// Usage: velocity *= ExponentialDecay(3.0f, fDeltaTime);
constexpr float ExponentialDecay(float fDecayRate, float fDeltaTime)
{
	float x = fDecayRate * fDeltaTime;
	return (2.0f - x) / (2.0f + x);
}

// Frame-rate independent interpolation factor using Padé (1,1) approximation
// Returns 1 - exp(-fRate * fDeltaTime) ≈ 2x / (2 + x) where x = fRate * fDeltaTime
// Use for "move toward target" operations (rotation, position lerp)
// Never exceeds 1.0 (no overshoot), frame-rate independent
// Usage: direction = lerp(direction, target, ExponentialInterpolant(10.0f, fDeltaTime));
constexpr float ExponentialInterpolant(float fRate, float fDeltaTime)
{
	float x = fRate * fDeltaTime;
	return (2.0f * x) / (2.0f + x);
}

template<typename... Args>
inline std::pair<XMVECTOR, XMVECTOR> XM_CALLCONV ComputeAabb(FXMVECTOR vecFirst, Args... vecRest)
{
	XMVECTOR vecMin = vecFirst;
	XMVECTOR vecMax = vecFirst;

	((vecMin = XMVectorMin(vecMin, vecRest), vecMax = XMVectorMax(vecMax, vecRest)), ...);

	return {vecMin, vecMax};
}

bool XM_CALLCONV AabbIntersectsArea(XMFLOAT4 f4Area, FXMVECTOR vecMin, FXMVECTOR vecMax);

bool XM_CALLCONV InsideArea(FXMVECTOR vecPosition, const XMFLOAT4& rf4Area);
bool XM_CALLCONV InsideArea(FXMVECTOR vecPosition, FXMVECTOR vecArea);

// A 2D convex hull in world space: CCW vertices plus a precomputed AABB for broadphase rejection.
// pVertices points into caller-owned storage (e.g. a scratch vector) that must outlive the hull;
// BuildWorldHull fills it from a template's island-local polygon. Used by IslandChainPlacement to
// pack islands by their true valid-area hull (rectangles may overlap underwater, hulls may not).
struct ConvexHull2D
{
	const XMFLOAT2* pVertices = nullptr;
	int32_t iVertexCount = 0;
	XMFLOAT2 f2AabbMin {};
	XMFLOAT2 f2AabbMax {};
};

// Rotate a CCW island-local hull (centered at origin) by fRotation (CCW, matching GlobalElevation's
// convention) and translate to f2WorldPos, writing the world-space verts into rOutVertices (caller
// scratch, >= iLocalCount entries). Returns a ConvexHull2D over rOutVertices with its AABB filled.
inline ConvexHull2D BuildWorldHull(const XMFLOAT2* pLocalVertices, int32_t iLocalCount, XMFLOAT2 f2WorldPos, float fRotation, XMFLOAT2* rOutVertices)
{
	float fCos = std::cos(fRotation);
	float fSin = std::sin(fRotation);

	XMFLOAT2 f2Min {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
	XMFLOAT2 f2Max {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
	for (int32_t i = 0; i < iLocalCount; ++i)
	{
		float fLocalX = pLocalVertices[i].x;
		float fLocalY = pLocalVertices[i].y;
		float fWorldX = f2WorldPos.x + fLocalX * fCos - fLocalY * fSin;
		float fWorldY = f2WorldPos.y + fLocalX * fSin + fLocalY * fCos;
		rOutVertices[i] = {fWorldX, fWorldY};
		f2Min.x = std::min(f2Min.x, fWorldX);
		f2Min.y = std::min(f2Min.y, fWorldY);
		f2Max.x = std::max(f2Max.x, fWorldX);
		f2Max.y = std::max(f2Max.y, fWorldY);
	}

	return {rOutVertices, iLocalCount, f2Min, f2Max};
}

// Broadphase: do the two hulls' precomputed AABBs overlap? Strict < so edge-touching AABBs count as
// non-overlapping (lets islands pack flush).
inline bool AabbsOverlap2D(const ConvexHull2D& rA, const ConvexHull2D& rB)
{
	return rA.f2AabbMin.x < rB.f2AabbMax.x && rB.f2AabbMin.x < rA.f2AabbMax.x
	    && rA.f2AabbMin.y < rB.f2AabbMax.y && rB.f2AabbMin.y < rA.f2AabbMax.y;
}

// Separating Axis Theorem for two CCW convex polygons. True iff they share interior area; edge-
// touching returns false so placement can pack hulls flush without registering overlap. Scale-
// invariant (axes are un-normalized edge normals). Deterministic: fixed axis order (A's edges then
// B's, ascending index) and pure scalar float math.
inline bool ConvexHullsOverlap(const ConvexHull2D& rA, const ConvexHull2D& rB)
{
	if (!AabbsOverlap2D(rA, rB))
	{
		return false;
	}

	for (int32_t iPoly = 0; iPoly < 2; ++iPoly)
	{
		const ConvexHull2D& rEdgeHull = (iPoly == 0) ? rA : rB;
		for (int32_t i = 0; i < rEdgeHull.iVertexCount; ++i)
		{
			const XMFLOAT2& rV0 = rEdgeHull.pVertices[i];
			const XMFLOAT2& rV1 = rEdgeHull.pVertices[(i + 1) % rEdgeHull.iVertexCount];
			// Outward normal of the CCW edge (v1 - v0) is (edge.y, -edge.x).
			float fAxisX = rV1.y - rV0.y;
			float fAxisY = rV0.x - rV1.x;

			float fMinA = std::numeric_limits<float>::max();
			float fMaxA = std::numeric_limits<float>::lowest();
			for (int32_t j = 0; j < rA.iVertexCount; ++j)
			{
				float fProj = rA.pVertices[j].x * fAxisX + rA.pVertices[j].y * fAxisY;
				fMinA = std::min(fMinA, fProj);
				fMaxA = std::max(fMaxA, fProj);
			}

			float fMinB = std::numeric_limits<float>::max();
			float fMaxB = std::numeric_limits<float>::lowest();
			for (int32_t j = 0; j < rB.iVertexCount; ++j)
			{
				float fProj = rB.pVertices[j].x * fAxisX + rB.pVertices[j].y * fAxisY;
				fMinB = std::min(fMinB, fProj);
				fMaxB = std::max(fMaxB, fProj);
			}

			if (fMaxA <= fMinB || fMaxB <= fMinA)
			{
				return false;
			}
		}
	}

	return true;
}

} // namespace common
