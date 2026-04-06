#include "Players.h"

#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
#include "Frame/NavQuery.h"
#include "Frame/TerrainUtils.h"
#include "Profile/ProfileManager.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
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
constexpr float kfTerrainPushVelocity = 15.0f;
constexpr float kfMaxPushVelocity = 20.0f;

// AI behavior
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
				.fIntensity = game::gWindDepositPlayerIntensity.Get(),
				.fWidth = game::gWindDepositPlayerWidth.Get(),
				.fLengthMultiplier = game::gWindDepositPlayerLengthMultiplier.Get(),
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

		// Debug: copy nav waypoint from PostRender for debug line rendering
		if constexpr (kbDebugRender)
		{
			rCurrent.pVecDebugNavDestinations[i] = rPreviousPostRender.pVecDebugNavWaypoints[i];
			rCurrent.pVecDebugIslandDestinations[i] = rPreviousPostRender.pVecIslandDestinations[i];
		}
#endif // BT_CLIENT
	}
}

void PlayersPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
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
	const SpaceshipsPostRender& rSpaceshipsPostRender = *rPreviousFrame.postRender.pSpaceships;
	int64_t iSpaceshipCount = rSpaceshipsPostRender.iCount;

	// Frame area and center
	XMVECTOR vecArea = rStaticData.vecArea;
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
		float fTransferLockTimer = rPrevious.pfTransferLockTimers[i];
		float fArrivalGracePeriod = std::max(0.0f, rPrevious.pfArrivalGracePeriods[i] - fDeltaTime);
		float fFrameChangeTimer = rPrevious.pfFrameChangeTimers[i];
		float fNavigationDelay = rPrevious.pfNavigationDelays[i];
		int8_t iNavDirection = rPrevious.piNavDirections[i];
		XMVECTOR vecIslandDestination = rPrevious.pVecIslandDestinations[i];
		XMVECTOR vecPosition = rPreviousInterpolate.pVecPositions[i];

		// Transfer lock: maintain constant velocity, skip AI and weapon logic
		if (fTransferLockTimer > 0.0f)
		{
			fTransferLockTimer -= fDeltaTime;
			Log(kDebug, "Player {} transferLock={}", i, fTransferLockTimer); // DT TEMP DEBUG
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

			// Frame change timer (only ticks while roaming)
			if (iNavDirection == -1)
			{
				fFrameChangeTimer -= fDeltaTime;
				if (fFrameChangeTimer <= 0.0f)
				{
					iNavDirection = static_cast<int8_t>(common::Random(3u, rFrame.postRender.randomEngine));
					Log(kLogNavData, kDebug, "Player {} GlobalId: {} started transition NavDir: {}", i, rCurrent.pGlobalPlayerIds[i].iValue, iNavDirection); // DT TEMP DEBUG
				}
			}

			if (iNavDirection == 4)
			{
				// Navigate to random island destination
				if (XMVectorGetW(vecIslandDestination) == 0.0f)
				{
					// Generate random point within island bounds
					float fIslandMinX = XMVectorGetX(vecArea) + rStaticData.f2IslandOffset.x;
					float fIslandMaxY = XMVectorGetY(vecArea) - rStaticData.f2IslandOffset.y;
					float fIslandMinY = fIslandMaxY - Frame::kfIslandHeight;

					float fX = fIslandMinX + common::Random<Frame::kfIslandWidth>(rFrame.postRender.randomEngine);
					float fY = fIslandMinY + common::Random<Frame::kfIslandHeight>(rFrame.postRender.randomEngine);
					vecIslandDestination = XMVectorSet(fX, fY, 0.0f, 1.0f);

					// Snap to navigable area if inside an obstacle
					vecIslandDestination = engine::NavQuerySnapToNavigable(vecIslandDestination, rStaticData.navData);
					vecIslandDestination = XMVectorSetW(vecIslandDestination, 1.0f);
					Log(kLogNavData, kDebug, "Player {} NavSwitch: island dest generated pos={} dest={}", i, vecPosition, vecIslandDestination); // DT TEMP DEBUG
				}

				XMVECTOR vecDebugWaypoint = XMVectorZero();
				XMVECTOR vecNavDirection = XMVectorZero();
				{
					engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdateNavQuery);
					vecNavDirection = engine::NavQueryDirection(vecPosition, vecIslandDestination, rStaticData.navData, &vecDebugWaypoint);
				}
				if (XMVectorGetX(XMVector3LengthSq(vecNavDirection)) > 0.001f)
				{
					vecAiDirection = vecNavDirection;
				}
				else
				{
					vecAiDirection = XMVector3Normalize(XMVectorSetZ(XMVectorSubtract(vecIslandDestination, vecPosition), 0.0f));
				}
				Log(kLogNavData, kDebug, "Player {} Nav4: pos={} dest={} waypoint={} dir={}", i, vecPosition, vecIslandDestination, vecDebugWaypoint, vecAiDirection); // DT TEMP DEBUG

				// Arrival check
				float fDistanceSquared = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vecPosition, vecIslandDestination)));
				if (fDistanceSquared < 100.0f)
				{
					Log(kLogNavData, kDebug, "Player {} NavSwitch: arrived at island dest pos={} dest={}", i, vecPosition, vecIslandDestination); // DT TEMP DEBUG
					iNavDirection = -1;
					fFrameChangeTimer = fNavigationDelay;
					vecIslandDestination = XMVectorZero();
				}
#if defined(BT_CLIENT)
				if constexpr (kbDebugRender)
				{
					rCurrent.pVecDebugNavWaypoints[i] = XMVectorSetW(vecDebugWaypoint, 1.0f);
				}
