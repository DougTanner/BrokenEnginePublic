#include "Player.h"

#include "Audio/AudioManager.h"
#include "File/FileManager.h"
#include "Frame/Collision.h"
#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Frame/Render.h"
#include "Graphics/AnimationData.h"
#include "Graphics/Camera.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Input/Input.h"
#include "Profile/ProfileManager.h"
#include "Ui/WrapperBase.h"
#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Explosions.h"
#include "Frame/Collections/Missiles.h"
#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Puffs.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/ParticleManager.h"
#include "Graphics/Managers/PipelineManager.h"

#include "Data/Audio.h"
#include "Data/Scene.h"
#include "Data/Texture.h"

namespace game
{

using enum PlayerFlags;
using enum FrameInputHeldFlags;

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
constexpr float kfShieldShrinkSpeed = 1.5f;
constexpr float kfShieldRotationSpeed = 4.0f;
constexpr float kfHexShieldIntensityDecay = 1.25f;

// Hex shield rendering
constexpr float kfHexShieldLightingIntensity = 125.0f;
constexpr float kfHexShieldSizeScale = 0.1f;
constexpr float kfHexShieldColorMix = 0.85f;

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
constexpr float kfMissileAcceleration = 30.0f;
constexpr float kfMissileSpawnBarrelOffset = 1.1f;
constexpr float kfMissileSpawnPreMove = 1.5f;
constexpr float kfMissileSpawnAngle = XM_PIDIV16;
constexpr float kfMissileAngleJitter = XM_PIDIV16;

// Explosion type
constexpr uint32_t kuiExplosionBaseParticleCount = 16;
constexpr float kfExplosionParticleVelocityMin = 5.0f;
constexpr float kfExplosionParticleVelocityRandom = 15.0f;

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
constexpr float kfCameraShakeAdd = 0.25f;
constexpr float kfCameraShakeMax = 1.0f;

// Render
constexpr float kfDeathShrinkPower = 2.0f;

void PlayerInterpolate::Register()
{
	engine::AreaLightsInterpolate::RegisterType(suiAreaLightTypeIndex,
	{
		.crc = data::kTexturesBlasterBC74pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
		.fVisibleIntensity = kfAreaLightVisibleIntensity,
		.fLightingSize = kfAreaLightLightingSize,
		.fLightingIntensity = kfAreaLightLightingIntensity,
	});

	BlastersInterpolate::RegisterType(suiBlasterTypeIndex,
	{
		.f2Size = {kfBlasterSizeX, kfBlasterSizeY},
		.uiAreaLightTypeIndex = suiAreaLightTypeIndex,
	});

	// Register player explosion type
	engine::ExplosionsInterpolate::RegisterType(suiExplosionTypeIndex,
	{
		.uiPrimaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex(),
		.uiSecondaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex(),
		.uiPrimaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex(),
		.uiSecondaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex(),
		.uiTrailTypeIndex = engine::ExplosionsInterpolate::GetTrailTypeIndex(),
		.uiWindRadialControllerTypeIndex = engine::ExplosionsInterpolate::GetWindRadialControllerTypeIndex(),
		.uiBaseParticleCount = kuiExplosionBaseParticleCount,
		.uiParticleColor = 0xFF0000FF,
		.fParticleVelocityMin = kfExplosionParticleVelocityMin,
		.fParticleVelocityRandom = kfExplosionParticleVelocityRandom,
		.fParticleVerticalVelocityMin = kfExplosionParticleVerticalVelocityMin,
		.fParticleVerticalVelocityRandom = kfExplosionParticleVerticalVelocityRandom,
		.fParticleIntensityDecay = kfExplosionParticleIntensityDecay,
	});

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
}

void PlayerInterpolate::GraphicsResources()
{
	engine::Buffer* pStorageBuffers = engine::gpBufferManager->CreateDynamicBuffer(kCrc, engine::kBufferMain, kName, sizeof(shaders::ModelLayout));
	engine::gpPipelineManager->CreateDynamicModelPipeline(kCrc, kName, kModel, pStorageBuffers);
	engine::gpPipelineManager->CreateDynamicModelPipelineShadow(kCrc, kName, kModel, pStorageBuffers);
}

void PlayerInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	PlayerInterpolate& rCurrent = rFrameInterpolate.player;
	const PlayerInterpolate& rPrevious = rPreviousFrame.interpolate.player;
	const PlayerPostRender& rPreviousPostRender = rPreviousFrame.postRender.player;
	float fDeltaTime = rFrameInterpolate.fDeltaTime;

