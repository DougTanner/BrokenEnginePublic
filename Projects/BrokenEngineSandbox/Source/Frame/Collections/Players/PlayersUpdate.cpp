#include "Players.h"

#include "Frame/HealthDamage.h"
#include "Frame/TerrainUtils.h"
#include "Profile/ProfileManager.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Frame/SmokeSpreadTest.h"

#if defined(BT_CLIENT)
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#include "Data/Scene.h"
#endif

#include "Data/Audio.h"

namespace game
{

thread_local int64_t PlayersPostRender::siCollisionLayerIndex = 0;

using enum PlayerFlags;

// Interpolate update
constexpr float kfRotateTowardsSpeed = 10.0f;
constexpr float kfWantedDirectionSpeed = 15.0f;
#if defined(BT_CLIENT)
constexpr float kfShieldShrinkSpeed = 1.5f;
constexpr float kfShieldRotationSpeed = 4.0f;
constexpr float kfHexShieldIntensityDecay = 1.25f;

// Hex shield rendering
constexpr float kfHexShieldLightingIntensity = 125.0f;
constexpr float kfHexShieldSizeScale = 0.1f;
constexpr float kfHexShieldColorMix = 0.85f;

// Player model (defined in PlayersRender.cpp)
extern const common::crc_t kPlayerModel;

// Rotation tilt
constexpr float kfRotationTiltFactor = 0.015f;
constexpr float kfRotationTiltMax = 0.4f;
#endif // BT_CLIENT

// Player movement
constexpr float kfAccelerationDecay = 3.0f;
constexpr float kfAcceleration = 100.0f;

// Terrain collision
constexpr float kfPlayerRadius = 1.5f;
constexpr float kfPushMargin = 1.0f;
constexpr float kfTerrainPushVelocity = 15.0f;
constexpr float kfMaxPushVelocity = 20.0f;

// Damage response
constexpr float kfShieldHitSoundVolumeBase = 0.1f;
constexpr float kfShieldHitSoundVolumeScale = 0.1f;
constexpr float kfShieldCooldown = 2.0f;
constexpr float kfShieldDownSoundCooldown = 2.0f;
constexpr float kfShieldDownSoundVolume = 0.1f;
constexpr float kfArmorHitSoundDamageThreshold = 3.0f;
constexpr float kfArmorHitSoundVolumeBase = 0.2f;
constexpr float kfArmorHitSoundVolumeScale = 0.5f;

// AI behavior
constexpr float kfBurstDuration = 0.5f;
constexpr float kfBurstCooldown = 0.5f;
constexpr float kfMissileBurstDuration = 0.4f;
constexpr float kfMissileBurstCooldown = 3.6f;
constexpr float kfTargetRange = 80.0f;

[[nodiscard]] static bool XM_CALLCONV HasLineOfSight(FXMVECTOR vecFrom, FXMVECTOR vecTo)
{
	static constexpr float kfStepInterval = 8.0f;
	static constexpr float kfBlockingElevation = 0.4f;

	XMVECTOR vecDelta = XMVectorSubtract(vecTo, vecFrom);
	float fDistance = XMVectorGetX(XMVector3Length(vecDelta));
	int64_t iSteps = static_cast<int64_t>(fDistance / kfStepInterval);
	if (iSteps <= 0)
	{
		return true;
	}

	XMVECTOR vecStep = vecDelta / static_cast<float>(iSteps);
	XMVECTOR vecCurrent = vecFrom;
	for (int64_t k = 1; k < iSteps; ++k)
	{
		vecCurrent = XMVectorAdd(vecCurrent, vecStep);
		if (engine::gpIslandTerrain->GlobalElevation(vecCurrent) > kfBlockingElevation)
		{
			return false;
		}
	}
	return true;
}

void PlayersInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	PlayersInterpolate& rCurrent = *rFrameInterpolate.pPlayers;
	const PlayersInterpolate& rPrevious = *rPreviousFrame.interpolate.pPlayers;
	const PlayersPostRender& rPreviousPostRender = *rPreviousFrame.postRender.pPlayers;
	float fDeltaTime = rFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		XMVECTOR vecDirection = rPrevious.pVecDirections[i];
		float fDestroyedTime = rPrevious.pfDestroyedTimes[i];
		float fAnimationTime = rPrevious.pfAnimationTimes[i];

