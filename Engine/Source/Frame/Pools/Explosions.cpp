#include "Explosions.h"

#include "Graphics/Managers/ParticleManager.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

using enum ExplosionFlags;

constexpr float kfPrimaryTime = 0.075f;
constexpr float kfPrimarySize = 2.5f;
constexpr float kfSecondaryPositionMin = 0.25f;
constexpr float kfSecondaryPositionJitter = 1.0f;

// Light
constexpr float kfPrimaryVisibleSize = 1.25f;
constexpr float kfPrimaryVisibleIntensity = 0.6f;
constexpr float kfPrimaryLightingSize = 2.25f;
constexpr float kfPrimaryLightingIntensity = 900.0f;
constexpr float kfSecondaryVisibleSize = 1.25f;
constexpr float kfSecondaryVisibleIntensity = kfPrimaryVisibleIntensity;
constexpr float kfSecondaryLightingSize = 0.75f * kfPrimaryLightingSize;
constexpr float kfSecondaryLightingIntensity = 0.25f * kfPrimaryLightingIntensity;

// Puffs
constexpr float kfPrimaryPuffSize = 2.0f;
constexpr float kfPrimaryPuffStartTime = 0.0f;
constexpr float kfPrimaryPuffEndTime = 0.2f;
constexpr float kfPrimaryPuffIntensity = 4.0f / (kfPrimaryPuffEndTime - kfPrimaryPuffStartTime);
constexpr float kfSecondaryPuffTimes = 0.5f * (kfPrimaryPuffEndTime - kfPrimaryPuffStartTime);
constexpr float kfSecondaryPuffIntensity = 1.0f / kfSecondaryPuffTimes;

// Trails
constexpr float kfTrailDelayTime = 0.0f;
constexpr float kfTrailTimeMin = 0.2f;
constexpr float kfTrailTimeRandom = 0.2f;
constexpr float kfTrailIntensityMin = 0.025f;
constexpr float kfTrailIntensityRandom = 0.025f;
constexpr float kfTrailStart = 0.4f;
constexpr float kfTrailLengthMin = 0.5f;
constexpr float kfTrailLengthRandom = 3.0f;

// Pushers
constexpr float kfPusherStartTime = 0.0f;
constexpr float kfPusherEndTime = 0.025f;
constexpr float kfPusherRadius = 6.0f;
constexpr float kfPusherIntensity = 20000.0f;
constexpr float kfPusherPower = 3.0f;

// Particles
constexpr int32_t kiParticleCookie = 6;
constexpr float kfParticlePositionJitter = 0.5f;
constexpr float kfParticleVelocityMin = 1.0f;
constexpr float kfParticleVelocityRandom = 10.0f;
constexpr float kfParticleVerticalVelocity = 20.0f;
constexpr float kfParticleVelocityDecay = 1.0f;
constexpr float kfParticleGravity = 30.0f;
constexpr float kfParticleWidth = 0.035f;
constexpr float kfParticleLength = 0.12f;
constexpr float kfParticleIntensityMin = 0.25f;
constexpr float kfParticleIntensityRandom = 3.0f;
constexpr float kfParticleIntensityDecay = 1.4f;
constexpr float kfParticleIntensityPower = 2.5f;
constexpr float kfParticleLightingSize = 10.0f;
constexpr float kfParticleLightingIntesnity = 400.0f;

