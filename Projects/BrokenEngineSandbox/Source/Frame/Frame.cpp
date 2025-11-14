#include "Frame.h"

#include "Frame/FrameBase.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Profile/ProfileManager.h"

#include "Game.h"
#include "Input/Input.h"


namespace game
{

using enum FrameFlags;

Frame::Frame(FrameFlags_t initialFlags)
{
	interpolate.f4GlobalArea = engine::gpIslands->mf4GlobalArea;

	engine::Navmesh::SetupGrid(interpolate.f4GlobalArea, postRender.navmesh);

	interpolate.flags |= initialFlags;

	interpolate.flags |= engine::gDashMouseDirection.Get<int64_t>() == 0 ? kDashMouseCursor : kDashMouseAcceleration;
	interpolate.flags |= engine::gDashGamepadDirection.Get<int64_t>() == 0 ? kDashGamepadFiring : kDashGamepadAcceleration;

	interpolate.flags |= engine::gPrimaryHoldToggle.Get<int64_t>() == 0 ? kPrimaryHold : kPrimaryToggle;

	interpolate.player.vecPosition = Player::kVecSpawnPosition;
}

void WriteFrameInterpolate([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;
	const FrameInterpolate& rPreviousInterpolate = rPreviousFrame.interpolate;

	// State updates (merged from old MoveCamera phase)
	rInterpolate.flags = rPreviousInterpolate.flags;
	rInterpolate.fEndTime = rPreviousInterpolate.fEndTime;
	rInterpolate.fWaveDisplayTimeLeft = std::max(rPreviousInterpolate.fWaveDisplayTimeLeft - fDeltaTime, 0.0f);
	rInterpolate.bNextWave = rPreviousInterpolate.bNextWave;
	rInterpolate.iWave = rPreviousInterpolate.iWave;
	rInterpolate.iLastSpawn = rPreviousInterpolate.iLastSpawn;
	rInterpolate.iClumpsLeft = rPreviousInterpolate.iClumpsLeft;
	rInterpolate.iClumpSize = rPreviousInterpolate.iClumpSize;
	rInterpolate.iNextClumpSpawn = rPreviousInterpolate.iNextClumpSpawn;
	rInterpolate.fNextClumpSpawnTime = rPreviousInterpolate.fNextClumpSpawnTime - fDeltaTime;

	// Sun angle
	if (rInterpolate.flags & kMainMenu)
	{
		rInterpolate.fSunAngle = rPreviousInterpolate.fSunAngle;
	}
	else
	{
		static constexpr float kfNoonSpeedStart = XM_PIDIV2 - XM_PIDIV8;
		static constexpr float kfNoonSpeedEnd = XM_PIDIV2 + XM_PIDIV8;
		static constexpr float kfNightSpeedStart = XM_PI;
		static constexpr float kfNightSpeedEnd = XM_2PI;
		if (rPreviousInterpolate.fSunAngle >= kfNoonSpeedStart && rPreviousInterpolate.fSunAngle < kfNoonSpeedEnd)
		{
			rInterpolate.fSunAngle = rPreviousInterpolate.fSunAngle + fDeltaTime * 0.025f;
		}
		else if (rPreviousInterpolate.fSunAngle >= kfNightSpeedStart && rPreviousInterpolate.fSunAngle < kfNightSpeedEnd)
		{
			rInterpolate.fSunAngle = rPreviousInterpolate.fSunAngle + fDeltaTime * 0.5f;
		}
		else
		{
			rInterpolate.fSunAngle = rPreviousInterpolate.fSunAngle + fDeltaTime * 0.01f;
		}
	}

	if (rInterpolate.fSunAngle >= XM_2PI)
	{
		rInterpolate.fSunAngle -= XM_2PI;
	}
	else if (rInterpolate.fSunAngle < 0.0f)
	{
		rInterpolate.fSunAngle += XM_2PI;
	}

	if (gpGame->meUiState == UiState::kGraphics)
	{
		rInterpolate.fSunAngle = engine::gSunAngleOverride.Get();
	}
#if defined(ENABLE_DEBUG_INPUT)
	else if (gpGame->meUiState == UiState::kTweaks)
	{
		rInterpolate.fSunAngle = engine::gSunAngleOverride.Get();
	}
#endif

	// Calculate directional offset for camera smoothing
	XMVECTOR vecOffset = 10.0f * rFrameInputHeld.vecDirection;
	vecOffset = XMVectorMultiply(vecOffset, XMVectorSet(1.0f, engine::gpSwapchainManager->mfAspectRatio, 0.0f, 0.0f));
	constexpr float kfOffsetSmooth = 0.75f;
	rInterpolate.vecCameraOffsetSmoothed = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfOffsetSmooth), vecOffset, XMVectorMultiply(XMVectorReplicate(1.0f - fDeltaTime * kfOffsetSmooth), rPreviousInterpolate.vecCameraOffsetSmoothed));

