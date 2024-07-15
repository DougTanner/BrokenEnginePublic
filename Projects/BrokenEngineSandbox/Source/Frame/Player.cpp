#include "Player.h"

#include "Audio/AudioManager.h"
#include "Frame/Render.h"
#include "Graphics/Islands.h"
#include "Graphics/Managers/ParticleManager.h"
#include "Profile/ProfileManager.h"
#include "Ui/Wrapper.h"

#include "Game.h"

using namespace DirectX;

using enum engine::ExplosionFlags;
using enum engine::TargetFlags;

namespace game
{

using enum BlasterFlags;
using enum PlayerFlags;

constexpr float kfAcceleration = 65.0f;

// Hit by blasters
constexpr float kfBlasterCollisionRadiusShield = 2.0f;
constexpr float kfBlasterCollisionRadius = 1.0f;

constexpr int64_t kiImpactParticleCount = 32;
constexpr int32_t kiImpactParticleCookie = 4;
constexpr float kfImpactParticlePositionRandom = 0.01f;
constexpr float kfImpactParticleVelocityMin = 2.5f;
constexpr float kfImpactParticleVelocityRandom = 10.0f;
constexpr float kfImpactParticleVerticalVelocity = 5.0f;
constexpr float kfImpactParticleVelocityDecay = 5.0f;
constexpr float kfImpactParticleGravity = -15.0f;
constexpr float kfImpactParticleWidth = 0.05f;
constexpr float kfImpactParticleLength = 0.2f;
constexpr float kfImpactParticleIntensityMin = 0.5f;
constexpr float kfImpactParticleIntensityRandom = 2.0f;
constexpr float kfImpactParticleIntensityDecay = 0.75f;
constexpr float kfImpactParticleIntensityPower = 4.0f;
constexpr float kfImpactParticleAngle = 2.0f;
constexpr float kfImpactParticleLightingSize = 5.0f;
constexpr float kfImpactParticleLightingIntesnity = 1000.0f;

// Exploding
constexpr float kfDestroyTime = 0.7f;
constexpr float kfDestroyVelocity = 30.0f;
constexpr float kfDestroyExplosionInterval = 0.005f;
constexpr float kfExplosionsRadius = 30.0f;
constexpr float kfExplosionPositionJitter = 1.0f;
constexpr float kfExplosionDirectionJitter = 0.5f;
constexpr float kfExplosionIntensity = 1.5f;
constexpr float kfExplosionParticleCount = 16.0f;
constexpr float kfExplosionSizeStart = 2.0f;
constexpr float kfExplosionSizeEnd = 0.5f;
constexpr float kfExplosionSmoke = 0.25f;

// Armor pickup
constexpr float kfArmorPickupDelay = 0.5f;
constexpr float kfArmorPickupSizeRadius = 4.0f;
constexpr float kfArmorPickupSizeMin = 0.5f;
constexpr float kfArmorPickupRadius = 1.0f;
constexpr float kfArmorPickupSpeed = 50.0f;
constexpr float kfArmorPickupRegen = 2.0f;
constexpr float kfArmorPickupPullRadius = 15.0f;
constexpr float kfArmorPickupFadeStart = 2.0f;
constexpr float kfArmorPickupFade = 2.0f;

// Shell
constexpr float kfShellTime = 5.0f;
constexpr float kfShellArea = 2.0f;

// Dash
constexpr float kfDashTime = 0.45f;
constexpr float kfDashEnergy = 10.0f;

float Player::MaxArmor([[maybe_unused]] const Frame& __restrict rFrame) { return kfPlayerArmor; }
float Player::MaxShield([[maybe_unused]] const Frame& __restrict rFrame) { return kfPlayerShield; }
float Player::MaxEnergy([[maybe_unused]] const Frame& __restrict rFrame) { return kfPlayerEnergy; }
float Player::MissileCapacity([[maybe_unused]] const Frame& __restrict rFrame) { return kfPlayerMissileCapacity; }

std::tuple<int64_t, int64_t> Player::SecondaryCapacity(const Frame& __restrict rFrame)
{
	return std::make_tuple(static_cast<int64_t>(rFrame.player.fMissiles), static_cast<int64_t>(MissileCapacity(rFrame)));
}

void Player::Global([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	Player& rCurrent = rFrame.player;
	const Player& rPrevious = rPreviousFrame.player;

	// Position
	if (!(rPrevious.flags & kExploding)) [[likely]]
	{
		rCurrent.vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPrevious.vecVelocity, rPrevious.vecPosition);
	}
	else
	{
		rCurrent.vecPosition = rPrevious.vecPosition;
	}

	rCurrent.vecPosition = XMVectorSetZ(rCurrent.vecPosition, engine::gBaseHeight.Get());
}

void Player::InterpolateDash([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	Player& rCurrent = rFrame.player;

	// Area light
	if (rCurrent.flags & kExploding || rCurrent.fSkillTime <= 0.0f) [[likely]]
	{
		rFrame.areaLights.Remove(rCurrent.uiDashAreaLight);
	}
	else
	{
		constexpr float kfDashVisibleIntensity = 1.0f;
		constexpr float kfDashLightingArea = 2.0f;
		constexpr float kfDashLightingIntensity = 800.0f;
		constexpr float kfDashLength = 2.25f;
		constexpr float kfDashWidth = 3.25f;
		constexpr float kfDashOffset = 3.5f;

		float fDashPercent = std::pow(rCurrent.fSkillTime / kfDashTime, 0.5f);
		float fLength = kfDashLength;
		float fWidth = fDashPercent * kfDashWidth;
		auto vecDirection = rCurrent.vecDashDirection;

		auto vecOffset = XMVectorMultiply(XMVectorReplicate(kfDashOffset), XMVector3Normalize(vecDirection));
		const auto [vecTopLeftVisible, vecTopRightVisible, vecBottomLeftVisible, vecBottomRightVisible] = common::CalculateArea(XMVectorAdd(rCurrent.vecPosition, vecOffset), vecDirection, 0.0f, fLength, fWidth);
		const auto [vecTopLeftLighting, vecTopRightLighting, vecBottomLeftLighting, vecBottomRightLighting] = common::CalculateArea(XMVectorAdd(rCurrent.vecPosition, vecOffset), vecDirection, 0.0f, kfDashLightingArea * fLength, kfDashLightingArea * fWidth);
		rFrame.areaLights.Add(rCurrent.uiDashAreaLight,
		{
			.crc = data::kTexturesDashBC7Dash0pngCrc,
			.pf2Texcoords = { {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, },
			.pVecVisiblePositions = {vecTopLeftVisible, vecTopRightVisible, vecBottomLeftVisible, vecBottomRightVisible},
			.fVisibleIntensity = fDashPercent * kfDashVisibleIntensity,
			.pVecLightingPositions = {vecTopLeftLighting, vecTopRightLighting, vecBottomLeftLighting, vecBottomRightLighting},
			.fLightingIntensity = fDashPercent * kfDashLightingIntensity,
		});
	}
}

void Player::Interpolate([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	Player& rCurrent = rFrame.player;
	const Player& rPrevious = rPreviousFrame.player;

	auto vecGlobalPosition = rCurrent.vecPosition;
	rCurrent = rPreviousFrame.player;
	rCurrent.vecPosition = vecGlobalPosition;

	rCurrent.fSkillTime -= fDeltaTime;
	rCurrent.fShieldCooldown -= fDeltaTime;

	constexpr float kfShieldShrink = 1.5f;
	rCurrent.fShieldShrink = std::clamp(rCurrent.fShieldShrink + (rCurrent.fShield > 0.0f ? fDeltaTime * kfShieldShrink : fDeltaTime * -kfShieldShrink), 0.0f, 1.0f);

	// Direction
	constexpr float kfRotateTowards = 10.0f;

	rCurrent.vecDirection = common::RotateTowardsPercent(rCurrent.vecDirection, rPrevious.vecWantedDirection, fDeltaTime * kfRotateTowards);

	// Decay velocity, then add acceleration
	float fAcceleration = kfAcceleration;
	auto vecAcceleration = XMVectorReplicate(fDeltaTime * fAcceleration);
	vecAcceleration = XMVectorMultiply(vecAcceleration, XMVector3Normalize(XMVectorSet(rFrameInputHeld.f2MovePlayer.x, rFrameInputHeld.f2MovePlayer.y, 0.0f, 0.0f)));
	rCurrent.vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(1.0f - 3.0f * fDeltaTime), rCurrent.vecVelocity, vecAcceleration);

	float fElevation = engine::gpIslands->GlobalElevation(rCurrent.vecPosition);
	float fPushHeight = engine::gBaseHeight.Get() - 0.5f;
	if (fElevation >= fPushHeight) [[unlikely]]
	{
		auto vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslands->GlobalNormal(rCurrent.vecPosition), 0.0f));
		rCurrent.vecVelocity = XMVector3Reflect(rCurrent.vecVelocity, vecTerrainNormal);
		rCurrent.vecPosition += 0.005f * vecTerrainNormal;
	}

	// Armor pull
	for (decltype(rFrame.billboards.uiMaxIndex) i = 0; i <= rFrame.billboards.uiMaxIndex; ++i)
	{
		if (!rFrame.billboards.pbUsed[i])
		{
			continue;
		}

		engine::BillboardInfo& rBillboardInfo = rFrame.billboards.pObjectInfos[i];
		engine::Billboard& rBillboard = rFrame.billboards.pObjects[i];
		
		if (!(rBillboardInfo.flags & engine::BillboardFlags::kTypeArmor))
		{
			continue;
		}

		rBillboard.fTime += fDeltaTime;
		if (rBillboard.fTime < kfArmorPickupDelay)
		{
			continue;
		}

		auto vecToPlayer = XMVectorSubtract(rCurrent.vecPosition, rBillboardInfo.vecPosition);
		float fDistance = XMVectorGetX(XMVector3Length(vecToPlayer));

		if (fDistance < kfArmorPickupPullRadius)
		{
			auto vecToPlayerNormal = XMVector3Normalize(vecToPlayer);
			rBillboardInfo.vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfArmorPickupSpeed), vecToPlayerNormal, rBillboardInfo.vecPosition);
			rBillboardInfo.fAlpha = 1.0f;
		}
		else if (rBillboard.fTime > kfArmorPickupFadeStart)
		{
			rBillboardInfo.fAlpha -= fDeltaTime * kfArmorPickupFade;
		}
	}

	// Hex shield
	if (!(rCurrent.flags & kExploding)) [[likely]]
	{
		rCurrent.fShieldRotation += fDeltaTime * 4.0f;

		XMStoreFloat4(&rCurrent.hexShieldLayout.f4Position, rCurrent.vecPosition);
		auto matRotation = XMMatrixRotationZ(rCurrent.fShieldRotation);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rCurrent.hexShieldLayout.f3x4Transform[0]), matRotation);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rCurrent.hexShieldLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matRotation)));
		rCurrent.hexShieldLayout.f4Color = XMFLOAT4 {0.0f, 1.0f, 1.0f, 0.25f};
		rCurrent.hexShieldLayout.f4LightingColor = XMFLOAT4 {0.25f, 1.0f, 1.0f, 0.0f};
		rCurrent.hexShieldLayout.fLightingIntensity = 125.0f;
		rCurrent.hexShieldLayout.fSize = rCurrent.fShieldShrink * 0.1f;
		rCurrent.hexShieldLayout.fColorMix = 0.25f;
		rCurrent.hexShieldLayout.fMinimumIntensity = 0.0f;

		for (int64_t i = 0; i < shaders::kiHexShieldDirections; ++i)
		{
			rCurrent.hexShieldLayout.pfVertIntensities[i] = std::max(rCurrent.hexShieldLayout.pfVertIntensities[i] - 1.25f * fDeltaTime, 0.0f);
			rCurrent.hexShieldLayout.pfFragIntensities[i] = std::max(rCurrent.hexShieldLayout.pfFragIntensities[i] - 1.25f * fDeltaTime, 0.0f);
		}

		rFrame.hexShields.Add(rCurrent.uiHexShield,
		{
			.hexShieldLayout = rCurrent.hexShieldLayout,
		});
	}
	else
	{
		rFrame.hexShields.Remove(rCurrent.uiHexShield);
	}

	// Target
	rFrame.targets.Add(rCurrent.uiTarget,
	{
		.flags = {kDestination, kTargetIsPlayer},
		.vecPosition = rCurrent.vecPosition,
	});

	// Spotlight
	static constexpr float kfSpotlightStart = XM_PI - XM_PIDIV16;
	static constexpr float kfSpotlightEnd = XM_PIDIV32;
	static constexpr float kfSpotlightRange = (XM_2PI - kfSpotlightStart) + kfSpotlightEnd;
	float fSpotlightPercent = 0.0f;
	if (rFrame.fSunAngle >= kfSpotlightStart)
	{
		fSpotlightPercent = (rFrame.fSunAngle - kfSpotlightStart) / kfSpotlightRange;
	}
	else if (rFrame.fSunAngle <= kfSpotlightEnd)
	{
		fSpotlightPercent = (XM_2PI - kfSpotlightStart + rFrame.fSunAngle) / kfSpotlightRange;
	}
	if (fSpotlightPercent > 0.5f)
	{
		fSpotlightPercent = 1.0f - fSpotlightPercent;
	}
	fSpotlightPercent *= 2.0f;
	fSpotlightPercent = std::pow(fSpotlightPercent, 0.1f);

	if (rCurrent.flags & kExploding || fSpotlightPercent <= 0.0f)
	{
		rFrame.areaLights.Remove(rCurrent.uiSpotlightAreaLight);
	}
	else
	{
		std::optional<XMVECTOR> optionalClosestEnemy = Frame::ClosestEnemy(rFrame, rCurrent.vecPosition);

		if (optionalClosestEnemy.has_value())
		{
			constexpr float kfLight = 2.0f;
			constexpr float kfSpotlightVisibleIntensity = kfLight * 0.045f;
			constexpr float kfSpotlightVisibleLength = 1.5f;
			constexpr float kfSpotlightVisibleWidth = 1.25f;
			constexpr float kfSpotlightLightingLength = 2.5f;
			constexpr float kfSpotlightLightingWidth = 2.5f;
			constexpr float kfSpotlightLightingIntensity = kfLight * 120.0f;
			constexpr float kfSpotlightLength = 30.0f;
			constexpr float kfSpotlightWidth = 7.5f;
			constexpr float kfSpotlightOffset = 0.5f;
			constexpr float kfMinimumLighting = 0.25f;

			float fLength = fSpotlightPercent * kfSpotlightLength;
			float fWidth = fSpotlightPercent * kfSpotlightWidth;
			auto vecWantedDirection = common::DirectionTo(rCurrent.vecPosition, optionalClosestEnemy.value());
			static constexpr float kfSpotlightRotateTowards = 4.0f;
			rCurrent.vecSpotlightDirection = common::RotateTowardsPercent(rCurrent.vecSpotlightDirection, vecWantedDirection, fDeltaTime * kfSpotlightRotateTowards);

			auto vecOffset = XMVectorMultiply(XMVectorReplicate(kfSpotlightOffset), rCurrent.vecSpotlightDirection);
			const auto [vecTopLeftVisible, vecTopRightVisible, vecBottomLeftVisible, vecBottomRightVisible] = common::CalculateArea(XMVectorAdd(rCurrent.vecPosition, vecOffset), rCurrent.vecSpotlightDirection, kfSpotlightVisibleLength * fLength, 0.0f, kfSpotlightVisibleWidth * fWidth);
			const auto [vecTopLeftLighting, vecTopRightLighting, vecBottomLeftLighting, vecBottomRightLighting] = common::CalculateArea(XMVectorAdd(rCurrent.vecPosition, vecOffset), rCurrent.vecSpotlightDirection, kfSpotlightLightingLength * fLength, 0.0f, kfSpotlightLightingWidth * fWidth);
			rFrame.areaLights.Add(rCurrent.uiSpotlightAreaLight,
			{
				.crc = data::kTexturesBC4SpotlightpngCrc,
				.pf2Texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},},
				.pVecVisiblePositions = {vecTopLeftVisible, vecTopRightVisible, vecBottomLeftVisible, vecBottomRightVisible},
				.fVisibleIntensity = fSpotlightPercent * kfSpotlightVisibleIntensity,
				.pVecLightingPositions = {vecTopLeftLighting, vecTopRightLighting, vecBottomLeftLighting, vecBottomRightLighting},
				.fLightingIntensity = fSpotlightPercent * kfSpotlightLightingIntensity,
				.vecDirectionMultipliers = XMVectorMax(engine::DirectionToDirectionMultipliers(rCurrent.vecSpotlightDirection), XMVectorReplicate(kfMinimumLighting)),
			});
		}
	}

	if (rCurrent.flags & kExploding) [[unlikely]]
	{
		return;
	}

	InterpolateDash(rFrame, rPreviousFrame, rFrameInputHeld, fDeltaTime);
}