void Explosions::SetupExplosion(game::Frame& rFrame, const ExplosionInfo& rExplosionInfo, Explosion& rExplosion)
{
	rExplosion.fStartTime = rFrame.fCurrentTime;

	float fLightPercent = rExplosionInfo.fLightPercent;
	float fSizePercent = rExplosionInfo.fSizePercent;
	float fSmokePercent = rExplosionInfo.fSmokePercent;
	float fTimePercent = rExplosionInfo.fTimePercent;

	// Primary explosion
	uint32_t uiExplosionColor = 0xFFFFFFFF;

	float fPrimaryRotation = common::Random<XM_2PI>(rFrame.randomEngine);

	rFrame.pointLightControllers3.Add(rFrame.pointLights, rFrame.fCurrentTime,
	{
		.bDestroysSelf = true,
		.pfTimes =
		{
			0.0f,
			0.4f * kfPrimaryTime,
			3.0f * kfPrimaryTime,
		},
		.pObjectInfos =
		{
			{.vecPosition = rExplosionInfo.vecPosition, .uiColor = uiExplosionColor, .fVisibleArea = 0.3f * fSizePercent * kfPrimaryVisibleSize, .fVisibleIntensity = 0.25f * fLightPercent * kfPrimaryVisibleIntensity, .fLightingArea = 0.6f * fSizePercent * kfPrimaryLightingSize, .fLightingIntensity = 0.25f * fLightPercent * kfPrimaryLightingIntensity, .crc = data::kTexturesBC7ExplosionpngCrc, .fRotation = fPrimaryRotation},
			{.vecPosition = rExplosionInfo.vecPosition, .uiColor = uiExplosionColor, .fVisibleArea = 0.6f * fSizePercent * kfPrimaryVisibleSize, .fVisibleIntensity = fLightPercent * kfPrimaryVisibleIntensity,         .fLightingArea = 1.5f * fSizePercent * kfPrimaryLightingSize, .fLightingIntensity = fLightPercent * kfPrimaryLightingIntensity,         .crc = data::kTexturesBC7ExplosionpngCrc, .fRotation = fPrimaryRotation},
			{.vecPosition = rExplosionInfo.vecPosition, .uiColor = uiExplosionColor, .fVisibleArea = 0.6f * fSizePercent * kfPrimaryVisibleSize, .fVisibleIntensity = 0.0f,                                              .fLightingArea = 1.2f * fSizePercent * kfPrimaryLightingSize, .fLightingIntensity = 0.0f,                                               .crc = data::kTexturesBC7ExplosionpngCrc, .fRotation = fPrimaryRotation},
		},
	});
	rFrame.puffControllers2.Add(rFrame.puffs, rFrame.fCurrentTime,
	{
		.bDestroysSelf = true,
		.pfTimes =
		{
			fTimePercent * kfPrimaryPuffStartTime,
			fTimePercent * kfPrimaryPuffEndTime,
		},
		.pObjectInfos =
		{
			{.vecPosition = rExplosionInfo.vecPosition, .fIntensity = fSmokePercent * kfPrimaryPuffIntensity, .fArea = 0.1f * fSizePercent * kfPrimaryPuffSize, .fCookie = 4.0f},
			{.vecPosition = rExplosionInfo.vecPosition, .fIntensity = fSmokePercent * kfPrimaryPuffIntensity, .fArea = 0.6f * fSizePercent * kfPrimaryPuffSize, .fCookie = 4.0f},
		},
	});

	// Secondary explosions
	int64_t kiSecondaryExplosions = 4; // + static_cast<int64_t>(common::Random<4.0f>(rFrame.randomEngine));
	float kfSecondaryExplosions = static_cast<float>(kiSecondaryExplosions);

	float fDelayDelta = (0.75f * fTimePercent * kfPrimaryTime) / kfSecondaryExplosions;
	float fDelay = fDelayDelta;
	for (int64_t i = 0; i < kiSecondaryExplosions; ++i, fDelay += fDelayDelta)
	{
		auto vecSecondaryExplosionPosition = XMVector3Rotate(XMVectorSet(kfSecondaryPositionMin + std::pow(fSizePercent, 1.5f) * common::Random<kfSecondaryPositionJitter>(rFrame.randomEngine), 0.0f, 0.0f, 0.0f), XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, common::Random<XM_2PI>(rFrame.randomEngine)));
		vecSecondaryExplosionPosition = XMVectorAdd(vecSecondaryExplosionPosition, rExplosionInfo.vecPosition);

		uiExplosionColor = 0x000000FF;
		uiExplosionColor |= ((100 + common::Random(155, rFrame.randomEngine)) << 24) | ((55 + common::Random(200, rFrame.randomEngine)) << 16) | ((55 + common::Random(200, rFrame.randomEngine)) << 8) | (55 + common::Random(200, rFrame.randomEngine));

		float fSecondaryLightSize = (0.25f + common::Random<0.5f>(rFrame.randomEngine)) * fSizePercent * kfSecondaryVisibleSize;
		rFrame.pointLightControllers3.Add(rFrame.pointLights, rFrame.fCurrentTime,
		{
			.bDestroysSelf = true,
			.pfTimes =
			{
				fDelay + 0.0f,
				fDelay + 1.0f * kfPrimaryTime,
				fDelay + 1.0f * kfPrimaryTime + 1.0f * kfPrimaryTime + common::Random<2.0f>(rFrame.randomEngine) * kfPrimaryTime,
			},
			.pObjectInfos =
			{
				{.vecPosition = vecSecondaryExplosionPosition, .uiColor = uiExplosionColor, .fVisibleArea = 0.0f,               .fVisibleIntensity = 0.0f,                                        .fLightingArea = 0.0f,                           .fLightingIntensity = 0.0f,                                          .crc = data::kTexturesBC7ExplosionpngCrc, .fRotation = fPrimaryRotation},
				{.vecPosition = vecSecondaryExplosionPosition, .uiColor = uiExplosionColor, .fVisibleArea = fSecondaryLightSize,.fVisibleIntensity = fLightPercent * kfSecondaryVisibleIntensity, .fLightingArea = 2.0f * kfSecondaryLightingSize, .fLightingIntensity = fLightPercent * kfSecondaryLightingIntensity, .crc = data::kTexturesBC7ExplosionpngCrc, .fRotation = fPrimaryRotation},
				{.vecPosition = vecSecondaryExplosionPosition, .uiColor = uiExplosionColor, .fVisibleArea = 0.0f,               .fVisibleIntensity = 0.0f,                                        .fLightingArea = 0.0f,                           .fLightingIntensity = 0.0f,                                          .crc = data::kTexturesBC7ExplosionpngCrc, .fRotation = fPrimaryRotation},
			},
		});

		float fSecondaryPuffSize = (0.25f + common::Random<0.5f>(rFrame.randomEngine)) * fSizePercent * kfPrimaryPuffSize;
		rFrame.puffControllers2.Add(rFrame.puffs, rFrame.fCurrentTime,
		{
			.bDestroysSelf = true,
			.pfTimes =
			{
				fDelay,
				fDelay + fTimePercent * kfSecondaryPuffTimes,
			},
			.pObjectInfos =
			{
				{.vecPosition = rExplosionInfo.vecPosition,    .fIntensity = fSmokePercent * kfSecondaryPuffIntensity, .fArea = 0.1f * fSecondaryPuffSize, .fCookie = 4.0f},
				{.vecPosition = vecSecondaryExplosionPosition, .fIntensity = fSmokePercent * kfSecondaryPuffIntensity, .fArea = 0.6f * fSecondaryPuffSize, .fCookie = 4.0f},
			},
		});
	}

	// Trails
	auto vecDirection2dNormal = XMVector3Normalize(XMVectorMultiply(XMVectorSet(1.0f, 1.0f, 0.0f, 0.0f), rExplosionInfo.vecDirection));

	for (int64_t i = 0; i < rExplosionInfo.uiTrailCount; ++i)
	{
		rExplosion.pTrails[i] = 0;
				
		rExplosion.pfTrailTimes[i] = fTimePercent * (kfTrailTimeMin + common::Random<kfTrailTimeRandom>(rFrame.randomEngine));

		rExplosion.pfTrailIntensities[i] = fSmokePercent * (kfTrailIntensityMin + common::Random<kfTrailIntensityRandom>(rFrame.randomEngine));

		auto vecDirection = vecDirection2dNormal;
		if (i != 0)
		{
			float fTrailAngle = rExplosionInfo.fTrailAngle;
			vecDirection = XMVector3Rotate(vecDirection, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, fTrailAngle * common::Random(rFrame.randomEngine)));
		}
		rExplosion.pVecTrailStartPositions[i] = XMVectorMultiplyAdd(vecDirection, XMVectorReplicate(kfTrailStart), rExplosionInfo.vecPosition);
		rExplosion.pVecTrailEndPositions[i] = XMVectorMultiplyAdd(vecDirection, XMVectorReplicate(kfTrailLengthMin + kfTrailLengthRandom * common::Random(rFrame.randomEngine)), rExplosionInfo.vecPosition);

		rFrame.trails.Add(rExplosion.pTrails[i], rFrame.fCurrentTime,
		{
			.vecPosition = rExplosion.pVecTrailStartPositions[i],
			.fIntensity = 0.0f,
		});
	}

	// Particles
	int64_t iParticleCount = rExplosionInfo.uiParticleCount;
	for (int64_t i = 0; i < iParticleCount; ++i)
	{
		float fParticleAngle = rExplosionInfo.fParticleAngle;

		auto vecPosition = rExplosionInfo.vecPosition;
		vecPosition = XMVectorAdd(vecPosition, XMVectorSet(-kfParticlePositionJitter + common::Random<2.0f * kfParticlePositionJitter>(rFrame.randomEngine), -kfParticlePositionJitter + common::Random<2.0f * kfParticlePositionJitter>(rFrame.randomEngine), 0.0f, 0.0f));
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecPosition);

		auto vecVelocity = XMVectorMultiply(XMVectorReplicate(kfParticleVelocityMin + common::Random<kfParticleVelocityRandom>(rFrame.randomEngine)), vecDirection2dNormal);
		vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, -0.5f * fParticleAngle + fParticleAngle * common::Random(rFrame.randomEngine)));
		vecVelocity = XMVectorSetZ(vecVelocity, common::Random<kfParticleVerticalVelocity>(rFrame.randomEngine));
		XMFLOAT4A f4Velocity {};
		XMStoreFloat4A(&f4Velocity, vecVelocity);

		uint32_t uiParticleColor = 0xFF0000FF;
		if (rExplosionInfo.flags & kYellow)
		{
			uiParticleColor |= ((100 + common::Random(25, rFrame.randomEngine)) << 16) | ((common::Random(25, rFrame.randomEngine)) << 8);
		}
		else if (rExplosionInfo.flags & kRed)
		{
			uiParticleColor |= ((50 + common::Random(25, rFrame.randomEngine)) << 16) | ((common::Random(25, rFrame.randomEngine)) << 8);
		}

		ParticleManager::Spawn(gpParticleManager->mLongParticlesSpawnLayout,
		{
			.i4Misc = {static_cast<int32_t>(uiParticleColor), kiParticleCookie, static_cast<int32_t>(kfParticleLightingIntesnity), 0},
			.f4MiscOne = {kfParticleVelocityDecay, kfParticleGravity, kfParticleIntensityDecay, kfParticleLightingSize},
			.f4MiscTwo = {kfParticleWidth, kfParticleLength, kfParticleIntensityMin + common::Random<kfParticleIntensityRandom>(rFrame.randomEngine), kfParticleIntensityPower},
			.f4MiscThree = {},
			.f4Position = f4Position,
			.f4Velocity = f4Velocity,
		});
	}
}

