#include "Camera.h"

#if defined(BT_CLIENT)

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
			float fNightEase = std::sin(mfSunAngle - XM_PI);
			mfSunAngle = mfSunAngle + rFrameInterpolate.fDeltaTime * (0.01f + fNightEase * 0.19f);
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
		vecTargetPosition = XMVectorAdd(kVecMainMenuPosition, XMVectorSet(0.0f, 0.0f, engine::gBaseHeight.Get(), 0.0f));
	}
	else if (gpGame->ClientPlayerId().IsValid())
	{
		auto coordIt = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
		bool bHasCoord = coordIt != gpGame->mCoordFrames.end() && coordIt->second.pCurrent != nullptr;
		std::optional<int64_t> oIdx = bHasCoord ? gpGame->ClientPlayerIndex(*gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers) : std::nullopt;
		if (oIdx)
		{
			// DT TEMP: Log when camera starts tracking a new player
			engine::global_id_t focusedId = gpGame->ClientPlayerId();
			XMVECTOR vecPlayerPos = rFrameInterpolate.pPlayers->pVecPositions[*oIdx];

			if (focusedId != mLastTrackedPlayerId)
			{
				Log(kVerbose, "Camera NowTracking GlobalPlayerId: {} Coord: ({},{}) Index: {}", focusedId.iValue, gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, *oIdx);
				mLastTrackedPlayerId = focusedId;
			}

			// Compute velocity from position delta using real time
			if (mfLastKnownPlayerTime > 0.0f && mfTime > mfLastKnownPlayerTime)
			{
				float fElapsedTime = mfTime - mfLastKnownPlayerTime;
				mVecLastKnownPlayerVelocity = XMVectorScale(XMVectorSubtract(vecPlayerPos, mVecLastKnownPlayerPosition), 1.0f / fElapsedTime);
			}

			mVecLastKnownPlayerPosition = vecPlayerPos;
			mfLastKnownPlayerTime = mfTime;
			vecTargetPosition = XMVectorAdd(vecPlayerPos, gpGame->mVecVisualErrorOffset);
		}
		else
		{
			// DT TEMP: Diagnostic logging for camera player lookup failure (throttled to once per second)
			engine::global_id_t focusedId = gpGame->ClientPlayerId();
			static float sfLastLogTime = -1.0f;
			if (mfTime - sfLastLogTime >= 1.0f)
			{
				sfLastLogTime = mfTime;
				if (bHasCoord)
				{
					const PlayersPostRender& rPlayers = *gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers;
					Log(kVerbose, "Camera PlayerNotFound FocusedGlobalId: {} Coord: ({},{}) PostRenderCount: {} InterpolateCount: {}",
						focusedId.iValue, gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, rPlayers.iCount, rFrameInterpolate.pPlayers->iCount);
					for (int64_t i = 0; i < rPlayers.iCount; ++i)
					{
						Log(kVerbose, "  PostRender[{}] GlobalPlayerId: {}", i, rPlayers.pGlobalPlayerIds[i].iValue);
					}
				}
				else
				{
					Log(kVerbose, "Camera CoordNotFound FocusedGlobalId: {} Coord: ({},{})", focusedId.iValue, gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y);
				}
			}

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
float Camera::SunAngle() const
{
	// Apply time of day slider override when in Graphics or Tweaks UI
	bool bUseOverride = (game::gpGame->meUiState == game::UiState::kGraphics);
	if constexpr (kbDebugInput)
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