void XM_CALLCONV SpawnPlayerExplosion(Frame& __restrict rFrame, float fPercent, FXMVECTOR vecDirection)
{
	Player& rCurrent = rFrame.player;

	auto vecPosition = XMVectorAdd(XMVectorSet(-kfExplosionPositionJitter + common::Random<2.0f * kfExplosionPositionJitter>(rFrame.randomEngine), -kfExplosionPositionJitter + common::Random<2.0f * kfExplosionPositionJitter>(rFrame.randomEngine), 0.0f, 0.0f), rCurrent.vecPosition);
	auto vecFinalDirection = XMVector3Normalize(XMVectorAdd(XMVectorSet(-kfExplosionDirectionJitter + common::Random<2.0f * kfExplosionDirectionJitter>(rFrame.randomEngine), -kfExplosionDirectionJitter + common::Random<2.0f * kfExplosionDirectionJitter>(rFrame.randomEngine), 0.0f, 0.0f), vecDirection));

	float fAdjustedPercent = (std::pow((1.0f - fPercent) + 1.0f, 0.3f) - 1.0f) * kfExplosionsRadius;
	vecPosition = XMVectorMultiplyAdd(vecFinalDirection, XMVectorReplicate(fAdjustedPercent), vecPosition);

	engine::explosion_t uiExplosion = 0;
	rFrame.explosions.Add(uiExplosion, rFrame,
	{
		.flags = {kDestroysSelf, kYellow},
		.vecPosition = vecPosition,
		.vecDirection = vecFinalDirection,
		.uiParticleCount = static_cast<uint32_t>(fPercent * kfExplosionParticleCount),
		.fParticleAngle = fPercent * XM_PIDIV2,
		.uiTrailCount = 2,
		.fTrailAngle = fPercent * XM_PIDIV2,
		.fLightPercent = fPercent * kfExplosionIntensity,
		.fPusherPercent = 1.0f,
		.fSizePercent = fPercent * kfExplosionSizeStart + (1.0f - fPercent) * kfExplosionSizeEnd,
		.fSmokePercent = fPercent * kfExplosionSmoke,
		.fTimePercent = fPercent,
	});
}