	// Load
	XMVECTOR vecPosition = rPrevious.vecPosition;
	XMVECTOR vecDirection = rPrevious.vecDirection;
	float fDestroyedTime = rPrevious.fDestroyedTime;
	float fAnimationTime = rPrevious.fAnimationTime;
	engine::wind_trail_t windTrail = rPrevious.windTrail;
	engine::hex_shields_t uiHexShield = rPrevious.uiHexShield;
	float fShieldRotation = rPrevious.fShieldRotation;
	float fShieldShrink = rPrevious.fShieldShrink;

	// Position
	if (!(rPreviousPostRender.flags & kExploding)) [[likely]]
	{
		vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPreviousPostRender.vecVelocity, vecPosition);
	}
	vecPosition = XMVectorSetZ(vecPosition, engine::gBaseHeight.Get());

	// Direction
	vecDirection = common::RotateTowardsPercent(vecDirection, rPreviousPostRender.vecWantedDirection, common::ExponentialInterpolant(kfRotateTowardsSpeed, fDeltaTime));

	// Rotation tilt from velocity
	float fRotationAccelerationX = std::clamp(kfRotationTiltFactor * XMVectorGetX(rPreviousPostRender.vecVelocity), -kfRotationTiltMax, kfRotationTiltMax);
	float fRotationAccelerationY = std::clamp(-kfRotationTiltFactor * XMVectorGetY(rPreviousPostRender.vecVelocity), -kfRotationTiltMax, kfRotationTiltMax);

	// Death countdown
	if (rPreviousPostRender.flags & kExploding) [[unlikely]]
	{
		fDestroyedTime = std::max(fDestroyedTime - fDeltaTime, 0.0f);
	}

	// Hex shield animation
	fShieldRotation += fDeltaTime * kfShieldRotationSpeed;
	fShieldShrink = std::clamp(fShieldShrink + (rPreviousPostRender.fShield > 0.0f ? fDeltaTime * kfShieldShrinkSpeed : -fDeltaTime * kfShieldShrinkSpeed), 0.0f, 1.0f);

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
	}

	// Save
	rCurrent.vecPosition = vecPosition;
	rCurrent.vecDirection = vecDirection;
	rCurrent.fDestroyedTime = fDestroyedTime;
	rCurrent.fAnimationTime = fAnimationTime;
	rCurrent.fRotationAccelerationX = fRotationAccelerationX;
	rCurrent.fRotationAccelerationY = fRotationAccelerationY;
	rCurrent.windTrail = windTrail;
	rCurrent.uiHexShield = uiHexShield;
	rCurrent.fShieldRotation = fShieldRotation;
	rCurrent.fShieldShrink = fShieldShrink;

	// Sync wind trail
	if (rCurrent.windTrail.IsValid())
	{
		engine::WindTrailsInterpolate::Sync(rFrameInterpolate, rCurrent.windTrail,
		{
			.vecPosition = vecPosition,
			.fIntensity = engine::gWindDepositPlayerIntensity.Get(),
			.fWidth = engine::gWindDepositPlayerWidth.Get(),
			.fLengthMultiplier = engine::gWindDepositPlayerLengthMultiplier.Get(),
		}, false);
	}

	// Copy and decay hex shield direction intensities
	for (int64_t i = 0; i < shaders::kiHexShieldDirections; ++i)
	{
		rCurrent.pf4HexShieldDirections[i] = rPrevious.pf4HexShieldDirections[i];
		rCurrent.pfHexShieldVertIntensities[i] = std::max(rPrevious.pfHexShieldVertIntensities[i] - kfHexShieldIntensityDecay * fDeltaTime, 0.0f);
		rCurrent.pfHexShieldFragIntensities[i] = std::max(rPrevious.pfHexShieldFragIntensities[i] - kfHexShieldIntensityDecay * fDeltaTime, 0.0f);
	}

	// Sync hex shield to engine collection (if exists and not exploding)
	// Note: Creation/removal happens in PostRender::Spawn/Destroy
	if (rCurrent.uiHexShield.IsValid() && !(rPreviousPostRender.flags & kExploding))
	{
		// Build transform (rotation around Z)
		XMMATRIX matRotation = XMMatrixRotationZ(rCurrent.fShieldRotation);
		XMFLOAT3X4 f3x4Transform {};
		XMFLOAT3X4 f3x4TransformNormal {};
		XMStoreFloat3x4(&f3x4Transform, matRotation);
		XMStoreFloat3x4(&f3x4TransformNormal, XMMatrixTranspose(XMMatrixInverse(nullptr, matRotation)));

		// Build SyncData
		engine::HexShieldsInterpolate::SyncData syncData
		{
			.vecPosition = rCurrent.vecPosition,
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
			.fSize = rCurrent.fShieldShrink * kfHexShieldSizeScale,
			.fColorMix = kfHexShieldColorMix,
		};

		// Copy direction arrays
		for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
		{
			syncData.pf4Directions[j] = rCurrent.pf4HexShieldDirections[j];
			syncData.pfVertIntensities[j] = rCurrent.pfHexShieldVertIntensities[j];
			syncData.pfFragIntensities[j] = rCurrent.pfHexShieldFragIntensities[j];
		}

		engine::HexShieldsInterpolate::Sync(rFrameInterpolate, rCurrent.uiHexShield, syncData);
	}
}

void PlayerPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput)
{
	PlayerPostRender& __restrict rCurrent = rFrame.postRender.player;
	const PlayerPostRender& rPrevious = rPreviousFrame.postRender.player;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;
	// Load
	PlayerFlags_t flags = rPrevious.flags;
	engine::alignment_t alignment = rPrevious.alignment;
	float fNextBlasterFireTime = rPrevious.fNextBlasterFireTime;
	float fNextSecondarySpawnTime = rPrevious.fNextSecondarySpawnTime;
	XMVECTOR vecVelocity = rPrevious.vecVelocity;
	XMVECTOR vecWantedDirection = rPrevious.vecWantedDirection;
	float fArmor = rPrevious.fArmor;
	float fShield = rPrevious.fShield;
	float fShieldCooldown = rPrevious.fShieldCooldown - fDeltaTime;
	float fDestroyedExplosionTime = rPrevious.fDestroyedExplosionTime - fDeltaTime;
	float fShieldDownSoundCooldown = rPrevious.fShieldDownSoundCooldown - fDeltaTime;

	// Fire blasters based on input
	if (rFrameInput.flags & kPrimary)
	{
		flags.Set(kFireBlaster);
	}
	else
	{
		fNextBlasterFireTime = 0.0f;
	}

	// Fire missiles based on input
	if (rFrameInput.flags & kSecondary)
	{
		flags.Set(kFireMissile);
	}

	// Apply movement: decay existing velocity and add acceleration from input
	XMVECTOR vecAcceleration = XMVectorMultiply(XMVectorReplicate(fDeltaTime * kfAcceleration), XMVector3Normalize(XMVectorSet(rFrameInput.f3MovePlayer.x, rFrameInput.f3MovePlayer.y, rFrameInput.f3MovePlayer.z, 0.0f)));
	vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(common::ExponentialDecay(kfAccelerationDecay, fDeltaTime)), vecVelocity, vecAcceleration);

	// Direction
	vecWantedDirection = rFrameInput.vecDirection;

	// Shield regeneration
	if (fShieldCooldown <= 0.0f)
	{
		fShield = std::min(fShield + fDeltaTime * kfPlayerShieldRegen, kfPlayerShield);
	}

	// Terrain collision - add velocity away from terrain, gentle at first then ramping up
	XMVECTOR vecPosition = rPreviousFrame.interpolate.player.vecPosition;
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
	rCurrent.flags = flags;
	rCurrent.alignment = alignment;
	rCurrent.fNextBlasterFireTime = fNextBlasterFireTime;
	rCurrent.fNextSecondarySpawnTime = fNextSecondarySpawnTime;
	rCurrent.vecVelocity = vecVelocity;
	rCurrent.vecWantedDirection = vecWantedDirection;
	rCurrent.fArmor = fArmor;
	rCurrent.fShield = fShield;
	rCurrent.fShieldCooldown = fShieldCooldown;
	rCurrent.fDestroyedExplosionTime = fDestroyedExplosionTime;
	rCurrent.fShieldDownSoundCooldown = fShieldDownSoundCooldown;
}

void PlayerPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

void PlayerPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayerInterpolate& rCurrentInterpolate = rFrame.interpolate.player;
	PlayerPostRender& rCurrentPostRender = rFrame.postRender.player;

	// Remove wind trail when exploding
	if ((rCurrentPostRender.flags & kExploding) && rCurrentInterpolate.windTrail.IsValid())
	{
		engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.windTrail);
	}

	// Remove hex shield when exploding
	if ((rCurrentPostRender.flags & kExploding) && rCurrentInterpolate.uiHexShield.IsValid())
	{
		engine::HexShieldsPostRender::Remove(rFrame, rCurrentInterpolate.uiHexShield);
	}
}

void PlayerPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayerInterpolate& rCurrentInterpolate = rFrame.interpolate.player;
	PlayerPostRender& rCurrentPostRender = rFrame.postRender.player;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	// Create wind trail if it doesn't exist and not exploding
	if (!rCurrentInterpolate.windTrail.IsValid() && !(rCurrentPostRender.flags & kExploding))
	{
		engine::WindTrailsPostRender::Add(rFrame, rCurrentInterpolate.windTrail);
		engine::WindTrailsInterpolate::Sync(rFrame.interpolate, rCurrentInterpolate.windTrail,
		{
			.vecPosition = rCurrentInterpolate.vecPosition,
			.fIntensity = engine::gWindDepositPlayerIntensity.Get(),
			.fWidth = engine::gWindDepositPlayerWidth.Get(),
			.fLengthMultiplier = engine::gWindDepositPlayerLengthMultiplier.Get(),
		}, true);
	}

	// Create hex shield if it doesn't exist and not exploding
	if (!rCurrentInterpolate.uiHexShield.IsValid() && !(rCurrentPostRender.flags & kExploding))
	{
		engine::HexShieldsPostRender::Add(rFrame, rCurrentInterpolate.uiHexShield, PlayerInterpolate::suiHexShieldTypeIndex);
	}

	// Spawn blasters
	if (rCurrentPostRender.flags & kFireBlaster)
	{
		rCurrentPostRender.flags.Clear(kFireBlaster);

		// Calculate base blaster direction and barrel offset normal (constant for all spawns this frame)
		XMVECTOR vecBaseDirection = rCurrentPostRender.vecWantedDirection;
		XMVECTOR vecLeftNormal = XMVector3Normalize(XMVector3Cross(vecBaseDirection, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));

		// Decrement timer and spawn multiple blasters if needed
		rCurrentPostRender.fNextBlasterFireTime -= fDeltaTime;

		while (rCurrentPostRender.fNextBlasterFireTime <= 0.0f)
		{
			// Inter-frame time: how much time has elapsed since this blaster should have spawned
			float fInterFrameTime = -rCurrentPostRender.fNextBlasterFireTime;

			// Interpolate player position backwards to where they were when this blaster spawned
			XMVECTOR vecPlayerPositionAtSpawn = rCurrentInterpolate.vecPosition - fInterFrameTime * rCurrentPostRender.vecVelocity;

			// Alternate barrels
			rCurrentPostRender.flags.Toggle(kBlasterSpawnLeft);
			float fBarrelOffset = (rCurrentPostRender.flags & kBlasterSpawnLeft) ? kfBlastersSpawnBarrelOffset : -kfBlastersSpawnBarrelOffset;

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
				.uiTypeIndex = PlayerInterpolate::suiBlasterTypeIndex,
				.alignment = rCurrentPostRender.alignment,
				.fWindTrailIntensity = engine::gWindDepositPlayerBlastersIntensity.Get(),
				.fWindTrailWidth = engine::gWindDepositPlayerBlastersWidth.Get(),
				.fWindTrailLengthMultiplier = engine::gWindDepositBlastersLengthMultiplier.Get(),
			});

			rCurrentPostRender.fNextBlasterFireTime += kfBlasterFireInterval;
		}
	}

	// Spawn missiles (always decrement timer so releasing and re-pressing fires immediately after cooldown)
	rCurrentPostRender.fNextSecondarySpawnTime -= fDeltaTime;

	if (rCurrentPostRender.flags & kFireMissile)
	{
		rCurrentPostRender.flags.Clear(kFireMissile);

		if (rCurrentPostRender.fNextSecondarySpawnTime < 0.0f && !(rCurrentPostRender.flags & kExploding))
		{
			rCurrentPostRender.fNextSecondarySpawnTime = kfMissileSpawnInterval;

			// Toggle spawn side
			rCurrentPostRender.flags.Toggle(kMissileSpawnLeft);
			bool bLeftSide = rCurrentPostRender.flags & kMissileSpawnLeft;

			// Base direction is player's wanted direction
			XMVECTOR vecBaseDirection = rCurrentPostRender.vecWantedDirection;

			// Calculate barrel offset normal (perpendicular to facing direction)
			XMVECTOR vecLeftNormal = XMVector3Normalize(XMVector3Cross(vecBaseDirection, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
			float fBarrelOffset = bLeftSide ? kfMissileSpawnBarrelOffset : -kfMissileSpawnBarrelOffset;

			// Calculate angled firing direction with jitter (angles outward from center)
			float fAngleOffset = bLeftSide ? -kfMissileSpawnAngle : kfMissileSpawnAngle;
			XMVECTOR vecAngledDirection = XMVector3TransformNormal(vecBaseDirection, XMMatrixRotationZ(fAngleOffset));
			XMVECTOR vecJitteredDirection = common::RandomAngleJitter(vecAngledDirection, kfMissileAngleJitter, rFrame.postRender.randomEngine);

			// Calculate spawn position: barrel offset + pre-move along jittered direction
			XMVECTOR vecSpawnPosition = rCurrentInterpolate.vecPosition + fBarrelOffset * vecLeftNormal;
			XMVECTOR vecMissilePosition = vecSpawnPosition + kfMissileSpawnPreMove * vecJitteredDirection;
			XMVECTOR vecMissileVelocity = XMVectorReplicate(kfMissileInitialVelocity) * vecJitteredDirection;

			// Spawn with stored direction = player's wanted direction (for untargeted orientation)
			MissilesPostRender::Spawn(rFrame,
			{
				.vecPosition = vecMissilePosition,
				.vecDirection = vecJitteredDirection,
				.vecVelocity = vecMissileVelocity,
				.vecStoredDirection = vecBaseDirection,
				.uiTarget = Frame::GetMissileTarget(rFrame, vecMissilePosition, vecBaseDirection, TargetFlags::kTargetIsEnemy),
				.fAcceleration = kfMissileAcceleration,
				.flags = MissileFlags::kTargetEnemy,
				.alignment = rCurrentPostRender.alignment,
			});
		}
	}

	// Spawn death explosions
	if ((rCurrentPostRender.flags & kExploding) && rCurrentPostRender.fDestroyedExplosionTime <= 0.0f && rCurrentInterpolate.fDestroyedTime > 0.0f)
	{
		rCurrentPostRender.fDestroyedExplosionTime = kfDestroyExplosionInterval;

		float fPercent = rCurrentInterpolate.fDestroyedTime / kfDestroyTime;

		// Random direction for explosion
		XMVECTOR vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(common::Random<XM_2PI>(rFrame.postRender.randomEngine)));

		XMVECTOR vecJitteredPosition = common::RandomPositionJitter<1.0f>(rCurrentInterpolate.vecPosition, rFrame.postRender.randomEngine);
		XMVECTOR vecJitteredDirection = common::RandomDirectionJitter<0.5f>(vecDirection, rFrame.postRender.randomEngine);

		// Radial offset based on time
		float fAdjustedPercent = (std::pow((1.0f - fPercent) + 1.0f, kfDeathRadialPower) - 1.0f) * kfExplosionsRadius;
		vecJitteredPosition = XMVectorMultiplyAdd(vecJitteredDirection, XMVectorReplicate(fAdjustedPercent), vecJitteredPosition);

		engine::ExplosionsPostRender::Spawn(rFrame, rFrame.interpolate.fCurrentTime,
		{
			.uiTypeIndex = PlayerInterpolate::suiExplosionTypeIndex,
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

// Player collision arrays (single-element for the one player)
static const float& sfPlayerRadius = kfPlayerRadius;
static float sfPlayerDamage = 0.0f;  // Player doesn't deal collision damage
static engine::CollisionFlags_t sPlayerFlags {};

void PlayerPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	ScopedSuppressAllocationTracking suppressTracking;

	PlayerInterpolate& rCurrentInterpolate = rFrame.interpolate.player;
	PlayerPostRender& rCurrentPostRender = rFrame.postRender.player;

	// Add player layer to CollisionSystem
	siCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = &rCurrentInterpolate.vecPosition,
		.pfRadii = &sfPlayerRadius,
		.pfDamages = &sfPlayerDamage,
		.pFlags = &sPlayerFlags,
		.iCount = 1,
		.uiCategory = CollisionCategory::kPlayer,
		.uiCollidesWith = CollisidesWith::kPlayer,
		.pAlignments = &rCurrentPostRender.alignment,
	});
}

static void XM_CALLCONV ApplyDamage(const Frame& rFrame, PlayerInterpolate& rPlayerInterpolate, PlayerPostRender& rPlayer, float fDamage, FXMVECTOR vecDamagePosition, float fHexShieldIntensity = 1.0f)
{
	// Shield absorbs damage first
	if (rPlayer.fShield > 0.0f)
	{
		// Play shield hit sound with pitch based on remaining shield
		engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioShieldArmor465540__steaq__scifishieldhitwavwavCrc, vecDamagePosition, kfShieldHitSoundVolumeBase + kfShieldHitSoundVolumeScale * (1.0f - rPlayer.fShield / kfPlayerShield));

		// Update hex shield direction intensity
		// Find lowest intensity direction slot
		int64_t iLowestIntensityIndex = 0;
		for (int64_t k = 1; k < shaders::kiHexShieldDirections; ++k)
		{
			if (rPlayerInterpolate.pfHexShieldFragIntensities[k] < rPlayerInterpolate.pfHexShieldFragIntensities[iLowestIntensityIndex])
			{
				iLowestIntensityIndex = k;
			}
		}
		// Store damage direction and intensity
		XMVECTOR vecDamageDirection = XMVector3Normalize(XMVectorSubtract(vecDamagePosition, rPlayerInterpolate.vecPosition));
		XMStoreFloat4(&rPlayerInterpolate.pf4HexShieldDirections[iLowestIntensityIndex], vecDamageDirection);
		rPlayerInterpolate.pfHexShieldVertIntensities[iLowestIntensityIndex] = fHexShieldIntensity;
		rPlayerInterpolate.pfHexShieldFragIntensities[iLowestIntensityIndex] = fHexShieldIntensity;

		float fShieldDamage = std::min(rPlayer.fShield, fDamage);
		rPlayer.fShield -= fShieldDamage;
		fDamage -= fShieldDamage;

		if (rPlayer.fShield <= 0.0f)
		{
			rPlayer.fShieldCooldown = kfShieldCooldown;

			// Play shield down sound with cooldown to prevent spam
			if (rPlayer.fShieldDownSoundCooldown <= 0.0f)
			{
				rPlayer.fShieldDownSoundCooldown = kfShieldDownSoundCooldown;
				engine::gpAudioManager->PlayOneShot(rFrame, data::kAudioShieldArmor570852__rafaelzimrp__magicshielddownwavCrc, false, kfShieldDownSoundVolume);
			}
		}
	}

	// Remaining damage goes to armor
	if (fDamage > 0.0f)
	{
		// Play armor hit sound with pitch based on remaining armor (only for significant damage)
		if (fDamage > kfArmorHitSoundDamageThreshold)
		{
			engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioShieldArmor330629__stormwaveaudio__scififorcefieldimpact15wavCrc, vecDamagePosition, kfArmorHitSoundVolumeBase + kfArmorHitSoundVolumeScale * (1.0f - rPlayer.fArmor / kfPlayerArmor));
		}

		if constexpr (!kbEnableInvincibility)
		{
			rPlayer.fArmor -= fDamage;
			gpCamera->mfShake = std::min(gpCamera->mfShake + kfCameraShakeAdd, kfCameraShakeMax);
		}
	}
}

void PlayerPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	PlayerInterpolate& rCurrentInterpolate = rFrame.interpolate.player;
	PlayerPostRender& rCurrentPostRender = rFrame.postRender.player;

	if (rCurrentPostRender.flags & kExploding)
	{
		return;
	}

	// Check collision results
	if (engine::Collision::HasCollision(siCollisionLayerIndex, 0))
	{
		const std::vector<engine::CollisionResult>* pCollisions = engine::Collision::GetCollisions(siCollisionLayerIndex, 0);
		for (const engine::CollisionResult& rResult : *pCollisions)
		{
			if (rResult.uiOtherCategory == CollisionCategory::kSpaceship)
			{
				ApplyDamage(rFrame, rCurrentInterpolate, rCurrentPostRender, kfSpaceshipCollisionDamage, rResult.vecContactPoint);
			}
			else if (rResult.uiOtherCategory == CollisionCategory::kBlaster)
			{
				ApplyDamage(rFrame, rCurrentInterpolate, rCurrentPostRender, rResult.fDamageReceived, rResult.vecContactPoint);

				// Spawn impact VFX at contact point
				engine::PuffsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, PlayerInterpolate::suiImpactPuffControllerTypeIndex, rResult.vecContactPoint);
				engine::PointLightsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, PlayerInterpolate::suiImpactPointLightControllerTypeIndex, rResult.vecContactPoint, 0.0f);
			}
		}
	}

	// Check for death
	if (rCurrentPostRender.fArmor <= 0.0f)
	{
		rCurrentPostRender.flags.Set(kExploding);
		rCurrentInterpolate.fDestroyedTime = kfDestroyTime;
		rCurrentPostRender.fDestroyedExplosionTime = kfDestroyExplosionInterval;
	}
}

