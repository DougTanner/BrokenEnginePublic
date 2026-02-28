#include "Player.h"

#include "Ui/WrapperBase.h"
#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Explosions.h"
#include "Frame/Collections/Missiles.h"

#include "Data/Texture.h"

#ifdef BT_CLIENT
#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Puffs.h"
#include "Data/Scene.h"
#endif

#include "Frame/HealthDamage.h"
#include "Input/Input.h"
#include "Profile/ProfileManager.h"

#include "Data/Audio.h"

namespace engine
{
template struct Collection<game::PlayersInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<game::PlayersPostRender>;
}

namespace game
{

using enum PlayerFlags;
using enum FrameInputHeldFlags;

constexpr float kfPlayerSpawnSpacing = 20.0f;

#ifdef BT_CLIENT
#if 1
constexpr common::crc_t kModel = data::kModelsspaceship2scenegltfCrc;
constexpr float kfSize = 0.55f;
#endif

#if 0
constexpr common::crc_t kModel = data::kModelsblack_dragon_with_idle_animationscenegltfCrc;
constexpr float kfSize = 3.0f;
#endif
#if 0
constexpr common::crc_t kModel = data::kModelschernovan_nemesisscenegltfCrc;
constexpr float kfSize = 3.0f;
#endif
#if 0
constexpr common::crc_t kModel = data::kModelsmirascenegltfCrc;
constexpr float kfSize = 0.1f;
#endif
#if 0
constexpr common::crc_t kModel = data::kModelsDamagedHelmetDamagedHelmetgltfCrc;
constexpr float kfSize = 20.0f;
#endif
#if 0
constexpr common::crc_t kModel = data::kModelsSpaceshipscenegltfCrc;
constexpr float kfSize = 0.1f;
#endif
#endif

// Player death explosion constants
constexpr float kfDestroyTime = 0.7f;
constexpr float kfDestroyExplosionInterval = 0.005f;
constexpr float kfExplosionsRadius = 30.0f;
constexpr float kfExplosionIntensity = 1.5f;
constexpr float kfExplosionParticleCount = 16.0f;
constexpr float kfExplosionSizeStart = 2.0f;
constexpr float kfExplosionSizeEnd = 0.5f;
constexpr float kfExplosionSmoke = 0.25f;
constexpr float kfExplosionParticleVerticalVelocityMin = 0.0f;
constexpr float kfExplosionParticleVerticalVelocityRandom = 20.0f;
constexpr float kfExplosionParticleIntensityDecay = 2.4f;

// Interpolate update
constexpr float kfRotateTowardsSpeed = 10.0f;
#ifdef BT_CLIENT
constexpr float kfShieldShrinkSpeed = 1.5f;
constexpr float kfShieldRotationSpeed = 4.0f;
constexpr float kfHexShieldIntensityDecay = 1.25f;

// Hex shield rendering
constexpr float kfHexShieldLightingIntensity = 125.0f;
constexpr float kfHexShieldSizeScale = 0.1f;
constexpr float kfHexShieldColorMix = 0.85f;
#endif

// Player movement
constexpr float kfAccelerationDecay = 3.0f;
constexpr float kfAcceleration = 100.0f;

// Terrain collision
constexpr float kfPlayerRadius = 1.5f;
constexpr float kfPushMargin = 1.0f;
constexpr float kfTerrainPushVelocity = 15.0f;
constexpr float kfMaxPushVelocity = 20.0f;

// Blaster
constexpr float kfBlasterSizeX = 0.5f;
constexpr float kfBlasterSizeY = 1.5f;
constexpr float kfBlasterFireInterval = 0.05f;
constexpr float kfBlastersSpeed = 150.0f;
constexpr float kfBlastersSpawnBarrelOffset = 0.7f;
constexpr float kfBlastersSpawnPreMove = 0.75f;
constexpr float kfBlasterAngleJitter = 0.03f;

constexpr float kfAreaLightVisibleIntensity = 1.0f;
constexpr float kfAreaLightLightingSize = 2.0f;
constexpr float kfAreaLightLightingIntensity = 700.0f;

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

#ifdef BT_CLIENT
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

// Rotation tilt
constexpr float kfRotationTiltFactor = 0.015f;
constexpr float kfRotationTiltMax = 0.4f;
#endif

// Death explosion spawn
constexpr float kfDeathRadialPower = 0.3f;
constexpr uint32_t kuiDeathTrailCount = 2;

// Damage response
constexpr float kfShieldHitSoundVolumeBase = 0.1f;
constexpr float kfShieldHitSoundVolumeScale = 0.1f;
constexpr float kfShieldCooldown = 2.0f;
constexpr float kfShieldDownSoundCooldown = 2.0f;
constexpr float kfShieldDownSoundVolume = 0.1f;
constexpr float kfArmorHitSoundDamageThreshold = 3.0f;
constexpr float kfArmorHitSoundVolumeBase = 0.2f;
constexpr float kfArmorHitSoundVolumeScale = 0.5f;
#ifdef BT_CLIENT
// Render
constexpr float kfDeathShrinkPower = 2.0f;
#endif

void PlayersInterpolate::Register()
{
#ifdef BT_CLIENT
	engine::AreaLightsInterpolate::RegisterType(suiAreaLightTypeIndex,
	{
		.crc = data::kTexturesBlasterBC74pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
		.fVisibleIntensity = kfAreaLightVisibleIntensity,
		.fLightingSize = kfAreaLightLightingSize,
		.fLightingIntensity = kfAreaLightLightingIntensity,
	});
#endif

	BlastersInterpolate::RegisterType(suiBlasterTypeIndex,
	{
		.f2Size = {kfBlasterSizeX, kfBlasterSizeY},
		.uiAreaLightTypeIndex = suiAreaLightTypeIndex,
	});

	// Register player explosion type
	engine::ExplosionsInterpolate::RegisterType(suiExplosionTypeIndex,
	{
#ifdef BT_CLIENT
		.uiPrimaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex(),
		.uiSecondaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex(),
		.uiPrimaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex(),
		.uiSecondaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex(),
		.uiTrailTypeIndex = engine::ExplosionsInterpolate::GetTrailTypeIndex(),
		.uiWindRadialControllerTypeIndex = engine::ExplosionsInterpolate::GetWindRadialControllerTypeIndex(),
#endif
		.uiBaseParticleCount = kuiExplosionBaseParticleCount,
		.uiParticleColor = 0xFF0000FF,
		.fParticleVelocityMin = kfExplosionParticleVelocityMin,
		.fParticleVelocityRandom = kfExplosionParticleVelocityRandom,
		.fParticleVerticalVelocityMin = kfExplosionParticleVerticalVelocityMin,
		.fParticleVerticalVelocityRandom = kfExplosionParticleVerticalVelocityRandom,
		.fParticleIntensityDecay = kfExplosionParticleIntensityDecay,
	});

#ifdef BT_CLIENT
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
#endif
}

#ifdef BT_CLIENT
void PlayersInterpolate::GraphicsResources()
{
	engine::Buffer* pStorageBuffers = engine::gpBufferManager->CreateDynamicBuffer(kCrc, engine::kBufferMain, kName, sizeof(shaders::ModelLayout));
	engine::gpPipelineManager->CreateDynamicModelPipeline(kCrc, kName, kModel, pStorageBuffers);
	engine::gpPipelineManager->CreateDynamicModelPipelineShadow(kCrc, kName, kModel, pStorageBuffers);
}
#endif

void PlayersInterpolate::AllocateAndCopy(PlayersInterpolate& rCurrent, const PlayersInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Static fields - memcpy (never modified in Update)
	if (rCurrent.iCount > 0)
	{
#ifdef BT_CLIENT
		std::memcpy(rCurrent.pWindTrails, rPrevious.pWindTrails, rCurrent.iCount * sizeof(rCurrent.pWindTrails[0]));
		std::memcpy(rCurrent.pHexShields, rPrevious.pHexShields, rCurrent.iCount * sizeof(rCurrent.pHexShields[0]));
#endif
	}
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

#ifdef BT_CLIENT
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
		if (engine::gAnimationDataMap.contains(kModel))
		{
			const engine::AnimationData& rAnimationData = engine::gAnimationDataMap.at(kModel);
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
	}
}

void PlayersPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput)
{
	PlayersPostRender& __restrict rCurrent = *rFrame.postRender.pPlayers;
	const PlayersPostRender& rPrevious = *rPreviousFrame.postRender.pPlayers;
	const PlayersInterpolate& rPreviousInterpolate = *rPreviousFrame.interpolate.pPlayers;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		const PlayerInput& rPlayerInput = rFrameInput.playerInputs.at(i);

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

		// Fire flags — uniform for all players
		if (rPlayerInput.flags & kPrimary)
		{
			flags.Set(kFireBlaster);
		}
		else
		{
			fNextBlasterFireTime = 0.0f;
		}

		if (rPlayerInput.flags & kSecondary)
		{
			flags.Set(kFireMissile);
		}

		// Apply movement: decay existing velocity and add acceleration from input
		XMVECTOR vecAcceleration = XMVectorMultiply(XMVectorReplicate(fDeltaTime * kfAcceleration), XMVector3Normalize(XMVectorSet(rPlayerInput.f3Move.x, rPlayerInput.f3Move.y, rPlayerInput.f3Move.z, 0.0f)));
		vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(common::ExponentialDecay(kfAccelerationDecay, fDeltaTime)), vecVelocity, vecAcceleration);

		// Direction
		vecWantedDirection = rPlayerInput.vecDirection;

		// Shield regeneration
		if (fShieldCooldown <= 0.0f)
		{
			fShield = std::min(fShield + fDeltaTime * kfPlayerShieldRegen, kfPlayerShield);
		}

		// Terrain collision - add velocity away from terrain, gentle at first then ramping up
		XMVECTOR vecPosition = rPreviousInterpolate.pVecPositions[i];
		float fElevation = engine::gpIslands->GlobalElevation(vecPosition);
		float fPushHeight = engine::gBaseHeight.Get() - kfPlayerRadius - kfPushMargin;
		if (fElevation >= fPushHeight) [[unlikely]]
		{
			XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslands->GlobalNormal(vecPosition), 0.0f));
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
	}
}

void PlayersPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

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
#ifdef BT_CLIENT
				.fShieldRotation = rCurrentInterpolate.pfShieldRotations[i],
				.fShieldShrink = rCurrentInterpolate.pfShieldShrinks[i],
#endif
				.uiPlayerFlags = static_cast<uint8_t>(std::to_underlying(rCurrentPostRender.pFlags[i].meFlags) & ~std::to_underlying(kTransfer)),
			},
			.iEntityId = rCurrentPostRender.puiIds[i].ToUuid().Value(),
		};
		ComputeTransferDelta(bounds, vecPosition, request.iDeltaX, request.iDeltaY);

		if (rFrame.postRender.transferRequests.size() == rFrame.postRender.transferRequests.capacity()) [[unlikely]]
		{
			DEBUG_BREAK();
		}
		rFrame.postRender.transferRequests.push_back(request);

		// Remove owned objects
#ifdef BT_CLIENT
		if (rCurrentInterpolate.pWindTrails[i].IsValid())
		{
			engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.pWindTrails[i]);
		}
		if (rCurrentInterpolate.pHexShields[i].IsValid())
		{
			engine::HexShieldsPostRender::Remove(rFrame, rCurrentInterpolate.pHexShields[i]);
		}
#endif

		engine::RemoveIndexableElement(rCurrentInterpolate, rCurrentPostRender, rCurrentPostRender.puiIds[i], rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void PlayersPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
#ifdef BT_CLIENT
		// Remove wind trail when exploding
		if ((rCurrentPostRender.pFlags[i] & kExploding) && rCurrentInterpolate.pWindTrails[i].IsValid())
		{
			engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.pWindTrails[i]);
		}

		// Remove hex shield when exploding
		if ((rCurrentPostRender.pFlags[i] & kExploding) && rCurrentInterpolate.pHexShields[i].IsValid())
		{
			engine::HexShieldsPostRender::Remove(rFrame, rCurrentInterpolate.pHexShields[i]);
		}
