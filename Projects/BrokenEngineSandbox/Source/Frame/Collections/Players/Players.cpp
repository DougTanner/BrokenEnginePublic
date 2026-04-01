#include "Players.h"

#include "Data/Audio.h"
#include "Data/Texture.h"
#include "Frame/HealthDamage.h"
#include "Frame/TerrainUtils.h"
#include "Input/Input.h"
#include "Profile/ProfileManager.h"
#include "Ui/WrapperBase.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Explosions/Explosions.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"

#if defined(BT_CLIENT)
#include "Data/Scene.h"
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#endif

namespace engine
{
template struct Collection<game::PlayersInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<game::PlayersPostRender>;
}

namespace game
{

using enum PlayerFlags;

// Player death explosion constants
constexpr float kfExplosionsRadius = 30.0f;
constexpr float kfExplosionIntensity = 1.5f;
constexpr float kfExplosionParticleCount = 16.0f;
constexpr float kfExplosionSizeStart = 2.0f;
constexpr float kfExplosionSizeEnd = 0.5f;
constexpr float kfExplosionSmoke = 0.25f;
constexpr float kfExplosionParticleVerticalVelocityMin = 0.0f;
constexpr float kfExplosionParticleVerticalVelocityRandom = 20.0f;
constexpr float kfExplosionParticleIntensityDecay = 2.4f;

// Blaster
constexpr float kfBlasterSizeX = 0.5f;
constexpr float kfBlasterSizeY = 1.5f;
constexpr float kfBlasterFireInterval = 0.05f;
constexpr float kfBlastersSpeed = 150.0f;
constexpr float kfBlastersSpawnBarrelOffset = 0.7f;
constexpr float kfBlastersSpawnPreMove = 0.75f;
constexpr float kfBlasterAngleJitter = 0.03f;

constexpr float kfAreaLightVisibleIntensity = 1.0f;
constexpr float kfAreaLightLightingSize = 50.0f;
constexpr float kfAreaLightLightingIntensity = 100.0f;

// Missile spawn
constexpr float kfMissileSpawnInterval = 0.2f;
constexpr float kfMissileInitialVelocity = 30.0f;
constexpr float kfMissileAcceleration = 40.0f;
constexpr float kfMissileSpawnBarrelOffset = 1.1f;
constexpr float kfMissileSpawnPreMove = 1.5f;
constexpr float kfMissileSpawnAngle = XM_PIDIV16;
constexpr float kfMissileAngleJitter = XM_PIDIV16;

// Explosion type
constexpr uint32_t kuiExplosionBaseParticleCount = 16;
constexpr float kfExplosionParticleVelocityMin = 5.0f;
constexpr float kfExplosionParticleVelocityRandom = 15.0f;

#if defined(BT_CLIENT)
// Impact point light
constexpr float kfImpactPointLightDuration = 0.4f;
constexpr float kfImpactPointLightStartVisibleArea = 0.75f;
constexpr float kfImpactPointLightStartVisibleIntensity = 1.0f;
constexpr float kfImpactPointLightStartLightingArea = 1.5f;
constexpr float kfImpactPointLightStartLightingIntensity = 40.0f;
constexpr float kfImpactPointLightEndVisibleIntensity = 0.5f;
constexpr float kfImpactPointLightEndLightingIntensity = 10.0f;

// Impact puff
constexpr float kfImpactPuffDuration = 0.1f;
constexpr float kfImpactPuffStartArea = 0.15f;
constexpr float kfImpactPuffStartIntensity = 4.0f;
constexpr float kfImpactPuffEndArea = 0.5f;
#endif // BT_CLIENT

// Death explosion spawn
constexpr float kfDeathRadialPower = 0.3f;
constexpr uint32_t kuiDeathTrailCount = 2;

void PlayersInterpolate::Register()
{
#if defined(BT_CLIENT)
	engine::AreaLightsInterpolate::RegisterType(suiAreaLightTypeIndex,
	{
		.crc = data::kTexturesBlasterBC74pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
		.fVisibleIntensity = kfAreaLightVisibleIntensity,
		.fLightingSize = kfAreaLightLightingSize,
		.fLightingIntensity = kfAreaLightLightingIntensity,
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
			{.fVisibleArea = kfImpactPointLightStartVisibleArea, .fVisibleIntensity = kfImpactPointLightStartVisibleIntensity, .fLightingArea = kfImpactPointLightStartLightingArea, .fLightingIntensity = kfImpactPointLightStartLightingIntensity, .fRotation = 0.0f},
			{.fVisibleArea = 0.0f, .fVisibleIntensity = kfImpactPointLightEndVisibleIntensity, .fLightingArea = 0.0f, .fLightingIntensity = kfImpactPointLightEndLightingIntensity, .fRotation = 0.0f},
			{},
			{},
		},
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
			{.fArea = kfImpactPuffStartArea, .fIntensity = kfImpactPuffStartIntensity, .fRotation = 0.0f},
			{.fArea = kfImpactPuffEndArea, .fIntensity = 0.0f, .fRotation = 0.0f},
			{},
			{},
		},
	});

	// Register hex shield type for player
	engine::HexShieldsInterpolate::RegisterType(suiHexShieldTypeIndex,
	{
		.uiColor = 0x40FFFF00,        // Cyan with 25% alpha (RGBA)
		.uiLightingColor = 0x40FFFF00, // Cyan (RGBA)
		.fMinimumIntensity = 0.0f,    // Shield invisible when idle
	});
