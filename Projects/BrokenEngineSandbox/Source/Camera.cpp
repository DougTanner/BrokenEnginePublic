#include "Pch.h"

#include "Camera.h"

#include "Frame/Frame.h"
#include "Graphics/GltfPipelines.h"

namespace game
{

constexpr float kfCameraPositionBlend = 8.0f;

Camera::Camera()
{
}

void Camera::Update(const Frame& rFrame)
{
	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mRealTime.GetDeltaNs(true));

	const auto& rInterpolate = rFrame.interpolate;

	// Calculate target position based on menu or game mode
	XMVECTOR vecTargetPosition;
	if (rInterpolate.flags & FrameFlags::kMainMenu)
	{
		vecTargetPosition = XMVectorAdd(kVecMainMenuPosition, XMVectorSet(40.0f * (-1.0f + std::cos(0.01f * rInterpolate.fCurrentTime)), 40.0f * std::sin(0.01f * rInterpolate.fCurrentTime), engine::gBaseHeight.Get(), 0.0f));
	}
	else
	{
		vecTargetPosition = rInterpolate.player.vecPosition + rInterpolate.vecCameraOffsetSmoothed;
	}

	// Blend from previous camera position toward target position
	float fBlend = std::clamp(fDeltaTime * kfCameraPositionBlend, 0.0f, 1.0f);
	mVecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fBlend), vecTargetPosition, XMVectorMultiply(XMVectorReplicate(1.0f - fBlend), mVecPosition));

	// Calculate eye position relative to camera position
	auto vecQuaternionEye = XMQuaternionRotationNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rInterpolate.fCameraEyeRotation);
	auto vecEyePositionRelative = XMVectorSet(0.0f, -rInterpolate.fCameraEyeHeight, 0.0f, 1.0f);
	vecEyePositionRelative = XMVector3Rotate(vecEyePositionRelative, vecQuaternionEye);
	mVecToEyeNormal = XMVector3Normalize(vecEyePositionRelative);

	mVecEyePosition = XMVectorAdd(mVecPosition, vecEyePositionRelative);

	mfShake = rInterpolate.fCameraShake;
}

} // namespace game
