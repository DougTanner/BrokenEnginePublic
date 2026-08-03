#include "Players.h"

#include "Data/Texture.h"
#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
#include "Frame/TerrainUtils.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Explosions/Explosions.h"
#include "Frame/Collections/Pushers/Pushers.h"

#include "Ui/ParticleWrappers.h"
#if defined(BT_CLIENT)
#include "Data/Scene.h"
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#include "Ui/LightingWrappers.h"
#include "Ui/SmokeWrappers.h"
#endif

namespace engine
{
template struct Collection<game::PlayersInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<game::PlayersPostRender>;
}

namespace game
{

using enum PlayerFlags;

// Type registration constants (used by Register)
constexpr float kfBlasterSizeX = kfPlayerRadius * 0.3333f;
constexpr float kfBlasterSizeY = kfPlayerRadius * 1.0f;


constexpr uint32_t kuiExplosionBaseParticleCount = 16;
constexpr float kfExplosionParticleVelocityMin = 5.0f;
constexpr float kfExplosionParticleVelocityRandom = 15.0f;
constexpr float kfExplosionParticleVerticalVelocityMin = 0.0f;
constexpr float kfExplosionParticleVerticalVelocityRandom = 20.0f;
constexpr float kfExplosionParticleIntensityDecay = 2.4f;

#if defined(BT_CLIENT)
// Impact point light timing
constexpr float kfImpactPointLightDuration = 0.4f;

// Impact puff timing
constexpr float kfImpactPuffDuration = 0.1f;
#endif // BT_CLIENT

// PlayersInterpolate::Update constants
constexpr float kfRotateTowardsSpeed = 10.0f;
#if defined(BT_CLIENT)
constexpr float kfShieldShrinkSpeed = 1.5f;
constexpr float kfShieldRotationSpeed = 4.0f;

// Hex shield rendering
constexpr float kfHexShieldSizeScale = kfPlayerRadius * 0.0667f;
constexpr float kfHexShieldColorMix = 0.85f;
constexpr float kfHexShieldFadeThreshold = 0.25f;

// Player model (defined in PlayersRender.cpp)
extern const common::crc_t kPlayerModel;

// Rotation tilt
constexpr float kfRotationTiltFactor = 0.015f;
constexpr float kfRotationTiltMax = 0.4f;
#endif // BT_CLIENT

void PlayersInterpolate::Register()
{
#if defined(BT_CLIENT)
	engine::AreaLightsInterpolate::RegisterType(suiAreaLightTypeIndex,
	{
		.crc = data::kTexturesBlasterBC74pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
		.fVisibleIntensity = gPlayerAreaLightVisibleIntensity.Get(),
		.fLightingSize = gPlayerBlasterLightingArea.Get(),
		.fLightingIntensity = gPlayerBlasterLightingIntensity.Get(),
		.pVisibleIntensityWrapper = &gPlayerAreaLightVisibleIntensity,
		.pLightingSizeWrapper = &gPlayerBlasterLightingArea,
		.pLightingIntensityWrapper = &gPlayerBlasterLightingIntensity,
	});
#endif // BT_CLIENT

	BlastersInterpolate::RegisterType(suiBlasterTypeIndex,
	{
		.f2Size = {kfBlasterSizeX, kfBlasterSizeY},
		.uiAreaLightTypeIndex = suiAreaLightTypeIndex,
	});

	// Register player explosion type
	engine::ExplosionsInterpolate::RegisterType(suiExplosionTypeIndex,
	{
#if defined(BT_CLIENT)
		.uiPrimaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex(),
		.uiSecondaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex(),
		.uiPrimaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex(),
		.uiSecondaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex(),
		.uiTrailTypeIndex = engine::ExplosionsInterpolate::GetTrailTypeIndex(),
		.uiWindRadialControllerTypeIndex = engine::ExplosionsInterpolate::GetWindRadialControllerTypeIndex(),
#endif // BT_CLIENT
		.uiBaseParticleCount = kuiExplosionBaseParticleCount,
		.uiParticleColor = 0xFF0000FF,
		.fParticleVelocityMin = kfExplosionParticleVelocityMin,
		.fParticleVelocityRandom = kfExplosionParticleVelocityRandom,
		.fParticleVerticalVelocityMin = kfExplosionParticleVerticalVelocityMin,
		.fParticleVerticalVelocityRandom = kfExplosionParticleVerticalVelocityRandom,
		.fParticleIntensityDecay = kfExplosionParticleIntensityDecay,
		.pParticleWidthScale = &gPlayerExplosionParticleWidth,
		.pParticleLengthScale = &gPlayerExplosionParticleLength,
		.pParticleLengthSpreadScale = &gPlayerExplosionParticleLengthSpread,
		.pParticlePositionJitterScale = &gPlayerExplosionParticlePositionJitter,
		.pParticleVelocityBaseScale = &gPlayerExplosionParticleVelocityBase,
		.pParticleVelocitySpreadScale = &gPlayerExplosionParticleVelocitySpread,
		.pParticleVerticalVelocityBaseScale = &gPlayerExplosionParticleVerticalVelocityBase,
		.pParticleVerticalVelocitySpreadScale = &gPlayerExplosionParticleVerticalVelocitySpread,
		.pParticleVelocityDecayScale = &gPlayerExplosionParticleVelocityDecay,
		.pParticleGravityScale = &gPlayerExplosionParticleGravity,
		.pParticleVisibleIntensityScale = &gPlayerExplosionParticleVisibleIntensity,
		.pParticleIntensitySpreadScale = &gPlayerExplosionParticleIntensitySpread,
		.pParticleIntensityDecayScale = &gPlayerExplosionParticleIntensityDecay,
		.pParticleIntensityPowerScale = &gPlayerExplosionParticleIntensityPower,
	});

#if defined(BT_CLIENT)
	// Register impact point light type and controller (flash effect when hit)
	uint8_t uiImpactPointLightTypeIndex = 0xFF;
	engine::PointLightsInterpolate::RegisterType(uiImpactPointLightTypeIndex,
	{
		.crc = data::kTexturesBC7ExplosionpngCrc,
		.uiColor = 0xFFFFFFFF,
	});
	engine::PointLightsInterpolate::RegisterControllerType(suiImpactPointLightControllerTypeIndex,
	{
		.uiBaseTypeIndex = uiImpactPointLightTypeIndex,
		.uiKeyframeCount = 2,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, kfImpactPointLightDuration, 0.0f, 0.0f},
		.keyframes =
		{
			{.fVisibleArea = 1.0f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.0f, .fLightingIntensity = 1.0f, .fRotation = 0.0f},
			{.fVisibleArea = 1.0f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.0f, .fLightingIntensity = 1.0f, .fRotation = 0.0f},
			{},
			{},
		},
		.ppVisibleAreaScales = {&gPlayerImpactVisibleAreaOne, &gPlayerImpactVisibleAreaTwo, nullptr, nullptr},
		.ppVisibleIntensityScales = {&gPlayerImpactVisibleIntensityOne, &gPlayerImpactVisibleIntensityTwo, nullptr, nullptr},
		.ppLightingAreaScales = {&gPlayerImpactLightingAreaOne, &gPlayerImpactLightingAreaTwo, nullptr, nullptr},
		.ppLightingIntensityScales = {&gPlayerImpactLightingIntensityOne, &gPlayerImpactLightingIntensityTwo, nullptr, nullptr},
	});