	// Integrate camera position from velocity
	rInterpolate.fCameraEyeHeight = rPreviousInterpolate.fCameraEyeHeight + fDeltaTime * rPreviousInterpolate.fCameraEyeHeightVelocity;
	rInterpolate.fCameraEyeRotation = rPreviousInterpolate.fCameraEyeRotation + fDeltaTime * rPreviousInterpolate.fCameraEyeRotationVelocity;

	// Clamp camera height and rotation to valid ranges
	rInterpolate.fCameraEyeHeight = std::clamp(rInterpolate.fCameraEyeHeight, 60.0f, 300.0f);
	rInterpolate.fCameraEyeRotation = std::clamp(rInterpolate.fCameraEyeRotation, -XM_PIDIV2, -0.9f);

	// Decay camera shake
	rInterpolate.fCameraShake = std::max(rPreviousInterpolate.fCameraShake - fDeltaTime * 2.0f, 0.0f);

	// Death screen check
	rInterpolate.flags.Set(kDeathScreen, rPreviousInterpolate.player.fArmor <= 0.0f);
}

void NextWave(Frame& __restrict rFrame)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	++rInterpolate.iWave;
	rInterpolate.fWaveDisplayTimeLeft = FrameInterpolate::kfWaveDisplayTime;
}

void WriteFramePostRender([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] const FrameInputPressed& __restrict rFrameInputPressed, [[maybe_unused]] float fDeltaTime)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	// Process camera input to set velocity for next frame integration
	if (rFrameInputHeld.fRotateEye == 0.0f)
	{
		rInterpolate.fCameraEyeRotationVelocity = 0.0f;
	}
	else
	{
		rInterpolate.fCameraEyeRotationVelocity = 1.0f * rFrameInputHeld.fRotateEye;
	}

	rInterpolate.fCameraEyeHeightVelocity = 0.0f;
#if defined(ENABLE_DEBUG_INPUT)
	constexpr float kfZoomMultiplier = 2.0f;
	if (rFrameInputHeld.flags & FrameInputHeldFlags::kZoomOut)
	{
		rInterpolate.fCameraEyeHeightVelocity = kfZoomMultiplier * rInterpolate.fCameraEyeHeight;
	}
	else if (rFrameInputHeld.flags & FrameInputHeldFlags::kZoomIn)
	{
		rInterpolate.fCameraEyeHeightVelocity = -kfZoomMultiplier * rInterpolate.fCameraEyeHeight;
	}
#endif
}

