#include "Player.h"

#include "Audio/AudioManager.h"
#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Explosions.h"
#include "Frame/Collections/Missiles.h"
#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Puffs.h"
#include "Frame/Collision.h"
#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Graphics/Managers/ParticleManager.h"
#include "Graphics/Camera.h"
#include "Graphics/Graphics.h"
#include "Graphics/GltfPipelines.h"
#include "Graphics/Islands.h"
#include "Input/Input.h"

namespace game
{

using enum PlayerFlags;
using enum FrameInputHeldFlags;

// Player death explosion constants
constexpr float kfDestroyTime = 0.7f;
constexpr float kfDestroyExplosionInterval = 0.005f;
constexpr float kfExplosionsRadius = 30.0f;
constexpr float kfExplosionPositionJitter = 1.0f;
constexpr float kfExplosionDirectionJitter = 0.5f;
constexpr float kfExplosionIntensity = 1.5f;
constexpr float kfExplosionParticleCount = 16.0f;
constexpr float kfExplosionSizeStart = 2.0f;
constexpr float kfExplosionSizeEnd = 0.5f;
constexpr float kfExplosionSmoke = 0.25f;

float MaxArmor([[maybe_unused]] const Frame& __restrict rFrame)
{
	return kfPlayerArmor;
}
float MaxShield([[maybe_unused]] const Frame& __restrict rFrame)
{
	return kfPlayerShield;
}
float MaxEnergy([[maybe_unused]] const Frame& __restrict rFrame)
{
	return kfPlayerEnergy;
}
float MissileCapacity([[maybe_unused]] const Frame& __restrict rFrame)
{
	return kfPlayerMissileCapacity;
}

void PlayerInterpolate::Register()
{
	engine::AreaLightsInterpolate::RegisterType(suiAreaLightTypeIndex,
	{
		.crc = data::kTexturesBlasterBC74pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
		.fVisibleIntensity = 1.25f,
		.fLightingSize = 2.0f,
		.fLightingIntensity = 200.0f,
	});

	BlastersInterpolate::RegisterType(suiBlasterTypeIndex,
	{
		.f2Size = {0.11f, 1.5f},
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
		.uiBaseParticleCount = 16,
		.uiParticleColor = 0xFF0000FF,
		.fParticleVelocityMin = 5.0f,
		.fParticleVelocityRandom = 15.0f,
		.fPusherRadius = 6.0f,
		.fPusherIntensity = 20000.0f,
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
		.pfTimes = {0.0f, 0.4f, 0.0f, 0.0f},
		.keyframes =
		{
			{.fVisibleArea = 0.75f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.5f, .fLightingIntensity = 40.0f, .fRotation = 0.0f},
			{.fVisibleArea = 0.0f, .fVisibleIntensity = 0.5f, .fLightingArea = 0.0f, .fLightingIntensity = 10.0f, .fRotation = 0.0f},
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
		.pfTimes = {0.0f, 0.1f, 0.0f, 0.0f},
		.keyframes =
		{
			{.fArea = 0.15f, .fIntensity = 4.0f, .fRotation = 0.0f},
			{.fArea = 0.5f, .fIntensity = 0.0f, .fRotation = 0.0f},
			{},
			{},
		},
	});
}

void PlayerInterpolate::GraphicsResources()
{
	AllocatePipelines();
}

void PlayerInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	PlayerInterpolate& rCurrent = rFrameInterpolate.player;
	const PlayerInterpolate& rPrevious = rPreviousFrame.interpolate.player;
	const PlayerPostRender& rPreviousPostRender = rPreviousFrame.postRender.player;

	static constexpr float kfRotateTowardsSpeed = 10.0f;
	static constexpr float kfShieldShrinkSpeed = 1.5f;
	static constexpr float kfShieldRotationSpeed = 4.0f;
	static constexpr float kfHexShieldIntensityDecay = 1.25f;

	// Load
	XMVECTOR vecPosition = rPrevious.vecPosition;
	XMVECTOR vecDirection = rPrevious.vecDirection;
	float fDestroyedTime = rPrevious.fDestroyedTime;
	engine::hex_shields_t uiHexShield = rPrevious.uiHexShield;
	float fShieldRotation = rPrevious.fShieldRotation;
	float fShieldShrink = rPrevious.fShieldShrink;

	// Position
	if (!(rPreviousPostRender.flags & kExploding)) [[likely]]
	{
		vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPreviousPostRender.vecVelocity, vecPosition);
	}
	vecPosition = XMVectorSetZ(vecPosition, engine::gBaseHeight.Get());