bool PlayerInterpolate::operator==(const PlayerInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual(vecPosition, rOther.vecPosition);
	bEqual &= common::BreakOnNotEqual(windTrail.ToUuid().Value(), rOther.windTrail.ToUuid().Value());
	bEqual &= common::BreakOnNotEqual(vecDirection, rOther.vecDirection);
	bEqual &= common::BreakOnNotEqual(fDestroyedTime, rOther.fDestroyedTime);
	bEqual &= common::BreakOnNotEqual(fAnimationTime, rOther.fAnimationTime);
	bEqual &= common::BreakOnNotEqual(fRotationAccelerationX, rOther.fRotationAccelerationX);
	bEqual &= common::BreakOnNotEqual(fRotationAccelerationY, rOther.fRotationAccelerationY);
	bEqual &= common::BreakOnNotEqual(uiHexShield.ToUuid().Value(), rOther.uiHexShield.ToUuid().Value());
	bEqual &= common::BreakOnNotEqual(fShieldRotation, rOther.fShieldRotation);
	bEqual &= common::BreakOnNotEqual(fShieldShrink, rOther.fShieldShrink);
	for (int64_t i = 0; i < shaders::kiHexShieldDirections; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pf4HexShieldDirections[i], rOther.pf4HexShieldDirections[i]);
		bEqual &= common::BreakOnNotEqual(pfHexShieldVertIntensities[i], rOther.pfHexShieldVertIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfHexShieldFragIntensities[i], rOther.pfHexShieldFragIntensities[i]);
	}
	return bEqual;
}

