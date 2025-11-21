#pragma once

namespace engine
{

struct FrameBase;

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

	CameraBase() = default;
	virtual ~CameraBase() = default;

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

	void CalculateMatricesAndVisibleArea();
};

} // namespace engine