#endif
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

void PlayersPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	// Process spawn events from FrameInput
	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		if ((rStatusChange.eType == StatusChangeType::kSpawnPlayer || rStatusChange.eType == StatusChangeType::kRespawnPlayer) && rCurrentInterpolate.iCount < kiMaxSpawnedPlayers)
		{
			// Respawn clears death screen
			if (rStatusChange.eType == StatusChangeType::kRespawnPlayer)
			{
				rFrame.interpolate.gameFlags.Clear(GameFlags::kDeathScreen);
			}

			// Compute frame center from world-space vecArea for spawn offset
			float fCenterX = (XMVectorGetX(rFrame.postRender.vecArea) + XMVectorGetZ(rFrame.postRender.vecArea)) * 0.5f;
			float fCenterY = (XMVectorGetW(rFrame.postRender.vecArea) + XMVectorGetY(rFrame.postRender.vecArea)) * 0.5f;

			int64_t iIndex = rCurrentInterpolate.iCount;
			PlayersPostRender::Spawn(rFrame,
			{
				// DT: TEMP
			.vecPosition = kbEnableAutoInput
				? XMVectorSet(fCenterX, fCenterY + 90.0f, 0.0f, 1.0f)
				: XMVectorSet(fCenterX + 45.0f + static_cast<float>(iIndex) * kfPlayerSpawnSpacing, fCenterY + (-12.0f), 0.0f, 1.0f),
				.vecDirection = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
				.alignment = rFrame.postRender.playerAlignment,
			});
		}
	}

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
#ifdef BT_CLIENT
		// Create wind trail if it doesn't exist and not exploding
		if (!rCurrentInterpolate.pWindTrails[i].IsValid() && !(rCurrentPostRender.pFlags[i] & kExploding))
		{
			engine::WindTrailsPostRender::Add(rFrame, rCurrentInterpolate.pWindTrails[i]);
			engine::WindTrailsInterpolate::Sync(rFrame.interpolate, rCurrentInterpolate.pWindTrails[i],
			{
				.vecPosition = rCurrentInterpolate.pVecPositions[i],
				.fIntensity = engine::gWindDepositPlayerIntensity.Get(),
				.fWidth = engine::gWindDepositPlayerWidth.Get(),
				.fLengthMultiplier = engine::gWindDepositPlayerLengthMultiplier.Get(),
			});
		}

		// Create hex shield if it doesn't exist and not exploding
		if (!rCurrentInterpolate.pHexShields[i].IsValid() && !(rCurrentPostRender.pFlags[i] & kExploding))
		{
			engine::HexShieldsPostRender::Add(rFrame, rCurrentInterpolate.pHexShields[i], PlayersInterpolate::suiHexShieldTypeIndex);
		}
