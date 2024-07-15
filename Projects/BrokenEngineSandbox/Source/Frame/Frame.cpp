#include "Frame.h"

#include "Frame/FrameBase.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Profile/ProfileManager.h"

#include "Game.h"
#include "Graphics/Islands.h"
#include "Input/Input.h"

using namespace DirectX;

using enum engine::FrameType;
using enum engine::TargetFlags;

namespace game
{

using enum BlasterFlags;
using enum FrameFlags;
using enum UiState;

Frame::Frame(FrameFlags_t initialFlags, engine::IslandsFlip eInitialIslandsFlip)
: engine::FrameBase(eInitialIslandsFlip)
{
	flags |= initialFlags;
	engine::gpIslands->SetIslandsFlip(eInitialIslandsFlip);

	flags |= engine::gDashMouseDirection.Get<int64_t>() == 0 ? kDashMouseCursor : kDashMouseAcceleration;
	flags |= engine::gDashGamepadDirection.Get<int64_t>() == 0 ? kDashGamepadFiring : kDashGamepadAcceleration;

	flags |= engine::gPrimaryHoldToggle.Get<int64_t>() == 0 ? kPrimaryHold : kPrimaryToggle;

	auto vecSpawnPosition = ApplyFlip(eInitialIslandsFlip, Player::kVecSpawnPosition);
	player.vecPosition = vecSpawnPosition;
	camera.vecPosition = flags & kMainMenu ? Camera::kVecMainMenuPosition : vecSpawnPosition;
}

void FrameGlobal([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	rFrame.flags = rPreviousFrame.flags;
	rFrame.fEndTime = rPreviousFrame.fEndTime;
	
	rFrame.fWaveDisplayTimeLeft = std::max(rPreviousFrame.fWaveDisplayTimeLeft - fDeltaTime, 0.0f);

	rFrame.bNextWave = rPreviousFrame.bNextWave;
	rFrame.iWave = rPreviousFrame.iWave;
	rFrame.iLastSpawn = rPreviousFrame.iLastSpawn;
	rFrame.iClumpsLeft = rPreviousFrame.iClumpsLeft;
	rFrame.iClumpSize = rPreviousFrame.iClumpSize;
	rFrame.iNextClumpSpawn = rPreviousFrame.iNextClumpSpawn;
	rFrame.fNextClumpSpawnTime = rPreviousFrame.fNextClumpSpawnTime - fDeltaTime;

	// Sun angle
	if (rFrame.flags & kMainMenu)
	{
		rFrame.fSunAngle = rPreviousFrame.fSunAngle;
	}
	else
	{
		static constexpr float kfNoonSpeedStart = XM_PIDIV2 - XM_PIDIV8;
		static constexpr float kfNoonSpeedEnd = XM_PIDIV2 + XM_PIDIV8;
		static constexpr float kfNightSpeedStart = XM_PI;
		static constexpr float kfNightSpeedEnd = XM_2PI;
		if (rPreviousFrame.fSunAngle >= kfNoonSpeedStart && rPreviousFrame.fSunAngle < kfNoonSpeedEnd)
		{
			rFrame.fSunAngle = rPreviousFrame.fSunAngle + fDeltaTime * 0.025f;
		}
		else if (rPreviousFrame.fSunAngle >= kfNightSpeedStart && rPreviousFrame.fSunAngle < kfNightSpeedEnd)
		{
			rFrame.fSunAngle = rPreviousFrame.fSunAngle + fDeltaTime * 0.5f;
		}
		else
		{
			rFrame.fSunAngle = rPreviousFrame.fSunAngle + fDeltaTime * 0.01f;
		}
	}

	if (rFrame.fSunAngle >= XM_2PI)
	{
		rFrame.fSunAngle -= XM_2PI;
	}
	else if (rFrame.fSunAngle < 0.0f)
	{
		rFrame.fSunAngle += XM_2PI;
	}

	if (gpGame->meUiState == kGraphics)
	{
		rFrame.fSunAngle = engine::gSunAngleOverride.Get();
	}
#if defined(ENABLE_DEBUG_INPUT)
	else if (gpGame->meUiState == kTweaks)
	{
		rFrame.fSunAngle = engine::gSunAngleOverride.Get();
	}
#endif
}

void FrameInterpolate([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	rFrame.flags.Set(kDeathScreen, rPreviousFrame.player.fArmor <= 0.0f);
}

void NextWave(Frame& __restrict rFrame)
{
	++rFrame.iWave;
	rFrame.fWaveDisplayTimeLeft = Frame::kfWaveDisplayTime;
}

void FramePostRender([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
}

void SpawnSpaceships(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, int64_t iSpawnCount)
{
	rFrame.iLastSpawn = 0;

	float fSpawnRadius = 0.6f * (rFrameInput.f4LargeVisibleArea.z - rFrameInput.f4LargeVisibleArea.x);

	if (iSpawnCount >= 10 && common::Random(2, rFrame.randomEngine) == 0)
	{
		// Spawn in a circle around the player
		iSpawnCount = (iSpawnCount * 2) / 3;

		float fDeltaAngle = XM_2PI / static_cast<float>(iSpawnCount);

		float fCurrentAngle = 0.0f;
		for (int64_t i = 0; i < iSpawnCount; ++i, fCurrentAngle += fDeltaAngle)
		{
			auto vecDirection = XMVector3Normalize(XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fCurrentAngle)));
			auto vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fSpawnRadius), vecDirection, rFrame.player.vecPosition);
		retry_distance:
			float fTerrainElevation = engine::gpIslands->GlobalElevation(vecPosition);
			if (fTerrainElevation > engine::gBaseHeight.Get() - 1.0f)
			{
				vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(10.0f), vecDirection, vecPosition);
				goto retry_distance;
			}

			auto vecDirectionToPlayerNormal = XMVector3Normalize(XMVectorSubtract(rFrame.player.vecPosition, vecPosition));
			Spaceships::Spawn(rFrame, vecPosition, vecDirectionToPlayerNormal);
		}
	}
	else
	{
		auto vecSpawnPosition = Frame::EnemySpawnPosition(rFrame);
		float fPlayerDistanceFromOrigin = common::Distance(rFrame.player.vecPosition, vecSpawnPosition);
		if (fPlayerDistanceFromOrigin < fSpawnRadius)
		{
			// Origin is visible, spawn at random position
		retry_center:
			auto vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(common::Random<XM_2PI>(rFrame.randomEngine)));
			vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(fSpawnRadius), vecDirection, rFrame.player.vecPosition);
			float fTerrainElevation = engine::gpIslands->GlobalElevation(vecSpawnPosition);
			if (fTerrainElevation > 0.0f)
			{
				fSpawnRadius += 1.0f;
				goto retry_center;
			}
		}

		auto vecDirectionToPlayerNormal = XMVector3Normalize(XMVectorSubtract(rFrame.player.vecPosition, vecSpawnPosition));

		for (int64_t i = 0; i < iSpawnCount; ++i)
		{
			static constexpr float kfSpawnJitter = 2.0f;
			auto vecJitter = XMVectorSet(-kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rFrame.randomEngine), -kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rFrame.randomEngine), 0.0f, 0.0f);
			Spaceships::Spawn(rFrame, vecSpawnPosition + vecJitter, vecDirectionToPlayerNormal);
		}
	}
}

void FrameSpawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	if (rFrame.flags & kMainMenu)
	{
		return;
	}

	if (rFrame.flags & kFirstSpawn)
	{
		rFrame.flags &= kFirstSpawn;

		auto vecOffset = ApplyFlip(rFrame.eIslandsFlip, XMVectorSet(75.0f, 0.0f, 0.0f, 0.0f));
		auto vecDirection = ApplyFlip(rFrame.eIslandsFlip, XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f));

		int64_t iCount = 1;
		for (int64_t i = 0; i < iCount; ++i)
		{
			vecOffset += ApplyFlip(rFrame.eIslandsFlip, XMVectorSet(2.5f, 2.5f, 0.0f, 0.0f));

			static constexpr float kfSpawnJitter = 0.2f;
			auto vecJitter = XMVectorSet(-kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rFrame.randomEngine), -kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rFrame.randomEngine), 0.0f, 0.0f);
			Spaceships::Spawn(rFrame, rFrame.player.vecPosition + vecOffset + vecJitter, vecDirection);
			vecJitter = XMVectorSet(-kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rFrame.randomEngine), -kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rFrame.randomEngine), 0.0f, 0.0f);
			Spaceships::Spawn(rFrame, rFrame.player.vecPosition + XMVectorSet(1.0f, -1.0f, 1.0f, 1.0f) * vecOffset + vecJitter, vecDirection);
		}

		return;
	}

	int64_t iSpawnCount = 0;

	int64_t iWaveSpawnCount = 6 + (12 * rFrame.iWave) / 9;
	int64_t iClumpsTotal = rFrame.spaceships.iCount;
	if (rFrame.bNextWave)
	{
		rFrame.bNextWave = false;

		rFrame.iClumpsLeft = rFrame.iWave / 2;
		rFrame.iClumpSize = iWaveSpawnCount;
		rFrame.iNextClumpSpawn = rFrame.iClumpSize / 2;
		rFrame.fNextClumpSpawnTime = 10.0f;

		iSpawnCount = rFrame.iClumpSize;
	}
	else if (rFrame.iClumpsLeft > 0 && (iClumpsTotal <= rFrame.iNextClumpSpawn || rFrame.fNextClumpSpawnTime <= 0.0f))
	{
		--rFrame.iClumpsLeft;

		iSpawnCount = rFrame.iClumpSize;

		rFrame.iNextClumpSpawn = (iClumpsTotal + rFrame.iClumpSize) / 2;
		rFrame.iClumpSize /= 2;

		rFrame.fNextClumpSpawnTime = 10.0f;
	}

	if (iSpawnCount > 0 && iSpawnCount > iWaveSpawnCount / 4)
	{
		SpawnSpaceships(rFrame, rFrameInput, iSpawnCount);
	}

	iClumpsTotal = rFrame.spaceships.iCount;

	// If only two enemies are left, and they're offscreen, end wave
	bool bFewEnemiesAndOffscreen = true;
	if (rFrame.iWave < 3 || iClumpsTotal > 2)
	{
		bFewEnemiesAndOffscreen = false;
	}
	if (bFewEnemiesAndOffscreen)
	{
		constexpr float kfAddVisibleArea = 20.0f;

		for (int64_t i = 0; i < rFrame.spaceships.iCount; ++i)
		{
			if (!engine::OutsideVisibleArea(rFrameInput, rFrame.spaceships.pVecPositions[i], kfAddVisibleArea, kfAddVisibleArea, kfAddVisibleArea, kfAddVisibleArea))
			{
				bFewEnemiesAndOffscreen = false;
				break;
			}
		}
	}

	if (iClumpsTotal == 0 || bFewEnemiesAndOffscreen)
	{
		rFrame.bNextWave = true;
		NextWave(rFrame);
	}
}

void FrameDestroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	if (rFrame.bNextWave)
	{
		for (int64_t i = 0; i < rFrame.blasters.iCount; ++i)
		{
			Blasters::Destroy(rFrame, i);
		}
		rFrame.blasters.iCount = 0;

		for (int64_t i = 0; i < rFrame.missiles.iCount; ++i)
		{
			Missiles::Destroy(rFrame, i);
		}
		rFrame.missiles.iCount = 0;
	}
}

DirectX::FXMVECTOR XM_CALLCONV Frame::EnemySpawnPosition(Frame& __restrict rFrame)
{
	auto vecSpawnPosition = ApplyFlip(rFrame.eIslandsFlip, kVecEnemySpawnPosition);
	return XMVectorSetZ(vecSpawnPosition, engine::gBaseHeight.Get());
}

std::optional<FXMVECTOR> XM_CALLCONV Frame::ClosestEnemy(Frame& __restrict rFrame, FXMVECTOR vecPosition)
{
	float fClosestDistance = std::numeric_limits<float>::max();
	auto vecClosestPosition = XMVectorZero();

	for (decltype(rFrame.targets.uiMaxIndex) i = 0; i <= rFrame.targets.uiMaxIndex; ++i)
	{
		if (!rFrame.targets.pbUsed[i])
		{
			continue;
		}

		engine::TargetInfo& rTargetInfo = rFrame.targets.pObjectInfos[i];

		if (!(rTargetInfo.flags & kDestination) || (rTargetInfo.flags & kTargetIsEnemy) == 0)
		{
			continue;
		}

		float fDistance = common::Distance(vecPosition, rTargetInfo.vecPosition);
		if (fDistance < fClosestDistance)
		{
			fClosestDistance = fDistance;
			vecClosestPosition = rTargetInfo.vecPosition;
		}
	}

	return fClosestDistance < std::numeric_limits<float>::max() ? std::make_optional(vecClosestPosition) : std::nullopt;
}