#endif // BT_CLIENT
			}
			else if (iNavDirection >= 0)
			{
				// Navigate toward neighboring frame center
				vecIslandDestination = XMVectorZero();
				XMVECTOR vecDestination = vecFrameCenter;
				switch (iNavDirection)
				{
					case 0:
						vecDestination = XMVectorAdd(vecFrameCenter, XMVectorSet(0.0f, Frame::kfCellHeight, 0.0f, 0.0f));
						break;
					case 1:
						vecDestination = XMVectorAdd(vecFrameCenter, XMVectorSet(0.0f, -Frame::kfCellHeight, 0.0f, 0.0f));
						break;
					case 2:
						vecDestination = XMVectorAdd(vecFrameCenter, XMVectorSet(Frame::kfCellWidth, 0.0f, 0.0f, 0.0f));
						break;
					case 3:
						vecDestination = XMVectorAdd(vecFrameCenter, XMVectorSet(-Frame::kfCellWidth, 0.0f, 0.0f, 0.0f));
						break;
				}

				XMVECTOR vecDebugWaypoint = XMVectorZero();
				XMVECTOR vecNavDirection = XMVectorZero();
				{
					engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdateNavQuery);
					vecNavDirection = engine::NavQueryDirection(vecPosition, vecDestination, rStaticData.navData, kbDebugRender ? &vecDebugWaypoint : nullptr);
				}
				float fNavLengthSquared = XMVectorGetX(XMVector3LengthSq(vecNavDirection)); // DT TEMP DEBUG
				Log(kLogNavData, kDebug, "Player {} nav dir={} pos={} navResult={}", i, iNavDirection, vecPosition, vecNavDirection); // DT TEMP DEBUG
				if (fNavLengthSquared > 0.001f)
				{
					vecAiDirection = vecNavDirection;
				}
				else
				{
					vecAiDirection = XMVector3Normalize(XMVectorSetZ(XMVectorSubtract(vecDestination, vecPosition), 0.0f));
					Log(kLogNavData, kDebug, "Player {} navQuery returned zero, fallback dir={}", i, vecAiDirection); // DT TEMP DEBUG
				}
#if defined(BT_CLIENT)
				if constexpr (kbDebugRender)
				{
					rCurrent.pVecDebugNavWaypoints[i] = XMVectorSetW(vecDebugWaypoint, 1.0f);
				}
#endif // BT_CLIENT
			}
			else
			{
				vecIslandDestination = XMVectorZero();
				auto [vecNewAiDirection] = ComputeAiSteering(vecPosition, vecAiDirection, vecFrameCenter, fDeltaTime, i % 2 == 0);
				vecAiDirection = vecNewAiDirection;
#if defined(BT_CLIENT)
				if constexpr (kbDebugRender)
				{
					rCurrent.pVecDebugNavWaypoints[i] = XMVectorZero();
				}
#endif // BT_CLIENT
			}

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

				if (rSpaceshipsPostRender.pfArrivalGracePeriods[j] > 0.0f)
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

			// Fallback: if no in-range target, find nearest alive spaceship for look direction
			XMVECTOR vecLookPosition = vecClosestPosition;
			bool bLookTargetFound = bTargetFound;
			if (!bLookTargetFound)
			{
				float fLookClosestDistance = std::numeric_limits<float>::max();
				for (int64_t j = 0; j < iSpaceshipCount; ++j)
				{
					if (rSpaceshipsInterpolate.pfDestroyedTimes[j] != -1.0f)
					{
						continue;
					}

					float fDistance = common::Distance(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]);
					if (fDistance < fLookClosestDistance)
					{
						fLookClosestDistance = fDistance;
						vecLookPosition = rSpaceshipsInterpolate.pVecPositions[j];
						bLookTargetFound = true;
					}
				}
			}

			// --- Apply AI results ---

			// Fire flags: fire continuously at in-range/LOS targets (rate limited by weapon spawn timers)
			if (bTargetFound)
			{
				flags.Set(kFireBlaster);
				flags.Set(kFireMissile);
			}

			// Apply movement: decay existing velocity and add acceleration from AI direction
			XMVECTOR vecAcceleration = XMVectorMultiply(XMVectorReplicate(fDeltaTime * kfAcceleration), vecAiDirection);
			vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(common::ExponentialDecay(kfAccelerationDecay, fDeltaTime)), vecVelocity, vecAcceleration);
			Log(kDebug, "Player {} aiDir={} vel={}", i, vecAiDirection, vecVelocity); // DT TEMP DEBUG

			// Direction: face nearest spaceship (prioritize in-range/visible/LOS, fallback to any alive)
			if (bLookTargetFound)
			{
				XMVECTOR vecLookDirection = XMVector3Normalize(XMVectorSubtract(vecLookPosition, vecPosition));
				vecWantedDirection = common::RotateTowardsPercent(vecWantedDirection, vecLookDirection, common::ExponentialInterpolant(kfWantedDirectionSpeed, fDeltaTime));
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
			Log(kDebug, "Player {} terrainPush elev={} pen={} push={} velAfter={}", i, fElevation, fPenetration, fPushStrength, vecVelocity); // DT TEMP DEBUG
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
		rCurrent.pfTransferLockTimers[i] = fTransferLockTimer;
		rCurrent.pfArrivalGracePeriods[i] = fArrivalGracePeriod;
		rCurrent.pfFrameChangeTimers[i] = fFrameChangeTimer;
		rCurrent.pfNavigationDelays[i] = fNavigationDelay;
		rCurrent.piNavDirections[i] = iNavDirection;
		rCurrent.pVecIslandDestinations[i] = vecIslandDestination;
	}
}

} // namespace game
