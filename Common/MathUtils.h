#pragma once

namespace common
{

struct RandomEngine;

struct AreaVertices
{
	XMVECTOR vecTopLeft {};
	XMVECTOR vecTopRight {};
	XMVECTOR vecBottomLeft {};
	XMVECTOR vecBottomRight {};

	bool operator==(const AreaVertices& rOther) const = default;
};

XMVECTOR XM_CALLCONV ColorToVector(uint32_t uiColor);
XMVECTOR XM_CALLCONV ToBaseHeight(FXMVECTOR vecPosition, FXMVECTOR vecEyePosition, float fBaseHeight);
float RotationFromPosition(FXMVECTOR vecPosition);
XMVECTOR XM_CALLCONV QuaternionFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
AreaVertices XM_CALLCONV CalculateArea(FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fForward, float fBack, float fWidth);
bool XM_CALLCONV InsideAreaVertices(FXMVECTOR vecPosition, const AreaVertices& rAreaVertices);
XMVECTOR XM_CALLCONV RotateTowardsPercent(FXMVECTOR vecDirection, FXMVECTOR vecTowards, float fPercent);
XMVECTOR XM_CALLCONV RandomAngleJitter(FXMVECTOR vecDirection, float fMaxJitter, RandomEngine& rRandomEngine);

inline XMMATRIX XM_CALLCONV RotationMatrixFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f))
{
	return XMMatrixRotationQuaternion(QuaternionFromDirection(vecDirection, vecOriginNormal, vecUp));
}

inline float XM_CALLCONV Distance(FXMVECTOR vecOne, FXMVECTOR vecTwo)
{
	return XMVectorGetX(XMVector3Length(XMVectorSubtract(vecTwo, vecOne)));
}

inline XMVECTOR XM_CALLCONV DirectionTo(FXMVECTOR vecFrom, FXMVECTOR vecTo)
{
	if (XMVectorGetX(XMVectorNearEqual(vecFrom, vecTo, g_XMEpsilon)) != 0.0f) [[unlikely]]
	{
		return XMVectorZero();
	}

	return XMVector3Normalize(XMVectorSubtract(vecTo, vecFrom));
}

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

inline float FromGamma(float fGamma)
{
	return std::pow(std::max(0.0f, fGamma), 1.0f / 2.2f);
}

// Frame-rate independent exponential decay factor using Padé (1,1) approximation
// Approximates exp(-fDecayRate * fDeltaTime) for consistent behavior at any timestep
// Fast (no transcendentals), stable (never negative), accurate (~1% for x < 1.0)
// Usage: velocity *= ExponentialDecay(3.0f, fDeltaTime);
inline float ExponentialDecay(float fDecayRate, float fDeltaTime)
{
	float x = fDecayRate * fDeltaTime;
	return (2.0f - x) / (2.0f + x);
}

// Frame-rate independent interpolation factor using Padé (1,1) approximation
// Returns 1 - exp(-fRate * fDeltaTime) ≈ 2x / (2 + x) where x = fRate * fDeltaTime
// Use for "move toward target" operations (rotation, position lerp)
// Never exceeds 1.0 (no overshoot), frame-rate independent
// Usage: direction = lerp(direction, target, ExponentialInterpolant(10.0f, fDeltaTime));
inline float ExponentialInterpolant(float fRate, float fDeltaTime)
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

inline bool XM_CALLCONV AabbIntersectsArea(XMFLOAT4 f4Area, FXMVECTOR vecMin, FXMVECTOR vecMax)
{
	float fMinX = XMVectorGetX(vecMin);
	float fMaxX = XMVectorGetX(vecMax);
	float fMinY = XMVectorGetY(vecMin);
	float fMaxY = XMVectorGetY(vecMax);

	return !(fMaxX < f4Area.x || fMinX > f4Area.z || fMaxY < f4Area.w || fMinY > f4Area.y);
}

inline bool XM_CALLCONV PointOutsideArea(FXMVECTOR vecPosition, const XMFLOAT4& rf4Area)
{
	float fX = XMVectorGetX(vecPosition);
	float fY = XMVectorGetY(vecPosition);
	return fX <= rf4Area.x || fX >= rf4Area.z || fY >= rf4Area.y || fY <= rf4Area.w;
}

} // namespace common