#endif // BT_CLIENT
}

void PlayersInterpolate::AllocateAndCopy(PlayersInterpolate& rCurrent, const PlayersInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Static fields - memcpy (never modified in Update)
	if (rCurrent.iCount > 0)
	{
#if defined(BT_CLIENT)
		std::memcpy(rCurrent.pWindTrails, rPrevious.pWindTrails, rCurrent.iCount * sizeof(rCurrent.pWindTrails[0]));
		std::memcpy(rCurrent.pHexShields, rPrevious.pHexShields, rCurrent.iCount * sizeof(rCurrent.pHexShields[0]));
#endif
	}
}

void PlayersPostRender::AllocateAndCopy(PlayersPostRender& rCurrent, const PlayersPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Static fields - memcpy (never modified in Update)
	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
		std::memcpy(rCurrent.pAlignments, rPrevious.pAlignments, rCurrent.iCount * sizeof(rCurrent.pAlignments[0]));
		std::memcpy(rCurrent.pClientGuids, rPrevious.pClientGuids, rCurrent.iCount * sizeof(rCurrent.pClientGuids[0]));
		std::memcpy(rCurrent.pGlobalPlayerIds, rPrevious.pGlobalPlayerIds, rCurrent.iCount * sizeof(rCurrent.pGlobalPlayerIds[0]));
	}
}

#if defined(BT_CLIENT)
static void RemoveOwnedVisuals(Frame& rFrame, PlayersInterpolate& rCurrentInterpolate, int64_t i)
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

void PlayersPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	const FrameBounds bounds = ComputeFrameBounds(rFrame.postRender.vecArea);

	// Reverse iteration for swap-and-pop safety with RemoveIndexableElement
	for (int64_t i = rCurrentInterpolate.iCount - 1; i >= 0; --i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kTransfer)) [[likely]]
		{
			continue;
		}

		XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

		// Build transfer request
		TransferRequest request
		{
			.eType = StatusChangeType::kTransferPlayer,
			.data = {
				.vecPosition = vecPosition,
				.vecDirection = rCurrentInterpolate.pVecDirections[i],
				.vecVelocity = rCurrentPostRender.pVecVelocities[i],
				.alignment = rCurrentPostRender.pAlignments[i],
				.fHealth = rCurrentPostRender.pfArmors[i],
				.fShield = rCurrentPostRender.pfShields[i],
				.fNextBlasterFireTime = rCurrentPostRender.pfNextBlasterFireTimes[i],
				.fNextSecondarySpawnTime = rCurrentPostRender.pfNextSecondarySpawnTimes[i],
				.fShieldCooldown = rCurrentPostRender.pfShieldCooldowns[i],
				.fShieldDownSoundCooldown = rCurrentPostRender.pfShieldDownSoundCooldowns[i],
				.fAnimationTime = rCurrentInterpolate.pfAnimationTimes[i],
#if defined(BT_CLIENT)
				.fShieldRotation = rCurrentInterpolate.pfShieldRotations[i],
				.fShieldShrink = rCurrentInterpolate.pfShieldShrinks[i],
#endif
				.uiPlayerFlags = static_cast<uint8_t>(std::to_underlying(rCurrentPostRender.pFlags[i].meFlags) & ~std::to_underlying(kTransfer)),
			},
			.iEntityId = rCurrentPostRender.puiIds[i].ToUuid().Value(),
		};
		request.data.globalPlayerId = rCurrentPostRender.pGlobalPlayerIds[i];
		request.data.uiClientGuidHigh = rCurrentPostRender.pClientGuids[i].uiHigh;
		request.data.uiClientGuidLow = rCurrentPostRender.pClientGuids[i].uiLow;
		ComputeTransferDelta(bounds, vecPosition, request.iDeltaX, request.iDeltaY);

		if (rFrame.postRender.transferRequests.size() == rFrame.postRender.transferRequests.capacity()) [[unlikely]]
		{
			DEBUG_BREAK();
		}
		rFrame.postRender.transferRequests.push_back(request);
		Log(kLogNetwork, kVerbose, "  Transfer Player: {} Delta: ({},{})", request.iEntityId, request.iDeltaX, request.iDeltaY);

