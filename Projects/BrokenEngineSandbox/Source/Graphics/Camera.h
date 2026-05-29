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
	static constexpr float kfCameraEyeHeightDefault = 150.0f;
	// Shadow textures are pre-allocated to cover the visible area at this reference eye height; the shadow
	// texel world size is then fixed across zoom (no resample -> no pop/shimmer). Above this height the
	// fixed coverage crops on screen. Release pins kfEyeHeightMax to this value (see Camera.cpp).
	static constexpr float kfEyeHeightMaxReference = 600.0f;
	// Lighting deposit/spread/combine textures are pre-allocated to cover the visible area at this reference eye
	// height (headroom M = round(450/150) = 3), independent of shadow's. NOT a density knob: the lighting texel
	// world size targets a constant on-screen density at any height (the reference cancels in the texel formula),
	// so this only sets the allocation/headroom and the ramp-target clamp. On-screen crop ceiling = M^2 * default =
	// 1350 m, so the whole Release range (<= kfEyeHeightMaxReference) is covered with margin; dev zoom past 1350 m crops.
	static constexpr float kfLightingEyeHeightMaxReference = 450.0f;
	// Initial zoom-target on construction. WHEEL_DELTA (120) * kfEyeHeightPerWheelTick (0.1) = 12 units per click;
	// 4 clicks above kfCameraEyeHeightDefault gives a comfortable starting frame with zoom range either way.
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