void SpawnSpaceships(Frame& __restrict rFrame, int64_t iSpawnCount)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	rInterpolate.iLastSpawn = 0;

	float fSpawnRadius = 100.0f;

	if (iSpawnCount >= 10 && common::Random(2, rInterpolate.randomEngine) == 0)
	{
		// Spawn in a circle around the player
		iSpawnCount = (iSpawnCount * 2) / 3;

		float fDeltaAngle = XM_2PI / static_cast<float>(iSpawnCount);

		float fCurrentAngle = 0.0f;
		for (int64_t i = 0; i < iSpawnCount; ++i, fCurrentAngle += fDeltaAngle)
		{
			auto vecDirection = XMVector3Normalize(XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fCurrentAngle)));
			auto vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fSpawnRadius), vecDirection, rInterpolate.player.vecPosition);
		retry_distance:
			float fTerrainElevation = engine::gpIslands->GlobalElevation(vecPosition);
			if (fTerrainElevation > engine::gBaseHeight.Get() - 1.0f)
			{
				vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(10.0f), vecDirection, vecPosition);
				goto retry_distance;
			}

			auto vecDirectionToPlayerNormal = XMVector3Normalize(XMVectorSubtract(rInterpolate.player.vecPosition, vecPosition));
			Spaceships::Spawn(rFrame, vecPosition, vecDirectionToPlayerNormal);
		}
	}
	else
	{
		auto vecSpawnPosition = Frame::EnemySpawnPosition();
		float fPlayerDistanceFromOrigin = common::Distance(rInterpolate.player.vecPosition, vecSpawnPosition);
		if (fPlayerDistanceFromOrigin < fSpawnRadius)
		{
			// Origin is visible, spawn at random position
		retry_center:
			auto vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(common::Random<XM_2PI>(rInterpolate.randomEngine)));
			vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(fSpawnRadius), vecDirection, rInterpolate.player.vecPosition);
			float fTerrainElevation = engine::gpIslands->GlobalElevation(vecSpawnPosition);
			if (fTerrainElevation > 0.0f)
			{
				fSpawnRadius += 1.0f;
				goto retry_center;
			}
		}

		auto vecDirectionToPlayerNormal = XMVector3Normalize(XMVectorSubtract(rInterpolate.player.vecPosition, vecSpawnPosition));

		for (int64_t i = 0; i < iSpawnCount; ++i)
		{
			static constexpr float kfSpawnJitter = 2.0f;
			auto vecJitter = XMVectorSet(-kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rInterpolate.randomEngine), -kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rInterpolate.randomEngine), 0.0f, 0.0f);
			Spaceships::Spawn(rFrame, vecSpawnPosition + vecJitter, vecDirectionToPlayerNormal);
		}
	}
}

void WriteFramePostRenderSpawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] const FrameInputPressed& __restrict rFrameInputPressed, [[maybe_unused]] float fDeltaTime)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	if (rInterpolate.flags & kMainMenu)
	{
		return;
	}

	if (rInterpolate.flags & kFirstSpawn)
	{
		rInterpolate.flags.Clear(kFirstSpawn);

		auto vecOffset = XMVectorSet(75.0f, 0.0f, 0.0f, 0.0f);
		auto vecDirection = XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f);

		int64_t iCount = 1;
		for (int64_t i = 0; i < iCount; ++i)
		{
			vecOffset += XMVectorSet(2.5f, 2.5f, 0.0f, 0.0f);

			static constexpr float kfSpawnJitter = 0.2f;
			auto vecJitter = XMVectorSet(-kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rInterpolate.randomEngine), -kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rInterpolate.randomEngine), 0.0f, 0.0f);
			Spaceships::Spawn(rFrame, rInterpolate.player.vecPosition + vecOffset + vecJitter, vecDirection);
			vecJitter = XMVectorSet(-kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rInterpolate.randomEngine), -kfSpawnJitter + common::Random<2.0f * kfSpawnJitter>(rInterpolate.randomEngine), 0.0f, 0.0f);
			Spaceships::Spawn(rFrame, rInterpolate.player.vecPosition + XMVectorSet(1.0f, -1.0f, 1.0f, 1.0f) * vecOffset + vecJitter, vecDirection);
		}

		return;
	}

	int64_t iSpawnCount = 0;

	int64_t iWaveSpawnCount = 6 + (12 * rInterpolate.iWave) / 9;
	int64_t iClumpsTotal = rInterpolate.spaceships.iCount;
	if (rInterpolate.bNextWave)
	{
		rInterpolate.bNextWave = false;

		rInterpolate.iClumpsLeft = rInterpolate.iWave / 2;
		rInterpolate.iClumpSize = iWaveSpawnCount;
		rInterpolate.iNextClumpSpawn = rInterpolate.iClumpSize / 2;
		rInterpolate.fNextClumpSpawnTime = 10.0f;

		iSpawnCount = rInterpolate.iClumpSize;
	}
	else if (rInterpolate.iClumpsLeft > 0 && (iClumpsTotal <= rInterpolate.iNextClumpSpawn || rInterpolate.fNextClumpSpawnTime <= 0.0f))
	{
		--rInterpolate.iClumpsLeft;

		iSpawnCount = rInterpolate.iClumpSize;

		rInterpolate.iNextClumpSpawn = (iClumpsTotal + rInterpolate.iClumpSize) / 2;
		rInterpolate.iClumpSize /= 2;

		rInterpolate.fNextClumpSpawnTime = 10.0f;
	}

	if (iSpawnCount > 0 && iSpawnCount > iWaveSpawnCount / 4)
	{
		SpawnSpaceships(rFrame, iSpawnCount);
	}

	iClumpsTotal = rInterpolate.spaceships.iCount;
	if (iClumpsTotal == 0)
	{
		rInterpolate.bNextWave = true;
		NextWave(rFrame);
	}
}

void WriteFramePostRenderDestroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] const FrameInputPressed& __restrict rFrameInputPressed, [[maybe_unused]] float fDeltaTime)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	if (rInterpolate.bNextWave)
	{
		for (int64_t i = 0; i < rInterpolate.blasters.iCount; ++i)
		{
			Blasters::Destroy(rFrame, i);
		}
		rInterpolate.blasters.iCount = 0;

		for (int64_t i = 0; i < rInterpolate.missiles.iCount; ++i)
		{
			Missiles::Destroy(rFrame, i);
		}
		rInterpolate.missiles.iCount = 0;
	}
}

FXMVECTOR XM_CALLCONV Frame::EnemySpawnPosition()
{
	auto vecSpawnPosition = kVecEnemySpawnPosition;
	return XMVectorSetZ(vecSpawnPosition, engine::gBaseHeight.Get());
}

std::optional<FXMVECTOR> XM_CALLCONV Frame::ClosestEnemy(Frame& __restrict rFrame, FXMVECTOR vecPosition)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	float fClosestDistance = std::numeric_limits<float>::max();
	auto vecClosestPosition = XMVectorZero();

	for (decltype(rInterpolate.targets.uiMaxIndex) i = 0; i <= rInterpolate.targets.uiMaxIndex; ++i)
	{
		if (!rInterpolate.targets.pbUsed[i])
		{
			continue;
		}

		engine::TargetInfo& rTargetInfo = rInterpolate.targets.pObjectInfos[i];

		if (!(rTargetInfo.flags & engine::TargetFlags::kDestination) || (rTargetInfo.flags & engine::TargetFlags::kTargetIsEnemy) == 0)
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
[[nodiscard]] engine::target_t Frame::GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, engine::TargetFlags_t targetFlags)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	engine::target_t uiTarget = 0;
	float fSmallestAngle = std::numeric_limits<float>::max();
	engine::subscriber_t uiLeastSubscribers = std::numeric_limits<engine::subscriber_t>::max();

	for (decltype(rInterpolate.targets.uiMaxIndex) i = 0; i <= rInterpolate.targets.uiMaxIndex; ++i)
	{
		if (!rInterpolate.targets.pbUsed[i])
		{
			continue;
		}

		engine::TargetInfo& rTargetInfo = rInterpolate.targets.pObjectInfos[i];

		if (!(rTargetInfo.flags & engine::TargetFlags::kDestination) || (rTargetInfo.flags & targetFlags) == 0)
		{
			continue;
		}

		auto vecMissilePosition = vecPosition;
		auto vecMissileDirection = vecDirection;
		auto vecToTargetNormal = XMVector3Normalize(XMVectorSubtract(rTargetInfo.vecPosition, vecMissilePosition));
		float fAngle = std::abs(XMVectorGetX(XMVector3AngleBetweenNormals(vecMissileDirection, vecToTargetNormal)));

		engine::Target& rTarget = rInterpolate.targets.pObjects[i];
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
		++(rInterpolate.targets.Get(uiTarget).uiSubscribers);
	}

	return uiTarget;
}