		// Position
		if (!(rPreviousPostRender.pFlags[i] & kExploding)) [[likely]]
		{
			vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPreviousPostRender.pVecVelocities[i], vecPosition);
		}
		vecPosition = XMVectorSetZ(vecPosition, engine::gBaseHeight.Get());

		// Direction
		vecDirection = common::RotateTowardsPercent(vecDirection, rPreviousPostRender.pVecWantedDirections[i], common::ExponentialInterpolant(kfRotateTowardsSpeed, fDeltaTime));

		// Death countdown
		if (rPreviousPostRender.pFlags[i] & kExploding) [[unlikely]]
		{
			fDestroyedTime = std::max(fDestroyedTime - fDeltaTime, 0.0f);
		}

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;
		rCurrent.pfDestroyedTimes[i] = fDestroyedTime;
		rCurrent.pfAnimationTimes[i] = fAnimationTime;

#if defined(BT_CLIENT)
		// Rotation tilt from velocity
		float fRotationAccelerationX = std::clamp(kfRotationTiltFactor * XMVectorGetX(rPreviousPostRender.pVecVelocities[i]), -kfRotationTiltMax, kfRotationTiltMax);
		float fRotationAccelerationY = std::clamp(-kfRotationTiltFactor * XMVectorGetY(rPreviousPostRender.pVecVelocities[i]), -kfRotationTiltMax, kfRotationTiltMax);
		rCurrent.pfRotationAccelerationXs[i] = fRotationAccelerationX;
		rCurrent.pfRotationAccelerationYs[i] = fRotationAccelerationY;

		// Hex shield animation
		float fShieldRotation = rPrevious.pfShieldRotations[i];
		float fShieldShrink = rPrevious.pfShieldShrinks[i];
		fShieldRotation += fDeltaTime * kfShieldRotationSpeed;
		fShieldShrink = std::clamp(fShieldShrink + (rPreviousPostRender.pfShields[i] > 0.0f ? fDeltaTime * kfShieldShrinkSpeed : -fDeltaTime * kfShieldShrinkSpeed), 0.0f, 1.0f);
		rCurrent.pfShieldRotations[i] = fShieldRotation;
		rCurrent.pfShieldShrinks[i] = fShieldShrink;

		// Animation time (only if model has skeletal animation)
		if (engine::gAnimationDataMap.contains(kPlayerModel))
		{
			const engine::AnimationData& rAnimationData = engine::gAnimationDataMap.at(kPlayerModel);
			float fAnimationDuration = rAnimationData.mpAnimations[0].fDuration;
			fAnimationTime += fDeltaTime;
			if (fAnimationTime >= fAnimationDuration)
			{
				fAnimationTime = std::fmod(fAnimationTime, fAnimationDuration);
			}
			rCurrent.pfAnimationTimes[i] = fAnimationTime;
		}

		// Sync wind trail
		if (rCurrent.pWindTrails[i].IsValid())
		{
			engine::WindTrailsInterpolate::Sync(rFrameInterpolate, rCurrent.pWindTrails[i],
			{
				.vecPosition = vecPosition,
				.fIntensity = engine::gWindDepositPlayerIntensity.Get(),
				.fWidth = engine::gWindDepositPlayerWidth.Get(),
				.fLengthMultiplier = engine::gWindDepositPlayerLengthMultiplier.Get(),
			});
		}

		// Copy and decay hex shield direction intensities
		HexShieldDirections hexShieldDirections = rPrevious.pHexShieldDirections[i];
		HexShieldIntensities hexShieldVertIntensities {};
		HexShieldIntensities hexShieldFragIntensities {};
		for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
		{
			hexShieldVertIntensities.data[j] = std::max(rPrevious.pHexShieldVertIntensities[i].data[j] - kfHexShieldIntensityDecay * fDeltaTime, 0.0f);
			hexShieldFragIntensities.data[j] = std::max(rPrevious.pHexShieldFragIntensities[i].data[j] - kfHexShieldIntensityDecay * fDeltaTime, 0.0f);
		}
		rCurrent.pHexShieldDirections[i] = hexShieldDirections;
		rCurrent.pHexShieldVertIntensities[i] = hexShieldVertIntensities;
		rCurrent.pHexShieldFragIntensities[i] = hexShieldFragIntensities;

		// Sync hex shield to engine collection (if exists and not exploding)
		if (rCurrent.pHexShields[i].IsValid() && !(rPreviousPostRender.pFlags[i] & kExploding))
		{
			// Build transform (rotation around Z)
			XMMATRIX matRotation = XMMatrixRotationZ(rCurrent.pfShieldRotations[i]);
			XMFLOAT3X4 f3x4Transform {};
			XMFLOAT3X4 f3x4TransformNormal {};
			XMStoreFloat3x4(&f3x4Transform, matRotation);
			XMStoreFloat3x4(&f3x4TransformNormal, XMMatrixTranspose(XMMatrixInverse(nullptr, matRotation)));

			// Build SyncData
			engine::HexShieldsInterpolate::SyncData syncData
			{
				.vecPosition = rCurrent.pVecPositions[i],
				.pf4Transforms =
				{
					{f3x4Transform._11, f3x4Transform._12, f3x4Transform._13, f3x4Transform._14},
					{f3x4Transform._21, f3x4Transform._22, f3x4Transform._23, f3x4Transform._24},
					{f3x4Transform._31, f3x4Transform._32, f3x4Transform._33, f3x4Transform._34},
				},
				.pf4TransformNormals =
				{
					{f3x4TransformNormal._11, f3x4TransformNormal._12, f3x4TransformNormal._13, f3x4TransformNormal._14},
					{f3x4TransformNormal._21, f3x4TransformNormal._22, f3x4TransformNormal._23, f3x4TransformNormal._24},
					{f3x4TransformNormal._31, f3x4TransformNormal._32, f3x4TransformNormal._33, f3x4TransformNormal._34},
				},
				.pf4Directions = {},
				.pfVertIntensities = {},
				.pfFragIntensities = {},
				.fLightingIntensity = kfHexShieldLightingIntensity,
				.fSize = rCurrent.pfShieldShrinks[i] * kfHexShieldSizeScale,
				.fColorMix = kfHexShieldColorMix,
			};

			// Copy direction arrays
			for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
			{
				syncData.pf4Directions[j] = rCurrent.pHexShieldDirections[i].data[j];
				syncData.pfVertIntensities[j] = rCurrent.pHexShieldVertIntensities[i].data[j];
				syncData.pfFragIntensities[j] = rCurrent.pHexShieldFragIntensities[i].data[j];
			}

			engine::HexShieldsInterpolate::Sync(rFrameInterpolate, rCurrent.pHexShields[i], syncData);
		}