	// Register impact puff type and controller (smoke puff when hit)
	uint8_t uiImpactPuffTypeIndex = 0xFF;
	engine::PuffsInterpolate::RegisterType(uiImpactPuffTypeIndex,
	{
		.crc = data::kTexturesSmokeBC44jpgCrc,
		.uiColor = 0xFFFFFFFF,
	});
	engine::PuffsInterpolate::RegisterControllerType(suiImpactPuffControllerTypeIndex,
	{
		.uiBaseTypeIndex = uiImpactPuffTypeIndex,
		.uiKeyframeCount = 2,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, kfImpactPuffDuration, 0.0f, 0.0f},
		.keyframes =
		{
			{.fArea = 1.0f, .fIntensity = 1.0f, .fRotation = 0.0f},
			{.fArea = 1.0f, .fIntensity = 1.0f, .fRotation = 0.0f},
			{},
			{},
		},
		.ppAreaScales = {&gPlayerImpactPuffAreaOne, &gPlayerImpactPuffAreaTwo, nullptr, nullptr},
		.ppIntensityScales = {&gPlayerImpactPuffIntensityOne, &gPlayerImpactPuffIntensityTwo, nullptr, nullptr},
	});

	// Register hex shield type for player
	engine::HexShieldsInterpolate::RegisterType(suiHexShieldTypeIndex,
	{
		.uiColor = 0x40FFFF00,        // Cyan with 25% alpha (ABGR)
		.uiLightingColor = 0x40FFFF00, // Cyan (ABGR)
		.fMinimumIntensity = 0.0f,    // Shield invisible when idle
	});
#endif // BT_CLIENT
}

void PlayersInterpolate::AllocateAndCopy(PlayersInterpolate& rCurrent, const PlayersInterpolate& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

void PlayersPostRender::AllocateAndCopy(PlayersPostRender& rCurrent, const PlayersPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

#if defined(BT_CLIENT)
void PlayersInterpolate::RemoveOwnedVisuals(Frame& rFrame, PlayersInterpolate& rCurrentInterpolate, int64_t i)
{
	if (rCurrentInterpolate.pWindTrails[i].IsValid())
	{
		engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.pWindTrails[i]);
	}
	if (rCurrentInterpolate.pHexShields[i].IsValid())
	{
		engine::HexShieldsPostRender::Remove(rFrame, rCurrentInterpolate.pHexShields[i]);
	}
}
#endif // BT_CLIENT

void PlayersPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
#if defined(BT_CLIENT)
		if (rCurrentPostRender.pFlags[i] & kExploding)
		{
			PlayersInterpolate::RemoveOwnedVisuals(rFrame, rCurrentInterpolate, i);
		}
#endif // BT_CLIENT
	}

	// Remove dead players (reverse iteration for swap-and-pop safety)
	for (int64_t i = rCurrentInterpolate.iCount - 1; i >= 0; --i)
	{
		if ((rCurrentPostRender.pFlags[i] & kExploding) && rCurrentInterpolate.pfDestroyedTimes[i] <= 0.0f)
		{
			engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[i]);
			engine::RemoveIndexableElement(rCurrentInterpolate, rCurrentPostRender, rCurrentPostRender.puiIds[i], rCurrentInterpolate.Members(), rCurrentPostRender.Members());
		}
	}
}

