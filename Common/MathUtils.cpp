#include "MathUtils.h"

namespace common
{

XMVECTOR XM_CALLCONV ToBaseHeight(FXMVECTOR vecPosition, FXMVECTOR vecEyePosition, float fBaseHeight)
{
	return XMPlaneIntersectLine(XMPlaneFromPointNormal(XMVectorSet(0.0f, 0.0f, fBaseHeight, 0.0f), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)), vecPosition, vecEyePosition);
}

float RotationFromPosition(FXMVECTOR vecPosition)
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);
	return std::acos(f4Position.x / std::sqrt(f4Position.x * f4Position.x + f4Position.y * f4Position.y));
}

XMVECTOR XM_CALLCONV QuaternionFromDirection(FXMVECTOR vecDirection, FXMVECTOR vecOriginNormal, FXMVECTOR vecUp)
{
	if (XMVector4EqualInt(XMVectorEqual(vecOriginNormal, vecDirection), XMVectorTrueInt()))
	{
		return XMQuaternionIdentity();
	}

	auto vec = XMVector3Normalize(XMVector3Cross(vecOriginNormal, vecDirection));
	if (XMVector4EqualInt(XMVectorEqual(vec, XMVectorZero()), XMVectorTrueInt()))
	{
		return XMQuaternionRotationNormal(vecUp, XM_PI);
	}

	float fAngle = std::acos(std::clamp(XMVectorGetX(XMVector3Dot(vecDirection, vecOriginNormal)), -1.0f, 1.0f));
	return XMQuaternionRotationNormal(vec, fAngle);
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
	if (XMVectorGetX(XMVectorNearEqual(vecFrom, vecTo, g_XMEpsilon)) != 0.0f) [[unlikely]]
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
	if (std::abs(fA) < 1.0e-6f)
	{
		// Target speed approximately equals projectile speed: linear fallback
		if (std::abs(fB) > 1.0e-6f)
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

float FromGamma(float fGamma)
{
	return std::pow(std::max(0.0f, fGamma), 1.0f / 2.2f);
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
	XMFLOAT4A f4Color {};
	XMStoreFloat4A(&f4Color, vecColor);

	static constexpr float kfMultiplier = 255.0f;
	return static_cast<uint32_t>(kfMultiplier * f4Color.x) << 24 | static_cast<uint32_t>(kfMultiplier * f4Color.y) << 16 | static_cast<uint32_t>(kfMultiplier * f4Color.z) << 8 | static_cast<uint32_t>(kfMultiplier * f4Color.w);
}

uint32_t ColorLerp(uint32_t uiA, uint32_t uiB, float fPercent)
{
	return ColorToUint(XMVectorLerp(ColorToVector(uiA), ColorToVector(uiB), fPercent));
}

} // namespace common
