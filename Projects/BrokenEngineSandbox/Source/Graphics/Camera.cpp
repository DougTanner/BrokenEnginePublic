#include "Camera.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "Frame/Frame.h"
#include "Frame/Collections/Players/Players.h"
#include "Ui/GraphicsSettingsWrappersBase.h"

namespace
{
	// TEMP zoom-stutter diagnostic — remove once root cause is identified
	common::DiagnosticLog gZoomStutterDiag(0, "Temp/ZoomStutterDiag.log");
	bool gbZoomStutterHeaderWritten = false;
}

namespace game
{

// Mouse-wheel zoom: per-frame scroll delta nudges target height; current eases toward target
constexpr float kfEyeHeightPerWheelTick = 0.1f;
constexpr float kfEyeHeightBlend = 4.0f;
constexpr float kfEyeHeightMin = 150.0f;
constexpr float kfEyeHeightMax = 600.0f;

constexpr float kfCameraPositionBlend = 8.0f;
constexpr float kfJumpDistanceThreshold = 50.0f;
constexpr float kfJumpDuration = 2.0f;
constexpr float kfJumpCancelThreshold = 5.0f;

constexpr float Smoothstep(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

Camera::Camera()
{
	gpCamera = this;

	mVecPosition = XMVectorAdd(XMVectorAdd(kVecMenuIslandCenter, kVecMenuCameraOffset), XMVectorSet(0.0f, 0.0f, engine::gBaseHeight.Get(), 0.0f));
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
	// Use the wall-clock dt the engine just measured for player interpolation. Driving camera blend,
	// shake decay, and mfTime off the same source keeps the camera in sync with the interpolated player
	// across vsync misses — otherwise the player advances by the actual wall delta while the camera
	// advances by a fixed 1/refreshRate, producing visible relative stutter at high zoom.
	float fDeltaTime = static_cast<float>(gpGame->mfLastRenderFrameSeconds);
	mfTime += fDeltaTime;

	// TEMP zoom-stutter diagnostic — capture state across all branches
	XMVECTOR vecPlayerPosLogged {};
	bool bHasPlayerLogged = false;
	float fAdaptiveBlendLogged = 0.0f;
	float fBlendLogged = 0.0f;
	float fJumpTLogged = 0.0f;

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
			mfSunAngle = mfSunAngle + rFrameInterpolate.fDeltaTime * 0.025f;
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

	// Free camera: WASD in main menu (debug only). Bypasses target/blend; W=0 movement vector preserves position W=1.
	bool bFreeCameraActive = false;
	if constexpr (kbFreeCamera)
	{
		if (rFrameInterpolate.gameFlags & GameFlags::kMainMenu)
		{
			bFreeCameraActive = true;
			XMVECTOR vecMove = XMVectorSet(gpInput->mCameraInput.f2Move.x, gpInput->mCameraInput.f2Move.y, 0.0f, 0.0f);
			constexpr float kfFreeCameraSpeed = 200.0f;
			mVecPosition = XMVectorAdd(mVecPosition, XMVectorScale(vecMove, kfFreeCameraSpeed * fDeltaTime));
			mVecPreviousTargetPosition = mVecPosition;
			mbJumping = false;
		}
	}

	// Calculate target position based on menu or game mode
	XMVECTOR vecTargetPosition {};
	if (bFreeCameraActive)
	{
		vecTargetPosition = mVecPosition;
	}
	else if (rFrameInterpolate.gameFlags & GameFlags::kMainMenu)
	{
		vecTargetPosition = XMVectorAdd(XMVectorAdd(kVecMenuIslandCenter, kVecMenuCameraOffset), XMVectorSet(0.0f, 0.0f, engine::gBaseHeight.Get(), 0.0f));
	}
	else if (gpGame->ClientPlayerId().IsValid())
	{
		auto coordIt = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
		bool bHasCoord = coordIt != gpGame->mCoordFrames.end() && coordIt->second.iSnapshotCount > 0;
		std::optional<int64_t> oIdx = bHasCoord ? gpGame->ClientPlayerIndex(*gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers) : std::nullopt;
		if (oIdx)
		{
			engine::global_id_t focusedId = gpGame->ClientPlayerId();
			XMVECTOR vecPlayerPos = rFrameInterpolate.pPlayers->pVecPositions[*oIdx];

			if (focusedId != mLastTrackedPlayerId)
			{
				LOG(kGraphics, kVerbose, "Camera NowTracking GlobalPlayerId: {} Coord: ({},{}) Index: {}", focusedId, gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, *oIdx);
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

			// TEMP zoom-stutter diagnostic
			vecPlayerPosLogged = vecPlayerPos;
			bHasPlayerLogged = true;
		}
		else
		{
			engine::global_id_t focusedId = gpGame->ClientPlayerId();
			static float sfLastLogTime = -1.0f;
			if (mfTime - sfLastLogTime >= 1.0f)
			{
				sfLastLogTime = mfTime;
				if (bHasCoord)
				{
					const PlayersPostRender& rPlayers = *gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers;
					LOG(kGraphics, kVerbose, "Camera PlayerNotFound FocusedGlobalId: {} Coord: ({},{}) PostRenderCount: {} InterpolateCount: {}",
						focusedId, gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, rPlayers.iCount, rFrameInterpolate.pPlayers->iCount);
					for (int64_t i = 0; i < rPlayers.iCount; ++i)
					{
						LOG(kGraphics, kVerbose, "  PostRender[{}] GlobalPlayerId: {}", i, rPlayers.pGlobalPlayerIds[i]);
					}
				}
				else
				{
					LOG(kGraphics, kVerbose, "Camera CoordNotFound FocusedGlobalId: {} Coord: ({},{})", focusedId, gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y);
				}
			}

			// Player not found — extrapolate from last known position and velocity
			float fElapsedTime = std::clamp(mfTime - mfLastKnownPlayerTime, 0.0f, 2.0f);
			vecTargetPosition = XMVectorMultiplyAdd(XMVectorReplicate(fElapsedTime), mVecLastKnownPlayerVelocity, mVecLastKnownPlayerPosition);
		}
	}
	else
	{
		// No fleet found for this client — fall back to the canonical menu pose so we don't strand
		// the camera at whatever stale gameplay position last set mVecPosition.
		vecTargetPosition = XMVectorAdd(XMVectorAdd(kVecMenuIslandCenter, kVecMenuCameraOffset), XMVectorSet(0.0f, 0.0f, engine::gBaseHeight.Get(), 0.0f));
	}

	// Detect target switch during jump: target jumped far from where it was last frame
	float fDistanceToTarget = XMVectorGetX(XMVector2Length(XMVectorSubtract(vecTargetPosition, mVecPosition)));
	float fTargetShift = XMVectorGetX(XMVector2Length(XMVectorSubtract(vecTargetPosition, mVecPreviousTargetPosition)));
	if (mbJumping && fTargetShift > kfJumpDistanceThreshold)
	{
		mVecJumpStartPosition = mVecPosition;
		mfJumpStartTime = mfTime;
	}
	else if (!mbJumping && fDistanceToTarget > kfJumpDistanceThreshold)
	{
		mbJumping = true;
		mVecJumpStartPosition = mVecPosition;
		mfJumpStartTime = mfTime;
	}

	if (mbJumping)
	{
		if (fDistanceToTarget < kfJumpCancelThreshold)
		{
			mbJumping = false;
		}
		else
		{
			float fElapsed = mfTime - mfJumpStartTime;
			if (fElapsed >= kfJumpDuration)
			{
				mbJumping = false;
				mVecPosition = vecTargetPosition;
			}
			else
			{
				float fT = Smoothstep(fElapsed / kfJumpDuration);
				mVecPosition = XMVectorLerp(mVecJumpStartPosition, vecTargetPosition, fT);
				fJumpTLogged = fT; // TEMP zoom-stutter diagnostic
			}
		}
	}

	if (!mbJumping)
	{
		// Tighten chase as eye descends so close-up tracking doesn't show pixel-space stutter
		float fAdaptiveBlend = kfCameraPositionBlend * std::max(1.0f, kfCameraEyeHeightDefault / mfCameraEyeHeight);
		float fBlend = std::clamp(fDeltaTime * fAdaptiveBlend, 0.0f, 1.0f);
		mVecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fBlend), vecTargetPosition, XMVectorMultiply(XMVectorReplicate(1.0f - fBlend), mVecPosition));
		// TEMP zoom-stutter diagnostic
		fAdaptiveBlendLogged = fAdaptiveBlend;
		fBlendLogged = fBlend;
	}

	mVecPreviousTargetPosition = vecTargetPosition;

	int iScrollDelta = gpInput->mCameraInput.iScrollDelta;
	mfCameraEyeHeightTarget = std::clamp(mfCameraEyeHeightTarget - static_cast<float>(iScrollDelta) * kfEyeHeightPerWheelTick, kfEyeHeightMin, kfEyeHeightMax);

	float fEyeBlend = std::clamp(fDeltaTime * kfEyeHeightBlend, 0.0f, 1.0f);
	mfCameraEyeHeight += (mfCameraEyeHeightTarget - mfCameraEyeHeight) * fEyeBlend;
	// DT: TEMP
	// mfCameraEyeHeight = mfCameraEyeHeightTarget;

	// Eye sits directly above target along +Z (straight-down view).
	// W=0 — eye-local offset, not a homogeneous point; added to mVecPosition (W=1) preserves position.
	auto vecEyePositionRelative = XMVectorSet(0.0f, 0.0f, mfCameraEyeHeight, 0.0f);
	mVecToEyeNormal = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	mVecEyePosition = XMVectorAdd(mVecPosition, vecEyePositionRelative);

	// Set controller vibration based on camera shake
	float fVibration = std::pow(mfShake, 0.5f);
	engine::gpRawInputManager->SetVibration(0, fVibration, fVibration);

	// Calculate matrices and visible area
	CalculateMatricesAndVisibleArea();

	// TEMP zoom-stutter diagnostic — emit one CSV row per frame to Temp/ZoomStutterDiag.log
	if (!gbZoomStutterHeaderWritten)
	{
		FILE_LOG(0, "frame,tick,mfTime,interpDt,eyeHeight,hasPlayer,playerX,playerY,errOffX,errOffY,tgtX,tgtY,camX,camY,jumping,jumpT,adaptiveBlend,blend,visAreaX,visAreaY,visAreaZ,visAreaW,quadX,quadY");
		gbZoomStutterHeaderWritten = true;
	}
	static int siZoomStutterFrame = 0;
	FILE_LOG(0, "{},{},{:.6f},{:.6f},{:.3f},{},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{},{:.4f},{:.4f},{:.4f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f}",
		siZoomStutterFrame, miFrame, mfTime, rFrameInterpolate.fDeltaTime, mfCameraEyeHeight,
		bHasPlayerLogged ? 1 : 0,
		XMVectorGetX(vecPlayerPosLogged), XMVectorGetY(vecPlayerPosLogged),
		XMVectorGetX(gpGame->mVecVisualErrorOffset), XMVectorGetY(gpGame->mVecVisualErrorOffset),
		XMVectorGetX(vecTargetPosition), XMVectorGetY(vecTargetPosition),
		XMVectorGetX(mVecPosition), XMVectorGetY(mVecPosition),
		mbJumping ? 1 : 0, fJumpTLogged,
		fAdaptiveBlendLogged, fBlendLogged,
		f4RenderVisibleArea.x, f4RenderVisibleArea.y, f4RenderVisibleArea.z, f4RenderVisibleArea.w,
		f2VisibleAreaQuadSize.x, f2VisibleAreaQuadSize.y);
	++siZoomStutterFrame;

	// Persist zoom-target changes (and any focus changes that came through unhooked paths). Diff-checked, so no-op on most frames.
	gpGame->CaptureClientStateAndSaveIfChanged();
}

// Return sun angle, applying UI slider override when in Graphics or ImGui mode
float Camera::SunAngle() const
{
	// Apply time of day slider override when in Graphics or Tweaks UI
	bool bUseOverride = (game::gpGame->meUiState == game::UiState::kGraphicsSettings);
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