static void ProcessSpawnStatusChanges([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		if (rStatusChange.eType == StatusChangeType::kDestroyPlayer)
		{
			int64_t iPlayerUuid = std::get<DestroyPlayerData>(rStatusChange.data).iPlayerUuid;
			player_t destroyId {engine::uuid_t {iPlayerUuid}};

			auto idIt = rCurrentInterpolate.idToIndexMap.find(destroyId);
			if (idIt != rCurrentInterpolate.idToIndexMap.end())
			{
				engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[idIt->second]);
#if defined(BT_CLIENT)
				PlayersInterpolate::RemoveOwnedVisuals(rFrame, rCurrentInterpolate, idIt->second);
#endif // BT_CLIENT

				engine::RemoveIndexableElement(rCurrentInterpolate, rCurrentPostRender, destroyId, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
			}
			continue;
		}

		if (rStatusChange.eType == StatusChangeType::kSpawnPlayer || rStatusChange.eType == StatusChangeType::kRespawnPlayer)
		{
			engine::global_id_t globalPlayerId {};
			PlayerFlags_t spawnFlags {PlayerFlags::kBlasterSpawnLeft};
			engine::GridCoord spawnFleetWantedCoord {};
			uint8_t uiSpawnPendingFleetTicks = 0;
			// kRespawnPlayer is reserved (no issuer repo-wide) — kept compilable, not wired up.
			// TRAP: only kSpawnPlayer extracts SpawnPlayerData, so a respawn leaves globalPlayerId at 0.
			// A wired-up respawn spawning with id 0 would be invisible to both global-id re-resolution scans
			// (ServerBroadcaster + FleetNavigationController) — never receiving fleet updates or weapon
			// toggles. Any future respawn must carry a real global id (or reuse kSpawnPlayer).
			if (rStatusChange.eType == StatusChangeType::kSpawnPlayer)
			{
				const SpawnPlayerData& rSpawnData = std::get<SpawnPlayerData>(rStatusChange.data);
				globalPlayerId.iValue = rSpawnData.iGlobalId;
				if (rSpawnData.bIsFlagship)
				{
					spawnFlags.Set(kIsFlagship);
				}
				spawnFleetWantedCoord = rSpawnData.fleetWantedCoord;
				uiSpawnPendingFleetTicks = rSpawnData.uiPendingFleetWantedCoordTicks;
			}

			// Compute frame center from world-space vecArea for spawn offset
			float fCenterX = (XMVectorGetX(rStaticData.vecArea) + XMVectorGetZ(rStaticData.vecArea)) * 0.5f;
			float fCenterY = (XMVectorGetW(rStaticData.vecArea) + XMVectorGetY(rStaticData.vecArea)) * 0.5f;

			XMVECTOR vecSpawnPosition = XMVectorSet(fCenterX + 45.0f, fCenterY + (-12.0f), engine::gBaseHeight.Get(), 1.0f);
			ASSERT(!IsOutOfBounds(ComputeFrameBounds(rStaticData.vecArea), vecSpawnPosition));

			PlayersPostRender::Spawn(rFrame,
			{
				.vecPosition = vecSpawnPosition,
				.vecDirection = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
				.vecVelocity = XMVectorZero(),
				.alignment = rFrame.postRender.playerAlignment,
				.flags = spawnFlags,
				.fArrivalGracePeriod = kfArrivalGracePeriod,
				.globalPlayerId = globalPlayerId,
				.fleetWantedCoord = spawnFleetWantedCoord,
				.uiPendingFleetWantedCoordTicks = uiSpawnPendingFleetTicks,
			});
		}
	}
}

