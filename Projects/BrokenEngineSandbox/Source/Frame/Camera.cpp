#include "Camera.h"

#include "Graphics/Managers/SwapchainManager.h"
#include "Ui/Wrapper.h"

#include "Frame/Frame.h"
#include "Input/Input.h"

using namespace DirectX;

namespace game
{

constexpr float kfZoomMultiplier = 2.0f;

bool Camera::operator==(const Camera& rOther) const
{
	bool bEqual = true;

	bEqual &= vecPosition == rOther.vecPosition;

	bEqual &= fEyeHeight == rOther.fEyeHeight;
	bEqual &= fEyeRotation == rOther.fEyeRotation;
	bEqual &= fCameraShake == rOther.fCameraShake;
	bEqual &= vecOffsetSmoothed == rOther.vecOffsetSmoothed;
	bEqual &= vecEyePosition == rOther.vecEyePosition;
	bEqual &= vecToEyeNormal == rOther.vecToEyeNormal;

	bEqual &= fEyeHeightVelocity == rOther.fEyeHeightVelocity;
	bEqual &= fEyeRotationVelocity == rOther.fEyeRotationVelocity;

	common::BreakOnNotEqual(bEqual);

	return bEqual;
}

void Camera::Global([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	Camera& rCurrent = rFrame.camera;
	const Camera& rPrevious = rPreviousFrame.camera;

	// Load
	auto vecPosition = rPrevious.vecPosition;

	// Position (rough, for visible area calculation)
	if (rFrame.flags & FrameFlags::kMainMenu)
	{
		vecPosition = XMVectorAdd(kVecMainMenuPosition, XMVectorSet(40.0f * (-1.0f + std::cos(0.01f * rFrame.fCurrentTime)), 40.0f * std::sin(0.01f * rFrame.fCurrentTime), engine::gBaseHeight.Get(), 0.0f));
	}
	else
	{
		vecPosition = rFrame.player.vecPosition + rPrevious.vecOffsetSmoothed;
	}

	// Save
	rCurrent.vecPosition = vecPosition;
}

void Camera::Interpolate([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	Camera& rCurrent = rFrame.camera;
	const Camera& rPrevious = rPreviousFrame.camera;

	// 1. operator== 2. Copy() 3. Load/Save in Global() or Main() or PostRender() 4. Spawn()
	// Make sure to Remove() any pools in Destroy()
	VERIFY_SIZE(rCurrent, 128);

	// Load
	auto vecPosition = rPrevious.vecPosition;
	float fEyeHeight = rPrevious.fEyeHeight;
	float fEyeRotation = rPrevious.fEyeRotation;
	float fCameraShake = std::max(rPrevious.fCameraShake - fDeltaTime, 0.0f);
	auto vecOffsetSmoothed = rPrevious.vecOffsetSmoothed;
	auto vecEyePosition = rPrevious.vecEyePosition;
	auto vecToEyeNormal = rPrevious.vecToEyeNormal;

	// Offset
	auto vecOffset = 10.0f * rFrameInputHeld.vecDirection;
	vecOffset = XMVectorMultiply(vecOffset, XMVectorSet(1.0f, engine::gpSwapchainManager->mfAspectRatio, 0.0f, 0.0f));

	constexpr float kfOffsetSmooth = 0.75f;
	vecOffsetSmoothed = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfOffsetSmooth), vecOffset, XMVectorMultiply(XMVectorReplicate(1.0f - fDeltaTime * kfOffsetSmooth), vecOffsetSmoothed));

	// Position
	if (rFrame.flags & FrameFlags::kMainMenu)
	{
		vecPosition = XMVectorAdd(kVecMainMenuPosition, XMVectorSet(40.0f * (-1.0f + std::cos(0.01f * rFrame.fCurrentTime)), 40.0f * std::sin(0.01f * rFrame.fCurrentTime), engine::gBaseHeight.Get(), 0.0f));
	}
	else
	{
		vecPosition = rFrame.player.vecPosition + vecOffsetSmoothed;
	}

	auto vecQuaternionEye = XMQuaternionRotationNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), fEyeRotation);
	auto vecEyePositionRelative = XMVectorSet(0.0f, -fEyeHeight, 0.0f, 1.0f);
	vecEyePositionRelative = XMVector3Rotate(vecEyePositionRelative, vecQuaternionEye);
	vecToEyeNormal = XMVector3Normalize(vecEyePositionRelative);

	vecEyePosition = XMVectorAdd(vecPosition, vecEyePositionRelative);

	// Eye
	fEyeHeight = std::clamp(fEyeHeight + fDeltaTime * rPrevious.fEyeHeightVelocity, 60.0f, 300.0f);
	fEyeRotation = std::clamp(fEyeRotation + fDeltaTime * rPrevious.fEyeRotationVelocity, -XM_PIDIV2, -0.9f);

	// Save
	rCurrent.vecPosition = vecPosition;
	rCurrent.fEyeHeight = fEyeHeight;
	rCurrent.fEyeRotation = fEyeRotation;
	rCurrent.fCameraShake = fCameraShake;
	rCurrent.vecOffsetSmoothed = vecOffsetSmoothed;
	rCurrent.vecEyePosition = vecEyePosition;
	rCurrent.vecToEyeNormal = vecToEyeNormal;
}

void Camera::PostRender([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Camera& rCurrent = rFrame.camera;
	const Camera& rPrevious = rPreviousFrame.camera;

	// Load
	float fEyeHeightVelocity = rPrevious.fEyeHeightVelocity;
	float fEyeRotationVelocity = rPrevious.fEyeRotationVelocity;

	// Eye height/rotation
	fEyeHeightVelocity = 0.0f;

	if (rFrameInput.held.fRotateEye == 0.0f)
	{
		fEyeRotationVelocity = 0.0f;
	}
	else
	{
		fEyeRotationVelocity = 1.0f * rFrameInput.held.fRotateEye;
	}

#if defined(ENABLE_DEBUG_INPUT)
	if (rFrameInput.held.flags & FrameInputHeldFlags::kZoomOut) [[unlikely]]
	{
		fEyeHeightVelocity = kfZoomMultiplier * rCurrent.fEyeHeight;
	}
	else if (rFrameInput.held.flags & FrameInputHeldFlags::kZoomIn) [[unlikely]]
	{
		fEyeHeightVelocity = -kfZoomMultiplier * rCurrent.fEyeHeight;
	}
	else
	{
		fEyeHeightVelocity = 0.0f;
	}
#endif

	rCurrent.fEyeHeightVelocity = fEyeHeightVelocity;
	rCurrent.fEyeRotationVelocity = fEyeRotationVelocity;
}

void Camera::Collide([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
}

void Camera::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
}

void Camera::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
}

void Camera::RenderGlobal([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
}

void Camera::RenderMain([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
}

} // namespace game