// DT: TODO Replace with area damage pool
void XM_CALLCONV Frame::AreaDamage(Frame& __restrict rFrame, FXMVECTOR vecPosition, float fDamage, float fRadius)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	CollectionAreaDamage(rFrame, vecPosition, fRadius, fDamage, Spaceships::kfFreezeTimeAreaDamage, rInterpolate.spaceships, SpaceshipFlags::kExploding, false);
}

void XM_CALLCONV Frame::BlasterImpact(Frame& __restrict rFrame, int64_t i, FXMVECTOR vecImpactPosition)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	auto vecImpactPositionAtBlasterHeight = XMVectorSetZ(vecImpactPosition, XMVectorGetZ(rInterpolate.blasters.pVecPositions[i]));
	rInterpolate.blasters.pFlags[i] |= BlasterFlags::kImpactObject;

	ASSERT(XMVectorGetZ(rInterpolate.blasters.pVecVelocities[i]) == 0.0f);
}

void XM_CALLCONV Frame::SpawnPickup(Frame& __restrict rFrame, FXMVECTOR vecPosition, float fChance, bool bForce)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	if (!bForce && common::Random(rInterpolate.randomEngine) > fChance)
	{
		return;
	}

	engine::billboard_t uiBillboard = 0;
	rInterpolate.billboards.Add(uiBillboard,
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
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	rInterpolate.fEndTime = rInterpolate.fCurrentTime;
	if (bRemoveAutosave)
	{
		gpGame->RemoveAutosave();
	}
}

void XM_CALLCONV SpawnDamageParticles(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fPercent)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

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
		int32_t iDamageParticleCookie = 36 + common::Random(3, rInterpolate.randomEngine);

		auto vecDamageParticlePosition = XMVectorMultiplyAdd(XMVectorReplicate(0.75f * kfDamageParticleOffset), vecDirection, vecPosition);
		vecDamageParticlePosition = XMVectorAdd(vecDamageParticlePosition, XMVectorSet(-kfDamageParticlePositionJitter + common::Random<2.0f * kfDamageParticlePositionJitter>(rInterpolate.randomEngine), -kfDamageParticlePositionJitter + common::Random<2.0f * kfDamageParticlePositionJitter>(rInterpolate.randomEngine), -kfDamageParticlePositionJitter + common::Random<2.0f * kfDamageParticlePositionJitter>(rInterpolate.randomEngine), 0.0f));
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecDamageParticlePosition);

		auto vecDamageParticleVelocity = XMVectorSet(-kfDamageParticleVelocityJitter + common::Random<2.0f * kfDamageParticleVelocityJitter>(rInterpolate.randomEngine), -kfDamageParticleVelocityJitter + common::Random<2.0f * kfDamageParticleVelocityJitter>(rInterpolate.randomEngine), -kfDamageParticleVelocityJitter + common::Random<2.0f * kfDamageParticleVelocityJitter>(rInterpolate.randomEngine), 0.0f);
		XMFLOAT4A f4Velocity {};
		XMStoreFloat4A(&f4Velocity, vecDamageParticleVelocity);

		uint32_t uiParticleColor = 0xFF0000FF | ((25 + common::Random(125, rInterpolate.randomEngine)) << 16) | ((common::Random(50, rInterpolate.randomEngine)) << 8);

		engine::ParticleManager::Spawn(engine::gpParticleManager->mSquareParticlesSpawnLayout,
		{
			.i4Misc = {static_cast<int32_t>(uiParticleColor), iDamageParticleCookie, static_cast<int32_t>(kfDamageParticleLightingIntesnity), 0},
			.f4MiscOne = {kfDamageParticleVelocityDecay, 0.0f, kfDamageParticleIntensityDecayMin, kfDamageParticleLightingSize},
			.f4MiscTwo = {kfDamageParticleSizeMin + common::Random<kfDamageParticleSizeRandom>(rInterpolate.randomEngine), 0.0f, kfDamageParticleIntensityMin + (1.0f - fPercent) * common::Random<kfDamageParticleIntensityRandom>(rInterpolate.randomEngine), kfDamageParticleIntensityPower},
			.f4MiscThree = {kfDamageParticleSizeDecay, -kfDamageParticleRotationDelta + common::Random<2.0f * kfDamageParticleRotationDelta>(rInterpolate.randomEngine), common::Random<XM_2PI>(rInterpolate.randomEngine), kfDamageParticleRotationDeltaDecay},
			.f4Position = f4Position,
			.f4Velocity = f4Velocity,
		});
	}
}
			