void PlayersPostRender::ProcessUpdateStatusChanges([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	int64_t iUpdateFleetCount = 0;
	engine::GridCoord updateFleetNewCoord {};
	int64_t iUpdateFleetFlagshipGlobalId = 0;

	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		if (rStatusChange.eType == StatusChangeType::kUpdateFleet)
		{
			const UpdateFleetData& rUpdate = std::get<UpdateFleetData>(rStatusChange.data);
			player_t updateId {engine::uuid_t {rUpdate.iPlayerUuid}};
			auto idIt = rCurrentInterpolate.idToIndexMap.find(updateId);
			if (idIt != rCurrentInterpolate.idToIndexMap.end())
			{
				int64_t iIndex = idIt->second;
				if (rUpdate.bIsFlagship)
				{
					rCurrentPostRender.pFlags[iIndex].Set(kIsFlagship);
					// NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) — read only by the kVerbose LOG after the loop, which compiles out at default log levels
					iUpdateFleetFlagshipGlobalId = rCurrentPostRender.pGlobalPlayerIds[iIndex].iValue;
				}
				else
				{
					rCurrentPostRender.pFlags[iIndex].Clear(kIsFlagship);
				}
				++iUpdateFleetCount;
				updateFleetNewCoord = rUpdate.fleetWantedCoord;
				rCurrentPostRender.pFleetWantedCoords[iIndex] = rUpdate.fleetWantedCoord;
				rCurrentPostRender.puiPendingFleetWantedCoordTicks[iIndex] = rUpdate.uiPendingFleetWantedCoordTicks;
			}
			continue;
		}

		if (rStatusChange.eType == StatusChangeType::kUpdatePlayer)
		{
			const UpdatePlayerData& rUpdate = std::get<UpdatePlayerData>(rStatusChange.data);
			player_t updateId {engine::uuid_t {rUpdate.iPlayerUuid}};

			auto idIt = rCurrentInterpolate.idToIndexMap.find(updateId);
			if (idIt != rCurrentInterpolate.idToIndexMap.end())
			{
				int64_t iIndex = idIt->second;
				// Weapon mode change is deferred via countdown ticks
				rCurrentPostRender.pFlags[iIndex].Set(kPendingUseMissiles, rUpdate.bUseMissiles);
				rCurrentPostRender.puiPendingWeaponModeTicks[iIndex] = rUpdate.uiPendingWeaponModeTicks;
				rCurrentPostRender.pfNavigationDelays[iIndex] = rUpdate.fNavigationDelay;
				rCurrentPostRender.pfFrameChangeTimers[iIndex] = rUpdate.fNavigationDelay;
			}
			else
			{
				LOG(kNetwork, kWarning, "ProcessUpdateStatusChanges::kUpdatePlayer Uuid: {} NOT FOUND in idToIndexMap", rUpdate.iPlayerUuid);
			}
			continue;
		}
	}

	if (iUpdateFleetCount > 0)
	{
		LOG(kNetwork, kVerbose, "ProcessUpdateStatusChanges::kUpdateFleet Coord: ({},{}) NewWantedCoord: ({},{}) Players: {} Flagship: {}", rStaticData.coord.x, rStaticData.coord.y, updateFleetNewCoord.x, updateFleetNewCoord.y, iUpdateFleetCount, iUpdateFleetFlagshipGlobalId);
	}
}

void PlayersPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	ProcessSpawnStatusChanges(rFrame, rFrameInput, rStaticData);

#if defined(BT_CLIENT)
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		// Create wind trail if it doesn't exist and not exploding
		if (!rCurrentInterpolate.pWindTrails[i].IsValid() && !(rCurrentPostRender.pFlags[i] & kExploding))
		{
			engine::WindTrailsPostRender::Add(rFrame, rCurrentInterpolate.pWindTrails[i]);
			engine::WindTrailsInterpolate::Sync(rFrame.interpolate, rCurrentInterpolate.pWindTrails[i],
			{
				.vecPosition = rCurrentInterpolate.pVecPositions[i],
				.fIntensity = game::gWindDepositPlayerIntensity.Get(),
				.fWidth = game::gWindDepositPlayerWidth.Get(),
				.fLengthMultiplier = game::gWindDepositPlayerLengthMultiplier.Get(),
			});
		}

		// Create hex shield if it doesn't exist and not exploding
		if (!rCurrentInterpolate.pHexShields[i].IsValid() && !(rCurrentPostRender.pFlags[i] & kExploding))
		{
			engine::HexShieldsPostRender::Add(rFrame, rCurrentInterpolate.pHexShields[i], PlayersInterpolate::suiHexShieldTypeIndex);
		}
	}
#endif // BT_CLIENT

	SpawnBlasters(rFrame);
	SpawnMissiles(rFrame);
	SpawnDeathExplosions(rFrame);
}

void PlayersPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	common::ValidateVector<true >(rInfo.vecPosition);
	common::ValidateVector<false>(rInfo.vecDirection);
	common::ValidateVector<false>(rInfo.vecVelocity);

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	auto [iIndex, newId] = engine::AddIndexableElement(rCurrentInterpolate, rCurrentPostRender, rFrame.postRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = rInfo.vecPosition;
	rCurrentInterpolate.pVecDirections[iIndex] = rInfo.vecDirection;
	rCurrentInterpolate.pfDestroyedTimes[iIndex] = 0.0f;
	rCurrentInterpolate.pfAnimationTimes[iIndex] = rInfo.fAnimationTime;
	rCurrentInterpolate.puiPushers[iIndex] = {};
	engine::PushersPostRender::Add(rFrame, rCurrentInterpolate.puiPushers[iIndex]);
#if defined(BT_CLIENT)
	rCurrentInterpolate.pfRotationAccelerationXs[iIndex] = 0.0f;
	rCurrentInterpolate.pfRotationAccelerationYs[iIndex] = 0.0f;
	rCurrentInterpolate.pWindTrails[iIndex] = {};
	rCurrentInterpolate.pHexShields[iIndex] = {};
	rCurrentInterpolate.pfShieldRotations[iIndex] = rInfo.fShieldRotation;
	rCurrentInterpolate.pfShieldShrinks[iIndex] = rInfo.fShieldShrink;
	rCurrentInterpolate.pHexShieldDirections[iIndex] = {};
	rCurrentInterpolate.pHexShieldVertIntensities[iIndex] = {};
	rCurrentInterpolate.pHexShieldFragIntensities[iIndex] = {};
#endif // BT_CLIENT

	// Initialize post render state
	rCurrentPostRender.puiIds[iIndex] = newId;
	rCurrentPostRender.pAlignments[iIndex] = rInfo.alignment;
	rCurrentPostRender.pfNextBlasterFireTimes[iIndex] = rInfo.fNextBlasterFireTime;
	rCurrentPostRender.pfNextSecondarySpawnTimes[iIndex] = rInfo.fNextSecondarySpawnTime;
	rCurrentPostRender.pVecVelocities[iIndex] = rInfo.vecVelocity;
	rCurrentPostRender.pVecWantedDirections[iIndex] = rInfo.vecDirection;
	rCurrentPostRender.pfArmors[iIndex] = rInfo.bTransfer ? rInfo.fArmor : kfPlayerArmor;
	rCurrentPostRender.pfShields[iIndex] = rInfo.bTransfer ? rInfo.fShield : kfPlayerShield;
	rCurrentPostRender.pfShieldCooldowns[iIndex] = rInfo.fShieldCooldown;
	rCurrentPostRender.pfDestroyedExplosionTimes[iIndex] = 0.0f;
	rCurrentPostRender.pfShieldDownSoundCooldowns[iIndex] = rInfo.fShieldDownSoundCooldown;
	rCurrentPostRender.pVecAiDirections[iIndex] = XMVectorZero();
	rCurrentPostRender.pfTransferLockTimers[iIndex] = rInfo.fTransferLockTimer;
	rCurrentPostRender.pfArrivalGracePeriods[iIndex] = rInfo.fArrivalGracePeriod;
	// Always consume random for determinism, even if fFrameChangeTimer is pre-set
	float fRandomTimer = 15.0f + common::Random<10.0f>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfFrameChangeTimers[iIndex] = (rInfo.fFrameChangeTimer > 0.0f) ? rInfo.fFrameChangeTimer : fRandomTimer;
	// Flagship navigates to island destination on enter; non-flagship starts roaming and follows flagship via proximity
	PlayerFlags_t spawnFlags = rInfo.flags;
	SetNavDirection(spawnFlags, (rInfo.flags & kIsFlagship) ? static_cast<int8_t>(4) : static_cast<int8_t>(-1));
	SetNavWaypointIndex(spawnFlags, 0); // restart largest -> smallest -> random sequence in each new frame (incl. cross-frame transfers)
	rCurrentPostRender.pFlags[iIndex] = spawnFlags;
	rCurrentPostRender.pfNavigationDelays[iIndex] = rInfo.fNavigationDelay;
	rCurrentPostRender.pVecIslandDestinations[iIndex] = XMVectorZero();
	rCurrentPostRender.pClientGuids[iIndex] = {};
	rCurrentPostRender.pGlobalPlayerIds[iIndex] = rInfo.globalPlayerId;
	rCurrentPostRender.pFleetWantedCoords[iIndex] = rInfo.fleetWantedCoord;
	rCurrentPostRender.puiPendingFleetWantedCoordTicks[iIndex] = rInfo.uiPendingFleetWantedCoordTicks;
	rCurrentPostRender.puiPendingWeaponModeTicks[iIndex] = rInfo.uiPendingWeaponModeTicks;
#if defined(BT_CLIENT)
	rCurrentPostRender.pVecDebugNavWaypoints[iIndex] = XMVectorZero();
#endif // BT_CLIENT
}