common::crc_t PlayerInterpolate::Crc(const PlayerInterpolate& rCurrent)
{
	common::crc_t checksum = 0;
	checksum ^= common::Crc(rCurrent.vecPosition);
	checksum ^= common::Crc(rCurrent.windTrail.ToUuid().Value());
	checksum ^= common::Crc(rCurrent.vecDirection);
	checksum ^= common::Crc(rCurrent.fDestroyedTime);
	checksum ^= common::Crc(rCurrent.fAnimationTime);
	checksum ^= common::Crc(rCurrent.fRotationAccelerationX);
	checksum ^= common::Crc(rCurrent.fRotationAccelerationY);
	checksum ^= common::Crc(rCurrent.uiHexShield.ToUuid().Value());
	checksum ^= common::Crc(rCurrent.fShieldRotation);
	checksum ^= common::Crc(rCurrent.fShieldShrink);
	for (int64_t i = 0; i < shaders::kiHexShieldDirections; ++i)
	{
		checksum ^= common::Crc(rCurrent.pf4HexShieldDirections[i]);
		checksum ^= common::Crc(rCurrent.pfHexShieldVertIntensities[i]);
		checksum ^= common::Crc(rCurrent.pfHexShieldFragIntensities[i]);
	}
	return checksum;
}

void PlayerInterpolate::Write(std::ostream& rStream) const
{
	common::Write(rStream, vecPosition);
	windTrail.Write(rStream);
	common::Write(rStream, vecDirection);
	common::Write(rStream, fDestroyedTime);
	common::Write(rStream, fAnimationTime);
	common::Write(rStream, fRotationAccelerationX);
	common::Write(rStream, fRotationAccelerationY);
	uiHexShield.Write(rStream);
	common::Write(rStream, fShieldRotation);
	common::Write(rStream, fShieldShrink);
	for (int64_t i = 0; i < shaders::kiHexShieldDirections; ++i)
	{
		common::Write(rStream, pf4HexShieldDirections[i]);
		common::Write(rStream, pfHexShieldVertIntensities[i]);
		common::Write(rStream, pfHexShieldFragIntensities[i]);
	}
}

void PlayerInterpolate::Read(std::istream& rStream)
{
	common::Read(rStream, vecPosition);
	windTrail.Read(rStream);
	common::Read(rStream, vecDirection);
	common::Read(rStream, fDestroyedTime);
	common::Read(rStream, fAnimationTime);
	common::Read(rStream, fRotationAccelerationX);
	common::Read(rStream, fRotationAccelerationY);
	uiHexShield.Read(rStream);
	common::Read(rStream, fShieldRotation);
	common::Read(rStream, fShieldShrink);
	for (int64_t i = 0; i < shaders::kiHexShieldDirections; ++i)
	{
		common::Read(rStream, pf4HexShieldDirections[i]);
		common::Read(rStream, pfHexShieldVertIntensities[i]);
		common::Read(rStream, pfHexShieldFragIntensities[i]);
	}
}

bool PlayerPostRender::operator==(const PlayerPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual(flags, rOther.flags);
	bEqual &= common::BreakOnNotEqual(alignment, rOther.alignment);
	bEqual &= common::BreakOnNotEqual(fNextBlasterFireTime, rOther.fNextBlasterFireTime);
	bEqual &= common::BreakOnNotEqual(fNextSecondarySpawnTime, rOther.fNextSecondarySpawnTime);
	bEqual &= common::BreakOnNotEqual(vecVelocity, rOther.vecVelocity);
	bEqual &= common::BreakOnNotEqual(vecWantedDirection, rOther.vecWantedDirection);
	bEqual &= common::BreakOnNotEqual(fArmor, rOther.fArmor);
	bEqual &= common::BreakOnNotEqual(fShield, rOther.fShield);
	bEqual &= common::BreakOnNotEqual(fShieldCooldown, rOther.fShieldCooldown);
	bEqual &= common::BreakOnNotEqual(fDestroyedExplosionTime, rOther.fDestroyedExplosionTime);
	bEqual &= common::BreakOnNotEqual(fShieldDownSoundCooldown, rOther.fShieldDownSoundCooldown);
	return bEqual;
}

