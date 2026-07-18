#pragma once

#include "Random.h"

namespace common
{

// Returns the minimum absolute value while preserving the sign of the first parameter
// Used for clamping velocity changes while maintaining direction
// Parameters: fA - Value whose sign is preserved, fB - Maximum absolute value
// Returns: Value with sign of fA and minimum absolute value of fA and fB
constexpr float MinAbs(float fA, float fB)
{
	return fA >= 0.0f ? std::min(fA, fB) : -std::min(-fA, fB);
}

// Compile-time ceiling function that rounds up to the nearest integer
// Used for constant calculations requiring compile-time evaluation
// Parameters: f - Float value to round up
// Returns: Smallest integer greater than or equal to f
#pragma warning(suppress: 26497) // consteval is stricter than constexpr
consteval int64_t Ceil(float f)
{
	return static_cast<float>(static_cast<int64_t>(f)) == f ? static_cast<int64_t>(f) : static_cast<int64_t>(f) + ((f > 0.0f) ? 1 : 0);
}

struct AreaVertices
{
	XMVECTOR vecTopLeft {};
	XMVECTOR vecTopRight {};
	XMVECTOR vecBottomLeft {};
	XMVECTOR vecBottomRight {};
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
// Operation order matches the compile-time RandomXYJitter<JITTER> form so identical magnitudes consume the RNG identically
inline XMVECTOR XM_CALLCONV RandomXYJitter(float fJitter, RandomEngine& rRandomEngine)
{
	return XMVectorSet(-fJitter + Random(2.0f * fJitter, rRandomEngine), -fJitter + Random(2.0f * fJitter, rRandomEngine), 0.0f, 0.0f);
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

// Computes on make_unsigned_t to avoid signed-overflow UB on large valid inputs; result is identical to the signed form for non-negative inputs
template<std::integral T>
constexpr inline T RoundUp(T iToRound, T iMultiple)
{
	using UnsignedT = std::make_unsigned_t<T>;
	return static_cast<T>(((static_cast<UnsignedT>(iToRound) + static_cast<UnsignedT>(iMultiple) - 1) / static_cast<UnsignedT>(iMultiple)) * static_cast<UnsignedT>(iMultiple));
}

template<std::integral T, T MULTIPLE>
constexpr inline T RoundUp(T iToRound)
{
	static_assert(MULTIPLE > 0, "Multiple must be positive");

	using UnsignedT = std::make_unsigned_t<T>;
	if constexpr (std::has_single_bit(static_cast<UnsignedT>(MULTIPLE)))
	{
		return static_cast<T>((static_cast<UnsignedT>(iToRound) + static_cast<UnsignedT>(MULTIPLE) - 1) & ~(static_cast<UnsignedT>(MULTIPLE) - 1));
	}
	else
	{
		return static_cast<T>(((static_cast<UnsignedT>(iToRound) + static_cast<UnsignedT>(MULTIPLE) - 1) / static_cast<UnsignedT>(MULTIPLE)) * static_cast<UnsignedT>(MULTIPLE));
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

template <std::unsigned_integral T>
inline T FloatToUnorm(float fValue)
{
	ASSERT(fValue >= 0.0f && fValue <= 1.0f);
	if constexpr (sizeof(T) <= 2)
	{
		// float represents max() exactly for 8/16-bit; preserves existing baked output bit-for-bit
		return static_cast<T>(static_cast<float>(std::numeric_limits<T>::max()) * fValue);
	}
	else
	{
		// 32/64-bit: static_cast<float>(max()) rounds up to 2^N and overflows the int cast at fValue == 1.0; scale in double
		return static_cast<T>(static_cast<double>(std::numeric_limits<T>::max()) * static_cast<double>(fValue));
	}
}

template <std::unsigned_integral T>
inline float UnormToFloat(T uiValue)
{
	if constexpr (sizeof(T) <= 2)
	{
		return static_cast<float>(uiValue) / static_cast<float>(std::numeric_limits<T>::max());
	}
	else
	{
		return static_cast<float>(static_cast<double>(uiValue) / static_cast<double>(std::numeric_limits<T>::max()));
	}
}

// Frame-rate independent exponential decay factor using Padé (1,1) approximation
// Approximates exp(-fDecayRate * fDeltaTime) for finite positive scaled times
// Always returns [0, 1]: non-positive or NaN scaled time returns 1, positive infinity returns 0
// Fast (no transcendentals), accurate (~1% for x < 0.35, ~9% at x = 1.0)
// Usage: velocity *= ExponentialDecay(3.0f, fDeltaTime);
constexpr float ExponentialDecay(float fDecayRate, float fDeltaTime)
{
	float fScaledTime = fDecayRate * fDeltaTime;
	if (!(fScaledTime > 0.0f))
	{
		return 1.0f;
	}
	if (fScaledTime == std::numeric_limits<float>::infinity())
	{
		return 0.0f;
	}
	return std::max(0.0f, (2.0f - fScaledTime) / (2.0f + fScaledTime));
}

// Frame-rate independent interpolation factor using Padé (1,1) approximation
// Returns 1 - exp(-fRate * fDeltaTime) ≈ 2x / (2 + x) where x = fRate * fDeltaTime
// Use for "move toward target" operations (rotation, position lerp)
// Returns [0, 1] only for scaled time in [0, 2]; callers outside that domain must clamp the result
// Usage: direction = lerp(direction, target, ExponentialInterpolant(10.0f, fDeltaTime));
constexpr float ExponentialInterpolant(float fRate, float fDeltaTime)
{
	float fScaledTime = fRate * fDeltaTime;
	return (2.0f * fScaledTime) / (2.0f + fScaledTime);
}

struct SinCos
{
	float fSin = 0.0f;
	float fCos = 1.0f;
};

// Deterministic sin/cos for simulation / CRC-fed paths. libm std::sin/std::cos rounding differs
// across toolchains, so it must never feed CRC'd or client/server-shared state; XMScalarSinCos is
// a header-inline minimax polynomial (basic IEEE ops only), bit-identical under /fp:strict.
inline SinCos DeterministicSinCos(float fRadians)
{
	SinCos result;
	XMScalarSinCos(&result.fSin, &result.fCos, fRadians);
	return result;
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

} // namespace common