// =============================================================================
// PlayersInterpolate::Update — synchronization-only per-player update
// =============================================================================

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
		// Enforce W=1.0 — prevents drift via MultiplyAdd's 4-lane propagation (pos.W += dt * vel.W).
		vecPosition = XMVectorSetW(vecPosition, 1.0f);

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

		// Sync pusher
		engine::PushersInterpolate::Sync(rFrameInterpolate, rCurrent.puiPushers[i],
		{
			.vecPosition = vecPosition,
			.fRadius = kfPlayerPusherRadius,
			.fIntensity = kfPlayerPusherIntensity,
			.fPower = kfPlayerPusherPower,
			.flags = {engine::PusherFlags::kTypeDefault},
		});

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
			hexShieldVertIntensities.data[j] = std::max(rPrevious.pHexShieldVertIntensities[i].data[j] - gHexShieldIntensityDecay.Get() * fDeltaTime, 0.0f);
			hexShieldFragIntensities.data[j] = std::max(rPrevious.pHexShieldFragIntensities[i].data[j] - gHexShieldIntensityDecay.Get() * fDeltaTime, 0.0f);
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
				.fLightingIntensity = gHexShieldLightingIntensity.Get(),
				.fSize = rCurrent.pfShieldShrinks[i] * kfHexShieldSizeScale,
				.fColorMix = kfHexShieldColorMix,
			};

			// Copy direction arrays (per-slot smoothstep tail so fade-out eases to zero)
			for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
			{
				const float fFragIntensity = rCurrent.pHexShieldFragIntensities[i].data[j];
				const float fFade = std::clamp(fFragIntensity / kfHexShieldFadeThreshold, 0.0f, 1.0f);
				const float fSmoothFade = fFade * fFade * (3.0f - 2.0f * fFade);
				syncData.pf4Directions[j] = rCurrent.pHexShieldDirections[i].data[j];
				syncData.pfVertIntensities[j] = rCurrent.pHexShieldVertIntensities[i].data[j] * fSmoothFade;
				syncData.pfFragIntensities[j] = fFragIntensity * fSmoothFade;
			}

			engine::HexShieldsInterpolate::Sync(rFrameInterpolate, rCurrent.pHexShields[i], syncData);
		}

#endif // BT_CLIENT
	}
}

// =============================================================================
// PlayersPostRender::PreCollision — collision-system glue
// =============================================================================

thread_local int64_t PlayersPostRender::siCollisionLayerIndex = 0;

// Player collision arrays
// thread_local: parallel per-Frame tick via Dispatch
static thread_local std::vector<float> sCollisionRadii;
static thread_local std::vector<float> sCollisionDamages;
static thread_local std::vector<engine::CollisionFlags_t> sCollisionFlags;

struct PlayerCollisionIntervalScratch
{
	std::vector<float> startTimes;
	std::vector<float> endTimes;
	std::vector<float> maxTimes;
};

static PlayerCollisionIntervalScratch& GetPlayerCollisionIntervalScratch()
{
	// Function-local TLS defers construction until first use; default construction is allocation-free
	// (empty vectors), so it is safe even before allocator startup completes. Growth sites suppress tracking.
	static thread_local PlayerCollisionIntervalScratch sScratch;
	return sScratch;
}

void PlayersPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	PlayerCollisionIntervalScratch& rCollisionScratch = GetPlayerCollisionIntervalScratch();
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppress;

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
	rCollisionScratch.startTimes.resize(uiCount);
	rCollisionScratch.endTimes.resize(uiCount);
	rCollisionScratch.maxTimes.resize(uiCount);
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		sCollisionRadii.at(static_cast<size_t>(i)) = kfPlayerRadius;
		sCollisionDamages.at(static_cast<size_t>(i)) = 0.0f; // Player doesn't deal collision damage
		sCollisionFlags.at(static_cast<size_t>(i)) = (rCurrentPostRender.pFlags[i] & kExploding) ? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided} : engine::CollisionFlags_t {};
		rCollisionScratch.startTimes.at(static_cast<size_t>(i)) = 0.0f;
		rCollisionScratch.endTimes.at(static_cast<size_t>(i)) = 1.0f;
		SegmentHit boundaryHit = TracePointToFrameExit(rStaticData.vecArea, rPreviousFrame.interpolate.pPlayers->pVecPositions[i], rCurrentInterpolate.pVecPositions[i], 0.0f, 1.0f);
		rCollisionScratch.maxTimes.at(static_cast<size_t>(i)) = boundaryHit.bHit ? boundaryHit.fTime : std::numeric_limits<float>::max();
	}

	// Add player layer to CollisionSystem
	siCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecStartPositions = rPreviousFrame.interpolate.pPlayers->pVecPositions,
		.pVecEndPositions = rCurrentInterpolate.pVecPositions,
		.pfStartTimes = rCollisionScratch.startTimes.data(),
		.pfEndTimes = rCollisionScratch.endTimes.data(),
		.pfMaxTimes = rCollisionScratch.maxTimes.data(),
		.pfRadii = sCollisionRadii.data(),
		.pfDamages = sCollisionDamages.data(),
		.pFlags = sCollisionFlags.data(),
		.pVecVelocities = rCurrentPostRender.pVecVelocities,
		.iCount = rCurrentInterpolate.iCount,
		.uiCategory = CollisionCategory::kPlayer,
		.uiCollidesWith = CollidesWith::kPlayer,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

