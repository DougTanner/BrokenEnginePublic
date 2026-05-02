#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;
struct FrameInterpolate;

class Camera : public engine::CameraBase
{
public:

	static constexpr XMVECTOR kVecMainMenuPosition {20.4f, -76.3f, 0.0f, 1.0f};
	static constexpr float kfDefaultSunAngle = 1.15f;
	static constexpr float kfCameraEyeHeightDefault = 150.0f;

	Camera();
	~Camera();

	void Update(const Frame& rFrame);
	void Update(const FrameInterpolate& rFrameInterpolate);

	float SunAngle() const override;
	float RawSunAngle() const { return mfSunAngle; }
	void ResetSunAngle() { mfSunAngle = kfDefaultSunAngle; }

	float mfTime = 0.0f;

	XMVECTOR mVecCameraOffsetSmoothed {};
	float mfCameraEyeHeight = 150.0f;
	float mfCameraEyeHeightTarget = 150.0f;
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
