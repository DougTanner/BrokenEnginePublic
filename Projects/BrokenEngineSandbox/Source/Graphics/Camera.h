#pragma once

#ifdef BT_CLIENT

namespace game
{

struct Frame;
struct FrameInterpolate;

class Camera : public engine::CameraBase
{
public:

	static inline constexpr XMVECTOR kVecMainMenuPosition {29.0f, -81.0f, 0.0f, 1.0f};
	static inline constexpr float kfDefaultSunAngle = 1.15f;

	Camera();
	~Camera();

	void Update(const Frame& rFrame);
	void Update(const FrameInterpolate& rFrameInterpolate);

	float SunAngle(bool bInternalOnly = false) const;
	void ResetSunAngle() { mfSunAngle = kfDefaultSunAngle; }

	common::Timer mRealTime;
	float mfTime = 0.0f;

	XMVECTOR mVecCameraOffsetSmoothed {};
	float mfCameraEyeHeight = 150.0f;
	float mfCameraEyeRotation = -1.2f;
	float mfCameraEyeHeightVelocity = 0.0f;
	float mfCameraEyeRotationVelocity = 0.0f;
	XMVECTOR mVecToEyeNormal {};

	// Camera shake intensity (0.0 - 1.0), set by damage, decays over time
	float mfShake = 0.0f;

	XMVECTOR mVecPreviousPosition {};

	int64_t miFrame = 0;

private:

	float mfSunAngle = kfDefaultSunAngle;
};

inline Camera* gpCamera = nullptr;

} // namespace game

#endif // BT_CLIENT