// =============================================================================
// PlayersPostRender::Update — orchestrator (per-iter helpers in PlayersNavigation.cpp + PlayersCombat.cpp)
// =============================================================================

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

	// Frame area and center
	XMVECTOR vecArea = rStaticData.vecArea;
	// W=1.0 keeps this a proper position — every downstream (frameCenter - vecPosition) and cardinal offset add stays W-clean,
	// so normalize fallbacks don't leak W into the AI direction and on into velocity.
	XMVECTOR vecFrameCenter = XMVectorSet((XMVectorGetX(vecArea) + XMVectorGetZ(vecArea)) * 0.5f, (XMVectorGetW(vecArea) + XMVectorGetY(vecArea)) * 0.5f, engine::gBaseHeight.Get(), 1.0f);

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
		float fShieldCooldown = std::max(0.0f, rPrevious.pfShieldCooldowns[i] - fDeltaTime);
		float fDestroyedExplosionTime = std::max(0.0f, rPrevious.pfDestroyedExplosionTimes[i] - fDeltaTime);
		float fShieldDownSoundCooldown = std::max(0.0f, rPrevious.pfShieldDownSoundCooldowns[i] - fDeltaTime);
		XMVECTOR vecAiDirection = rPrevious.pVecAiDirections[i];
		float fTransferLockTimer = rPrevious.pfTransferLockTimers[i];
		float fArrivalGracePeriod = std::max(0.0f, rPrevious.pfArrivalGracePeriods[i] - fDeltaTime);
		float fFrameChangeTimer = rPrevious.pfFrameChangeTimers[i];
		float fNavigationDelay = rPrevious.pfNavigationDelays[i];
		engine::GridCoord fleetWantedCoord = rPrevious.pFleetWantedCoords[i];
		uint8_t uiPendingFleetWantedCoordTicks = rPrevious.puiPendingFleetWantedCoordTicks[i];
		uint8_t uiPendingWeaponModeTicks = rPrevious.puiPendingWeaponModeTicks[i];
		int8_t iNavDirection = GetNavDirection(flags);
		int8_t iNavWaypointIndex = GetNavWaypointIndex(flags);
		XMVECTOR vecIslandDestination = rPrevious.pVecIslandDestinations[i];
		XMVECTOR vecPosition = rPreviousInterpolate.pVecPositions[i];

#if defined(BT_CLIENT)
		if constexpr (kbDebugRender)
		{
			// Debug nav waypoint persists across the staggered pathfind throttle: ComputeNavigation
			// overwrites it only on a recompute tick, so carry the previous tick's value forward here.
			// Also covers the transfer-lock path, where ComputeNavigation is skipped. Without this the
			// non-recompute ticks render stale buffer contents (line flashes to random positions).
			rCurrent.pVecDebugNavWaypoints[i] = rPrevious.pVecDebugNavWaypoints[i];
		}
#endif // BT_CLIENT

		// Decrement pending countdown ticks
		if (uiPendingFleetWantedCoordTicks > 0)
		{
			uiPendingFleetWantedCoordTicks--;
		}
		if (uiPendingWeaponModeTicks > 0)
		{
			uiPendingWeaponModeTicks--;
			if (uiPendingWeaponModeTicks == 0)
			{
				if (flags & kPendingUseMissiles)
				{
					flags.Set(kUseMissiles);
				}
				else
				{
					flags.Clear(kUseMissiles);
				}
			}
		}

		// Transfer lock: maintain constant velocity, skip AI and weapon logic
		if (fTransferLockTimer > 0.0f)
		{
			fTransferLockTimer -= fDeltaTime;
		}
		else
		{
			// AI block — see PlayersNavigation.cpp / PlayersCombat.cpp for the helpers
			ComputeNavigation(rFrame, rPreviousFrame, rStaticData, i, vecPosition, vecFrameCenter, fleetWantedCoord, uiPendingFleetWantedCoordTicks, flags, fDeltaTime, iNavDirection, iNavWaypointIndex, vecAiDirection, vecIslandDestination, fFrameChangeTimer);

			bool bLookTargetFound = false;
			XMVECTOR vecLookPosition = XMVectorZero();
			AcquireTarget(rPreviousFrame, vecPosition, flags, bLookTargetFound, vecLookPosition);

			float fAccelMul = (1.0f - kfPlayerJitterRange * 0.5f) + common::Random<kfPlayerJitterRange>(rFrame.postRender.randomEngine);
			float fDecayMul = (1.0f - kfPlayerJitterRange * 0.5f) + common::Random<kfPlayerJitterRange>(rFrame.postRender.randomEngine);
			ApplyMovement(iNavDirection, vecAiDirection, fDeltaTime, fAccelMul, fDecayMul, vecVelocity);

			if (bLookTargetFound)
			{
				UpdateFacing(vecPosition, vecLookPosition, vecWantedDirection);
			}
		}

		// Outside transfer-lock: always runs
		RegenerateShield(fDeltaTime, fShieldCooldown, fShield);
		ApplyTerrainPush(rStaticData, vecPosition, vecVelocity);
		ApplyPusherPush(rFrame, rPreviousFrame, i, vecPosition, vecVelocity);

		// Save
		SetNavDirection(flags, iNavDirection);
		SetNavWaypointIndex(flags, iNavWaypointIndex);
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
		rCurrent.pVecIslandDestinations[i] = vecIslandDestination;
		rCurrent.pFleetWantedCoords[i] = fleetWantedCoord;
		rCurrent.puiPendingFleetWantedCoordTicks[i] = uiPendingFleetWantedCoordTicks;
		rCurrent.puiPendingWeaponModeTicks[i] = uiPendingWeaponModeTicks;
	}
}

