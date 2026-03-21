#include "Pch.h"

#if defined(BT_CLIENT)

#include "Graphics/Camera.h"

namespace engine
{

XMVECTOR XM_CALLCONV CameraBase::ScreenToWorld(FXMVECTOR vecScreenPos, float fHeight)
{
	XMVECTOR vecPlane = XMPlaneFromPointNormal(XMVectorSet(0.0f, 0.0f, fHeight, 1.0f), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));

	float fViewportWidth = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	float fViewportHeight = static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	auto vecWorldPos = XMVectorMultiply(XMVectorSet(fViewportWidth, fViewportHeight, 1.0f, 1.0f), vecScreenPos);

	vecWorldPos = XMVectorSetZ(vecWorldPos, 0.0f);
	auto vecRayStart = XMVector3Unproject(vecWorldPos, 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, XMMatrixIdentity());
	vecWorldPos = XMVectorSetZ(vecWorldPos, 1.0f);
	auto vecRayEnd = XMVector3Unproject(vecWorldPos, 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, XMMatrixIdentity());

	return XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
}

void CameraBase::CalculateMatricesAndVisibleArea()
{
	auto vecToEyeNormal = XMVector3Normalize(XMVectorSubtract(mVecEyePosition, mVecPosition));
	auto vecUp = XMVector3Cross(vecToEyeNormal, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
	mMatView = XMMatrixLookAtRH(mVecEyePosition, mVecPosition, vecUp);

	static constexpr float kfNearClip = 1.0f;
	static constexpr float kfFarClip = 400.0f;
	float fViewportWidth = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	float fViewportHeight = static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	float fAspectRatio = gpSwapchainManager->mfAspectRatio;
	mMatPerspective = XMMatrixPerspectiveFovRH(XMConvertToRadians(gFov.Get() / fAspectRatio), fAspectRatio, kfNearClip, kfFarClip);

	// Create plane at Z=0 for projecting screen corners to world space
	XMVECTOR vecPlane = XMPlaneFromPointNormal(XMVectorZero(), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));

	XMMATRIX matIdentity = XMMatrixIdentity();

	XMVECTOR vecRayStart {};
	XMVECTOR vecRayEnd {};
	XMVECTOR vecIntersectPlane {};

	// Calculate visible area corners by unprojecting screen corners to world space at Z=0
	XMFLOAT3 f3ScreenPos { 0.0f, 0.0f, 0.0f };

	// Top left
	f3ScreenPos.x = 0.0f;
	f3ScreenPos.y = 0.0f;

	f3ScreenPos.z = 0.0f;
	vecRayStart = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, matIdentity);
	f3ScreenPos.z = 1.0f;
	vecRayEnd = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, matIdentity);

	vecIntersectPlane = XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
	XMStoreFloat4(&f4VisibleTopLeft, vecIntersectPlane);

	// Top right
	f3ScreenPos.x = fViewportWidth;
	f3ScreenPos.y = 0.0f;

	f3ScreenPos.z = 0.0f;
	vecRayStart = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, matIdentity);
	f3ScreenPos.z = 1.0f;
	vecRayEnd = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, matIdentity);

	vecIntersectPlane = XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
	XMStoreFloat4(&f4VisibleTopRight, vecIntersectPlane);

	// Bottom left
	f3ScreenPos.x = 0.0f;
	f3ScreenPos.y = fViewportHeight;

	f3ScreenPos.z = 0.0f;
	vecRayStart = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, matIdentity);
	f3ScreenPos.z = 1.0f;
	vecRayEnd = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, matIdentity);

	vecIntersectPlane = XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
	XMStoreFloat4(&f4VisibleBottomLeft, vecIntersectPlane);

	// Bottom right
	f3ScreenPos.x = fViewportWidth;
	f3ScreenPos.y = fViewportHeight;

	f3ScreenPos.z = 0.0f;
	vecRayStart = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, matIdentity);
	f3ScreenPos.z = 1.0f;
	vecRayEnd = XMVector3Unproject(XMLoadFloat3(&f3ScreenPos), 0.0f, 0.0f, fViewportWidth, fViewportHeight, 0.0f, 1.0f, mMatPerspective, mMatView, matIdentity);

	vecIntersectPlane = XMPlaneIntersectLine(vecPlane, vecRayStart, vecRayEnd);
	XMStoreFloat4(&f4VisibleBottomRight, vecIntersectPlane);

	f4LargeVisibleArea = XMFLOAT4 {f4VisibleTopLeft.x, f4VisibleTopLeft.y, f4VisibleTopRight.x, f4VisibleBottomRight.y};

	if (gpGraphics->mFramebufferExtent2D.width > gpGraphics->mFramebufferExtent2D.height) [[likely]]
	{
		f4RenderVisibleArea = f4LargeVisibleArea;
		f4RenderVisibleArea.x -= gVisibleAreaExtraTop.Get() * (f4RenderVisibleArea.y - f4RenderVisibleArea.w);
		f4RenderVisibleArea.y += gVisibleAreaExtraTop.Get() * (f4RenderVisibleArea.y - f4RenderVisibleArea.w);
		f4RenderVisibleArea.z += gVisibleAreaExtraTop.Get() * (f4RenderVisibleArea.y - f4RenderVisibleArea.w);
		f4RenderVisibleArea.w -= gVisibleAreaExtraBottom.Get() * (f4RenderVisibleArea.y - f4RenderVisibleArea.w);
	}
	else [[unlikely]]
	{
		f4RenderVisibleArea = f4LargeVisibleArea;
	}

	// Adjust visible area in world space to align with terrain and water polygon grid
	auto [iTerrainQuadX, iTerrainQuadY] = gpTextureManager->DetailTextureSize(gWorldDetail.Get());
	float fQuadsX = static_cast<float>(iTerrainQuadX);
	float fQuadsY = static_cast<float>(iTerrainQuadY);

	f2VisibleAreaQuadSize.x = common::RoundDown((f4RenderVisibleArea.z - f4RenderVisibleArea.x) / fQuadsX + 0.01f, 0.01f);
	f2VisibleAreaQuadSize.y = common::RoundDown((f4RenderVisibleArea.y - f4RenderVisibleArea.w) / fQuadsY + 0.01f, 0.01f);
	f4RenderVisibleArea.x = common::RoundDown(f4RenderVisibleArea.x, f2VisibleAreaQuadSize.x);
	f4RenderVisibleArea.y = common::RoundDown(f4RenderVisibleArea.y + f2VisibleAreaQuadSize.y, f2VisibleAreaQuadSize.y);
	f4RenderVisibleArea.z = f4RenderVisibleArea.x + fQuadsX * f2VisibleAreaQuadSize.x;
	f4RenderVisibleArea.w = f4RenderVisibleArea.y - fQuadsY * f2VisibleAreaQuadSize.y;
}

} // namespace engine

#endif // BT_CLIENT
