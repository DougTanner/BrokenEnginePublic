#pragma once

namespace common
{

struct RandomEngine;

struct AreaVertices
{
	DirectX::XMVECTOR vecTopLeft {};
	DirectX::XMVECTOR vecTopRight {};
	DirectX::XMVECTOR vecBottomLeft {};
	DirectX::XMVECTOR vecBottomRight {};

	bool operator==(const AreaVertices& rOther) const = default;

	DirectX::XMVECTOR XM_CALLCONV Center()
	{
		auto vecCenter = vecTopLeft;
		vecCenter = DirectX::XMVectorAdd(vecTopRight, vecCenter);
		vecCenter = DirectX::XMVectorAdd(vecBottomLeft, vecCenter);
		vecCenter = DirectX::XMVectorAdd(vecBottomRight, vecCenter);
		vecCenter = DirectX::XMVectorMultiply(DirectX::XMVectorReplicate(0.25f), vecCenter);
		return vecCenter;
	}
};

DirectX::XMVECTOR XM_CALLCONV ColorToVector(uint32_t uiColor);
DirectX::XMVECTOR XM_CALLCONV Project(DirectX::FXMVECTOR vecA, DirectX::FXMVECTOR vecB);
DirectX::XMVECTOR XM_CALLCONV ToBaseHeight(DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecEyePosition, float fBaseHeight);
float RotationFromPosition(DirectX::FXMVECTOR vecPosition);
DirectX::XMVECTOR XM_CALLCONV QuaternionFromDirection(DirectX::FXMVECTOR vecDirection, DirectX::FXMVECTOR vecOriginNormal, DirectX::FXMVECTOR vecUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
DirectX::XMVECTOR XM_CALLCONV Closest(DirectX::FXMVECTOR vecOrigin, DirectX::FXMVECTOR vecA, DirectX::FXMVECTOR vecB);
AreaVertices XM_CALLCONV CalculateArea(DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecDirection, float fForward, float fBack, float fWidth);
bool XM_CALLCONV InsideAreaVertices(DirectX::FXMVECTOR vecPosition, const AreaVertices& rAreaVertices);
DirectX::XMVECTOR XM_CALLCONV RotateTowards(DirectX::FXMVECTOR vecDirection, DirectX::FXMVECTOR vecTowards, float fAmount);
DirectX::XMVECTOR XM_CALLCONV RotateTowardsPercent(DirectX::FXMVECTOR vecDirection, DirectX::FXMVECTOR vecTowards, float fPercent);
DirectX::XMVECTOR XM_CALLCONV CircleJitter(DirectX::FXMVECTOR vecPosition, float fDistance, common::RandomEngine& randomEngine);

inline DirectX::XMMATRIX XM_CALLCONV RotationMatrixFromDirection(DirectX::FXMVECTOR vecDirection, DirectX::FXMVECTOR vecOriginNormal, DirectX::FXMVECTOR vecUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f))
{
	return DirectX::XMMatrixRotationQuaternion(QuaternionFromDirection(vecDirection, vecOriginNormal, vecUp));
}

inline float XM_CALLCONV Distance(DirectX::FXMVECTOR vecOne, DirectX::FXMVECTOR vecTwo)
{
	return DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(vecTwo, vecOne)));
}

inline DirectX::XMVECTOR XM_CALLCONV DirectionTo(DirectX::FXMVECTOR vecFrom, DirectX::FXMVECTOR vecTo)
{
	if (DirectX::XMVectorGetX(DirectX::XMVectorNearEqual(vecFrom, vecTo, DirectX::g_XMEpsilon)) != 0.0f) [[unlikely]]
	{
		return DirectX::XMVectorZero();
	}

	return DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(vecTo, vecFrom));
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
