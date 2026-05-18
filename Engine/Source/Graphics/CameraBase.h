#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class CameraBase
{
public:

	XMMATRIX mMatView {};
	XMMATRIX mMatPerspective {};

	XMFLOAT2 f2VisibleAreaQuadSize {};
	XMFLOAT4 f4RenderVisibleArea {};
	XMFLOAT4 f4LargeVisibleArea {};

	XMFLOAT4 f4VisibleTopLeft {};
	XMFLOAT4 f4VisibleTopRight {};
	XMFLOAT4 f4VisibleBottomLeft {};
	XMFLOAT4 f4VisibleBottomRight {};

	XMVECTOR mVecPosition {};
	XMVECTOR mVecEyePosition {};
	XMVECTOR mVecToEyeNormal {};

	float mfShake = 0.0f;
	int64_t miFrame = 0;

	// Visible-area LOD (read by renderer to pick which mesh-LOD region to draw). Equation:
	// floor(log4(eyeDistance / kfMinEyeHeight)) clamped to BufferManager::kiVisibleAreaLodCount-1.
	// Higher LOD = fewer mesh quads (each dim halved per LOD; total quads /4 per LOD).
	int miVisibleAreaLod = 0;

	// LOD-stable shadow-region world size, latched once per LOD transition. Sized to the LOD upper
	// bound (kfMinEyeHeight * 4^(iLod+1)) so it always covers the visible area at any eye distance
	// within the LOD. Z-stable: bit-identical across all the per-integer-meter zoom-bucket crossings
	// that re-latch f2VisibleAreaQuadSize. Used by shadow snap to eliminate texel-scale shimmer on
	// Z motion (the shadow texel is fVisibleWidth/fTextureWidth — if fVisibleWidth jumps per meter,
	// the shadow resamples at a different scale each frame and high-contrast edges flicker).
	float mfLodStableShadowWidth = 0.0f;
	float mfLodStableShadowHeight = 0.0f;

	CameraBase() = default;
	virtual ~CameraBase() = default;

	virtual float SunAngle() const { return mfSunAngle; }

	XMVECTOR XM_CALLCONV ScreenToWorld(FXMVECTOR vecScreenPos, float fHeight);

	inline bool XM_CALLCONV InVisibleArea(XMFLOAT4 f4VisibleArea, XMFLOAT4 f4Position, float fAdjustLeft = 0.0f, float fAdjustRight = 0.0f, float fAdjustTop = 0.0f, float fAdjustBottom = 0.0f)
	{
		return !(f4Position.x < f4VisibleArea.x - fAdjustLeft || f4Position.x > f4VisibleArea.z + fAdjustRight || f4Position.y > f4VisibleArea.y + fAdjustTop || f4Position.y < f4VisibleArea.w - fAdjustBottom);
	}

	inline bool XM_CALLCONV InVisibleArea(XMFLOAT4 f4VisibleArea, FXMVECTOR vecPosition, float fAdjustLeft = 0.0f, float fAdjustRight = 0.0f, float fAdjustTop = 0.0f, float fAdjustBottom = 0.0f)
	{
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecPosition);
		return InVisibleArea(f4VisibleArea, f4Position, fAdjustLeft, fAdjustRight, fAdjustTop, fAdjustBottom);
	}

	inline bool XM_CALLCONV AabbIntersectsVisibleArea(XMFLOAT4 f4VisibleArea, FXMVECTOR vecMin, FXMVECTOR vecMax)
	{
		return common::AabbIntersectsArea(f4VisibleArea, vecMin, vecMax);
	}

protected:

	float mfSunAngle = 1.4f;

	void CalculateMatricesAndVisibleArea();

private:

	XMFLOAT2 mf2LatchedQuadSize {};
	// 0 is unreachable as a real bucket (eye distance always >= kfEyeHeightMin = 150), so the
	// first-frame change-detect always fires. Using INT_MIN here would cause signed-integer
	// overflow when the hysteresis check evaluates `miVisibleAreaZoomBucket - 1`.
	int miVisibleAreaZoomBucket = 0;
	uint32_t muiVisibleAreaLatchKey = 0;
};

} // namespace engine

#endif // BT_CLIENT
