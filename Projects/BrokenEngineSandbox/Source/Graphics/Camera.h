#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;
struct FrameInterpolate;

class Camera : public engine::CameraBase
{
public:

	// Origin-coord island center in world space. Pinned to (0, 0) by the kOriginCoord overrides
	// in ComputeIslandOffset / ComputeIslandRotation (Frame.h) — centered offset + zero rotation.
	// Held as a named anchor so any future change to the menu-island position auto-moves the camera.
	static constexpr XMVECTOR kVecMenuIslandCenter {0.0f, 0.0f, 0.0f, 1.0f};

	// Main-menu camera target, as world-space XY offset from the menu island center.
	static constexpr XMVECTOR kVecMenuCameraOffset {20.4f, -76.3f, 0.0f, 0.0f};

	static constexpr float kfDefaultSunAngle = 1.8f;
	static constexpr float kfCameraEyeHeightDefault = 150.0f;
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
	float mfCameraEyeRotation = -1.2f;
	float mfCameraEyeHeightVelocity = 0.0f;
	float mfCameraEyeRotationVelocity = 0.0f;
	int miPreviousScrollWheelValue = 0;
	bool mbScrollWheelInitialized = false;
	XMVECTOR mVecLastKnownPlayerPosition {};
	XMVECTOR mVecLastKnownPlayerVelocity {};
	float mfLastKnownPlayerTime = 0.0f;
	engine::global_id_t mLastTrackedPlayerId {};

	XMVECTOR mVecJumpStartPosition {};
	XMVECTOR mVecPreviousTargetPosition {};
	float mfJumpStartTime = 0.0f;
	bool mbJumping = false;

};

inline Camera* gpCamera = nullptr;

} // namespace game

#endif // BT_CLIENT