	// Terrain collision - push player away from elevated terrain
	{
		float fElevation = engine::gpIslands->GlobalElevation(vecPosition);
		static constexpr float kfPlayerRadius = 1.5f;
		static constexpr float kfPushMargin = 0.25f;
		float fPushHeight = engine::gBaseHeight.Get() - kfPlayerRadius - kfPushMargin;
		if (fElevation >= fPushHeight) [[unlikely]]
		{
			XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslands->GlobalNormal(vecPosition), 0.0f));
			float fPenetration = fElevation - fPushHeight;
			vecPosition = XMVectorSubtract(vecPosition, XMVectorScale(vecTerrainNormal, fPenetration + kfPushMargin));
		}
	}

	// Direction
	vecDirection = common::RotateTowardsPercent(vecDirection, rPreviousPostRender.vecWantedDirection, common::ExponentialInterpolant(kfRotateTowardsSpeed, fDeltaTime));

	// Death countdown
	if (rPreviousPostRender.flags & kExploding) [[unlikely]]
	{
		fDestroyedTime = std::max(fDestroyedTime - fDeltaTime, 0.0f);
	}

	// Hex shield animation
	fShieldRotation += fDeltaTime * kfShieldRotationSpeed;
	fShieldShrink = std::clamp(fShieldShrink + (rPreviousPostRender.fShield > 0.0f ? fDeltaTime * kfShieldShrinkSpeed : -fDeltaTime * kfShieldShrinkSpeed), 0.0f, 1.0f);

	// Save
	rCurrent.vecPosition = vecPosition;
	rCurrent.vecDirection = vecDirection;
	rCurrent.fDestroyedTime = fDestroyedTime;
	rCurrent.uiHexShield = uiHexShield;
	rCurrent.fShieldRotation = fShieldRotation;
	rCurrent.fShieldShrink = fShieldShrink;

	// Copy and decay hex shield direction intensities
	for (int64_t i = 0; i < shaders::kiHexShieldDirections; ++i)
	{
		rCurrent.pf4HexShieldDirections[i] = rPrevious.pf4HexShieldDirections[i];
		rCurrent.pfHexShieldVertIntensities[i] = std::max(rPrevious.pfHexShieldVertIntensities[i] - kfHexShieldIntensityDecay * fDeltaTime, 0.0f);
		rCurrent.pfHexShieldFragIntensities[i] = std::max(rPrevious.pfHexShieldFragIntensities[i] - kfHexShieldIntensityDecay * fDeltaTime, 0.0f);
	}

	// Sync hex shield to engine collection
	engine::HexShieldsInterpolate& rHexShields = rFrameInterpolate.hexShields;

	// Hex shield constants
	static constexpr uint32_t kuiHexShieldColor = 0x4000FFFF;       // Cyan with 25% alpha
	static constexpr uint32_t kuiHexShieldLightingColor = 0x00FFFF40; // Cyan with 0% alpha
	static constexpr float kfHexShieldLightingIntensity = 125.0f;
	static constexpr float kfHexShieldSizeScale = 0.1f;
	static constexpr float kfHexShieldColorMix = 0.25f;
	static constexpr float kfHexShieldMinimumIntensity = 0.0f;

	if (rPreviousPostRender.flags & kExploding)
	{
		// Remove hex shield when exploding
		if (rCurrent.uiHexShield.IsValid())
		{
			rHexShields.idToIndexMap.erase(rCurrent.uiHexShield);
			rCurrent.uiHexShield = {};
		}
		return;
	}

	// Create hex shield if it doesn't exist
	if (!rCurrent.uiHexShield.IsValid())
	{
		// Need to add via PostRender to get proper ID generation
		// For now, generate ID manually (this should be done in a spawn phase normally)
		rCurrent.uiHexShield = engine::hex_shields_t::Generate(const_cast<FramePostRender&>(rPreviousFrame.postRender));

		// Grow capacity if needed
		if (rHexShields.iCount >= rHexShields.iCapacity)
		{
			int64_t iNewCapacity = 2 * rHexShields.iCapacity + 1;
			engine::GrowCapacityWithCopy(rHexShields, iNewCapacity, rHexShields.iCount, rHexShields.Members());
		}

		uint64_t uiSpawnIndex = static_cast<uint64_t>(rHexShields.iCount++);
		rHexShields.idToIndexMap[rCurrent.uiHexShield] = uiSpawnIndex;
	}

	// Get index for this hex shield
	uint64_t iIndex = rHexShields.idToIndexMap.at(rCurrent.uiHexShield);

	// Update position
	rHexShields.pVecPositions[iIndex] = rCurrent.vecPosition;

	// Update transform (rotation around Z)
	XMMATRIX matRotation = XMMatrixRotationZ(rCurrent.fShieldRotation);
	XMFLOAT3X4 f3x4Transform {};
	XMFLOAT3X4 f3x4TransformNormal {};
	XMStoreFloat3x4(&f3x4Transform, matRotation);
	XMStoreFloat3x4(&f3x4TransformNormal, XMMatrixTranspose(XMMatrixInverse(nullptr, matRotation)));
	rHexShields.pf4Transforms[0][iIndex] = {f3x4Transform._11, f3x4Transform._12, f3x4Transform._13, f3x4Transform._14};
	rHexShields.pf4Transforms[1][iIndex] = {f3x4Transform._21, f3x4Transform._22, f3x4Transform._23, f3x4Transform._24};
	rHexShields.pf4Transforms[2][iIndex] = {f3x4Transform._31, f3x4Transform._32, f3x4Transform._33, f3x4Transform._34};
	rHexShields.pf4TransformNormals[0][iIndex] = {f3x4TransformNormal._11, f3x4TransformNormal._12, f3x4TransformNormal._13, f3x4TransformNormal._14};
	rHexShields.pf4TransformNormals[1][iIndex] = {f3x4TransformNormal._21, f3x4TransformNormal._22, f3x4TransformNormal._23, f3x4TransformNormal._24};
	rHexShields.pf4TransformNormals[2][iIndex] = {f3x4TransformNormal._31, f3x4TransformNormal._32, f3x4TransformNormal._33, f3x4TransformNormal._34};

	// Update colors and properties
	rHexShields.puiColors[iIndex] = kuiHexShieldColor;
	rHexShields.puiLightingColors[iIndex] = kuiHexShieldLightingColor;
	rHexShields.pfLightingIntensities[iIndex] = kfHexShieldLightingIntensity;
	rHexShields.pfSizes[iIndex] = rCurrent.fShieldShrink * kfHexShieldSizeScale;
	rHexShields.pfColorMixes[iIndex] = kfHexShieldColorMix;
	rHexShields.pfMinimumIntensities[iIndex] = kfHexShieldMinimumIntensity;

	// Update direction intensities
	for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
	{
		rHexShields.pf4Directions[j][iIndex] = rCurrent.pf4HexShieldDirections[j];
		rHexShields.pfVertIntensities[j][iIndex] = rCurrent.pfHexShieldVertIntensities[j];
		rHexShields.pfFragIntensities[j][iIndex] = rCurrent.pfHexShieldFragIntensities[j];
	}
}