#endif

		// Spawn blasters
		if (rCurrentPostRender.pFlags[i] & kFireBlaster)
		{
			rCurrentPostRender.pFlags[i].Clear(kFireBlaster);

			// Calculate base blaster direction and barrel offset normal
			XMVECTOR vecBaseDirection = rCurrentPostRender.pVecWantedDirections[i];
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
					.fWindTrailIntensity = engine::gWindDepositPlayerBlastersIntensity.Get(),
					.fWindTrailWidth = engine::gWindDepositPlayerBlastersWidth.Get(),
					.fWindTrailLengthMultiplier = engine::gWindDepositBlastersLengthMultiplier.Get(),
				});

				rCurrentPostRender.pfNextBlasterFireTimes[i] += kfBlasterFireInterval;
			}
		}

		// Spawn missiles (always decrement timer so releasing and re-pressing fires immediately after cooldown)
		rCurrentPostRender.pfNextSecondarySpawnTimes[i] -= fDeltaTime;

		if (rCurrentPostRender.pFlags[i] & kFireMissile)
		{
			rCurrentPostRender.pFlags[i].Clear(kFireMissile);

			if (rCurrentPostRender.pfNextSecondarySpawnTimes[i] < 0.0f && !(rCurrentPostRender.pFlags[i] & kExploding))
			{
				rCurrentPostRender.pfNextSecondarySpawnTimes[i] = kfMissileSpawnInterval;

				// Toggle spawn side
				rCurrentPostRender.pFlags[i].Toggle(kMissileSpawnLeft);
				bool bLeftSide = rCurrentPostRender.pFlags[i] & kMissileSpawnLeft;

				// Base direction is player's wanted direction
				XMVECTOR vecBaseDirection = rCurrentPostRender.pVecWantedDirections[i];

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

		// Spawn death explosions
		if ((rCurrentPostRender.pFlags[i] & kExploding) && rCurrentPostRender.pfDestroyedExplosionTimes[i] <= 0.0f && rCurrentInterpolate.pfDestroyedTimes[i] > 0.0f)
		{
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
}

void PlayersPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	auto [iIndex, newId] = engine::AddIndexableElement(rCurrentInterpolate, rCurrentPostRender, rFrame.postRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = rInfo.vecPosition;
	rCurrentInterpolate.pVecDirections[iIndex] = rInfo.vecDirection;
	rCurrentInterpolate.pfDestroyedTimes[iIndex] = 0.0f;
	rCurrentInterpolate.pfAnimationTimes[iIndex] = rInfo.fAnimationTime;
	rCurrentInterpolate.pfRotationAccelerationXs[iIndex] = 0.0f;
	rCurrentInterpolate.pfRotationAccelerationYs[iIndex] = 0.0f;
#ifdef BT_CLIENT
	rCurrentInterpolate.pWindTrails[iIndex] = {};
	rCurrentInterpolate.pHexShields[iIndex] = {};
	rCurrentInterpolate.pfShieldRotations[iIndex] = rInfo.fShieldRotation;
	rCurrentInterpolate.pfShieldShrinks[iIndex] = rInfo.fShieldShrink;
	rCurrentInterpolate.pHexShieldDirections[iIndex] = {};
	rCurrentInterpolate.pHexShieldVertIntensities[iIndex] = {};
	rCurrentInterpolate.pHexShieldFragIntensities[iIndex] = {};
#endif

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
}

// Player collision arrays
static std::vector<float> sCollisionRadii;
static std::vector<float> sCollisionDamages;
static std::vector<engine::CollisionFlags_t> sCollisionFlags;

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
		.uiCollidesWith = CollisidesWith::kPlayer,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

static void XM_CALLCONV ApplyDamage(const Frame& rFrame, PlayersInterpolate& rPlayerInterpolate, PlayersPostRender& rPlayer, int64_t i, float fDamage, FXMVECTOR vecDamagePosition, float fHexShieldIntensity = 1.0f)
{
	// Shield absorbs damage first
	if (rPlayer.pfShields[i] > 0.0f)
	{
		// Play shield hit sound with pitch based on remaining shield
#ifdef BT_CLIENT
		engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioShieldArmor465540__steaq__scifishieldhitwavwavCrc, vecDamagePosition, kfShieldHitSoundVolumeBase + kfShieldHitSoundVolumeScale * (1.0f - rPlayer.pfShields[i] / kfPlayerShield));
#endif

		// Update hex shield direction intensity
#ifdef BT_CLIENT
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
#endif

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
#ifdef BT_CLIENT
				engine::gpAudioManager->PlayOneShot(rFrame, data::kAudioShieldArmor570852__rafaelzimrp__magicshielddownwavCrc, false, kfShieldDownSoundVolume);
#endif
			}
		}
	}

	// Remaining damage goes to armor
	if (fDamage > 0.0f)
	{
		// Play armor hit sound with pitch based on remaining armor (only for significant damage)
#ifdef BT_CLIENT
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
#ifdef BT_CLIENT
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

bool PlayersInterpolate::operator==(const PlayersInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecDirections[i], rOther.pVecDirections[i]);
		bEqual &= common::BreakOnNotEqual(pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfAnimationTimes[i], rOther.pfAnimationTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfRotationAccelerationXs[i], rOther.pfRotationAccelerationXs[i]);
		bEqual &= common::BreakOnNotEqual(pfRotationAccelerationYs[i], rOther.pfRotationAccelerationYs[i]);
#ifdef BT_CLIENT
		bEqual &= common::BreakOnNotEqual(pWindTrails[i].ToUuid().Value(), rOther.pWindTrails[i].ToUuid().Value());
		bEqual &= common::BreakOnNotEqual(pHexShields[i].ToUuid().Value(), rOther.pHexShields[i].ToUuid().Value());
		bEqual &= common::BreakOnNotEqual(pfShieldRotations[i], rOther.pfShieldRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfShieldShrinks[i], rOther.pfShieldShrinks[i]);
		bEqual &= common::BreakOnNotEqual(pHexShieldDirections[i], rOther.pHexShieldDirections[i]);
		bEqual &= common::BreakOnNotEqual(pHexShieldVertIntensities[i], rOther.pHexShieldVertIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pHexShieldFragIntensities[i], rOther.pHexShieldFragIntensities[i]);
#endif
	}

	return bEqual;
}

