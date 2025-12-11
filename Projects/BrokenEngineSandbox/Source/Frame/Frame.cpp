#include "Frame.h"

#include "Frame/Collision.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace game
{

using enum FrameFlags;

void FrameInterpolate::Register()
{
	// Parent
	FrameInterpolateBase::Register();

	// Player
	PlayerInterpolate::Register();

	// Collections
	BlastersInterpolate::Register();
	MissilesInterpolate::Register();
	SpaceshipsInterpolate::Register();
	TargetsInterpolate::Register();
}

void FrameInterpolate::GraphicsResources()
{
	// Parent
	FrameInterpolateBase::GraphicsResources();

	// Player
	PlayerInterpolate::GraphicsResources();

	// Collections
	BlastersInterpolate::GraphicsResources();
	MissilesInterpolate::GraphicsResources();
	SpaceshipsInterpolate::GraphicsResources();
	TargetsInterpolate::GraphicsResources();
}

void FrameInterpolate::AllocateAndCopy(FrameInterpolate& __restrict rCurrent, const FrameInterpolate& __restrict rPrevious)
{
	// Parent
	FrameInterpolateBase::AllocateAndCopy(rCurrent, rPrevious);

	// Collections
	BlastersInterpolate::AllocateAndCopy(rCurrent.blasters, rPrevious.blasters);
	MissilesInterpolate::AllocateAndCopy(rCurrent.missiles, rPrevious.missiles);
	SpaceshipsInterpolate::AllocateAndCopy(rCurrent.spaceships, rPrevious.spaceships);
	TargetsInterpolate::AllocateAndCopy(rCurrent.targets, rPrevious.targets);
}

void FrameInterpolate::Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	SCOPED_CPU_PROFILE(engine::kCpuTimerFrameInterpolate);

	const FrameInterpolate& rPrevious = rPreviousFrame.interpolate;

	// Parent
	FrameInterpolateBase::Update(rCurrent, rPreviousFrame, fDeltaTime);

	// Update sun angle with varying speeds
	if (!(rPreviousFrame.interpolate.flags & kMainMenu))
	{
		static constexpr float kfNoonSpeedStart = XM_PIDIV2 - XM_PIDIV8;
		static constexpr float kfNoonSpeedEnd = XM_PIDIV2 + XM_PIDIV8;
		static constexpr float kfNightSpeedStart = XM_PI;
		static constexpr float kfNightSpeedEnd = XM_2PI;
		if (rCurrent.fSunAngle >= kfNoonSpeedStart && rCurrent.fSunAngle < kfNoonSpeedEnd)
		{
			rCurrent.fSunAngle = rCurrent.fSunAngle + fDeltaTime * 0.025f;
		}
		else if (rCurrent.fSunAngle >= kfNightSpeedStart && rCurrent.fSunAngle < kfNightSpeedEnd)
		{
			rCurrent.fSunAngle = rCurrent.fSunAngle + fDeltaTime * 0.5f;
		}
		else
		{
			rCurrent.fSunAngle = rCurrent.fSunAngle + fDeltaTime * 0.01f;
		}

		if (rCurrent.fSunAngle >= XM_2PI)
		{
			rCurrent.fSunAngle = 0.0f;
		}
	}

	// Load
	FrameFlags_t flags = rPrevious.flags;
	float fSpawnTimer = rPrevious.fSpawnTimer;

	// Update spawn timer
	fSpawnTimer += fDeltaTime;

	// Save
	rCurrent.flags = flags;
	rCurrent.fSpawnTimer = fSpawnTimer;

	// Player
	PlayerInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);

	// Collections
	BlastersInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	MissilesInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	SpaceshipsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	TargetsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
}

void FrameInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	// Parent
	engine::FrameInterpolateBase::Render(rFrameInterpolate, iCommandBuffer);

	// Player
	PlayerInterpolate::Render(rFrameInterpolate, iCommandBuffer);

	// Collections
	BlastersInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	MissilesInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	SpaceshipsInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	TargetsInterpolate::Render(rFrameInterpolate, iCommandBuffer);
}

void FramePostRender::AllocateAndCopy(FramePostRender& __restrict rCurrent, const FramePostRender& __restrict rPrevious)
{
	// Parent
	engine::FramePostRenderBase::AllocateAndCopy(rCurrent, rPrevious);

	// Collections
	BlastersPostRender::AllocateAndCopy(rCurrent.blasters, rPrevious.blasters);
	MissilesPostRender::AllocateAndCopy(rCurrent.missiles, rPrevious.missiles);
	SpaceshipsPostRender::AllocateAndCopy(rCurrent.spaceships, rPrevious.spaceships);
	TargetsPostRender::AllocateAndCopy(rCurrent.targets, rPrevious.targets);
}

void FramePostRender::Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime, const FrameInput& __restrict rFrameInput)
{
	SCOPED_CPU_PROFILE(engine::kCpuTimerFramePostRender);

	// Parent
	FramePostRenderBase::Update(rFrame, rPreviousFrame, fDeltaTime, rFrameInput);

	// Player
	PlayerPostRender::Update(rFrame, rPreviousFrame, fDeltaTime, rFrameInput);

	// Collections
	BlastersPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	TargetsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
}

void FramePostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::Destroy(rFrame, fDeltaTime);

	// Player
	PlayerPostRender::Destroy(rFrame, fDeltaTime);

	// Collections
	BlastersPostRender::Destroy(rFrame, fDeltaTime);
	MissilesPostRender::Destroy(rFrame, fDeltaTime);
	SpaceshipsPostRender::Destroy(rFrame, fDeltaTime);
	TargetsPostRender::Destroy(rFrame, fDeltaTime);
}

static void SpawnSingleSpaceship(Frame& __restrict rFrame, float fDeltaTime)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	constexpr float kfSpawnRadius = 100.0f;

	// Random angle around player
	float fAngle = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
	auto vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngle));
	float fCurrentRadius = kfSpawnRadius;
	auto vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(fCurrentRadius), vecDirection, rInterpolate.player.vecPosition);

	// Avoid islands, retry with expanded radius
retry:
	float fTerrainElevation = engine::gpIslands->GlobalElevation(vecSpawnPosition);
	if (fTerrainElevation > 0.0f)
	{
		fCurrentRadius += 1.0f;
		vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(fCurrentRadius), vecDirection, rInterpolate.player.vecPosition);
		goto retry;
	}

	// If spawn position is outside bounds, spawn on opposite side of player (toward center)
	if (common::PointOutsideArea(vecSpawnPosition, rFrame.interpolate.f4GlobalArea))
	{
		vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(-kfSpawnRadius), vecDirection, rInterpolate.player.vecPosition);
	}

	XMVECTOR vecDirectionToPlayer = XMVector3Normalize(XMVectorSubtract(rInterpolate.player.vecPosition, vecSpawnPosition));
	SpaceshipsPostRender::Spawn(rFrame, fDeltaTime,
	{
		.vecPosition = vecSpawnPosition,
		.vecDirection = vecDirectionToPlayer,
	});
}

void FramePostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::Spawn(rFrame, fDeltaTime);

	// Player
	PlayerPostRender::Spawn(rFrame, fDeltaTime);

	// Collections
	BlastersPostRender::Spawn(rFrame, fDeltaTime);
	MissilesPostRender::Spawn(rFrame, fDeltaTime);
	SpaceshipsPostRender::Spawn(rFrame, fDeltaTime);
	TargetsPostRender::Spawn(rFrame, fDeltaTime);

	FrameInterpolate& rInterpolate = rFrame.interpolate;
	if (rFrame.interpolate.flags & FrameFlags::kMainMenu)
	{
		return;
	}

	// Spawn one spaceship every half second
	constexpr float kfSpawnInterval = 0.5f;
	while (rInterpolate.fSpawnTimer >= kfSpawnInterval)
	{
		rInterpolate.fSpawnTimer -= kfSpawnInterval;
		SpawnSingleSpaceship(rFrame, fDeltaTime);
	}
}

void FramePostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::PreCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Player
	PlayerPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Collections
	BlastersPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	TargetsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
}

void FramePostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::PostCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Player
	PlayerPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Collections
	BlastersPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);
	TargetsPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);

	engine::Collision::Clear();
}

void FramePostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);

	// Player
	PlayerPostRender::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);

	// Collections
	BlastersPostRender::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);
	TargetsPostRender::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);

	engine::Collision::ClearAreaDamage();
}

[[nodiscard]] target_t Frame::GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, TargetFlags_t targetFlags)
{
	// static constexpr float kfMaxTargetingRange = 65.0f;
	static constexpr float kfMaxTargetingRange = 45.0f;
	static constexpr float kfMaxTargetingRangeSquared = kfMaxTargetingRange * kfMaxTargetingRange;

	TargetsInterpolate& rTargetsInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rTargetsPostRender = rFrame.postRender.targets;

	target_t uiTarget {};
	float fSmallestAngle = std::numeric_limits<float>::max();

	for (int64_t i = 0; i < rTargetsInterpolate.iCount; ++i)
	{
		TargetFlags_t flags = rTargetsPostRender.pFlags[i];

		// Must be a destination and match the requested target flags
		if (!(flags & TargetFlags::kDestination) || (flags & targetFlags) == 0)
		{
			continue;
		}

		XMVECTOR vecTargetPosition = rTargetsInterpolate.pVecPositions[i];

		// Skip targets not visible to the player
		if (!FrameInterpolate::IsVisible(vecPosition, vecTargetPosition))
		{
			continue;
		}

		XMVECTOR vecToTarget = XMVectorSubtract(vecTargetPosition, vecPosition);
		float fDistanceSquared = XMVectorGetX(XMVector3LengthSq(vecToTarget));

		// Skip targets beyond maximum targeting range
		if (fDistanceSquared > kfMaxTargetingRangeSquared)
		{
			continue;
		}

		XMVECTOR vecToTargetNormal = XMVector3Normalize(vecToTarget);
		float fAngle = std::abs(XMVectorGetX(XMVector3AngleBetweenNormals(vecDirection, vecToTargetNormal)));

		if (fAngle < fSmallestAngle)
		{
			fSmallestAngle = fAngle;
			uiTarget = rTargetsPostRender.puiIds[i];
		}
	}

	if (uiTarget.IsValid())
	{
		TargetsPostRender::AddSubscriber(rFrame, uiTarget);
	}

	return uiTarget;
}

} // namespace game
