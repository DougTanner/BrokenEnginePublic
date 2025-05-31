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

	XMVECTOR XM_CALLCONV Center()
	{
		auto vecCenter = vecTopLeft;
		vecCenter = XMVectorAdd(vecTopRight, vecCenter);
		vecCenter = XMVectorAdd(vecBottomLeft, vecCenter);
		vecCenter = XMVectorAdd(vecBottomRight, vecCenter);
		vecCenter = XMVectorMultiply(XMVectorReplicate(0.25f), vecCenter);
		return vecCenter;
	}
};

XMVECTOR XM_CALLCONV ColorToVector(uint32_t uiColor);
XMVECTOR XM_CALLCONV Project(FXMVECTOR vecA, FXMVECTOR vecB);
XMVECTOR XM_CALLCONV ToBaseHeight(FXMVECTOR vecPosition, FXMVECTOR vecEyePosition, float fBaseHeight);
float RotationFromPosition(FXMVECTOR vecPosition);
XMVECTOR XM_CALLCONV QuaternionFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
XMVECTOR XM_CALLCONV Closest(FXMVECTOR vecOrigin, FXMVECTOR vecA, FXMVECTOR vecB);
AreaVertices XM_CALLCONV CalculateArea(FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fForward, float fBack, float fWidth);
bool XM_CALLCONV InsideAreaVertices(FXMVECTOR vecPosition, const AreaVertices& rAreaVertices);
XMVECTOR XM_CALLCONV RotateTowards(FXMVECTOR vecDirection, FXMVECTOR vecTowards, float fAmount);
XMVECTOR XM_CALLCONV RotateTowardsPercent(FXMVECTOR vecDirection, FXMVECTOR vecTowards, float fPercent);
XMVECTOR XM_CALLCONV CircleJitter(FXMVECTOR vecPosition, float fDistance, common::RandomEngine& randomEngine);

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

template<std::floating_point T>
constexpr inline T RoundDown(T fToRound, T fMultiple)
{
	float fInv = 1.0f / fMultiple;
	return std::floor(fToRound * fInv) / fInv;
}

template <typename T>
T SignOf(T val)
{
	return static_cast<T>((static_cast<T>(0) < val) - (val < static_cast<T>(0)));
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

} // namespace common