void XM_CALLCONV SpawnBurnParticles(FXMVECTOR vecPosition, FXMVECTOR vecVelocity, FXMVECTOR vecDirection, float fSize, float fIntensity)
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

bool FrameInterpolate::operator==(const FrameInterpolate& rOther) const
{
	bool bEqual = true;

	// Base class members
	bEqual &= engine::FrameBaseInterpolate::operator==(rOther);

	// FrameInterpolate members (from old FrameCamera)
	bEqual &= common::BreakOnNotEqual(flags, rOther.flags);
	bEqual &= common::BreakOnNotEqual(fEndTime, rOther.fEndTime);
	bEqual &= common::BreakOnNotEqual(fDeltaTime, rOther.fDeltaTime);
	bEqual &= common::BreakOnNotEqual(fWaveDisplayTimeLeft, rOther.fWaveDisplayTimeLeft);
	bEqual &= common::BreakOnNotEqual(bNextWave, rOther.bNextWave);
	bEqual &= common::BreakOnNotEqual(iWave, rOther.iWave);
	bEqual &= common::BreakOnNotEqual(iLastSpawn, rOther.iLastSpawn);
	bEqual &= common::BreakOnNotEqual(iClumpsLeft, rOther.iClumpsLeft);
	bEqual &= common::BreakOnNotEqual(iClumpSize, rOther.iClumpSize);
	bEqual &= common::BreakOnNotEqual(iNextClumpSpawn, rOther.iNextClumpSpawn);
	bEqual &= common::BreakOnNotEqual(fNextClumpSpawnTime, rOther.fNextClumpSpawnTime);

	// Collection members
	bEqual &= common::BreakOnNotEqual(player, rOther.player);
	bEqual &= common::BreakOnNotEqual(blasters, rOther.blasters);
	bEqual &= common::BreakOnNotEqual(missiles, rOther.missiles);
	bEqual &= common::BreakOnNotEqual(spaceships, rOther.spaceships);

	// Camera state
	bEqual &= common::BreakOnNotEqual(vecCameraOffsetSmoothed, rOther.vecCameraOffsetSmoothed);
	bEqual &= common::BreakOnNotEqual(fCameraEyeHeight, rOther.fCameraEyeHeight);
	bEqual &= common::BreakOnNotEqual(fCameraEyeRotation, rOther.fCameraEyeRotation);
	bEqual &= common::BreakOnNotEqual(fCameraShake, rOther.fCameraShake);
	bEqual &= common::BreakOnNotEqual(fCameraEyeHeightVelocity, rOther.fCameraEyeHeightVelocity);
	bEqual &= common::BreakOnNotEqual(fCameraEyeRotationVelocity, rOther.fCameraEyeRotationVelocity);

	return bEqual;
}

bool FramePostRender::operator==(const FramePostRender& rOther) const
{
	bool bEqual = true;

	// Base class members
	bEqual &= engine::FrameBasePostRender::operator==(rOther);

	// FramePostRender has no additional members

	return bEqual;
}

bool Frame::operator==(const Frame& rOther) const
{
	bool bEqual = true;

	bEqual &= common::BreakOnNotEqual(interpolate, rOther.interpolate);
	bEqual &= common::BreakOnNotEqual(postRender, rOther.postRender);

	return bEqual;
}

} // namespace game