#endif // BT_CLIENT
	}
}

void PlayersPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	PlayersPostRender& __restrict rCurrent = *rFrame.postRender.pPlayers;
	const PlayersPostRender& rPrevious = *rPreviousFrame.postRender.pPlayers;
	const PlayersInterpolate& rPreviousInterpolate = *rPreviousFrame.interpolate.pPlayers;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	// Spaceship data for target acquisition
	const SpaceshipsInterpolate& rSpaceshipsInterpolate = *rPreviousFrame.interpolate.pSpaceships;
	int64_t iSpaceshipCount = rPreviousFrame.postRender.pSpaceships->iCount;

	// Frame area and center
	XMVECTOR vecArea = rFrame.postRender.vecArea;
	XMVECTOR vecFrameCenter = XMVectorSet((XMVectorGetX(vecArea) + XMVectorGetZ(vecArea)) * 0.5f, (XMVectorGetW(vecArea) + XMVectorGetY(vecArea)) * 0.5f, 0.0f, 0.0f);

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		PlayerFlags_t flags = rPrevious.pFlags[i];
		float fNextBlasterFireTime = rPrevious.pfNextBlasterFireTimes[i];
		float fNextSecondarySpawnTime = rPrevious.pfNextSecondarySpawnTimes[i];
		XMVECTOR vecVelocity = rPrevious.pVecVelocities[i];
		XMVECTOR vecWantedDirection = rPrevious.pVecWantedDirections[i];
		float fArmor = rPrevious.pfArmors[i];
		float fShield = rPrevious.pfShields[i];
		float fShieldCooldown = rPrevious.pfShieldCooldowns[i] - fDeltaTime;
		float fDestroyedExplosionTime = rPrevious.pfDestroyedExplosionTimes[i] - fDeltaTime;
		float fShieldDownSoundCooldown = rPrevious.pfShieldDownSoundCooldowns[i] - fDeltaTime;
		XMVECTOR vecAiDirection = rPrevious.pVecAiDirections[i];
		float fAiFireTimer = rPrevious.pfAiFireTimers[i];
		float fAiMissileTimer = rPrevious.pfAiMissileTimers[i];
		float fAiEdgeCrossCooldown = rPrevious.pfAiEdgeCrossCooldowns[i];
		int8_t iAiEdgeCrossTarget = rPrevious.piAiEdgeCrossTargets[i];
		float fTransferLockTimer = rPrevious.pfTransferLockTimers[i];
		XMVECTOR vecPosition = rPreviousInterpolate.pVecPositions[i];

		// Transfer lock: maintain constant velocity, skip AI and weapon logic
		if (fTransferLockTimer > 0.0f)
		{
			fTransferLockTimer -= fDeltaTime;
		}
		else
		{
			// --- AI computation ---

			// Initialize direction if zero (first spawn or after reset)
			if (XMVectorGetX(XMVector3LengthSq(vecAiDirection)) < 0.001f)
			{
				float fAngle = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
				vecAiDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngle));
			}

			auto [vecNewAiDirection, fNewEdgeCrossCooldown, iNewEdgeCrossTarget] = ComputeAiSteering(vecPosition, vecAiDirection, vecArea, vecFrameCenter, fDeltaTime, fAiEdgeCrossCooldown, iAiEdgeCrossTarget, i % 2 == 0);
			vecAiDirection = vecNewAiDirection;
			fAiEdgeCrossCooldown = fNewEdgeCrossCooldown;
			iAiEdgeCrossTarget = iNewEdgeCrossTarget;

			// Find nearest alive spaceship
			float fClosestDistance = kfTargetRange;
			XMVECTOR vecClosestPosition = XMVectorZero();
			bool bTargetFound = false;

			for (int64_t j = 0; j < iSpaceshipCount; ++j)
			{
				if (rSpaceshipsInterpolate.pfDestroyedTimes[j] != -1.0f)
				{
					continue;
				}

				if (!FrameInterpolate::IsVisible(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]))
				{
					continue;
				}

				float fDistance = common::Distance(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]);
				if (fDistance < fClosestDistance)
				{
					if (!HasLineOfSight(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]))
					{
						continue;
					}

					fClosestDistance = fDistance;
					vecClosestPosition = rSpaceshipsInterpolate.pVecPositions[j];
					bTargetFound = true;
				}
			}

			// Manage burst timer
			fAiFireTimer -= fDeltaTime;
			if (fAiFireTimer <= 0.0f && bTargetFound)
			{
				fAiFireTimer = kfBurstDuration + kfBurstCooldown;
			}

			bool bFiring = fAiFireTimer > kfBurstCooldown && bTargetFound;

			// Manage missile timer
			fAiMissileTimer -= fDeltaTime;
			if (fAiMissileTimer <= 0.0f && bTargetFound)
			{
				fAiMissileTimer = kfMissileBurstDuration + kfMissileBurstCooldown;
			}

			bool bFiringMissiles = fAiMissileTimer > kfMissileBurstCooldown && bTargetFound;

			// --- Apply AI results ---

			// Fire flags
			if (bFiring)
			{
				flags.Set(kFireBlaster);
			}
			else
			{
				fNextBlasterFireTime = 0.0f;
			}

			if (bFiringMissiles)
			{
				flags.Set(kFireMissile);
			}

			// Apply movement: decay existing velocity and add acceleration from AI direction
			XMVECTOR vecAcceleration = XMVectorMultiply(XMVectorReplicate(fDeltaTime * kfAcceleration), vecAiDirection);
			vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(common::ExponentialDecay(kfAccelerationDecay, fDeltaTime)), vecVelocity, vecAcceleration);

			// Direction: face target if one exists, else face travel direction
			XMVECTOR vecTargetDirection = bTargetFound
				? XMVector3Normalize(XMVectorSubtract(vecClosestPosition, vecPosition))
				: (XMVectorGetX(XMVector3LengthSq(vecVelocity)) > 0.001f ? XMVector3Normalize(vecVelocity) : vecAiDirection);
			vecWantedDirection = common::RotateTowardsPercent(vecWantedDirection, vecTargetDirection, common::ExponentialInterpolant(kfWantedDirectionSpeed, fDeltaTime));

			if constexpr (kbEnableSmokeSpreadTest)
			{
				SmokeSpreadTestUpdatePlayer(vecVelocity, vecWantedDirection, flags, vecPosition);
			}
		}

		// Shield regeneration
		if (fShieldCooldown <= 0.0f)
		{
			fShield = std::min(fShield + fDeltaTime * kfPlayerShieldRegen, kfPlayerShield);
		}

		// Terrain collision - add velocity away from terrain, gentle at first then ramping up
		float fElevation = engine::gpIslandTerrain->GlobalElevation(vecPosition);
		float fPushHeight = engine::gBaseHeight.Get() - kfPlayerRadius - kfPushMargin;
		if (fElevation >= fPushHeight) [[unlikely]]
		{
			XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslandTerrain->GlobalNormal(vecPosition), 0.0f));
			float fPenetration = fElevation - fPushHeight;
			float fPushStrength = fPenetration * fPenetration * kfTerrainPushVelocity;

			// Cap velocity in push direction
			float fCurrentPushVelocity = XMVectorGetX(XMVector3Dot(vecVelocity, vecTerrainNormal));
			float fAllowedPush = std::max(kfMaxPushVelocity - fCurrentPushVelocity, 0.0f);
			fPushStrength = std::min(fPushStrength, fAllowedPush);

			vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fPushStrength), vecTerrainNormal, vecVelocity);
		}

		// Save
		rCurrent.pFlags[i] = flags;
		rCurrent.pfNextBlasterFireTimes[i] = fNextBlasterFireTime;
		rCurrent.pfNextSecondarySpawnTimes[i] = fNextSecondarySpawnTime;
		rCurrent.pVecVelocities[i] = vecVelocity;
		rCurrent.pVecWantedDirections[i] = vecWantedDirection;
		rCurrent.pfArmors[i] = fArmor;
		rCurrent.pfShields[i] = fShield;
		rCurrent.pfShieldCooldowns[i] = fShieldCooldown;
		rCurrent.pfDestroyedExplosionTimes[i] = fDestroyedExplosionTime;
		rCurrent.pfShieldDownSoundCooldowns[i] = fShieldDownSoundCooldown;
		rCurrent.pVecAiDirections[i] = vecAiDirection;
		rCurrent.pfAiFireTimers[i] = fAiFireTimer;
		rCurrent.pfAiMissileTimers[i] = fAiMissileTimer;
		rCurrent.pfAiEdgeCrossCooldowns[i] = fAiEdgeCrossCooldown;
		rCurrent.piAiEdgeCrossTargets[i] = iAiEdgeCrossTarget;
		rCurrent.pfTransferLockTimers[i] = fTransferLockTimer;
	}
}

void PlayersPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

// Player collision arrays
// thread_local: parallel per-Frame tick via Dispatch
static thread_local std::vector<float> sCollisionRadii;
static thread_local std::vector<float> sCollisionDamages;
static thread_local std::vector<engine::CollisionFlags_t> sCollisionFlags;

void PlayersPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	// Build collision arrays
	size_t uiCount = static_cast<size_t>(rCurrentInterpolate.iCount);
	sCollisionRadii.resize(uiCount);
	sCollisionDamages.resize(uiCount);
	sCollisionFlags.resize(uiCount);
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		sCollisionRadii.at(static_cast<size_t>(i)) = kfPlayerRadius;
		sCollisionDamages.at(static_cast<size_t>(i)) = 0.0f; // Player doesn't deal collision damage
		sCollisionFlags.at(static_cast<size_t>(i)) = (rCurrentPostRender.pFlags[i] & kExploding) ? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided} : engine::CollisionFlags_t {};
	}

	// Add player layer to CollisionSystem
	siCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = rCurrentInterpolate.pVecPositions,
		.pfRadii = sCollisionRadii.data(),
		.pfDamages = sCollisionDamages.data(),
		.pFlags = sCollisionFlags.data(),
		.iCount = rCurrentInterpolate.iCount,
		.uiCategory = CollisionCategory::kPlayer,
		.uiCollidesWith = CollidesWith::kPlayer,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

