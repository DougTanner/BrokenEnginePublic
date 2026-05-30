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
	// Release zoom-out ceiling (dev builds zoom further; see Camera.cpp). NOT a texel reference: the shadow/lighting
	// texel grids hold a constant on-screen pixel size at any height (the texels coarsen with zoom instead of cropping
	// coverage), so this is purely the gameplay limit on how far the camera can pull back.
	static constexpr float kfEyeHeightMaxRelease = 600.0f;
	// Headroom multipliers: the shadow and lighting (deposit/spread/combine) textures are allocated this much larger
	// than the wanted on-screen pixel size so the processed window can transiently grow during a fast zoom-out before
	// it runs off the texture (where the CLAMP_TO_BORDER edge reads as no-shadow / no-light). The steady-state window
	// equals the wanted pixel size at every settled height regardless of this value -- it only sets the transient room.
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

	// Eye height the shadow texel grid is currently sized for; ramps toward the live eye height (rate-limited)
	// so the texel world size changes too slowly to perceive. 0 = uninitialized (snap to target on first frame).
	float mfShadowTexelEyeHeight = 0.0f;

	// Eye height the lighting texel grid is currently sized for; ramps toward the live eye height (rate-limited),
	// independent of the shadow ramp. 0 = uninitialized (snap to target on first frame).
	float mfLightingTexelEyeHeight = 0.0f;

};

inline Camera* gpCamera = nullptr;

} // namespace game

#endif // BT_CLIENT