void PlayerPostRender::Update([[maybe_unused]] PlayerPostRender& __restrict rCurrent, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime, [[maybe_unused]] const FrameInput& __restrict rFrameInput)
{
	const PlayerPostRender& rPrevious = rPreviousFrame.postRender.player;
	static constexpr float kfAccelerationDecay = 3.0f;
	static constexpr float kfAcceleration = 100.0f;

	// Load
	PlayerFlags_t flags = rPrevious.flags;
	float fNextBlasterFireTime = rPrevious.fNextBlasterFireTime;
	float fNextSecondarySpawnTime = rPrevious.fNextSecondarySpawnTime;
	float fMissiles = rPrevious.fMissiles;
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
		flags |= kFireBlaster;
	}
	else
	{
		fNextBlasterFireTime = 0.0f;
	}

	// Fire missiles based on input
	if (rFrameInput.flags & kSecondary)
	{
		flags |= kFireMissile;
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

	// Terrain collision - reflect velocity off elevated terrain (bouncy response)
	XMVECTOR vecPosition = rPreviousFrame.interpolate.player.vecPosition;
	float fElevation = engine::gpIslands->GlobalElevation(vecPosition);
	static constexpr float kfPlayerRadius = 1.5f;
	static constexpr float kfPushMargin = 0.25f;
	float fPushHeight = engine::gBaseHeight.Get() - kfPlayerRadius - kfPushMargin;
	if (fElevation >= fPushHeight) [[unlikely]]
	{
		XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslands->GlobalNormal(vecPosition), 0.0f));
		vecVelocity = XMVector3Reflect(vecVelocity, vecTerrainNormal);
	}

	// Save
	rCurrent.flags = flags;
	rCurrent.fNextBlasterFireTime = fNextBlasterFireTime;
	rCurrent.fNextSecondarySpawnTime = fNextSecondarySpawnTime;
	rCurrent.fMissiles = fMissiles;
	rCurrent.vecVelocity = vecVelocity;
	rCurrent.vecWantedDirection = vecWantedDirection;
	rCurrent.fArmor = fArmor;
	rCurrent.fShield = fShield;
	rCurrent.fShieldCooldown = fShieldCooldown;
	rCurrent.fDestroyedExplosionTime = fDestroyedExplosionTime;
	rCurrent.fShieldDownSoundCooldown = fShieldDownSoundCooldown;
}

void PlayerPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void PlayerPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	PlayerInterpolate& rCurrentInterpolate = rFrame.interpolate.player;
	PlayerPostRender& rCurrentPostRender = rFrame.postRender.player;

	// Spawn blasters
	if (rCurrentPostRender.flags & kFireBlaster)
	{
		static constexpr float kfBlasterFireInterval = 0.05f;
		static constexpr float kfBlastersSpeed = 175.0f;
		static constexpr float kfBlastersSpawnBarrelOffset = 0.7f;
		static constexpr float kfBlastersSpawnPreMove = 0.75f;
		static constexpr float kfBlasterAngleJitter = 0.03f;

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
			BlastersPostRender::Spawn(rFrame, rPreviousFrame, fDeltaTime, vecFinalPosition, vecBlasterVelocity, PlayerInterpolate::suiBlasterTypeIndex, {});

			rCurrentPostRender.fNextBlasterFireTime += kfBlasterFireInterval;
		}
	}

	// Spawn missiles
	if (rCurrentPostRender.flags & kFireMissile)
	{
		static constexpr float kfMissileSpawnInterval = 0.1f;
		static constexpr float kfMissileInitialVelocity = 30.0f;
		static constexpr float kfMissileAcceleration = 30.0f;
		static constexpr float kfPreMoveForwards = 1.0f;

		rCurrentPostRender.flags.Clear(kFireMissile);

		// Regenerate missile capacity
		rCurrentPostRender.fMissiles = 1.0f; // DT: TEMP  std::min(rCurrentPostRender.fMissiles + fDeltaTime, MissileCapacity(rFrame));

		// Decrement timer and spawn missile if ready
		rCurrentPostRender.fNextSecondarySpawnTime -= fDeltaTime;

		if (rCurrentPostRender.fNextSecondarySpawnTime < 0.0f && rCurrentPostRender.fMissiles >= 1.0f && !(rCurrentPostRender.flags & kExploding))
		{
			rCurrentPostRender.fNextSecondarySpawnTime = kfMissileSpawnInterval;
			rCurrentPostRender.fMissiles -= 1.0f;

			XMVECTOR vecMissileDirection = rCurrentPostRender.vecWantedDirection;
			XMVECTOR vecMissilePosition = rCurrentInterpolate.vecPosition + kfPreMoveForwards * vecMissileDirection;
			XMVECTOR vecMissileVelocity = XMVectorReplicate(kfMissileInitialVelocity) * vecMissileDirection;

			MissilesPostRender::Spawn(rFrame, rPreviousFrame, fDeltaTime, vecMissilePosition, vecMissileDirection, vecMissileVelocity, Frame::GetMissileTarget(rFrame, vecMissilePosition, vecMissileDirection, TargetFlags::kTargetIsEnemy), kfMissileAcceleration, MissileFlags::kTargetEnemy);
		}
	}
	else
	{
		// Still regenerate missiles when not firing
		rCurrentPostRender.fMissiles = std::min(rCurrentPostRender.fMissiles + fDeltaTime, MissileCapacity(rFrame));
	}

	// Spawn death explosions
	if ((rCurrentPostRender.flags & kExploding) && rCurrentPostRender.fDestroyedExplosionTime <= 0.0f && rCurrentInterpolate.fDestroyedTime > 0.0f)
	{
		rCurrentPostRender.fDestroyedExplosionTime = kfDestroyExplosionInterval;

		float fPercent = rCurrentInterpolate.fDestroyedTime / kfDestroyTime;

		// Random direction for explosion
		XMVECTOR vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(common::Random<XM_2PI>(rFrame.postRender.randomEngine)));

		// Jittered position
		XMVECTOR vecJitteredPosition = XMVectorAdd(
			XMVectorSet(
				-kfExplosionPositionJitter + common::Random<2.0f * kfExplosionPositionJitter>(rFrame.postRender.randomEngine),
				-kfExplosionPositionJitter + common::Random<2.0f * kfExplosionPositionJitter>(rFrame.postRender.randomEngine),
				0.0f, 0.0f),
			rCurrentInterpolate.vecPosition);

		// Jittered direction
		XMVECTOR vecJitteredDirection = XMVector3Normalize(XMVectorAdd(
			XMVectorSet(
				-kfExplosionDirectionJitter + common::Random<2.0f * kfExplosionDirectionJitter>(rFrame.postRender.randomEngine),
				-kfExplosionDirectionJitter + common::Random<2.0f * kfExplosionDirectionJitter>(rFrame.postRender.randomEngine),
				0.0f, 0.0f),
			vecDirection));

		// Radial offset based on time
		float fAdjustedPercent = (std::pow((1.0f - fPercent) + 1.0f, 0.3f) - 1.0f) * kfExplosionsRadius;
		vecJitteredPosition = XMVectorMultiplyAdd(vecJitteredDirection, XMVectorReplicate(fAdjustedPercent), vecJitteredPosition);

		engine::ExplosionsPostRender::Spawn(
			rFrame,
			rFrame.interpolate.fCurrentTime,
			PlayerInterpolate::suiExplosionTypeIndex,
			vecJitteredPosition,
			vecJitteredDirection,
			{engine::ExplosionFlags::kDestroysSelf, engine::ExplosionFlags::kYellow},
			2,
			fPercent * XM_PIDIV2,
			static_cast<uint32_t>(fPercent * kfExplosionParticleCount),
			fPercent * XM_PIDIV2,
			fPercent * kfExplosionIntensity,
			1.0f,
			fPercent * kfExplosionSizeStart + (1.0f - fPercent) * kfExplosionSizeEnd,
			fPercent * kfExplosionSmoke,
			fPercent);
	}
}

void PlayerPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	PlayerInterpolate& rCurrentInterpolate = rFrame.interpolate.player;

	// Add player layer to CollisionSystem
	siCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = &rCurrentInterpolate.vecPosition,
		.iCount = 1,
		.uiCategory = game::CollisionCategory::kPlayer,
		.uiCollidesWith = game::CollisionMask::kPlayer,
		.fUniformRadius = 1.5f,
	});
}

static void XM_CALLCONV ApplyDamage(PlayerInterpolate& rPlayerInterpolate, PlayerPostRender& rPlayer, float fDamage, FXMVECTOR vecDamagePosition, float fHexShieldIntensity = 1.0f)
{
	// Shield absorbs damage first
	if (rPlayer.fShield > 0.0f)
	{
		// Play shield hit sound with pitch based on remaining shield
		engine::gpAudioManager->PlayOneShot3d(data::kAudioShieldArmor465540__steaq__scifishieldhitwavwavCrc, vecDamagePosition, 0.1f + 0.1f * (1.0f - rPlayer.fShield / kfPlayerShield));

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
		float fPenetration = kfPlayerShieldPenetration * fShieldDamage;
		rPlayer.fShield -= fShieldDamage;
		fDamage = fDamage - fShieldDamage + fPenetration;

		if (rPlayer.fShield <= 0.0f)
		{
			rPlayer.fShieldCooldown = 2.0f; // Cooldown before regen starts

			// Play shield down sound with cooldown to prevent spam
			if (rPlayer.fShieldDownSoundCooldown <= 0.0f)
			{
				rPlayer.fShieldDownSoundCooldown = 2.0f;
				engine::gpAudioManager->PlayOneShot(data::kAudioShieldArmor570852__rafaelzimrp__magicshielddownwavCrc, false, 0.1f);
			}
		}
	}

	// Remaining damage goes to armor
	if (fDamage > 0.0f)
	{
		// Play armor hit sound with pitch based on remaining armor (only for significant damage)
		if (fDamage > 3.0f)
		{
			engine::gpAudioManager->PlayOneShot3d(data::kAudioShieldArmor330629__stormwaveaudio__scififorcefieldimpact15wavCrc, vecDamagePosition, 0.2f + 0.5f * (1.0f - rPlayer.fArmor / kfPlayerArmor));
		}

	#if !defined(ENABLE_INVINCIBILITY)
		rPlayer.fArmor -= fDamage;
		gpCamera->fCameraShake = std::min(gpCamera->fCameraShake + 0.25f, 1.0f);
	#endif
	}
}

void PlayerPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
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
		const auto* pCollisions = engine::Collision::GetCollisions(siCollisionLayerIndex, 0);
		for (const auto& rResult : *pCollisions)
		{
			if (rResult.uiOtherCategory == game::CollisionCategory::kSpaceship)
			{
				rCurrentPostRender.flags |= kExploding;
				rCurrentInterpolate.fDestroyedTime = kfDestroyTime;
				rCurrentPostRender.fDestroyedExplosionTime = kfDestroyExplosionInterval;
			}
			else if (rResult.uiOtherCategory == game::CollisionCategory::kBlasterSpaceship)
			{
				ApplyDamage(rCurrentInterpolate, rCurrentPostRender, rResult.fDamageReceived, rResult.vecContactPoint);

				// Spawn impact VFX at contact point
				engine::PuffsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, PlayerInterpolate::suiImpactPuffControllerTypeIndex, rResult.vecContactPoint);
				engine::PointLightsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, PlayerInterpolate::suiImpactPointLightControllerTypeIndex, rResult.vecContactPoint, 0.0f);
			}
		}
	}

	// Check for death
	if (rCurrentPostRender.fArmor <= 0.0f)
	{
		rCurrentPostRender.flags |= kExploding;
		rCurrentInterpolate.fDestroyedTime = kfDestroyTime;
		rCurrentPostRender.fDestroyedExplosionTime = kfDestroyExplosionInterval;
	}
}

void PlayerPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void PlayerInterpolate::AllocatePipelines()
{
	engine::Buffer* pStorageBuffers = engine::gpBufferManager->CreateDynamicBuffer(kCrc, kpcName, sizeof(shaders::ObjectLayout));
	engine::gpPipelineManager->CreateDynamicGltfPipeline(kCrc, kpcName, data::kGltfspaceship2scenegltfCrc, data::kGltfspaceship2scenegltfGLTF_MODELCrc, pStorageBuffers);
	engine::gpPipelineManager->CreateDynamicGltfPipelineShadow(kCrc, kpcName, data::kGltfspaceship2scenegltfCrc, data::kGltfspaceship2scenegltfGLTF_MODELCrc, pStorageBuffers);
}

void PlayerInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	const PlayerInterpolate& rCurrent = rFrameInterpolate.player;

	constexpr float kfSize = 0.5f;
	// DT: TEMP float fSize = (flags & kExploding ? std::pow(fDestroyedTime / kfDestroyTime, 2.0f) : 1.0f) * kfSize;
	float fSize = kfSize;
	auto matScaling = XMMatrixScaling(fSize, fSize, fSize);
	auto matTranslation = XMMatrixTranslationFromVector(rCurrent.vecPosition);
	auto matRotationX = XMMatrixRotationX(XM_PIDIV2);
	auto matRotationY = XMMatrixRotationY(0.0f);
	auto matRotationZ = common::RotationMatrixFromDirection(rCurrent.vecDirection, XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));
	// DT: TEMP Add RotationX / RotationY to visual section of Interpolate
	// auto matRotationAccelerationX = XMMatrixRotationY(std::clamp(0.015f * XMVectorGetX(vecVelocity), -0.4f, 0.4f));
	// auto matRotationAccelerationY = XMMatrixRotationX(std::clamp(-0.015f * XMVectorGetY(vecVelocity), -0.4f, 0.4f));
	auto matRotationAccelerationX = XMMatrixIdentity();
	auto matRotationAccelerationY = XMMatrixIdentity();
	auto matTransform = XMMatrixMultiply(matRotationX, XMMatrixMultiply(matRotationY, XMMatrixMultiply(matRotationZ, XMMatrixMultiply(matRotationAccelerationX, XMMatrixMultiply(matRotationAccelerationY, XMMatrixMultiply(matScaling, matTranslation))))));

	// DT: TEMP Add display-only flag in Interpolate? Or position in Interpolate
	/* if (rFrame.flags & FrameFlags::kMainMenu)
	{
		matTransform = XMMatrixSet(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	} */

	auto pPlayerLayouts = reinterpret_cast<shaders::GltfLayout*>(engine::gpBufferManager->mDynamicStorageBuffers.at(kCrc)[iCommandBuffer].mpMappedMemory);
	shaders::GltfLayout& rPlayerLayout = pPlayerLayouts[0];
	XMStoreFloat4(&rPlayerLayout.f4Position, rCurrent.vecPosition);
	XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rPlayerLayout.f3x4Transform[0]), matTransform);
	XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rPlayerLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
	rPlayerLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 1.0f};
	engine::gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 1);
	engine::gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 1);

	// DT: TODO Remove ENABLE_GLTF_TEST