void Player::PostRenderBlasters([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime, [[maybe_unused]] std::optional<XMVECTOR>& rOptionalClosestEnemy, [[maybe_unused]] bool bClosestIsVisible)
{
	static constexpr float kfBlasterSpawnInterval = 0.21f;
	static constexpr float kfBlastersSpeed = 70.0f;
	static constexpr float kfBlastersSpawnBarrelOffset = 0.7f;
	static constexpr float kfBlastersSpawnPreMove = 0.03f;
	static constexpr float kfBlastersWidth = 0.11f;
	static constexpr float kfBlastersLength = 1.5f;
	static constexpr float kfBlasterVisibleIntensity = 1.5f;
	static constexpr float kfBlasterLightingArea = 2.75f;
	static constexpr float kfBlasterLightingIntensity = 4000.0f;

	Player& rCurrent = rFrame.player;

	rCurrent.fNextPrimarySpawnTime -= fDeltaTime;

	if (rFrameInput.pressedFlags & FrameInputPressedFlags::kTogglePrimary)
	{
		rCurrent.bBlasterToggledOn = !rCurrent.bBlasterToggledOn;
	}

	// Spawn blasters
	bool bSpawnBlasters = false;
	if (!rFrameInput.held.bGamepad && (rFrame.flags & FrameFlags::kPrimaryToggle))
	{
		bSpawnBlasters = rCurrent.bBlasterToggledOn;
	}
	else
	{
		bSpawnBlasters = rFrameInput.held.flags & FrameInputHeldFlags::kPrimary;
	}

	auto vecBlasterDirection = rFrameInput.held.vecDirection;

	if (bSpawnBlasters && rCurrent.fNextPrimarySpawnTime < 0.0f)
	{
		rCurrent.fNextPrimarySpawnTime = kfBlasterSpawnInterval;

		auto vecBlasterVelocity = XMVectorMultiply(XMVectorSet(kfBlastersSpeed, kfBlastersSpeed, 0.0f, 0.0f), vecBlasterDirection);

		auto vecBlasterPosition = rCurrent.vecPosition + kfBlastersSpawnPreMove * vecBlasterVelocity;
		auto vecLeftNormal = XMVector3Normalize(XMVector3Cross(XMVector3Normalize(vecBlasterVelocity), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));

		SpawnBlaster spawnBlaster
		{
			.flags = {kCollideEnemies, kSizeFromSpeed},
			.crc = data::kTexturesBlasterBC74pngCrc,
			.vecPosition = vecBlasterPosition,
			.vecVelocity = vecBlasterVelocity,
			.f2Size = {kfBlastersWidth, kfBlastersLength},
			.fVisibleIntensity = kfBlasterVisibleIntensity,
			.fLightArea = kfBlasterLightingArea,
			.fLightIntensity = kfBlasterLightingIntensity,
			.fDamage = kfBlasterDamage,
		};

		rCurrent.bBlasterSpawnLeft = !rCurrent.bBlasterSpawnLeft;
		spawnBlaster.vecPosition = XMVectorAdd(vecBlasterPosition, (rCurrent.bBlasterSpawnLeft ? kfBlastersSpawnBarrelOffset : -kfBlastersSpawnBarrelOffset) * vecLeftNormal);
		spawnBlaster.vecVelocity = vecBlasterVelocity;
		rFrame.blasters.AddSpawn(spawnBlaster);
	}
}

void Player::PostRenderMissiles([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime, [[maybe_unused]] std::optional<XMVECTOR>& rOptionalClosestEnemy, [[maybe_unused]] bool bClosestIsVisible)
{
	static constexpr float kfMissileAcceleration = 30.0f;
	static constexpr float kfMissileSpawnInterval = 0.9f;
	static constexpr float kfMissileAngle = 0.9f * XM_PIDIV16;
	static constexpr float kfMissileInitialVelocity = 30.0f;
	static constexpr float kfPreMoveForwards = 1.0f;
	static constexpr float kfPreMoveSideways = 0.2f;

	Player& rCurrent = rFrame.player;

	rCurrent.fNextSecondarySpawnTime -= fDeltaTime;

	// Regenerate missile capacity
	rCurrent.fMissiles = std::min(rCurrent.fMissiles + fDeltaTime, MissileCapacity(rFrame));

	// Spawn missiles
	bool bSpawnMissiles = rFrameInput.held.flags & FrameInputHeldFlags::kSecondary;
	auto vecMissileDirection = rFrameInput.held.vecDirection;

	if (bSpawnMissiles && rCurrent.fNextSecondarySpawnTime < 0.0f && rCurrent.fMissiles >= 1.0f && !(rCurrent.flags & kExploding))
	{
		rCurrent.fNextSecondarySpawnTime = kfMissileSpawnInterval;

		// Position
		auto vecMissilePosition = rCurrent.vecPosition;

		// Velocity from direction
		float fVelocityIncrease = 1.1f;

		int64_t iMissileSpawnCount = 1;
		if (iMissileSpawnCount > 1)
		{
			iMissileSpawnCount -= iMissileSpawnCount % 2;
		}

		float fAngle = iMissileSpawnCount == 1 ? 0.0f : 0.5f * kfMissileAngle;
		float fPreMoveSideways = iMissileSpawnCount == 1 ? 0.0f : 0.5f * kfPreMoveSideways;

		for (int64_t i = 0; i < iMissileSpawnCount; ++i)
		{
			if (rCurrent.fMissiles < 1.0f)
			{
				break;
			}
			rCurrent.fMissiles -= 1.0f;

			auto vecMissileDirectionFinal = XMVector4Transform(vecMissileDirection, XMMatrixRotationZ(fAngle));

			auto vecMissileVelocity = fVelocityIncrease * XMVectorReplicate(kfMissileInitialVelocity) * vecMissileDirectionFinal;

			auto vecMissilePositionFinal = vecMissilePosition + kfPreMoveForwards * XMVector3Normalize(vecMissileDirectionFinal);
			auto vecSideDirection = -XMVector3Cross(vecMissileDirection, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
			vecMissilePositionFinal += XMVectorReplicate(fPreMoveSideways) * vecSideDirection;

			rFrame.missiles.AddSpawn(
			{
				.flags = {MissileFlags::kTargetEnemy, MissileFlags::kTargetEnemy},
				.vecPosition = vecMissilePositionFinal,
				.vecDirection = vecMissileDirectionFinal,
				.vecVelocity = vecMissileVelocity,
				.uiTarget = game::Frame::GetMissileTarget(rFrame, rFrameInput, vecMissilePositionFinal, XMVector3Reflect(-vecMissileDirectionFinal, vecMissileDirection), kTargetIsEnemy),
				.fExplosionRadius = 1.0f,
				.fAcceleration = fVelocityIncrease * kfMissileAcceleration,
			});

			fAngle = (i % 2) == 0 ? -fAngle : -fAngle + kfMissileAngle;
			fPreMoveSideways = (i % 2) == 0 ? -fPreMoveSideways : -fPreMoveSideways + kfPreMoveSideways;
		}
	}
}

void Player::PostRenderDash([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Player& rCurrent = rFrame.player;

	if (rFrameInput.pressedFlags & FrameInputPressedFlags::kToggleSkill && rCurrent.fEnergy >= kfDashEnergy)
	{
		rCurrent.fEnergy -= kfDashEnergy;

		rCurrent.fSkillTime = kfDashTime;

		bool bDashCursorDirection = true;
		if (rFrameInput.held.bGamepad)
		{
			bDashCursorDirection = rFrame.flags & FrameFlags::kDashGamepadFiring ? true : false;
		}
		else
		{
			bDashCursorDirection = rFrame.flags & FrameFlags::kDashMouseCursor ? true : false;
		}
		rCurrent.vecDashDirection = bDashCursorDirection ? rFrameInput.held.vecDirection : XMVector3Normalize(XMVectorSet(rFrameInput.held.f2MovePlayer.x, rFrameInput.held.f2MovePlayer.y, 0.0f, 0.0f));

		constexpr float kfDashSpeed = 100.0f;
		rCurrent.vecVelocity = kfDashSpeed * rCurrent.vecDashDirection;
	}
}

void Player::PostRender([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Player& rCurrent = rFrame.player;

	rCurrent.vecWantedDirection = rFrameInput.held.vecDirection;
	std::optional<XMVECTOR> optionalClosestEnemy = Frame::ClosestEnemy(rFrame, rCurrent.vecPosition);
	bool bClosestIsVisible = optionalClosestEnemy.has_value() && engine::InsideVisibleArea(rFrameInput, optionalClosestEnemy.value());

	rCurrent.fShellTimeLeft -= fDeltaTime;
	rCurrent.fShieldDownSoundCooldown -= fDeltaTime;

	// When max armor increases, increase current armor by same amount
	if (MaxArmor(rFrame) > MaxArmor(rPreviousFrame))
	{
		rCurrent.fArmor += MaxArmor(rFrame) - MaxArmor(rPreviousFrame);
	}

	// Armor pickup
	for (decltype(rFrame.billboards.uiMaxIndex) i = 0; i <= rFrame.billboards.uiMaxIndex; ++i)
	{
		if (!rFrame.billboards.pbUsed[i])
		{
			continue;
		}

		engine::BillboardInfo& rBillboardInfo = rFrame.billboards.pObjectInfos[i];
		engine::Billboard& rBillboard = rFrame.billboards.pObjects[i];

		if (!(rBillboardInfo.flags & engine::BillboardFlags::kTypeArmor))
		{
			continue;
		}

		if (rBillboardInfo.fAlpha <= 0.0f)
		{
			rFrame.billboards.Remove(i);
			continue;
		}

		float fDistance = common::Distance(rCurrent.vecPosition, rBillboardInfo.vecPosition);
		rBillboardInfo.fSize = kfPickupSize * std::max(std::min(fDistance / kfArmorPickupSizeRadius, 1.0f), kfArmorPickupSizeMin);

		if (rBillboard.fTime < kfArmorPickupDelay)
		{
			continue;
		}

		if (fDistance < kfArmorPickupRadius)
		{
			rFrame.billboards.Remove(i);
			rCurrent.fArmor = std::min(rCurrent.fArmor + kfArmorPickupRegen, MaxArmor(rFrame));
		}
	}

	// Regenerate shield
	if (rCurrent.fShieldCooldown < 0.0f)
	{
		rCurrent.fShield = std::min(rCurrent.fShield + fDeltaTime * 1.0f * kfPlayerShieldRegen, MaxShield(rFrame));
	}

	// Regenerate or drain energy
	rCurrent.fEnergy = std::min(rCurrent.fEnergy + fDeltaTime, MaxEnergy(rFrame));

	// Spawn destruction explosions
	rCurrent.fDestroyedTime = std::max(0.0f, rCurrent.fDestroyedTime - fDeltaTime);
	rCurrent.fDestroyedExplosionTime = std::max(0.0f, rCurrent.fDestroyedExplosionTime - fDeltaTime);
	if (rCurrent.flags & kExploding && rCurrent.fDestroyedExplosionTime <= 0.0f)
	{
		rCurrent.fDestroyedExplosionTime = kfDestroyExplosionInterval;

		float fPercent = rCurrent.fDestroyedTime / kfDestroyTime;
		auto vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(common::Random<XM_2PI>(rFrame.randomEngine)));
		SpawnPlayerExplosion(rFrame, fPercent, vecDirection);
	}

	// Apply pushers
	rCurrent.vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rFrame.pushers.ApplyPush(rCurrent.vecPosition, 0, engine::PusherFlags::kTypeMines), rCurrent.vecVelocity);

	if (rCurrent.flags & kExploding) [[unlikely]]
	{
		return;
	}

	// Primary
	PostRenderBlasters(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, optionalClosestEnemy, bClosestIsVisible);

	// Secondary
	PostRenderMissiles(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, optionalClosestEnemy, bClosestIsVisible);

	// Skill
	PostRenderDash(rFrame, rPreviousFrame, rFrameInput, fDeltaTime);
}

void XM_CALLCONV Player::Damage(Frame& __restrict rFrame, float fDamage, FXMVECTOR vecPosition, float fHexShield, bool bSound)
{
	Player& rCurrent = rFrame.player;

	if (rCurrent.fSkillTime > 0.0f)
	{
		return;
	}

	if (fDamage <= 0.0f)
	{
		DEBUG_BREAK();
		return;
	}

	if (rCurrent.fShield > 0.0f)
	{
		if (bSound)
		{
			engine::gpAudioManager->PlayOneShot(data::kAudioShieldArmor465540__steaq__scifishieldhitwavwavCrc, vecPosition, 0.1f + 0.1f * (1.0f - rCurrent.fShield / kfPlayerShield));
		}

		int64_t iLowestIntensityIndex = 0;
		for (int64_t k = 0; k < shaders::kiHexShieldDirections; ++k)
		{
			if (rCurrent.hexShieldLayout.pfFragIntensities[k] < rCurrent.hexShieldLayout.pfFragIntensities[iLowestIntensityIndex])
			{
				iLowestIntensityIndex = k;
			}
		}
		XMStoreFloat4(&rCurrent.hexShieldLayout.pf4Directions[iLowestIntensityIndex], XMVector3Normalize(XMVectorSubtract(vecPosition, rCurrent.vecPosition)));
		rCurrent.hexShieldLayout.pfVertIntensities[iLowestIntensityIndex] = fHexShield;
		rCurrent.hexShieldLayout.pfFragIntensities[iLowestIntensityIndex] = fHexShield;

		float fShieldDamage = std::min(rCurrent.fShield, fDamage);

		// Penetration
		float fShieldPenetration = kfPlayerShieldPenetration;
		fShieldDamage -= fShieldPenetration * fShieldDamage;

		rCurrent.fShield -= fShieldDamage;
		fDamage -= fShieldDamage;

		if (rCurrent.fShield <= 0.0f)
		{
			constexpr float kfShieldCooldown = 2.0f;
			rCurrent.fShieldCooldown = kfShieldCooldown;

			if (rCurrent.fShieldDownSoundCooldown < 0.0f)
			{
				rCurrent.fShieldDownSoundCooldown = 2.0f;
				if (bSound)
				{
					engine::gpAudioManager->PlayOneShot(data::kAudioShieldArmor570852__rafaelzimrp__magicshielddownwavCrc, false, 0.1f);
				}
			}
		}
	}

	if (rCurrent.fShellTimeLeft > 0.0f)
	{
		return;
	}
		
	if (fDamage > 0.0f)
	{
		if (bSound && fDamage > 3.0f)
		{
			engine::gpAudioManager->PlayOneShot(data::kAudioShieldArmor330629__stormwaveaudio__scififorcefieldimpact15wavCrc, vecPosition, 0.2f + 0.5f * (1.0f - rCurrent.fArmor / kfPlayerArmor));
		}

	#if !defined(ENABLE_INVINCIBILITY)
		rCurrent.fArmor -= fDamage;
		rFrame.camera.fCameraShake = std::min(rFrame.camera.fCameraShake + 0.25f, 1.0f);
	#endif

		rCurrent.fShield += fDamage;
	}
}

float XM_CALLCONV Player::AreaDamage(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, float fDamage, const common::AreaVertices& rAreaVertices)
{
	float fAppliedDamage = 0.0f;

	Player& rCurrent = rFrame.player;

	if (common::InsideAreaVertices(rCurrent.vecPosition, rAreaVertices))
	{
		fAppliedDamage = fDamage;
		Player::Damage(rFrame, fDamage, rCurrent.vecPosition, false);
	}

	for (int64_t i = 0; i < rFrame.missiles.iCount; ++i)
	{
		if (rFrame.missiles.pFlags[i] & MissileFlags::kTargetPlayer || rFrame.missiles.pFlags[i] & MissileFlags::kExploding)
		{
			continue;
		}

		if (common::InsideAreaVertices(rFrame.missiles.pVecPositions[i], rAreaVertices))
		{
			Missiles::Explode(rFrame, rFrameInput, i, false);
		}
	}

	return fAppliedDamage;
}

std::tuple<float, XMVECTOR> Player::CollideBlasters(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, DirectX::FXMVECTOR vecPosition, float& rfShield)
{
	float fDamage = 0.0f;
	auto vecImpactPosition = vecPosition;

	for (int64_t j = 0; j < rFrame.blasters.iCount; ++j)
	{
		if (rFrame.blasters.pFlags[j] & kImpactObject || !(rFrame.blasters.pFlags[j] & kCollidePlayer))
		{
			continue;
		}

		float fCollisionRadius = rfShield >= rFrame.blasters.pfDamages[j] ? kfBlasterCollisionRadiusShield : kfBlasterCollisionRadius;
		float fDistance = common::Distance(rFrame.blasters.pVecPositions[j], vecPosition);
		if (fDistance > fCollisionRadius) [[likely]]
		{
			continue;
		}

		// Set impact position on the edge, in the direction of the blaster
		auto vecToPreviousPositionNormal = XMVector3Normalize(rPreviousFrame.blasters.pVecPositions[j] - vecPosition);
		vecImpactPosition = vecPosition + (rfShield > 0.0f ? 2.0f : 0.5f) * vecToPreviousPositionNormal;

		rFrame.blasters.pFlags[j] |= kImpactObject;

		// Damage
		fDamage += rFrame.blasters.pfDamages[j];

		// Impact effect
		rFrame.puffControllers2.Add(rFrame.puffs, rFrame.fCurrentTime,
		{
			.bDestroysSelf = true,
			.pfTimes =
			{
				0.0f,
				0.1f,
			},
			.pObjectInfos =
			{
				{.vecPosition = vecImpactPosition, .fIntensity = 4.0f, .fArea = 0.15f, .fCookie = 4.0f},
				{.vecPosition = vecImpactPosition, .fIntensity = 0.0f, .fArea = 0.5f,  .fCookie = 4.0f},
			},
		});

		rFrame.pointLightControllers2.Add(rFrame.pointLights, rFrame.fCurrentTime,
		{
			.bDestroysSelf = true,
			.pfTimes =
			{
				0.0f,
				0.4f,
			},
			.pObjectInfos =
			{
				{.vecPosition = vecImpactPosition, .fVisibleArea = 0.75f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.5f, .fLightingIntensity = 40.0f, .crc = data::kTexturesBC7ExplosionpngCrc, .fRotation = common::Random<XM_2PI>(rFrame.randomEngine)},
				{.vecPosition = vecImpactPosition, .fVisibleArea = 0.0f,  .fVisibleIntensity = 0.5f, .fLightingArea = 0.0f, .fLightingIntensity = 10.0f, .crc = data::kTexturesBC7ExplosionpngCrc, .fRotation = common::Random<XM_2PI>(rFrame.randomEngine)},
			},
		});

		for (int64_t k = 0; k < kiImpactParticleCount; ++k)
		{
			auto vecParticlesPosition = vecImpactPosition;
			vecParticlesPosition = XMVectorAdd(vecParticlesPosition, XMVectorSet(-kfImpactParticlePositionRandom + common::Random<2.0f * kfImpactParticlePositionRandom>(rFrame.randomEngine), -kfImpactParticlePositionRandom + common::Random<2.0f * kfImpactParticlePositionRandom>(rFrame.randomEngine), 0.0f, 0.0f));
			XMFLOAT4A f4Position {};
			XMStoreFloat4A(&f4Position, vecParticlesPosition);

			auto vecPaticlesDirection = -vecToPreviousPositionNormal;
			auto vecVelocity = XMVectorMultiply(XMVectorReplicate(-kfImpactParticleVelocityMin - common::Random<kfImpactParticleVelocityRandom>(rFrame.randomEngine)), vecPaticlesDirection);
			vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(-0.5f * kfImpactParticleAngle + common::Random<kfImpactParticleAngle>(rFrame.randomEngine), -0.5f * kfImpactParticleAngle + common::Random<kfImpactParticleAngle>(rFrame.randomEngine), -0.5f * kfImpactParticleAngle + common::Random<kfImpactParticleAngle>(rFrame.randomEngine)));
			vecVelocity = XMVectorSetZ(vecVelocity, common::Random<kfImpactParticleVerticalVelocity>(rFrame.randomEngine));
			XMFLOAT4A f4Velocity {};
			XMStoreFloat4A(&f4Velocity, vecVelocity);

			uint32_t uiParticleColor = 0xFF0000FF | ((0 + common::Random(70, rFrame.randomEngine)) << 16);

			engine::ParticleManager::Spawn(engine::gpParticleManager->mLongParticlesSpawnLayout,
			{
				.i4Misc = {static_cast<int32_t>(uiParticleColor), kiImpactParticleCookie, static_cast<int32_t>(kfImpactParticleLightingIntesnity), 0},
				.f4MiscOne = {kfImpactParticleVelocityDecay, kfImpactParticleGravity, kfImpactParticleIntensityDecay, kfImpactParticleLightingSize},
				.f4MiscTwo = {kfImpactParticleWidth, kfImpactParticleLength, kfImpactParticleIntensityMin + common::Random<kfImpactParticleIntensityRandom>(rFrame.randomEngine), kfImpactParticleIntensityPower},
				.f4MiscThree = {},
				.f4Position = f4Position,
				.f4Velocity = f4Velocity,
			});
		}
	}

	return std::make_tuple(fDamage, vecImpactPosition);
}

void Player::Collide([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Player& rCurrent = rFrame.player;

	if (rCurrent.flags & kExploding) [[unlikely]]
	{
		return;
	}

	auto [fBlasterDamage, vecImpactPosition] = CollideBlasters(rFrame, rPreviousFrame, rCurrent.vecPosition, rCurrent.fShield);
	if (fBlasterDamage > 0.0f)
	{
		Damage(rFrame, fBlasterDamage, vecImpactPosition, 1.0f);
	}

	// Collide enemy missiles
	for (int64_t j = 0; j < rFrame.missiles.iCount; ++j)
	{
		if (rFrame.missiles.pFlags[j] & MissileFlags::kTargetEnemy || rFrame.missiles.pFlags[j] & MissileFlags::kExploding)
		{
			continue;
		}

		auto vecToMissile = XMVectorSubtract(rFrame.missiles.pVecPositions[j], rCurrent.vecPosition);
		auto vecToMissileNormal = XMVector3Normalize(vecToMissile);

		float fDistance = XMVectorGetX(XMVector3Length(vecToMissile));
		if (fDistance > kfMissileDamagePlayerRadius) [[likely]]
		{
			continue;
		}

		Missiles::Explode(rFrame, rFrameInput, j, false);
	}

	if (rCurrent.fArmor <= 0.0f)
	{
		rCurrent.flags |= kExploding;
		rCurrent.fDestroyedTime = kfDestroyTime;
		rCurrent.fDestroyedExplosionTime = kfDestroyExplosionInterval;

		SpawnPlayerExplosion(rFrame, 1.0f, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));

		Frame::End(rFrame, true);
	}
}

void Player::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
}

void Player::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
}