bool PlayersPostRender::operator==(const PlayersPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i].ToUuid().Value(), rOther.puiIds[i].ToUuid().Value());
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(pAlignments[i], rOther.pAlignments[i]);
		bEqual &= common::BreakOnNotEqual(pfNextBlasterFireTimes[i], rOther.pfNextBlasterFireTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfNextSecondarySpawnTimes[i], rOther.pfNextSecondarySpawnTimes[i]);
		bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
		bEqual &= common::BreakOnNotEqual(pVecWantedDirections[i], rOther.pVecWantedDirections[i]);
		bEqual &= common::BreakOnNotEqual(pfArmors[i], rOther.pfArmors[i]);
		bEqual &= common::BreakOnNotEqual(pfShields[i], rOther.pfShields[i]);
		bEqual &= common::BreakOnNotEqual(pfShieldCooldowns[i], rOther.pfShieldCooldowns[i]);
		bEqual &= common::BreakOnNotEqual(pfDestroyedExplosionTimes[i], rOther.pfDestroyedExplosionTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfShieldDownSoundCooldowns[i], rOther.pfShieldDownSoundCooldowns[i]);
	}

	return bEqual;
}

#ifdef BT_CLIENT
static int64_t siRendered = 0;

void PlayersInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords)
{
	siRendered = 0;

	int64_t iTotalCount = 0;
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it != rRenderInterpolates.end())
		{
			// Players uses iCount not iCapacity for buffer sizing since count is always small
			const game::FrameInterpolate& rInterp = it->second;
			int64_t iCount = (rInterp.gameFlags & GameFlags::kMainMenu) ? 0 : rInterp.pPlayers->iCount;
			iTotalCount += iCount;
		}
	}

	if (iTotalCount == 0)
	{
		return;
	}

	VkDeviceSize requiredSize = iTotalCount * sizeof(shaders::ModelLayout);
	engine::Buffer& rBuffer = engine::gpBufferManager->mDynamicStorageBuffers[engine::kBufferMain].at(kCrc).at(iCommandBuffer);
	if (rBuffer.mInfo.dataVkDeviceSize < requiredSize)
	{
		engine::gpBufferManager->ResizeDynamicBuffer(kCrc, engine::kBufferMain, kName, requiredSize, iCommandBuffer);
		int64_t iFramebuffer = iCommandBuffer;
		engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
		engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
	}
}

void PlayersInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerRenderPlayer);

	const PlayersInterpolate& rCurrent = *rFrameInterpolate.pPlayers;

	int64_t iCount = (rFrameInterpolate.gameFlags & GameFlags::kMainMenu) ? 0 : rCurrent.iCount;

	if (iCount == 0)
	{
		return;
	}

	auto [pPlayerLayouts, iBufferCapacity] = engine::gpBufferManager->GetDynamicStorageBuffer<shaders::ModelLayout>(kCrc, engine::kBufferMain, iCommandBuffer);

	for (int64_t i = 0; i < iCount; ++i)
	{
		float fSize = kfSize;
		if (rCurrent.pfDestroyedTimes[i] > 0.0f)
		{
			fSize *= std::pow(rCurrent.pfDestroyedTimes[i] / kfDestroyTime, kfDeathShrinkPower);
		}

		auto matScaling = XMMatrixScaling(fSize, fSize, fSize);
		auto matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);
		auto matRotationX = XMMatrixRotationX(XM_PIDIV2);
		auto matRotationY = XMMatrixRotationY(0.0f);
		auto matRotationZ = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));
		auto matRotationAccelerationX = XMMatrixRotationY(rCurrent.pfRotationAccelerationXs[i]);
		auto matRotationAccelerationY = XMMatrixRotationX(rCurrent.pfRotationAccelerationYs[i]);
		auto matTransform = XMMatrixMultiply(matRotationX, XMMatrixMultiply(matRotationY, XMMatrixMultiply(matRotationZ, XMMatrixMultiply(matRotationAccelerationX, XMMatrixMultiply(matRotationAccelerationY, XMMatrixMultiply(matScaling, matTranslation))))));

		shaders::ModelLayout& rPlayerLayout = pPlayerLayouts[siRendered];
		XMStoreFloat4(&rPlayerLayout.f4Position, rCurrent.pVecPositions[i]);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rPlayerLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rPlayerLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
		rPlayerLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 0.0f};
		rPlayerLayout.uiMeshDataBase = 0;

		// Evaluate animation and upload mesh shader data (only if model has skeletal animation)
		if (engine::gAnimationDataMap.contains(kModel))
		{
			const engine::AnimationData& rAnimationData = engine::gAnimationDataMap.at(kModel);
			const engine::EagerChunk& rChunk = engine::gpFileManager->GetEagerChunkMap().at(kModel);
			uint32_t uiMaterialCount = rChunk.pHeader->sceneHeader.uiMaterialCount;

			// Allocate mesh data region
			int64_t iMeshDataBase = engine::gpBufferManager->AllocateMeshData(iCommandBuffer, uiMaterialCount);
			rPlayerLayout.uiMeshDataBase = static_cast<uint32_t>(iMeshDataBase);

			// Get mesh data buffer
			common::MeshData* pMeshData = reinterpret_cast<common::MeshData*>(engine::gpBufferManager->mMeshDataStorageBuffers.at(iCommandBuffer).mpMappedMemory) + iMeshDataBase;

			// Count skinned materials for joint matrix allocation
			int64_t iSkinnedMaterialCount = rAnimationData.SkinnedMaterialCount(uiMaterialCount);

			// Allocate joint matrix region
			int64_t iJointMatrixOffset = engine::gpBufferManager->AllocateJointMatrices(iCommandBuffer, iSkinnedMaterialCount * rAnimationData.mHeader.skeleton.uiSkinJointCount);

			// Get joint matrix buffer
			common::JointMatrix* pJointMatrices = reinterpret_cast<common::JointMatrix*>(engine::gpBufferManager->mJointMatrixStorageBuffers.at(iCommandBuffer).mpMappedMemory);

			// Evaluate animation for all materials
			rAnimationData.EvaluateAnimation(0, rCurrent.pfAnimationTimes[i], uiMaterialCount, pMeshData, pJointMatrices, iJointMatrixOffset);
		}

		++siRendered;
	}
}

void PlayersInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
	engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
}
#endif

} // namespace game