static void XM_CALLCONV ApplyDamage([[maybe_unused]] const Frame& rFrame, [[maybe_unused]] PlayersInterpolate& rPlayerInterpolate, PlayersPostRender& rPlayer, int64_t i, float fDamage, [[maybe_unused]] FXMVECTOR vecDamagePosition, [[maybe_unused]] float fHexShieldIntensity = 1.0f)
{
	// Shield absorbs damage first
	if (rPlayer.pfShields[i] > 0.0f)
	{
		// Play shield hit sound with pitch based on remaining shield
#if defined(BT_CLIENT)
		engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioShieldArmor465540__steaq__scifishieldhitwavwavCrc, vecDamagePosition, kfShieldHitSoundVolumeBase + kfShieldHitSoundVolumeScale * (1.0f - rPlayer.pfShields[i] / kfPlayerShield));
#endif

		// Update hex shield direction intensity
#if defined(BT_CLIENT)
		// Find lowest intensity direction slot
		int64_t iLowestIntensityIndex = 0;
		for (int64_t k = 1; k < shaders::kiHexShieldDirections; ++k)
		{
			if (rPlayerInterpolate.pHexShieldFragIntensities[i].data[k] < rPlayerInterpolate.pHexShieldFragIntensities[i].data[iLowestIntensityIndex])
			{
				iLowestIntensityIndex = k;
			}
		}
		// Store damage direction and intensity
		XMVECTOR vecDamageDirection = XMVector3Normalize(XMVectorSubtract(vecDamagePosition, rPlayerInterpolate.pVecPositions[i]));
		XMStoreFloat4(&rPlayerInterpolate.pHexShieldDirections[i].data[iLowestIntensityIndex], vecDamageDirection);
		rPlayerInterpolate.pHexShieldVertIntensities[i].data[iLowestIntensityIndex] = fHexShieldIntensity;
		rPlayerInterpolate.pHexShieldFragIntensities[i].data[iLowestIntensityIndex] = fHexShieldIntensity;
#endif // BT_CLIENT

		float fShieldDamage = std::min(rPlayer.pfShields[i], fDamage);
		rPlayer.pfShields[i] -= fShieldDamage;
		fDamage -= fShieldDamage;

		if (rPlayer.pfShields[i] <= 0.0f)
		{
			rPlayer.pfShieldCooldowns[i] = kfShieldCooldown;

			// Play shield down sound with cooldown to prevent spam
			if (rPlayer.pfShieldDownSoundCooldowns[i] <= 0.0f)
			{
				rPlayer.pfShieldDownSoundCooldowns[i] = kfShieldDownSoundCooldown;
#if defined(BT_CLIENT)
				engine::gpAudioManager->PlayOneShot(rFrame, data::kAudioShieldArmor570852__rafaelzimrp__magicshielddownwavCrc, false, kfShieldDownSoundVolume);
#endif
			}
		}
	}

	// Remaining damage goes to armor
	if (fDamage > 0.0f)
	{
		// Play armor hit sound with pitch based on remaining armor (only for significant damage)
#if defined(BT_CLIENT)
		if (fDamage > kfArmorHitSoundDamageThreshold)
		{
			engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioShieldArmor330629__stormwaveaudio__scififorcefieldimpact15wavCrc, vecDamagePosition, kfArmorHitSoundVolumeBase + kfArmorHitSoundVolumeScale * (1.0f - rPlayer.pfArmors[i] / kfPlayerArmor));
		}