#if defined(BT_CLIENT)
		RemoveOwnedVisuals(rFrame, rCurrentInterpolate, i);
#endif // BT_CLIENT

		engine::RemoveIndexableElement(rCurrentInterpolate, rCurrentPostRender, rCurrentPostRender.puiIds[i], rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void PlayersPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
#if defined(BT_CLIENT)
		if (rCurrentPostRender.pFlags[i] & kExploding)
		{
			RemoveOwnedVisuals(rFrame, rCurrentInterpolate, i);
		}
#endif // BT_CLIENT
	}

	// Remove dead players (reverse iteration for swap-and-pop safety)
	for (int64_t i = rCurrentInterpolate.iCount - 1; i >= 0; --i)
	{
		if ((rCurrentPostRender.pFlags[i] & kExploding) && rCurrentInterpolate.pfDestroyedTimes[i] <= 0.0f)
		{
			engine::RemoveIndexableElement(rCurrentInterpolate, rCurrentPostRender, rCurrentPostRender.puiIds[i], rCurrentInterpolate.Members(), rCurrentPostRender.Members());
		}
	}
}

static void ProcessSpawnStatusChanges([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput)
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
#if defined(BT_CLIENT)
				RemoveOwnedVisuals(rFrame, rCurrentInterpolate, idIt->second);
#endif // BT_CLIENT

				engine::RemoveIndexableElement(rCurrentInterpolate, rCurrentPostRender, destroyId, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
			}
			continue;
		}

		if (rStatusChange.eType == StatusChangeType::kWeaponModeChange)
		{
			int64_t iPlayerUuid = std::get<WeaponModeChangeData>(rStatusChange.data).iPlayerUuid;
			player_t toggleId {engine::uuid_t {iPlayerUuid}};

			auto idIt = rCurrentInterpolate.idToIndexMap.find(toggleId);
			if (idIt != rCurrentInterpolate.idToIndexMap.end())
			{
				rCurrentPostRender.pFlags[idIt->second].Toggle(kUseMissiles);
			}
			continue;
		}

		if (rStatusChange.eType == StatusChangeType::kSpawnPlayer || rStatusChange.eType == StatusChangeType::kRespawnPlayer)
		{
			// Respawn clears death screen
			if (rStatusChange.eType == StatusChangeType::kRespawnPlayer)
			{
				rFrame.interpolate.gameFlags.Clear(GameFlags::kDeathScreen);
			}

			engine::global_player_t globalPlayerId {};
			if (rStatusChange.eType == StatusChangeType::kSpawnPlayer)
			{
				globalPlayerId.iValue = std::get<SpawnPlayerData>(rStatusChange.data).iGlobalId;
			}

			// Compute frame center from world-space vecArea for spawn offset
			float fCenterX = (XMVectorGetX(rFrame.postRender.vecArea) + XMVectorGetZ(rFrame.postRender.vecArea)) * 0.5f;
			float fCenterY = (XMVectorGetW(rFrame.postRender.vecArea) + XMVectorGetY(rFrame.postRender.vecArea)) * 0.5f;

			XMVECTOR vecSpawnPosition = XMVectorSet(fCenterX + 45.0f, fCenterY + (-12.0f), 0.0f, 1.0f);
			ASSERT(!IsOutOfBounds(ComputeFrameBounds(rFrame.postRender.vecArea), vecSpawnPosition));

			PlayersPostRender::Spawn(rFrame,
			{
				.vecPosition = vecSpawnPosition,
				.vecDirection = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
				.alignment = rFrame.postRender.playerAlignment,
				.globalPlayerId = globalPlayerId,
			});
		}
	}
}

