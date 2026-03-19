#include "Pch.h"

#if defined(BT_CLIENT)

#include "Camera.h"

#include "Game.h"
#include "Frame/Frame.h"
#include "Frame/Collections/Players/Players.h"

namespace game
{

constexpr float kfCameraPositionBlend = 8.0f;

Camera::Camera()
{
	gpCamera = this;

	mVecPosition = XMVectorAdd(kVecMainMenuPosition, XMVectorSet(0.0f, 0.0f, engine::gBaseHeight.Get(), 0.0f));
}

Camera::~Camera()
{
	gpCamera = nullptr;
}

void Camera::Update(const Frame& rFrame)
{
	Update(rFrame.interpolate);
}

void Camera::Update(const FrameInterpolate& rFrameInterpolate)
{
	// Use display-rate dt: in FIFO mode each render frame is displayed for exactly 1/refreshRate
	float fDeltaTime = 1.0f / static_cast<float>(engine::gpGraphics->miMonitorRefreshRate);
	mfTime += fDeltaTime;

	// Decay camera shake using real-time
	mfShake = std::max(mfShake - fDeltaTime * 2.0f, 0.0f);

	miFrame = rFrameInterpolate.iTick;

	// Update sun angle with varying speeds (only during gameplay)
	if (!(rFrameInterpolate.gameFlags & GameFlags::kMainMenu))
	{
		static constexpr float kfNoonSpeedStart = XM_PIDIV2 - XM_PIDIV8;
		static constexpr float kfNoonSpeedEnd = XM_PIDIV2 + XM_PIDIV8;
		static constexpr float kfNightSpeedStart = XM_PI;
		static constexpr float kfNightSpeedEnd = XM_2PI;
		if (mfSunAngle >= kfNoonSpeedStart && mfSunAngle < kfNoonSpeedEnd)
		{
			mfSunAngle = mfSunAngle + rFrameInterpolate.fDeltaTime * 0.025f;
		}
		else if (mfSunAngle >= kfNightSpeedStart && mfSunAngle < kfNightSpeedEnd)
		{
			mfSunAngle = mfSunAngle + rFrameInterpolate.fDeltaTime * 0.5f;
		}
		else
		{
			mfSunAngle = mfSunAngle + rFrameInterpolate.fDeltaTime * 0.01f;
		}

		if (mfSunAngle >= XM_2PI)
		{
			mfSunAngle = 0.0f;
		}
	}

	// Calculate target position based on menu or game mode
	XMVECTOR vecTargetPosition {};
	if (rFrameInterpolate.gameFlags & GameFlags::kMainMenu)
	{
		vecTargetPosition = XMVectorAdd(kVecMainMenuPosition, XMVectorSet(40.0f * (-1.0f + std::cos(0.01f * mfTime)), 40.0f * std::sin(0.01f * mfTime), engine::gBaseHeight.Get(), 0.0f));
	}
	else if (gpGame->HumanPlayerId().IsValid())
	{
		if (std::optional<int64_t> oIdx = gpGame->HumanPlayerIndex(*rFrameInterpolate.pPlayers))
		{
			XMVECTOR vecPlayerPos = rFrameInterpolate.pPlayers->pVecPositions[*oIdx];

			// Compute velocity from position delta using real time
			if (mfLastKnownPlayerTime > 0.0f && mfTime > mfLastKnownPlayerTime)
			{
				float fElapsedTime = mfTime - mfLastKnownPlayerTime;
				mVecLastKnownPlayerVelocity = XMVectorScale(XMVectorSubtract(vecPlayerPos, mVecLastKnownPlayerPosition), 1.0f / fElapsedTime);
			}

			mVecLastKnownPlayerPosition = vecPlayerPos;
			mfLastKnownPlayerTime = mfTime;
			vecTargetPosition = vecPlayerPos;
		}
		else
		{
			// Player not found — extrapolate from last known position and velocity
			float fElapsedTime = std::clamp(mfTime - mfLastKnownPlayerTime, 0.0f, 2.0f);
			vecTargetPosition = XMVectorMultiplyAdd(XMVectorReplicate(fElapsedTime), mVecLastKnownPlayerVelocity, mVecLastKnownPlayerPosition);
		}
	}
	else
	{
		vecTargetPosition = mVecPosition;
	}

	// Blend from previous camera position toward target position
	float fBlend = std::clamp(fDeltaTime * kfCameraPositionBlend, 0.0f, 1.0f);
	mVecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fBlend), vecTargetPosition, XMVectorMultiply(XMVectorReplicate(1.0f - fBlend), mVecPosition));

	// Calculate eye position relative to camera position
	auto vecQuaternionEye = XMQuaternionRotationNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), mfCameraEyeRotation);
	auto vecEyePositionRelative = XMVectorSet(0.0f, -mfCameraEyeHeight, 0.0f, 1.0f);
	vecEyePositionRelative = XMVector3Rotate(vecEyePositionRelative, vecQuaternionEye);
	mVecToEyeNormal = XMVector3Normalize(vecEyePositionRelative);

	mVecEyePosition = XMVectorAdd(mVecPosition, vecEyePositionRelative);

	// Set controller vibration based on camera shake
	float fVibration = std::pow(mfShake, 0.5f);
	engine::gpRawInputManager->SetVibration(0, fVibration, fVibration);

	// Calculate matrices and visible area
	CalculateMatricesAndVisibleArea();
}

// Return sun angle, applying UI slider override when in Graphics or ImGui mode
float Camera::SunAngle(bool bInternalOnly) const
{
	if (bInternalOnly)
	{
		return mfSunAngle;
	}

	// Apply time of day slider override when in Graphics or Tweaks UI
	bool bUseOverride = (game::gpGame->meUiState == game::UiState::kGraphics);
	if constexpr (kbEnableDebugInput)
	{
		bUseOverride = bUseOverride || game::gpGame->mbShowImGui;
	}
	if (bUseOverride)
	{
		return engine::gSunAngleOverride.Get();
	}
	return mfSunAngle;
}

} // namespace game

#endif // BT_CLIENT
