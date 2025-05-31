#include "MathUtils.h"

namespace common
{

XMVECTOR XM_CALLCONV Project(FXMVECTOR vecA, FXMVECTOR vecB)
{
	auto vecADotB = XMVector3Dot(vecA, vecB);
	return XMVectorGetX(vecADotB) > 0.0f ? XMVectorMultiply(XMVectorDivide(vecADotB, XMVector3Dot(vecB, vecB)), vecB) : XMVectorZero();
}

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

XMVECTOR XM_CALLCONV Closest(FXMVECTOR vecOrigin, FXMVECTOR vecA, FXMVECTOR vecB)
{
	auto vecLengthA = XMVector3LengthSq(XMVectorSubtract(vecOrigin, vecA));
	auto vecLengthB = XMVector3LengthSq(XMVectorSubtract(vecOrigin, vecB));
	return XMVectorGetX(XMVectorGreater(vecLengthA, vecLengthB)) != 0.0f ? vecA : vecB;
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

bool XM_CALLCONV InsideAreaVertices(FXMVECTOR vecPosition, const AreaVertices& rAreaVertices)
{
	float fDist = 0.0f;
	bool bFirst = TriangleTests::Intersects(XMVectorSetZ(vecPosition, 0.0f), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rAreaVertices.vecTopRight, rAreaVertices.vecTopLeft, rAreaVertices.vecBottomLeft, fDist);
	bool bSecond = TriangleTests::Intersects(XMVectorSetZ(vecPosition, 0.0f), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rAreaVertices.vecBottomLeft, rAreaVertices.vecBottomRight, rAreaVertices.vecTopRight, fDist);
	return bFirst || bSecond;
}

XMVECTOR XM_CALLCONV RotateTowards(FXMVECTOR vecDirection, FXMVECTOR vecTowards, float fAmount)
{
	float fCrossZ = XMVectorGetZ(XMVector3Cross(vecTowards, vecDirection));
	return XMVector4Transform(vecDirection, XMMatrixRotationZ(fCrossZ > 0.0f ? -fAmount : fAmount));
}

XMVECTOR XM_CALLCONV RotateTowardsPercent(FXMVECTOR vecDirection, FXMVECTOR vecTowards, float fPercent)
{
	float fCrossZ = XMVectorGetZ(XMVector3Cross(vecTowards, vecDirection));
	float fAngle = XMVectorGetX(XMVector2AngleBetweenNormals(vecTowards, vecDirection));
	return XMVector4Transform(vecDirection, XMMatrixRotationZ(fPercent * (fCrossZ > 0.0f ? -fAngle : fAngle)));
}

XMVECTOR XM_CALLCONV CircleJitter(FXMVECTOR vecPosition, float fDistance, common::RandomEngine& randomEngine)
{
	auto vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(common::Random<XM_2PI>(randomEngine)));
	return XMVectorMultiplyAdd(XMVectorReplicate(fDistance * common::Random(randomEngine)), vecDirection, vecPosition);

}

} // namespace common
