#include "MathUtils.h"

namespace common
{

XMVECTOR XM_CALLCONV ToBaseHeight(FXMVECTOR vecPosition, FXMVECTOR vecEyePosition, float fBaseHeight)
{
	return XMPlaneIntersectLine(XMPlaneFromPointNormal(XMVectorSet(0.0f, 0.0f, fBaseHeight, 0.0f), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)), vecPosition, vecEyePosition);
}

float RotationFromPosition(FXMVECTOR vecPosition)
{
	// atan2 is single-valued over [-pi, pi], finite at the origin, and distinguishes north (+y) from south (-y)
	return std::atan2(XMVectorGetY(vecPosition), XMVectorGetX(vecPosition));
}

XMVECTOR XM_CALLCONV QuaternionFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp)
{
	XMVECTOR vecCross = XMVector3Cross(vecOriginNormal, vecDirection);
	float fDot = XMVectorGetX(XMVector3Dot(vecDirection, vecOriginNormal));

	// Tolerance test on the pre-normalized cross length: a nearly (anti-)parallel input yields a tiny cross whose
	// normalization is a garbage axis. fDot disambiguates parallel (identity) from anti-parallel (180 deg about up).
	static constexpr float kfParallelEpsilonSq = 1.0e-6f;
	if (XMVectorGetX(XMVector3LengthSq(vecCross)) < kfParallelEpsilonSq)
	{
		return fDot < 0.0f ? XMQuaternionRotationNormal(vecUp, XM_PI) : XMQuaternionIdentity();
	}

	float fAngle = std::acos(std::clamp(fDot, -1.0f, 1.0f));
	return XMQuaternionRotationNormal(XMVector3Normalize(vecCross), fAngle);
}

AreaVertices XM_CALLCONV CalculateArea(FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fForward, float fBack, float fWidth)
{
	auto vecForward = XMVectorMultiply(XMVectorReplicate(fForward), vecDirection);
	auto vecBack = XMVectorMultiply(XMVectorReplicate(-fBack), vecDirection);

	auto vecWidthNormal = XMVector3Normalize(XMVector3Cross(vecDirection, XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f)));
	auto vecLeft = XMVectorMultiply(XMVectorReplicate(fWidth), vecWidthNormal);
	auto vecRight = XMVectorMultiply(XMVectorReplicate(-fWidth), vecWidthNormal);

	auto vecTopLeft = XMVectorAdd(vecPosition, XMVectorAdd(vecForward, vecLeft));
	auto vecTopRight = XMVectorAdd(vecPosition, XMVectorAdd(vecForward, vecRight));
	auto vecBottomLeft = XMVectorAdd(vecPosition, XMVectorAdd(vecBack, vecLeft));
	auto vecBottomRight = XMVectorAdd(vecPosition, XMVectorAdd(vecBack, vecRight));

	return AreaVertices {vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight};
}

XMVECTOR XM_CALLCONV RotateTowardsPercent(FXMVECTOR vecDirection, FXMVECTOR vecTowards, float fPercent)
{
	float fCrossZ = XMVectorGetZ(XMVector3Cross(vecTowards, vecDirection));
	float fAngle = XMVectorGetX(XMVector2AngleBetweenNormals(vecTowards, vecDirection));
	return XMVector4Transform(vecDirection, XMMatrixRotationZ(fPercent * (fCrossZ > 0.0f ? -fAngle : fAngle)));
}

XMVECTOR XM_CALLCONV RandomAngleJitter(FXMVECTOR vecDirection, float fMaxJitter, RandomEngine& rRandomEngine)
{
	float fJitter = -fMaxJitter + Random<2.0f>(rRandomEngine) * fMaxJitter;
	return XMVector4Transform(vecDirection, XMMatrixRotationZ(fJitter));
}

XMMATRIX XM_CALLCONV RotationMatrixFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp)
{
	return XMMatrixRotationQuaternion(QuaternionFromDirection(vecDirection, vecOriginNormal, vecUp));
}

float XM_CALLCONV Distance(FXMVECTOR vecOne, FXMVECTOR vecTwo)
{
	return XMVectorGetX(XMVector3Length(XMVectorSubtract(vecTwo, vecOne)));
}

XMVECTOR XM_CALLCONV DirectionTo(FXMVECTOR vecFrom, FXMVECTOR vecTo)
{
	// 3D coincidence test: the prior single-lane XMVectorGetX(XMVectorNearEqual(...)) only compared lane X,
	// so points equal in X but far apart in Y/Z were wrongly treated as coincident
	static constexpr float kfCoincidentEpsilon = 1.0e-6f;
	if (XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vecTo, vecFrom))) < kfCoincidentEpsilon * kfCoincidentEpsilon) [[unlikely]]
	{
		return XMVectorZero();
	}

	return XMVector3Normalize(XMVectorSubtract(vecTo, vecFrom));
}