void Player::RenderGlobal([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
}

void Player::RenderMain([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
	const Player& rCurrent = rFrame.player;

	constexpr float kfSize = 0.5f;
	float fSize = (rCurrent.flags & kExploding ? std::pow(rCurrent.fDestroyedTime / kfDestroyTime, 2.0f) : 1.0f) * kfSize;
	auto matScaling = XMMatrixScaling(fSize, fSize, fSize);
	auto matTranslation = XMMatrixTranslationFromVector(rCurrent.vecPosition);
	auto matRotationX = XMMatrixRotationX(XM_PIDIV2);
	auto matRotationY = XMMatrixRotationY(0.0f);
	auto matRotationZ = common::RotationMatrixFromDirection(rCurrent.vecDirection, XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));
	auto matRotationAccelerationX = XMMatrixRotationY(std::clamp(0.015f * XMVectorGetX(rCurrent.vecVelocity), -0.4f, 0.4f));
	auto matRotationAccelerationY = XMMatrixRotationX(std::clamp(-0.015f * XMVectorGetY(rCurrent.vecVelocity), -0.4f, 0.4f));
	auto matTransform = XMMatrixMultiply(matRotationX, XMMatrixMultiply(matRotationY, XMMatrixMultiply(matRotationZ, XMMatrixMultiply(matRotationAccelerationX, XMMatrixMultiply(matRotationAccelerationY, XMMatrixMultiply(matScaling, matTranslation))))));

	if (rFrame.flags & FrameFlags::kMainMenu)
	{
		matTransform = XMMatrixSet(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	}
		
	auto pPlayerLayouts = reinterpret_cast<shaders::GltfLayout*>(engine::gpBufferManager->mPlayerStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	shaders::GltfLayout& rPlayerLayout = pPlayerLayouts[0];
	XMStoreFloat4(&rPlayerLayout.f4Position, rCurrent.vecPosition);
	XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rPlayerLayout.f3x4Transform[0]), matTransform);
	XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rPlayerLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
	rPlayerLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 1.0f};
	gpGltfPipelines->mpGltfPipelines[kGltfPipelinePlayer].WriteIndirectBuffer(iCommandBuffer, 1);
	gpGltfPipelines->mpGltfPipelines[kGltfPipelinePlayerShadow].WriteIndirectBuffer(iCommandBuffer, 1);

#if defined(ENABLE_GLTF_TEST)
	auto pGltfLayouts = reinterpret_cast<shaders::GltfLayout*>(engine::gpBufferManager->mGltfsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	shaders::GltfLayout& rGltfLayout = *pGltfLayouts;

	static constexpr float kfSize2 = 2.0f;
	static auto sMatPreMove = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
	static auto sMatPreRotate = XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f);
	matTranslation = XMMatrixTranslationFromVector(rCurrent.vecPosition + XMVectorSet(20.0f, 0.0f, 0.0f, 0.0f));
	matScaling = XMMatrixScaling(kfSize2, kfSize2, kfSize2);
	matRotationAccelerationX = XMMatrixRotationY(0.2f * XMVectorGetX(rCurrent.vecVelocity));
	matRotationAccelerationY = XMMatrixRotationX(-0.2f * XMVectorGetY(rCurrent.vecVelocity));
	matTransform = sMatPreMove * sMatPreRotate * XMMatrixMultiply(matRotationX, XMMatrixMultiply(matRotationY, XMMatrixMultiply(matRotationZ, XMMatrixMultiply(matRotationAccelerationX, XMMatrixMultiply(matRotationAccelerationY, XMMatrixMultiply(matScaling, matTranslation))))));
	XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4Transform[0]), matTransform);
	XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
	rGltfLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 0.0f};

	gpGltfPipelines->mpGltfPipelines[kGltfPipelineTest].WriteIndirectBuffer(iCommandBuffer, 1);
#endif
}

} // namespace game