// DT: TODO Move into missiles
[[nodiscard]] engine::target_t Frame::GetMissileTarget(Frame& __restrict rFrame, const game::FrameInput& __restrict rFrameInput, FXMVECTOR vecPosition, FXMVECTOR vecDirection, engine::TargetFlags_t targetFlags)
{
	engine::target_t uiTarget = 0;
	float fSmallestAngle = std::numeric_limits<float>::max();
	engine::subscriber_t uiLeastSubscribers = std::numeric_limits<engine::subscriber_t>::max();

	for (decltype(rFrame.targets.uiMaxIndex) i = 0; i <= rFrame.targets.uiMaxIndex; ++i)
	{
		if (!rFrame.targets.pbUsed[i])
		{
			continue;
		}

		engine::TargetInfo& rTargetInfo = rFrame.targets.pObjectInfos[i];

		if (!(rTargetInfo.flags & kDestination) || (rTargetInfo.flags & targetFlags) == 0)
		{
			continue;
		}

		// Only target visible
		static constexpr float kfExtraMissileTargetRange = 10.0f;
		if (engine::OutsideVisibleArea(rFrameInput, rTargetInfo.vecPosition, kfExtraMissileTargetRange, kfExtraMissileTargetRange, kfExtraMissileTargetRange, kfExtraMissileTargetRange))
		{
			continue;
		}

		auto vecMissilePosition = vecPosition;
		auto vecMissileDirection = vecDirection;
		auto vecToTargetNormal = XMVector3Normalize(XMVectorSubtract(rTargetInfo.vecPosition, vecMissilePosition));
		float fAngle = std::abs(XMVectorGetX(XMVector3AngleBetweenNormals(vecMissileDirection, vecToTargetNormal)));

		engine::Target& rTarget = rFrame.targets.pObjects[i];
		engine::subscriber_t uiSubscribers = rTarget.uiSubscribers;
		if (uiSubscribers < uiLeastSubscribers)
		{
			uiLeastSubscribers = uiSubscribers;
			fSmallestAngle = fAngle;
			uiTarget = i;
		}
		else if (uiSubscribers == uiLeastSubscribers && fAngle < fSmallestAngle)
		{
			fSmallestAngle = fAngle;
			uiTarget = i;
		}
	}

	if (uiTarget != 0)
	{
		++(rFrame.targets.Get(uiTarget).uiSubscribers);
	}

	return uiTarget;
}

// DT: TODO Replace with area damage pool
void XM_CALLCONV Frame::AreaDamage(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, DirectX::FXMVECTOR vecPosition, float fDamage, float fRadius)
{
	CollectionAreaDamage(rFrame, rFrameInput, vecPosition, fRadius, fDamage, Spaceships::kfFreezeTimeAreaDamage, rFrame.spaceships, SpaceshipFlags::kExploding, false);
}

void XM_CALLCONV Frame::BlasterImpact(Frame& __restrict rFrame, int64_t i, DirectX::FXMVECTOR vecImpactPosition)
{
	auto vecImpactPositionAtBlasterHeight = XMVectorSetZ(vecImpactPosition, XMVectorGetZ(rFrame.blasters.pVecPositions[i]));
	rFrame.blasters.pFlags[i] |= kImpactObject;

	ASSERT(XMVectorGetZ(rFrame.blasters.pVecVelocities[i]) == 0.0f);
}

void XM_CALLCONV Frame::SpawnPickup(Frame& __restrict rFrame, DirectX::FXMVECTOR vecPosition, float fChance, bool bForce)
{
	if (!bForce && common::Random(rFrame.randomEngine) > fChance)
	{
		return;
	}

	engine::billboard_t uiBillboard = 0;
	rFrame.billboards.Add(uiBillboard,
	{
		.flags = {engine::BillboardFlags::kTypeArmor},
		.crc = data::kTexturesBC7ArmorIconpngCrc,
		.fSize = kfPickupSize,
		.fAlpha = 1.0f,
		.fRotation = 0.0f,
		.vecPosition = vecPosition,
	});
}

void Frame::End(Frame& __restrict rFrame, bool bRemoveAutosave)
{
	rFrame.fEndTime = rFrame.fCurrentTime;
	if (bRemoveAutosave)
	{
		gpGame->RemoveAutosave();
	}
}