static void SpawnBlasters([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kFireBlaster))
		{
			continue;
		}
		rCurrentPostRender.pFlags[i].Clear(kFireBlaster);

		if (rCurrentPostRender.pFlags[i] & kUseMissiles)
		{
			continue;
		}

		// Calculate base blaster direction and barrel offset normal
		XMVECTOR vecBaseDirection = rCurrentInterpolate.pVecDirections[i];
		XMVECTOR vecLeftNormal = XMVector3Normalize(XMVector3Cross(vecBaseDirection, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));

		// Decrement timer and spawn multiple blasters if needed
		rCurrentPostRender.pfNextBlasterFireTimes[i] -= fDeltaTime;

		while (rCurrentPostRender.pfNextBlasterFireTimes[i] <= 0.0f)
		{
			// Inter-frame time: how much time has elapsed since this blaster should have spawned
			float fInterFrameTime = -rCurrentPostRender.pfNextBlasterFireTimes[i];

			// Interpolate player position backwards to where they were when this blaster spawned
			XMVECTOR vecPlayerPositionAtSpawn = rCurrentInterpolate.pVecPositions[i] - fInterFrameTime * rCurrentPostRender.pVecVelocities[i];

			// Alternate barrels
			rCurrentPostRender.pFlags[i].Toggle(kBlasterSpawnLeft);
			float fBarrelOffset = (rCurrentPostRender.pFlags[i] & kBlasterSpawnLeft) ? kfBlastersSpawnBarrelOffset : -kfBlastersSpawnBarrelOffset;

			// Apply random angle jitter to this blaster's direction
			XMVECTOR vecJitteredDirection = common::RandomAngleJitter(vecBaseDirection, kfBlasterAngleJitter, rFrame.postRender.randomEngine);
			XMVECTOR vecBlasterVelocity = kfBlastersSpeed * vecJitteredDirection;

			// Calculate spawn position: player position at spawn time + barrel offset + pre-move along velocity
			XMVECTOR vecSpawnPosition = vecPlayerPositionAtSpawn + fBarrelOffset * vecLeftNormal;
			XMVECTOR vecFinalPosition = vecSpawnPosition + kfBlastersSpawnPreMove * vecJitteredDirection + fInterFrameTime * vecBlasterVelocity;

			// Spawn blaster with calculated position and velocity
			BlastersPostRender::Spawn(rFrame,
			{
				.vecPosition = vecFinalPosition,
				.vecVelocity = vecBlasterVelocity,
				.uiTypeIndex = PlayersInterpolate::suiBlasterTypeIndex,
				.alignment = rCurrentPostRender.pAlignments[i],
				.fWindTrailIntensity = game::gWindDepositPlayerBlastersIntensity.Get(),
				.fWindTrailWidth = game::gWindDepositPlayerBlastersWidth.Get(),
				.fWindTrailLengthMultiplier = game::gWindDepositBlastersLengthMultiplier.Get(),
			});

			rCurrentPostRender.pfNextBlasterFireTimes[i] += kfBlasterFireInterval;
		}
	}
}