#endif

		if constexpr (!kbEnableInvincibility)
		{
			rPlayer.pfArmors[i] -= fDamage;
		}
	}
}

void PlayersPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	const FrameBounds bounds = ComputeFrameBounds(rFrame.postRender.vecArea);

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding)
		{
			continue;
		}

		// Flag for transfer if outside frame boundaries (Transfer phase handles removal)
		if (IsOutOfBounds(bounds, rCurrentInterpolate.pVecPositions[i])) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i].Set(kTransfer);
			continue;
		}

		// Check collision results
		if (engine::Collision::HasCollision(siCollisionLayerIndex, i))
		{
			const std::vector<engine::CollisionResult>* pCollisions = engine::Collision::GetCollisions(siCollisionLayerIndex, i);
			for (const engine::CollisionResult& rResult : *pCollisions)
			{
				if (rResult.uiOtherCategory == CollisionCategory::kSpaceship)
				{
					ApplyDamage(rFrame, rCurrentInterpolate, rCurrentPostRender, i, kfSpaceshipCollisionDamage, rResult.vecContactPoint);
				}
				else if (rResult.uiOtherCategory == CollisionCategory::kBlaster)
				{
					ApplyDamage(rFrame, rCurrentInterpolate, rCurrentPostRender, i, rResult.fDamageReceived, rResult.vecContactPoint);

					// Spawn impact VFX at contact point
#if defined(BT_CLIENT)
					engine::PuffsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, PlayersInterpolate::suiImpactPuffControllerTypeIndex, rResult.vecContactPoint);
					engine::PointLightsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, PlayersInterpolate::suiImpactPointLightControllerTypeIndex, rResult.vecContactPoint, 0.0f);
#endif
				}
			}
		}

		// Check for death
		if (rCurrentPostRender.pfArmors[i] <= 0.0f)
		{
			rCurrentPostRender.pFlags[i].Set(kExploding);
			rCurrentInterpolate.pfDestroyedTimes[i] = kfDestroyTime;
			rCurrentPostRender.pfDestroyedExplosionTimes[i] = kfDestroyExplosionInterval;
		}
	}
}

} // namespace game