void XM_CALLCONV SpawnDamageParticles(Frame& __restrict rFrame, DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecDirection, float fPercent)
{
	static constexpr int32_t kiDamageParticleCount = 1;
	static constexpr float kfDamageParticlePositionJitter = 0.2f;
	static constexpr float kfDamageParticleVelocityJitter = 2.0f;
	static constexpr float kfDamageParticleVelocityDecay = 1.5f;
	static constexpr float kfDamageParticleRotationDelta = 80.0f;
	static constexpr float kfDamageParticleRotationDeltaDecay = 1.0f;
	static constexpr float kfDamageParticleSizeMin = 0.25f;
	static constexpr float kfDamageParticleSizeRandom = 0.1f;
	static constexpr float kfDamageParticleSizeDecay = 4.0f;
	static constexpr float kfDamageParticleIntensityMin = 0.5f;
	static constexpr float kfDamageParticleIntensityRandom = 1.25f;
	static constexpr float kfDamageParticleIntensityDecayMin = 1.0f;
	static constexpr float kfDamageParticleIntensityPower = 4.0f;
	static constexpr float kfDamageParticleLightingSize = 5.0f;
	static constexpr float kfDamageParticleLightingIntesnity = 50.0f;
	static constexpr float kfDamageParticleOffset = -0.8f;

	for (int64_t j = 0; j < kiDamageParticleCount; ++j)
	{
		int32_t iDamageParticleCookie = 36 + common::Random(3, rFrame.randomEngine);

		auto vecDamageParticlePosition = XMVectorMultiplyAdd(XMVectorReplicate(0.75f * kfDamageParticleOffset), vecDirection, vecPosition);
		vecDamageParticlePosition = XMVectorAdd(vecDamageParticlePosition, XMVectorSet(-kfDamageParticlePositionJitter + common::Random<2.0f * kfDamageParticlePositionJitter>(rFrame.randomEngine), -kfDamageParticlePositionJitter + common::Random<2.0f * kfDamageParticlePositionJitter>(rFrame.randomEngine), -kfDamageParticlePositionJitter + common::Random<2.0f * kfDamageParticlePositionJitter>(rFrame.randomEngine), 0.0f));
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecDamageParticlePosition);

		auto vecDamageParticleVelocity = XMVectorSet(-kfDamageParticleVelocityJitter + common::Random<2.0f * kfDamageParticleVelocityJitter>(rFrame.randomEngine), -kfDamageParticleVelocityJitter + common::Random<2.0f * kfDamageParticleVelocityJitter>(rFrame.randomEngine), -kfDamageParticleVelocityJitter + common::Random<2.0f * kfDamageParticleVelocityJitter>(rFrame.randomEngine), 0.0f);
		XMFLOAT4A f4Velocity {};
		XMStoreFloat4A(&f4Velocity, vecDamageParticleVelocity);

		uint32_t uiParticleColor = 0xFF0000FF | ((25 + common::Random(125, rFrame.randomEngine)) << 16) | ((common::Random(50, rFrame.randomEngine)) << 8);

		engine::ParticleManager::Spawn(engine::gpParticleManager->mSquareParticlesSpawnLayout,
		{
			.i4Misc = {static_cast<int32_t>(uiParticleColor), iDamageParticleCookie, static_cast<int32_t>(kfDamageParticleLightingIntesnity), 0},
			.f4MiscOne = {kfDamageParticleVelocityDecay, 0.0f, kfDamageParticleIntensityDecayMin, kfDamageParticleLightingSize},
			.f4MiscTwo = {kfDamageParticleSizeMin + common::Random<kfDamageParticleSizeRandom>(rFrame.randomEngine), 0.0f, kfDamageParticleIntensityMin + (1.0f - fPercent) * common::Random<kfDamageParticleIntensityRandom>(rFrame.randomEngine), kfDamageParticleIntensityPower},
			.f4MiscThree = {kfDamageParticleSizeDecay, -kfDamageParticleRotationDelta + common::Random<2.0f * kfDamageParticleRotationDelta>(rFrame.randomEngine), common::Random<XM_2PI>(rFrame.randomEngine), kfDamageParticleRotationDeltaDecay},
			.f4Position = f4Position,
			.f4Velocity = f4Velocity,
		});
	}
}
			