#if defined(ENABLE_GLTF_TEST)
	auto pGltfLayouts = reinterpret_cast<shaders::GltfLayout*>(engine::gpBufferManager->mGltfsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	shaders::GltfLayout& rGltfLayout = *pGltfLayouts;

	static constexpr float kfSize2 = 2.0f;
	static auto sMatPreMove = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
	static auto sMatPreRotate = XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f);
	matTranslation = XMMatrixTranslationFromVector(vecPosition + XMVectorSet(20.0f, 0.0f, 0.0f, 0.0f));
	matScaling = XMMatrixScaling(kfSize2, kfSize2, kfSize2);
	matRotationAccelerationX = XMMatrixRotationY(0.2f * XMVectorGetX(vecVelocity));
	matRotationAccelerationY = XMMatrixRotationX(-0.2f * XMVectorGetY(vecVelocity));
	matTransform = sMatPreMove * sMatPreRotate * XMMatrixMultiply(matRotationX, XMMatrixMultiply(matRotationY, XMMatrixMultiply(matRotationZ, XMMatrixMultiply(matRotationAccelerationX, XMMatrixMultiply(matRotationAccelerationY, XMMatrixMultiply(matScaling, matTranslation))))));
	XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4Transform[0]), matTransform);
	XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
	rGltfLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 0.0f};

	gpGltfPipelines->mpGltfPipelines[kGltfPipelineTest].WriteIndirectBuffer(iCommandBuffer, 1);
#endif
}

bool PlayerInterpolate::operator==(const PlayerInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual(vecPosition, rOther.vecPosition);
	bEqual &= common::BreakOnNotEqual(vecDirection, rOther.vecDirection);
	bEqual &= common::BreakOnNotEqual(fDestroyedTime, rOther.fDestroyedTime);
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
	checksum ^= common::Crc(rCurrent.vecDirection);
	checksum ^= common::Crc(rCurrent.fDestroyedTime);
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
	common::Write(rStream, vecDirection);
	common::Write(rStream, fDestroyedTime);
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
	common::Read(rStream, vecDirection);
	common::Read(rStream, fDestroyedTime);
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
	bEqual &= common::BreakOnNotEqual(fNextBlasterFireTime, rOther.fNextBlasterFireTime);
	bEqual &= common::BreakOnNotEqual(fNextSecondarySpawnTime, rOther.fNextSecondarySpawnTime);
	bEqual &= common::BreakOnNotEqual(fMissiles, rOther.fMissiles);
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
	checksum ^= common::Crc(rCurrent.fNextBlasterFireTime);
	checksum ^= common::Crc(rCurrent.fNextSecondarySpawnTime);
	checksum ^= common::Crc(rCurrent.fMissiles);
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
	common::Write(rStream, fNextBlasterFireTime);
	common::Write(rStream, fNextSecondarySpawnTime);
	common::Write(rStream, fMissiles);
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
	common::Read(rStream, fNextBlasterFireTime);
	common::Read(rStream, fNextSecondarySpawnTime);
	common::Read(rStream, fMissiles);
	common::Read(rStream, vecVelocity);
	common::Read(rStream, vecWantedDirection);
	common::Read(rStream, fArmor);
	common::Read(rStream, fShield);
	common::Read(rStream, fShieldCooldown);
	common::Read(rStream, fDestroyedExplosionTime);
	common::Read(rStream, fShieldDownSoundCooldown);
}

} // namespace game
