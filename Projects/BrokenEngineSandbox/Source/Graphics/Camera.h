#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;
struct FrameInterpolate;

class Camera : public engine::CameraBase
{
public:

	// Origin-coord anchor used by main-menu camera math. Origin is sampled like any other cell,
	// so this is just the (0, 0) cell center; the camera converges on the focused player anyway.
	static constexpr XMVECTOR kVecMenuIslandCenter {0.0f, 0.0f, 0.0f, 1.0f};

	// Main-menu camera target, as world-space XY offset from the menu island center.
	static constexpr XMVECTOR kVecMenuCameraOffset {20.4f, -76.3f, 0.0f, 0.0f};

	static constexpr float kfDefaultSunAngle = 1.8f;
	static constexpr float kfCameraEyeHeightDefault = 300.0f;
	// Camera-height zoom-factor fade endpoint (2x default eye height = fully zoomed out). Single-sources the water
	// (WaterUniforms) and lighting (LightingUniforms) LerpAtHeight calls -- both reference this constant.
	static constexpr float kfWaveFadeEndHeight = 2.0f * kfCameraEyeHeightDefault;
	// Release zoom-out ceiling (dev builds zoom further; see Camera.cpp). NOT a texel reference: the shadow/lighting
	// texel grids hold a constant on-screen pixel size at any height (the texels coarsen with zoom instead of cropping
	// coverage), so this is purely the gameplay limit on how far the camera can pull back.
	static constexpr float kfEyeHeightMaxRelease = 600.0f;
	// Headroom multipliers: the shadow and lighting (deposit/spread/combine) textures are allocated this much larger
	// than the wanted on-screen pixel size. Because their texel-height references never fall below live eye height,
	// this margin keeps the raw live-frustum footprints inside their textures throughout zoom transitions. At settled
	// height, the raw live-frustum window equals the wanted pixel size regardless of this value.
	static constexpr float kfShadowHeadroomMultiplier = 1.5f;
	static constexpr float kfLightingHeadroomMultiplier = 1.5f;
	// Initial zoom-target on construction: a few wheel-clicks above the default for a comfortable opening frame with
	// zoom range either way. WHEEL_DELTA (120) * kfEyeHeightPerWheelTick (0.1) = 12 units per click.
	static constexpr float kfCameraEyeHeightInitial = kfCameraEyeHeightDefault + 48.0f;

	Camera();
	~Camera();

	void Update(const Frame& rFrame);
	void Update(const FrameInterpolate& rFrameInterpolate);

	float SunAngle() const override;
	float RawSunAngle() const { return mfSunAngle; }
	void ResetSunAngle() { mfSunAngle = kfDefaultSunAngle; }

	float mfTime = 0.0f;

	XMVECTOR mVecCameraOffsetSmoothed {};
	float mfCameraEyeHeight = kfCameraEyeHeightInitial;
	float mfCameraEyeHeightTarget = kfCameraEyeHeightInitial;
	XMVECTOR mVecLastKnownPlayerPosition {};
	XMVECTOR mVecLastKnownPlayerVelocity {};
	float mfLastKnownPlayerTime = 0.0f;
	engine::global_id_t mLastTrackedPlayerId {};

	XMVECTOR mVecJumpStartPosition {};
	XMVECTOR mVecPreviousTargetPosition {};
	float mfJumpStartTime = 0.0f;
	bool mbJumping = false;

	float mfEyeStartHeight = 0.0f;
	float mfEyeStartVelocity = 0.0f;
	float mfEyeStartTime = 0.0f;
	float mfEyeVelocity = 0.0f;
	bool mbEyeZooming = false;

	// Eye-height reference that sizes the shadow texel grid. Expands immediately with outward zoom and contracts
	// at the shadow rate inward. 0 = uninitialized (snap directly to live height on first frame).
	float mfShadowTexelEyeHeight = 0.0f;

	// Eye-height reference that sizes the lighting texel grid. Expands immediately with outward zoom and contracts
	// at the independent lighting rate inward. 0 = uninitialized (snap directly to live height on first frame).
	float mfLightingTexelEyeHeight = 0.0f;

};

inline Camera* gpCamera = nullptr;

} // namespace game

#endif // BT_CLIENT
