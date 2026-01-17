#pragma once

#include "Graphics/CameraBase.h"

namespace game
{

struct Frame;
struct FrameInterpolate;

class Camera : public engine::CameraBase
{
public:

	// DT: TODO Baseheight
	static inline constexpr XMVECTOR kVecMainMenuPosition {29.0f, -81.0f, 0.0f, 1.0f};

	Camera();
	~Camera();

	void Update(const Frame& rFrame);
	void Update(const FrameInterpolate& rFrameInterpolate);

	common::Timer mRealTime;
	float mfTime = 0.0f;

	XMVECTOR vecCameraOffsetSmoothed {};
	float fCameraEyeHeight = 150.0f;
	float fCameraEyeRotation = -1.2f;
	float fCameraShake = 0.0f;
	float fCameraEyeHeightVelocity = 0.0f;
	float fCameraEyeRotationVelocity = 0.0f;
	XMVECTOR mVecToEyeNormal {};

	// DT: TODO Move this to proper location
	float mfShake = 0.0f;

	float mfSunAngle = 1.15f;
	float mfPreviousFrameTime = 0.0f;
};

inline Camera* gpCamera = nullptr;

} // namespace game
