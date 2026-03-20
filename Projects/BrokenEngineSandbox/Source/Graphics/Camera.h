#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;
struct FrameInterpolate;

class Camera : public engine::CameraBase
{
public:

	static constexpr XMVECTOR kVecMainMenuPosition {29.0f, -81.0f, 0.0f, 1.0f};
	static constexpr float kfDefaultSunAngle = 1.15f;

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
	float mfCameraEyeRotation = -1.2f;
	float mfCameraEyeHeightVelocity = 0.0f;
	float mfCameraEyeRotationVelocity = 0.0f;
	XMVECTOR mVecLastKnownPlayerPosition {};
	XMVECTOR mVecLastKnownPlayerVelocity {};
	float mfLastKnownPlayerTime = 0.0f;

};

inline Camera* gpCamera = nullptr;

} // namespace game

#endif // BT_CLIENT