bool PlayersInterpolate::LogDifferences(const PlayersInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("PlayersInterpolate");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < CommonRowCount(rOther); ++i)
	{
		bEqual &= common::LogDifference_Vec("pVecPositions", i, pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::LogDifference_Vec("pVecDirections", i, pVecDirections[i], rOther.pVecDirections[i]);
		bEqual &= common::LogDifference<"pfDestroyedTimes">(i, pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
		bEqual &= common::LogDifference<"puiPushers">(i, puiPushers[i], rOther.puiPushers[i]);
	}

	return bEqual;
}

bool PlayersPostRender::LogDifferences(const PlayersPostRender& rOther) const
{
	common::ScopedLogDifferenceContext context("PlayersPostRender");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < CommonRowCount(rOther); ++i)
	{
		bEqual &= common::LogDifference<"puiIds">(i, puiIds[i].ToUuid().Value(), rOther.puiIds[i].ToUuid().Value());
		bEqual &= common::LogDifference<"pFlags">(i, pFlags[i], rOther.pFlags[i]);
		bEqual &= common::LogDifference<"pAlignments">(i, pAlignments[i], rOther.pAlignments[i]);
		bEqual &= common::LogDifference<"pfNextBlasterFireTimes">(i, pfNextBlasterFireTimes[i], rOther.pfNextBlasterFireTimes[i]);
		bEqual &= common::LogDifference<"pfNextSecondarySpawnTimes">(i, pfNextSecondarySpawnTimes[i], rOther.pfNextSecondarySpawnTimes[i]);
		bEqual &= common::LogDifference_Vec("pVecVelocities", i, pVecVelocities[i], rOther.pVecVelocities[i]);
		bEqual &= common::LogDifference_Vec("pVecWantedDirections", i, pVecWantedDirections[i], rOther.pVecWantedDirections[i]);
		bEqual &= common::LogDifference<"pfArmors">(i, pfArmors[i], rOther.pfArmors[i]);
		bEqual &= common::LogDifference<"pfShields">(i, pfShields[i], rOther.pfShields[i]);
		bEqual &= common::LogDifference<"pfShieldCooldowns">(i, pfShieldCooldowns[i], rOther.pfShieldCooldowns[i]);
		bEqual &= common::LogDifference<"pfDestroyedExplosionTimes">(i, pfDestroyedExplosionTimes[i], rOther.pfDestroyedExplosionTimes[i]);
		bEqual &= common::LogDifference<"pfShieldDownSoundCooldowns">(i, pfShieldDownSoundCooldowns[i], rOther.pfShieldDownSoundCooldowns[i]);
		bEqual &= common::LogDifference_Vec("pVecAiDirections", i, pVecAiDirections[i], rOther.pVecAiDirections[i]);
		bEqual &= common::LogDifference<"pfTransferLockTimers">(i, pfTransferLockTimers[i], rOther.pfTransferLockTimers[i]);
		bEqual &= common::LogDifference<"pfArrivalGracePeriods">(i, pfArrivalGracePeriods[i], rOther.pfArrivalGracePeriods[i]);
		bEqual &= common::LogDifference<"pfFrameChangeTimers">(i, pfFrameChangeTimers[i], rOther.pfFrameChangeTimers[i]);
		bEqual &= common::LogDifference<"pfNavigationDelays">(i, pfNavigationDelays[i], rOther.pfNavigationDelays[i]);
		bEqual &= common::LogDifference_Vec("pVecIslandDestinations", i, pVecIslandDestinations[i], rOther.pVecIslandDestinations[i]);
		bEqual &= common::LogDifference<"pClientGuids.uiHigh">(i, pClientGuids[i].uiHigh, rOther.pClientGuids[i].uiHigh);
		bEqual &= common::LogDifference<"pClientGuids.uiLow">(i, pClientGuids[i].uiLow, rOther.pClientGuids[i].uiLow);
		bEqual &= common::LogDifference<"pGlobalPlayerIds">(i, pGlobalPlayerIds[i], rOther.pGlobalPlayerIds[i]);
		bEqual &= common::LogDifference<"pFleetWantedCoords.x">(i, pFleetWantedCoords[i].x, rOther.pFleetWantedCoords[i].x);
		bEqual &= common::LogDifference<"pFleetWantedCoords.y">(i, pFleetWantedCoords[i].y, rOther.pFleetWantedCoords[i].y);
		bEqual &= common::LogDifference<"puiPendingFleetWantedCoordTicks">(i, puiPendingFleetWantedCoordTicks[i], rOther.puiPendingFleetWantedCoordTicks[i]);
		bEqual &= common::LogDifference<"puiPendingWeaponModeTicks">(i, puiPendingWeaponModeTicks[i], rOther.puiPendingWeaponModeTicks[i]);
	}

	return bEqual;
}

} // namespace game