void Explosions::Interpolate(game::Frame& __restrict rFrame)
{
	Explosions& rCurrent = rFrame.explosions;

	for (decltype(rCurrent.uiMaxIndex) i = 0; i <= rCurrent.uiMaxIndex; ++i)
	{
		if (!rCurrent.pbUsed[i])
		{
			continue;
		}

		ExplosionInfo& rExplosionInfo = rCurrent.pObjectInfos[i];
		Explosion& rExplosion = rCurrent.pObjects[i];
		
		float fTimePercent = rExplosionInfo.fTimePercent;

		float fExplosionTime = rFrame.fCurrentTime - rExplosion.fStartTime;

		float fEndTime = 0.0f;
		for (int64_t j = 0; j < rExplosionInfo.uiTrailCount; ++j)
		{
			float fTrailEndTime = fTimePercent * kfTrailDelayTime + rExplosion.pfTrailTimes[j];
			fEndTime = std::max(fEndTime, fTrailEndTime);

			if (fExplosionTime >= fTrailEndTime) [[unlikely]]
			{
				rFrame.trails.Remove(rExplosion.pTrails[j]);
			}
		}

		if (fExplosionTime >= fTimePercent * kfPusherStartTime && fExplosionTime <= fTimePercent * kfPusherEndTime)
		{
			float fPusherPercent = (fExplosionTime - fTimePercent * kfPusherStartTime) / (fTimePercent * kfPusherEndTime - fTimePercent * kfPusherStartTime);
			rFrame.pushers.Add(rExplosion.pusher,
			{
				.f2Position = {XMVectorGetX(rExplosionInfo.vecPosition), XMVectorGetY(rExplosionInfo.vecPosition)},
				.fRadius = kfPusherRadius + 3.0f * fPusherPercent * kfPusherRadius,
				.fIntensity = rExplosionInfo.fPusherPercent * (1.0f - fPusherPercent) * (1.0f - fPusherPercent) * (1.0f - fPusherPercent) * kfPusherIntensity,
				.fPower = kfPusherPower,
			});
		}
		else
		{
			rFrame.pushers.Remove(rExplosion.pusher);
		}

		if (rExplosionInfo.flags & kDestroysSelf && fExplosionTime >= fEndTime) [[unlikely]]
		{
			rFrame.pushers.Remove(rExplosion.pusher);
			rCurrent.Remove(i);
			continue;
		}

		for (int64_t j = 0; j < kiMaxExplosionTrails; ++j)
		{
			if (rExplosion.pTrails[j] != 0)
			{
				TrailInfo& rTrailInfo = rFrame.trails.GetInfo(rExplosion.pTrails[j]);

				float fTrailPercent = (fExplosionTime - fTimePercent * kfTrailDelayTime) / rExplosion.pfTrailTimes[j];

				static constexpr float kfGravity = 2.0f;
				rTrailInfo.vecPosition = XMVectorLerp(rExplosion.pVecTrailStartPositions[j], rExplosion.pVecTrailEndPositions[j] - fTrailPercent * XMVectorSet(0.0f, 0.0f, kfGravity, 0.0f), fTrailPercent);
				rTrailInfo.fIntensity = (1.0f - fTrailPercent) * rExplosion.pfTrailIntensities[j];
			}
		}
	}
}

} // namespace engine