void XM_CALLCONV SpawnBurnParticles(DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecVelocity, DirectX::FXMVECTOR vecDirection, float fSize, float fIntensity)
{
	static constexpr int32_t kiParticleCount = 2;
	static constexpr float kfParticleOffset = 0.8f;
	static constexpr float kfParticleVelocityMin = 50.0f;
	static constexpr float kfParticleVelocityRandom = 25.0f;
	static constexpr float kfParticleVelocityDecay = 6.0f;
	static constexpr float kfParticleRotationDelta = 200.0f;
	static constexpr float kfParticleRotationDeltaDecay = 1.0f;
	static constexpr float kfParticleSizeMin = 0.25f;
	static constexpr float kfParticleSizeRandom = 0.15f;
	static constexpr float kfParticleSizeDecay = 1.0f;
	static constexpr float kfParticleIntensityMin = 1.0f;
	static constexpr float kfParticleIntensityRandom = 1.0f;
	static constexpr float kfParticleIntensityDecayMin = 3.0f;
	static constexpr float kfParticleIntensityPower = 4.0f;
	static constexpr float kfAngleMin = 0.3f;
	static constexpr float kfAngleRandom = 0.2f;

	static constexpr float kfParticleLightingSize = 4.0f;
	static constexpr float kfParticleLightingIntesnity = 2000.0f;

	static common::RandomEngine sRandomEngine;

	fIntensity = std::max(fIntensity, 0.25f);

	for (int64_t j = 0; j < kiParticleCount; ++j)
	{
		int32_t iParticleCookie = 47; //  43 + common::Random(5, sRandomEngine);

		auto vecParticlePosition = XMVectorMultiplyAdd(XMVectorReplicate(fSize * kfParticleOffset), -vecDirection, vecPosition);
		vecParticlePosition = XMVectorAdd(vecParticlePosition, XMVectorSet(-fSize + fSize * common::Random<2.0f>(sRandomEngine), -fSize + fSize * common::Random<2.0f>(sRandomEngine), -fSize + fSize * common::Random<2.0f>(sRandomEngine), 0.0f));
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecParticlePosition);

		auto vecParticleDirection = XMVector3Normalize(XMVectorAdd(XMVectorSet(0.0f, 0.0f, kfAngleMin + common::Random<kfAngleRandom>(sRandomEngine), 0.0f), vecDirection));
		auto matRotation = XMMatrixTranspose(XMMatrixInverse(nullptr, XMMatrixRotationNormal(vecDirection, common::Random<XM_2PI>(sRandomEngine))));
		vecParticleDirection = XMVectorSetW(XMVector3Transform(vecParticleDirection, matRotation), 0.0f);
		auto vecParticleVelocity = XMVectorMultiply(XMVectorReplicate(kfParticleVelocityMin + common::Random<kfParticleVelocityRandom>(sRandomEngine)), vecParticleDirection);
		XMFLOAT4A f4Velocity {};
		XMStoreFloat4A(&f4Velocity, vecVelocity + vecParticleVelocity);

		uint32_t uiParticleColor = 0xFF0000AA | ((25 + common::Random(100, sRandomEngine)) << 16) | ((common::Random(50, sRandomEngine)) << 8);

		engine::ParticleManager::Spawn(engine::gpParticleManager->mSquareParticlesSpawnLayout,
		{
			.i4Misc = {static_cast<int32_t>(uiParticleColor), iParticleCookie, static_cast<int32_t>(fIntensity * kfParticleLightingIntesnity), 0},
			.f4MiscOne = {kfParticleVelocityDecay / fSize, 0.0f, kfParticleIntensityDecayMin, kfParticleLightingSize},
			.f4MiscTwo = {kfParticleSizeMin + common::Random<kfParticleSizeRandom>(sRandomEngine), 0.0f, fIntensity * (kfParticleIntensityMin + common::Random<kfParticleIntensityRandom>(sRandomEngine)), kfParticleIntensityPower},
			.f4MiscThree = {kfParticleSizeDecay, -kfParticleRotationDelta + common::Random<2.0f * kfParticleRotationDelta>(sRandomEngine), common::Random<XM_2PI>(sRandomEngine), kfParticleRotationDeltaDecay},
			.f4Position = f4Position,
			.f4Velocity = f4Velocity,
		});
	}
}

} // namespace game