static void SpawnMissiles([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		// Always decrement timer so releasing and re-pressing fires immediately after cooldown
		rCurrentPostRender.pfNextSecondarySpawnTimes[i] -= fDeltaTime;

		if (!(rCurrentPostRender.pFlags[i] & kFireMissile))
		{
			continue;
		}
		rCurrentPostRender.pFlags[i].Clear(kFireMissile);

		if (!(rCurrentPostRender.pFlags[i] & kUseMissiles))
		{
			continue;
		}

		if (rCurrentPostRender.pfNextSecondarySpawnTimes[i] >= 0.0f || (rCurrentPostRender.pFlags[i] & kExploding))
		{
			continue;
		}

		rCurrentPostRender.pfNextSecondarySpawnTimes[i] = kfMissileSpawnInterval;

		// Toggle spawn side
		rCurrentPostRender.pFlags[i].Toggle(kMissileSpawnLeft);
		bool bLeftSide = rCurrentPostRender.pFlags[i] & kMissileSpawnLeft;

		// Base direction is player's smoothed visual direction
		XMVECTOR vecBaseDirection = rCurrentInterpolate.pVecDirections[i];

		// Calculate barrel offset normal (perpendicular to facing direction)
		XMVECTOR vecLeftNormal = XMVector3Normalize(XMVector3Cross(vecBaseDirection, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
		float fBarrelOffset = bLeftSide ? kfMissileSpawnBarrelOffset : -kfMissileSpawnBarrelOffset;

		// Calculate angled firing direction with jitter (angles outward from center)
		float fAngleOffset = bLeftSide ? -kfMissileSpawnAngle : kfMissileSpawnAngle;
		XMVECTOR vecAngledDirection = XMVector3TransformNormal(vecBaseDirection, XMMatrixRotationZ(fAngleOffset));
		XMVECTOR vecJitteredDirection = common::RandomAngleJitter(vecAngledDirection, kfMissileAngleJitter, rFrame.postRender.randomEngine);

		// Calculate spawn position: barrel offset + pre-move along jittered direction
		XMVECTOR vecSpawnPosition = rCurrentInterpolate.pVecPositions[i] + fBarrelOffset * vecLeftNormal;
		XMVECTOR vecMissilePosition = vecSpawnPosition + kfMissileSpawnPreMove * vecJitteredDirection;
		XMVECTOR vecMissileVelocity = XMVectorReplicate(kfMissileInitialVelocity) * vecJitteredDirection;

		// Spawn with stored direction = player's wanted direction (for untargeted orientation)
		MissilesPostRender::Spawn(rFrame,
		{
			.vecPosition = vecMissilePosition,
			.vecDirection = vecJitteredDirection,
			.vecVelocity = vecMissileVelocity,
			.vecStoredDirection = vecBaseDirection,
			.uiTarget = Frame::GetMissileTarget(rFrame, vecMissilePosition, vecBaseDirection, rCurrentPostRender.pAlignments[i]),
			.fAcceleration = kfMissileAcceleration,
			.flags = MissileFlags::kTargetEnemy,
			.alignment = rCurrentPostRender.pAlignments[i],
		});
	}
}

static void SpawnDeathExplosions([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (!((rCurrentPostRender.pFlags[i] & kExploding) && rCurrentPostRender.pfDestroyedExplosionTimes[i] <= 0.0f && rCurrentInterpolate.pfDestroyedTimes[i] > 0.0f))
		{
			continue;
		}

		rCurrentPostRender.pfDestroyedExplosionTimes[i] = kfDestroyExplosionInterval;

		float fPercent = rCurrentInterpolate.pfDestroyedTimes[i] / kfDestroyTime;

		// Random direction for explosion
		XMVECTOR vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(common::Random<XM_2PI>(rFrame.postRender.randomEngine)));

		XMVECTOR vecJitteredPosition = common::RandomPositionJitter<1.0f>(rCurrentInterpolate.pVecPositions[i], rFrame.postRender.randomEngine);
		XMVECTOR vecJitteredDirection = common::RandomDirectionJitter<0.5f>(vecDirection, rFrame.postRender.randomEngine);

		// Radial offset based on time
		float fAdjustedPercent = (std::pow((1.0f - fPercent) + 1.0f, kfDeathRadialPower) - 1.0f) * kfExplosionsRadius;
		vecJitteredPosition = XMVectorMultiplyAdd(vecJitteredDirection, XMVectorReplicate(fAdjustedPercent), vecJitteredPosition);

		engine::ExplosionsPostRender::Spawn(rFrame, rFrame.interpolate.fCurrentTime,
		{
			.uiTypeIndex = PlayersInterpolate::suiExplosionTypeIndex,
			.vecPosition = vecJitteredPosition,
			.vecDirection = vecJitteredDirection,
			.flags = {engine::ExplosionFlags::kDestroysSelf, engine::ExplosionFlags::kYellow},
			.uiTrailCount = kuiDeathTrailCount,
			.fTrailAngle = fPercent * XM_PIDIV2,
			.uiParticleCount = static_cast<uint32_t>(fPercent * kfExplosionParticleCount),
			.fParticleAngle = fPercent * XM_PIDIV2,
			.fLightPercent = fPercent * kfExplosionIntensity,
			.fSizePercent = fPercent * kfExplosionSizeStart + (1.0f - fPercent) * kfExplosionSizeEnd,
			.fSmokePercent = fPercent * kfExplosionSmoke,
			.fTimePercent = fPercent,
		});
	}
}

void PlayersPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput)
{
	ProcessSpawnStatusChanges(rFrame, rFrameInput);

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

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	auto [iIndex, newId] = engine::AddIndexableElement(rCurrentInterpolate, rCurrentPostRender, rFrame.postRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = XMVectorSetW(rInfo.vecPosition, 1.0f);
	rCurrentInterpolate.pVecDirections[iIndex] = rInfo.vecDirection;
	rCurrentInterpolate.pfDestroyedTimes[iIndex] = 0.0f;
	rCurrentInterpolate.pfAnimationTimes[iIndex] = rInfo.fAnimationTime;
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
	rCurrentPostRender.pFlags[iIndex] = rInfo.flags;
	rCurrentPostRender.pAlignments[iIndex] = rInfo.alignment;
	rCurrentPostRender.pfNextBlasterFireTimes[iIndex] = rInfo.fNextBlasterFireTime;
	rCurrentPostRender.pfNextSecondarySpawnTimes[iIndex] = rInfo.fNextSecondarySpawnTime;
	rCurrentPostRender.pVecVelocities[iIndex] = rInfo.vecVelocity;
	rCurrentPostRender.pVecWantedDirections[iIndex] = rInfo.vecDirection;
	rCurrentPostRender.pfArmors[iIndex] = rInfo.fArmor > 0.0f ? rInfo.fArmor : kfPlayerArmor;
	rCurrentPostRender.pfShields[iIndex] = rInfo.fShield > 0.0f ? rInfo.fShield : kfPlayerShield;
	rCurrentPostRender.pfShieldCooldowns[iIndex] = rInfo.fShieldCooldown;
	rCurrentPostRender.pfDestroyedExplosionTimes[iIndex] = 0.0f;
	rCurrentPostRender.pfShieldDownSoundCooldowns[iIndex] = rInfo.fShieldDownSoundCooldown;
	rCurrentPostRender.pVecAiDirections[iIndex] = XMVectorZero();
	rCurrentPostRender.pfAiFireTimers[iIndex] = 0.0f;
	rCurrentPostRender.pfAiMissileTimers[iIndex] = 0.0f;
	rCurrentPostRender.pfAiEdgeCrossCooldowns[iIndex] = kfAiEdgeCrossCooldown;
	rCurrentPostRender.piAiEdgeCrossTargets[iIndex] = -1;
	rCurrentPostRender.pfTransferLockTimers[iIndex] = rInfo.fTransferLockTimer;
	rCurrentPostRender.pClientGuids[iIndex] = {};
	rCurrentPostRender.pGlobalPlayerIds[iIndex] = rInfo.globalPlayerId;
}

bool PlayersInterpolate::LogDifferences(const PlayersInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("PlayersInterpolate");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference_Vec("pVecPositions", i, pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::LogDifference_Vec("pVecDirections", i, pVecDirections[i], rOther.pVecDirections[i]);
		bEqual &= common::LogDifference<"pfDestroyedTimes">(i, pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
	}

	return bEqual;
}

bool PlayersPostRender::LogDifferences(const PlayersPostRender& rOther) const
{
	common::ScopedLogDifferenceContext context("PlayersPostRender");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
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
		bEqual &= common::LogDifference<"pfAiFireTimers">(i, pfAiFireTimers[i], rOther.pfAiFireTimers[i]);
		bEqual &= common::LogDifference<"pfAiMissileTimers">(i, pfAiMissileTimers[i], rOther.pfAiMissileTimers[i]);
		bEqual &= common::LogDifference<"pfAiEdgeCrossCooldowns">(i, pfAiEdgeCrossCooldowns[i], rOther.pfAiEdgeCrossCooldowns[i]);
		bEqual &= common::LogDifference<"piAiEdgeCrossTargets">(i, piAiEdgeCrossTargets[i], rOther.piAiEdgeCrossTargets[i]);
		bEqual &= common::LogDifference<"pfTransferLockTimers">(i, pfTransferLockTimers[i], rOther.pfTransferLockTimers[i]);
		bEqual &= common::LogDifference<"pClientGuids.uiHigh">(i, pClientGuids[i].uiHigh, rOther.pClientGuids[i].uiHigh);
		bEqual &= common::LogDifference<"pClientGuids.uiLow">(i, pClientGuids[i].uiLow, rOther.pClientGuids[i].uiLow);
		bEqual &= common::LogDifference<"pGlobalPlayerIds">(i, pGlobalPlayerIds[i].iValue, rOther.pGlobalPlayerIds[i].iValue);
	}

	return bEqual;
}

} // namespace game
