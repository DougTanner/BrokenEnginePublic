#include "Camera.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "Frame/Frame.h"
#include "Frame/Collections/Players/Players.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/ShadowWrappersBase.h"

namespace game
{

// Mouse-wheel zoom: per-frame scroll delta nudges target height; current eases toward target
constexpr float kfEyeHeightPerWheelTick = 0.1f;
constexpr float kfEyeBlendDuration = 0.35f;
constexpr float kfEyeHeightMin = engine::kfMinEyeHeight;
#if defined(BT_RELEASE)
constexpr float kfEyeHeightMax = Camera::kfEyeHeightMaxRelease; // Shipping: gameplay zoom-out ceiling
#else
constexpr float kfEyeHeightMax = 2000.0f; // Dev: full zoom range (texels just coarsen further, coverage preserved)
#endif

constexpr float kfCameraPositionBlend = 8.0f;
constexpr float kfJumpDistanceThreshold = 50.0f;
constexpr float kfJumpDuration = 2.0f;
constexpr float kfJumpCancelThreshold = 5.0f;

static void UpdateTexelEyeHeightReference(float fLiveEyeHeight, float fContractionMetersPerSecond, float fDeltaTime, float& rfReferenceEyeHeight)
{
	if (rfReferenceEyeHeight == 0.0f || fLiveEyeHeight > rfReferenceEyeHeight)
	{
		rfReferenceEyeHeight = fLiveEyeHeight;
		return;
	}

	float fMaxContraction = fContractionMetersPerSecond * fDeltaTime;
	rfReferenceEyeHeight = std::max(rfReferenceEyeHeight - fMaxContraction, fLiveEyeHeight);
}

constexpr float Smoothstep(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

Camera::Camera()
{
	ASSERT(gpCamera == nullptr);

	gpCamera = this;

	mVecPosition = XMVectorAdd(XMVectorAdd(kVecMenuIslandCenter, kVecMenuCameraOffset), XMVectorSet(0.0f, 0.0f, engine::gBaseHeight.Get(), 0.0f));
}

Camera::~Camera()
{
	if (gpCamera == this)
	{
		gpCamera = nullptr;
	}
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

	// Decay camera shake using real-time
	mfShake = std::max(mfShake - fDeltaTime * 2.0f, 0.0f);

	miFrame = rFrameInterpolate.iTick;

	// Update sun angle with varying speeds (only during gameplay)
	if (!(rFrameInterpolate.gameFlags & GameFlags::kMainMenu))
	{
		static constexpr float kfNightSpeedStart = XM_PI;
		static constexpr float kfNightSpeedEnd = XM_2PI;
		if (mfSunAngle >= kfNightSpeedStart && mfSunAngle < kfNightSpeedEnd)
		{
			mfSunAngle = mfSunAngle + rFrameInterpolate.fDeltaTime * 0.075f;
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
		}
		else
		{
			static float sfLastLogTime = -1.0f;
			if (mfTime - sfLastLogTime >= 1.0f)
			{
				sfLastLogTime = mfTime;
				if (bHasCoord)
				{
					const PlayersPostRender& rPlayers = *gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers;
					LOG(kGraphics, kVerbose, "Camera PlayerNotFound FocusedGlobalId: {} Coord: ({},{}) PostRenderCount: {} InterpolateCount: {}",
						gpGame->ClientPlayerId(), gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, rPlayers.iCount, rFrameInterpolate.pPlayers->iCount);
					for (int64_t i = 0; i < rPlayers.iCount; ++i)
					{
						LOG(kGraphics, kVerbose, "  PostRender[{}] GlobalPlayerId: {}", i, rPlayers.pGlobalPlayerIds[i]);
					}
				}
				else
				{
					LOG(kGraphics, kVerbose, "Camera CoordNotFound FocusedGlobalId: {} Coord: ({},{})", gpGame->ClientPlayerId(), gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y);
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
			}
		}
	}

	if (!mbJumping)
	{
		// Scale chase rate linearly with eye height: slow/loose at low altitude (close-up view stays calm), fast/tight at altitude (zoomed-out stays responsive).
		float fAdaptiveBlend = kfCameraPositionBlend * (mfCameraEyeHeight / kfCameraEyeHeightDefault);
		float fBlend = std::clamp(fDeltaTime * fAdaptiveBlend, 0.0f, 1.0f);
		mVecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fBlend), vecTargetPosition, XMVectorMultiply(XMVectorReplicate(1.0f - fBlend), mVecPosition));
	}

	mVecPreviousTargetPosition = vecTargetPosition;

	const float fEyeHeightBeforeZoom = mfCameraEyeHeight;
	int iScrollDelta = gpInput->mCameraInput.iScrollDelta;
	if (iScrollDelta != 0)
	{
		// Scale per-tick zoom delta with current eye height, bounded by sqrt so high altitudes don't get 4x ticks and over-build Hermite velocity that carries into low-altitude territory.
		float fEyeHeightDelta = static_cast<float>(iScrollDelta) * kfEyeHeightPerWheelTick * std::sqrt(mfCameraEyeHeight / kfCameraEyeHeightDefault);
		float fNewTarget = std::clamp(mfCameraEyeHeightTarget - fEyeHeightDelta, kfEyeHeightMin, kfEyeHeightMax);
		if (fNewTarget != mfCameraEyeHeightTarget)
		{
			// Re-anchor every tick that actually moves the target. Snapshot current position AND velocity so the new Hermite curve picks up continuously, eliminating mid-flight stepping that an endpoint-shifted smoothstep would produce.
			mfEyeStartHeight = mfCameraEyeHeight;
			mfEyeStartVelocity = mfEyeVelocity;
			mfEyeStartTime = mfTime;
			mfCameraEyeHeightTarget = fNewTarget;
			mbEyeZooming = true;
		}
	}

	if (mbEyeZooming)
	{
		float fEyeElapsed = mfTime - mfEyeStartTime;
		if (fEyeElapsed >= kfEyeBlendDuration)
		{
			mfCameraEyeHeight = mfCameraEyeHeightTarget;
			mfEyeVelocity = 0.0f;
			mbEyeZooming = false;
		}
		else
		{
			// Cubic Hermite from (mfEyeStartHeight, mfEyeStartVelocity) to (mfCameraEyeHeightTarget, 0) over kfEyeBlendDuration.
			float fT = fEyeElapsed / kfEyeBlendDuration;
			float fT2 = fT * fT;
			float fT3 = fT2 * fT;
			float fH00 = 2.0f * fT3 - 3.0f * fT2 + 1.0f;
			float fH10 = fT3 - 2.0f * fT2 + fT;
			float fH01 = -2.0f * fT3 + 3.0f * fT2;
			mfCameraEyeHeight = fH00 * mfEyeStartHeight + fH10 * mfEyeStartVelocity * kfEyeBlendDuration + fH01 * mfCameraEyeHeightTarget;
			float fDH00 = 6.0f * fT2 - 6.0f * fT;
			float fDH10 = 3.0f * fT2 - 4.0f * fT + 1.0f;
			float fDH01 = -6.0f * fT2 + 6.0f * fT;
			mfEyeVelocity = (fDH00 * mfEyeStartHeight + fDH10 * mfEyeStartVelocity * kfEyeBlendDuration + fDH01 * mfCameraEyeHeightTarget) / kfEyeBlendDuration;
		}
	}

	// Lighting temporal history has no prior-valid-window bounds, so every frame with actual outward eye movement
	// uses pure-current lighting and re-seeds history after the enlarged world area is rendered. Compare the live
	// Hermite result rather than its target so carried velocity and direction reversals follow visible motion.
	if (mfCameraEyeHeight > fEyeHeightBeforeZoom)
	{
		engine::gbLightingTemporalReset = true;
	}

	// Keep each world-texel reference at or above the live eye height: zero initialization and outward zoom snap
	// immediately for full viewport coverage, while inward zoom contracts at the existing independent rates so the
	// density change remains gradual. At a settled height both references converge to the live height.
	UpdateTexelEyeHeightReference(mfCameraEyeHeight, engine::gShadowTexelRampMetersPerSec.Get(), fDeltaTime, mfShadowTexelEyeHeight);
	UpdateTexelEyeHeightReference(mfCameraEyeHeight, engine::gLightingTexelRampMetersPerSec.Get(), fDeltaTime, mfLightingTexelEyeHeight);

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
