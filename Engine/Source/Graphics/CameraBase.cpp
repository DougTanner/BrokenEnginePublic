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
	static constexpr float kfMinFarClip = 400.0f;
	static constexpr float kfFarClipPerEyeDistance = 2.667f;
	float fEyeDistance = XMVectorGetX(XMVector3Length(XMVectorSubtract(mVecEyePosition, mVecPosition)));
	float fFarClip = std::max(kfMinFarClip, fEyeDistance * kfFarClipPerEyeDistance);
	float fViewportWidth = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	float fViewportHeight = static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	float fAspectRatio = gpSwapchainManager->mfAspectRatio;
	mMatPerspective = XMMatrixPerspectiveFovRH(XMConvertToRadians(gFov.Get() / fAspectRatio), fAspectRatio, kfNearClip, fFarClip);

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

	// Adjust visible area in world space to align with terrain and water polygon grid.
	// LOD bucket: floor(log4(eyeDist / kfMinEyeHeight)) selects mesh density and snap-grid
	// coarseness in lockstep. Each LOD reduces per-dim mesh quads by 2 (total by 4); the snap
	// quad size scales accordingly so the visible-area edges only move when the camera crosses
	// a full coarse-LOD quad. Within a LOD, the integer-eye-distance bucket below latches the
	// per-frame quadSize, so sub-pixel FP drift in mfCameraEyeHeight cannot oscillate the snap
	// (see GitHub flicker investigation: floor(area/quadSize) amplifies any quadSize jitter by
	// ~area/quadSize, so quadSize must be bit-stable across consecutive frames).
	XMFLOAT4 f4RawAreaIn = f4RenderVisibleArea;

	constexpr float kfMinEyeHeight = 150.0f;
	int iLod = std::clamp(static_cast<int>(std::floor(std::log2(std::max(fEyeDistance, kfMinEyeHeight) / kfMinEyeHeight) * 0.5f)),
	                      0, BufferManager::kiVisibleAreaLodCount - 1);
	// LOD hysteresis: refuse to flip back across the shared boundary if eye distance is still
	// near it. Boundaries are at kfMinEyeHeight * 4^L; 5% band absorbs FP rounding around
	// asymptotic settling.
	constexpr float kfLodHysteresisFraction = 0.05f;
	if (iLod == miVisibleAreaLod - 1)
	{
		float fBoundary = kfMinEyeHeight * std::pow(4.0f, static_cast<float>(miVisibleAreaLod));
		if (fEyeDistance > fBoundary * (1.0f - kfLodHysteresisFraction))
		{
			iLod = miVisibleAreaLod;
		}
	}
	else if (iLod == miVisibleAreaLod + 1)
	{
		float fBoundary = kfMinEyeHeight * std::pow(4.0f, static_cast<float>(miVisibleAreaLod + 1));
		if (fEyeDistance < fBoundary * (1.0f + kfLodHysteresisFraction))
		{
			iLod = miVisibleAreaLod;
		}
	}

	const auto& rLodMesh = gpBufferManager->mTerrainMeshLods[iLod];
	float fQuadsX = static_cast<float>(rLodMesh.iQuadCountX);
	float fQuadsY = static_cast<float>(rLodMesh.iQuadCountY);

	int iZoomBucket = static_cast<int>(std::floor(fEyeDistance));
	constexpr float kfZoomBucketHysteresis = 0.1f;
	if (iZoomBucket == miVisibleAreaZoomBucket - 1 && fEyeDistance > static_cast<float>(miVisibleAreaZoomBucket) - kfZoomBucketHysteresis)
	{
		iZoomBucket = miVisibleAreaZoomBucket;
	}
	else if (iZoomBucket == miVisibleAreaZoomBucket + 1 && fEyeDistance < static_cast<float>(miVisibleAreaZoomBucket + 1) + kfZoomBucketHysteresis)
	{
		iZoomBucket = miVisibleAreaZoomBucket;
	}

	uint32_t uiLatchKey = static_cast<uint32_t>(rLodMesh.iQuadCountX)
	                    ^ (static_cast<uint32_t>(rLodMesh.iQuadCountY) << 16)
	                    ^ gpGraphics->mFramebufferExtent2D.width
	                    ^ (gpGraphics->mFramebufferExtent2D.height << 16);

	if (iZoomBucket != miVisibleAreaZoomBucket || uiLatchKey != muiVisibleAreaLatchKey || iLod != miVisibleAreaLod)
	{
		miVisibleAreaZoomBucket = iZoomBucket;
		muiVisibleAreaLatchKey = uiLatchKey;
		miVisibleAreaLod = iLod;
		mf2LatchedQuadSize.x = (f4RenderVisibleArea.z - f4RenderVisibleArea.x) / fQuadsX;
		mf2LatchedQuadSize.y = (f4RenderVisibleArea.y - f4RenderVisibleArea.w) / fQuadsY;
	}

	f2VisibleAreaQuadSize = mf2LatchedQuadSize;
	f4RenderVisibleArea.x = common::RoundDown(f4RenderVisibleArea.x, f2VisibleAreaQuadSize.x);
	f4RenderVisibleArea.y = common::RoundDown(f4RenderVisibleArea.y + f2VisibleAreaQuadSize.y, f2VisibleAreaQuadSize.y);
	f4RenderVisibleArea.z = f4RenderVisibleArea.x + fQuadsX * f2VisibleAreaQuadSize.x;
	f4RenderVisibleArea.w = f4RenderVisibleArea.y - fQuadsY * f2VisibleAreaQuadSize.y;

	// kTemp: diagnose water-vertex flicker when camera looks still. Log whenever the snapped
	// visible area changes by less than one quad — that's the regime the snap is supposed to absorb.
	{
		static XMFLOAT4A sf4PrevPos {};
		static XMFLOAT4A sf4PrevEye {};
		static XMFLOAT4 sf4PrevRaw {};
		static XMFLOAT4 sf4PrevSnapped {};
		static XMFLOAT2 sf2PrevQuad {};
		static bool sbInit = false;
		XMFLOAT4A f4Pos {};
		XMFLOAT4A f4Eye {};
		XMStoreFloat4A(&f4Pos, mVecPosition);
		XMStoreFloat4A(&f4Eye, mVecEyePosition);
		if (sbInit)
		{
			float fPosDelta = std::sqrt((f4Pos.x - sf4PrevPos.x) * (f4Pos.x - sf4PrevPos.x) + (f4Pos.y - sf4PrevPos.y) * (f4Pos.y - sf4PrevPos.y));
			float fEyeDelta = std::sqrt((f4Eye.x - sf4PrevEye.x) * (f4Eye.x - sf4PrevEye.x)
			                          + (f4Eye.y - sf4PrevEye.y) * (f4Eye.y - sf4PrevEye.y)
			                          + (f4Eye.z - sf4PrevEye.z) * (f4Eye.z - sf4PrevEye.z));
			float fRawXDelta = f4RawAreaIn.x - sf4PrevRaw.x;
			float fRawYDelta = f4RawAreaIn.y - sf4PrevRaw.y;
			float fRawWidthDelta = (f4RawAreaIn.z - f4RawAreaIn.x) - (sf4PrevRaw.z - sf4PrevRaw.x);
			float fSnapXDelta = f4RenderVisibleArea.x - sf4PrevSnapped.x;
			float fSnapYDelta = f4RenderVisibleArea.y - sf4PrevSnapped.y;
			float fQuadXDelta = f2VisibleAreaQuadSize.x - sf2PrevQuad.x;
			float fQuadYDelta = f2VisibleAreaQuadSize.y - sf2PrevQuad.y;
			bool bCameraStill = fPosDelta < 0.01f && fEyeDelta < 0.01f;
			bool bSnapMoved = fSnapXDelta != 0.0f || fSnapYDelta != 0.0f || fQuadXDelta != 0.0f || fQuadYDelta != 0.0f;
			if (bCameraStill && bSnapMoved)
			{
				LOG(kTemp, kInfo,
				    "VisibleAreaFlicker posD={} eyeD={} rawXD={} rawYD={} rawWD={} quadXD={} quadYD={} snapXD={} snapYD={} quadX={} snapX={} rawX={}",
				    common::Wb(fPosDelta, 6), common::Wb(fEyeDelta, 6),
				    common::Wb(fRawXDelta, 8), common::Wb(fRawYDelta, 8), common::Wb(fRawWidthDelta, 8),
				    common::Wb(fQuadXDelta, 8), common::Wb(fQuadYDelta, 8),
				    common::Wb(fSnapXDelta, 6), common::Wb(fSnapYDelta, 6),
				    common::Wb(f2VisibleAreaQuadSize.x, 6),
				    common::Wb(f4RenderVisibleArea.x, 4),
				    common::Wb(f4RawAreaIn.x, 4));
			}
		}
		sf4PrevPos = f4Pos;
		sf4PrevEye = f4Eye;
		sf4PrevRaw = f4RawAreaIn;
		sf4PrevSnapped = f4RenderVisibleArea;
		sf2PrevQuad = f2VisibleAreaQuadSize;
		sbInit = true;
	}
}

} // namespace engine

#endif // BT_CLIENT