common::crc_t PlayerPostRender::Crc(const PlayerPostRender& rCurrent)
{
	common::crc_t checksum = 0;
	checksum ^= common::Crc(rCurrent.flags);
	checksum ^= common::Crc(rCurrent.alignment);
	checksum ^= common::Crc(rCurrent.fNextBlasterFireTime);
	checksum ^= common::Crc(rCurrent.fNextSecondarySpawnTime);
	checksum ^= common::Crc(rCurrent.vecVelocity);
	checksum ^= common::Crc(rCurrent.vecWantedDirection);
	checksum ^= common::Crc(rCurrent.fArmor);
	checksum ^= common::Crc(rCurrent.fShield);
	checksum ^= common::Crc(rCurrent.fShieldCooldown);
	checksum ^= common::Crc(rCurrent.fDestroyedExplosionTime);
	checksum ^= common::Crc(rCurrent.fShieldDownSoundCooldown);
	return checksum;
}

void PlayerPostRender::Write(std::ostream& rStream) const
{
	flags.Write(rStream);
	alignment.Write(rStream);
	common::Write(rStream, fNextBlasterFireTime);
	common::Write(rStream, fNextSecondarySpawnTime);
	common::Write(rStream, vecVelocity);
	common::Write(rStream, vecWantedDirection);
	common::Write(rStream, fArmor);
	common::Write(rStream, fShield);
	common::Write(rStream, fShieldCooldown);
	common::Write(rStream, fDestroyedExplosionTime);
	common::Write(rStream, fShieldDownSoundCooldown);
}

void PlayerPostRender::Read(std::istream& rStream)
{
	flags.Read(rStream);
	alignment.Read(rStream);
	common::Read(rStream, fNextBlasterFireTime);
	common::Read(rStream, fNextSecondarySpawnTime);
	common::Read(rStream, vecVelocity);
	common::Read(rStream, vecWantedDirection);
	common::Read(rStream, fArmor);
	common::Read(rStream, fShield);
	common::Read(rStream, fShieldCooldown);
	common::Read(rStream, fDestroyedExplosionTime);
	common::Read(rStream, fShieldDownSoundCooldown);
}

void PlayerInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerRenderPlayer);

	const PlayerInterpolate& rCurrent = rFrameInterpolate.player;

	float fSize = (rCurrent.fDestroyedTime > 0.0f ? std::pow(rCurrent.fDestroyedTime / kfDestroyTime, kfDeathShrinkPower) : 1.0f) * kfSize;
	auto matScaling = XMMatrixScaling(fSize, fSize, fSize);
	auto matTranslation = XMMatrixTranslationFromVector(rCurrent.vecPosition);
	auto matRotationX = XMMatrixRotationX(XM_PIDIV2);
	auto matRotationY = XMMatrixRotationY(0.0f);
	auto matRotationZ = common::RotationMatrixFromDirection(rCurrent.vecDirection, XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));
	auto matRotationAccelerationX = XMMatrixRotationY(rCurrent.fRotationAccelerationX);
	auto matRotationAccelerationY = XMMatrixRotationX(rCurrent.fRotationAccelerationY);
	auto matTransform = XMMatrixMultiply(matRotationX, XMMatrixMultiply(matRotationY, XMMatrixMultiply(matRotationZ, XMMatrixMultiply(matRotationAccelerationX, XMMatrixMultiply(matRotationAccelerationY, XMMatrixMultiply(matScaling, matTranslation))))));

	auto [pPlayerLayouts, iBufferCapacity] = engine::gpBufferManager->GetDynamicStorageBuffer<shaders::ModelLayout>(kCrc, engine::kBufferMain, iCommandBuffer);
	ASSERT(iBufferCapacity >= 1);
	shaders::ModelLayout& rPlayerLayout = pPlayerLayouts[0];
	XMStoreFloat4(&rPlayerLayout.f4Position, rCurrent.vecPosition);
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
		rAnimationData.EvaluateAnimation(0, rCurrent.fAnimationTime, uiMaterialCount, pMeshData, pJointMatrices, iJointMatrixOffset);
	}

	int64_t iCount = rFrameInterpolate.flags & FrameFlags::kMainMenu ? 0 : 1;
	engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
	engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
}

} // namespace game