XMVECTOR XM_CALLCONV ComputeLeadPosition(FXMVECTOR vecShooterPosition, FXMVECTOR vecTargetPosition, FXMVECTOR vecTargetVelocity, float fProjectileSpeed)
{
	XMVECTOR vecOffset = XMVectorSubtract(vecTargetPosition, vecShooterPosition);
	float fA = XMVectorGetX(XMVector3Dot(vecTargetVelocity, vecTargetVelocity)) - fProjectileSpeed * fProjectileSpeed;
	float fB = 2.0f * XMVectorGetX(XMVector3Dot(vecOffset, vecTargetVelocity));
	float fC = XMVectorGetX(XMVector3Dot(vecOffset, vecOffset));

	float fT = -1.0f;
	// Relative epsilon scaled by the largest coefficient magnitude: the prior absolute 1e-6f was effectively
	// "exactly zero" at kilometer-scale coordinates, mis-selecting the linear/quadratic branch. fRef > 0 whenever
	// shooter != target or the target moves; the all-zero degenerate produces a NaN root that falls through to the
	// vecTargetPosition return below.
	float fRef = std::max(std::abs(fA), std::max(std::abs(fB), std::abs(fC)));
	static constexpr float kfRelativeEpsilon = 1.0e-6f;
	if (std::abs(fA) < kfRelativeEpsilon * fRef)
	{
		// Target speed approximately equals projectile speed: linear fallback
		if (std::abs(fB) > kfRelativeEpsilon * fRef)
		{
			fT = -fC / fB;
		}
	}
	else
	{
		float fDiscriminant = fB * fB - 4.0f * fA * fC;
		if (fDiscriminant >= 0.0f)
		{
			float fSqrt = std::sqrt(fDiscriminant);
			float fInv2A = 0.5f / fA;
			float fT0 = (-fB - fSqrt) * fInv2A;
			float fT1 = (-fB + fSqrt) * fInv2A;
			// Smallest positive root
			if (fT0 > 0.0f && fT1 > 0.0f)
			{
				fT = std::min(fT0, fT1);
			}
			else if (fT0 > 0.0f)
			{
				fT = fT0;
			}
			else if (fT1 > 0.0f)
			{
				fT = fT1;
			}
		}
	}

	if (fT <= 0.0f)
	{
		return vecTargetPosition;
	}
	return XMVectorMultiplyAdd(vecTargetVelocity, XMVectorReplicate(fT), vecTargetPosition);
}

bool XM_CALLCONV AabbIntersectsArea(XMFLOAT4 f4Area, FXMVECTOR vecMin, FXMVECTOR vecMax)
{
	float fMinX = XMVectorGetX(vecMin);
	float fMaxX = XMVectorGetX(vecMax);
	float fMinY = XMVectorGetY(vecMin);
	float fMaxY = XMVectorGetY(vecMax);

	return !(fMaxX < f4Area.x || fMinX > f4Area.z || fMaxY < f4Area.w || fMinY > f4Area.y);
}

bool XM_CALLCONV InsideArea(FXMVECTOR vecPosition, const XMFLOAT4& rf4Area)
{
	float fX = XMVectorGetX(vecPosition);
	float fY = XMVectorGetY(vecPosition);
	return fX > rf4Area.x && fX < rf4Area.z && fY < rf4Area.y && fY > rf4Area.w;
}

bool XM_CALLCONV InsideArea(FXMVECTOR vecPosition, FXMVECTOR vecArea)
{
	// vecArea: x=minX, y=maxY, z=maxX, w=minY
	// Inside if: minX < posX < maxX AND minY < posY < maxY
	// Rearranged: posX > minX AND posY > minY AND maxX > posX AND maxY > posY
	XMVECTOR vecA = XMVectorPermute<0, 1, 6, 5>(vecPosition, vecArea);  // (posX, posY, maxX, maxY)
	XMVECTOR vecB = XMVectorPermute<4, 7, 0, 1>(vecPosition, vecArea);  // (minX, minY, posX, posY)
	uint32_t uiCR = XMVector4GreaterR(vecA, vecB);
	return XMComparisonAllTrue(uiCR);
}

XMVECTOR XM_CALLCONV ColorToVector(uint32_t uiColor)
{
	static constexpr float kfMultiplier = 1.0f / 255.0f;
	return XMVectorSet(kfMultiplier * static_cast<float>(uiColor >> 24), kfMultiplier * static_cast<float>((uiColor & 0x00FF0000) >> 16), kfMultiplier * static_cast<float>((uiColor & 0x0000FF00) >> 8), kfMultiplier * static_cast<float>(uiColor & 0x000000FF));
}

uint32_t XM_CALLCONV ColorToUint(FXMVECTOR vecColor)
{
	// Saturate to [0,1] so out-of-range lanes can't overflow an 8-bit field and bleed into the adjacent channel,
	// and round-to-nearest (+0.5f, lanes non-negative after saturate) so ColorToUint(ColorToVector(c)) == c
	XMFLOAT4A f4Color {};
	XMStoreFloat4A(&f4Color, XMVectorSaturate(vecColor));

	static constexpr float kfMultiplier = 255.0f;
	return static_cast<uint32_t>(kfMultiplier * f4Color.x + 0.5f) << 24 | static_cast<uint32_t>(kfMultiplier * f4Color.y + 0.5f) << 16 | static_cast<uint32_t>(kfMultiplier * f4Color.z + 0.5f) << 8 | static_cast<uint32_t>(kfMultiplier * f4Color.w + 0.5f);
}

uint32_t ColorLerp(uint32_t uiA, uint32_t uiB, float fPercent)
{
	return ColorToUint(XMVectorLerp(ColorToVector(uiA), ColorToVector(uiB), fPercent));
}

} // namespace common
