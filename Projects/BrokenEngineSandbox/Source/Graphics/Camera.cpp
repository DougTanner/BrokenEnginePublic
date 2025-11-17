#include "Pch.h"

#include "Camera.h"

#include "Frame/Frame.h"
#include "Graphics/GltfPipelines.h"
#include "Input/RawInputManager.h"

namespace game
{

constexpr float kfCameraPositionBlend = 8.0f;

Camera::Camera()
{
	gpCamera = this;

	mVecPosition = kVecMainMenuPosition;
}

Camera::~Camera()
{
	gpCamera = nullptr;
}

void Camera::Update(const Frame& rFrame)
{
	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mRealTime.GetDeltaNs(true));
	mfTime += fDeltaTime;

#if 0
	// Calculate directional offset for camera smoothing
	XMVECTOR vecOffset = 10.0f * rFrameInput.vecDirection;
	vecOffset = XMVectorMultiply(vecOffset, XMVectorSet(1.0f, engine::gpSwapchainManager->mfAspectRatio, 0.0f, 0.0f));
	constexpr float kfOffsetSmooth = 0.75f;
	rInterpolate.vecCameraOffsetSmoothed = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfOffsetSmooth), vecOffset, XMVectorMultiply(XMVectorReplicate(1.0f - fDeltaTime * kfOffsetSmooth), rPreviousInterpolate.vecCameraOffsetSmoothed));

	// Integrate camera position from velocity
	rInterpolate.fCameraEyeHeight = rPreviousInterpolate.fCameraEyeHeight + fDeltaTime * rPreviousInterpolate.fCameraEyeHeightVelocity;
	rInterpolate.fCameraEyeRotation = rPreviousInterpolate.fCameraEyeRotation + fDeltaTime * rPreviousInterpolate.fCameraEyeRotationVelocity;

	// Clamp camera height and rotation to valid ranges
	rInterpolate.fCameraEyeHeight = std::clamp(rInterpolate.fCameraEyeHeight, 60.0f, 300.0f);
	rInterpolate.fCameraEyeRotation = std::clamp(rInterpolate.fCameraEyeRotation, -XM_PIDIV2, -0.9f);

	// Decay camera shake
	rInterpolate.fCameraShake = std::max(rPreviousInterpolate.fCameraShake - fDeltaTime * 2.0f, 0.0f);
#endif

	// Calculate target position based on menu or game mode
	XMVECTOR vecTargetPosition {};
	if (rFrame.flags & FrameFlags::kMainMenu)
	{
		vecTargetPosition = XMVectorAdd(kVecMainMenuPosition, XMVectorSet(40.0f * (-1.0f + std::cos(0.01f * mfTime)), 40.0f * std::sin(0.01f * mfTime), engine::gBaseHeight.Get(), 0.0f));
	}
	else
	{
		vecTargetPosition = rFrame.interpolate.player.vecPosition; // +rInterpolate.vecCameraOffsetSmoothed;
	}

	// Blend from previous camera position toward target position
	float fBlend = std::clamp(fDeltaTime * kfCameraPositionBlend, 0.0f, 1.0f);
	mVecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fBlend), vecTargetPosition, XMVectorMultiply(XMVectorReplicate(1.0f - fBlend), mVecPosition));

	// Calculate eye position relative to camera position
	auto vecQuaternionEye = XMQuaternionRotationNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), fCameraEyeRotation);
	auto vecEyePositionRelative = XMVectorSet(0.0f, -fCameraEyeHeight, 0.0f, 1.0f);
	vecEyePositionRelative = XMVector3Rotate(vecEyePositionRelative, vecQuaternionEye);
	mVecToEyeNormal = XMVector3Normalize(vecEyePositionRelative);

	mVecEyePosition = XMVectorAdd(mVecPosition, vecEyePositionRelative);

	// DT: TEMP This should be moved elsewhere
	mfShake = fCameraShake;

	// Set controller vibration based on camera shake
	// DT: TEMP This should be moved elsewhere
	float fVibration = std::pow(mfShake, 0.5f);
	engine::gpRawInputManager->SetVibration(0, fVibration, fVibration);

	// Calculate matrices and visible area
	CalculateMatricesAndVisibleArea();
}

} // namespace game
