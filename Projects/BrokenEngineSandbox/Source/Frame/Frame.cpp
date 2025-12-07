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
	MissilesInterpolate::Register();
}

void FrameInterpolate::GraphicsResources()
{
	// Parent
	FrameInterpolateBase::GraphicsResources();

	// Player
	PlayerInterpolate::AllocatePipelines();

	// Collections
	MissilesInterpolate::AllocatePipelines();
	SpaceshipsInterpolate::AllocatePipelines();
}

void FrameInterpolate::Allocate(FrameInterpolate& __restrict rCurrent, const FrameInterpolate& __restrict rPrevious)
{
	// Parent
	FrameInterpolateBase::Allocate(rCurrent, rPrevious);

	// Collections
	engine::Allocate(rCurrent.blasters, rPrevious.blasters, rCurrent.blasters.Members());
	engine::Allocate(rCurrent.missiles, rPrevious.missiles, rCurrent.missiles.Members());
	engine::Allocate(rCurrent.spaceships, rPrevious.spaceships, rCurrent.spaceships.Members());
	engine::Allocate(rCurrent.targets, rPrevious.targets, rCurrent.targets.Members());
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
	PlayerInterpolate::Update(rCurrent.player, rPreviousFrame, fDeltaTime);

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
	MissilesInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	SpaceshipsInterpolate::Render(rFrameInterpolate, iCommandBuffer);
}

void FramePostRender::Allocate(FramePostRender& __restrict rCurrent, const FramePostRender& __restrict rPrevious)
{
	// Parent
	engine::FramePostRenderBase::Allocate(rCurrent, rPrevious);

	// Collections
	engine::Allocate(rCurrent.blasters, rPrevious.blasters, rCurrent.blasters.Members());
	engine::Allocate(rCurrent.missiles, rPrevious.missiles, rCurrent.missiles.Members());
	engine::Allocate(rCurrent.spaceships, rPrevious.spaceships, rCurrent.spaceships.Members());
	engine::Allocate(rCurrent.targets, rPrevious.targets, rCurrent.targets.Members());
}

void FramePostRender::Update(game::Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime, const game::FrameInput& __restrict rFrameInput)
{
	SCOPED_CPU_PROFILE(engine::kCpuTimerFramePostRender);

	game::FramePostRender& rCurrent = rFrame.postRender;

	// Parent
	FramePostRenderBase::Update(rFrame, rPreviousFrame, fDeltaTime, rFrameInput);

	// Player
	PlayerPostRender::Update(rCurrent.player, rPreviousFrame, fDeltaTime, rFrameInput);

	// Collections
	BlastersPostRender::Update(rCurrent.blasters, rPreviousFrame, fDeltaTime);
	MissilesPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	TargetsPostRender::Update(rCurrent.targets, rPreviousFrame.postRender.targets);
}

// Spawn a single spaceship at random angle from player, avoiding islands
static void SpawnSingleSpaceship(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime)
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

	auto vecDirectionToPlayer = XMVector3Normalize(XMVectorSubtract(rInterpolate.player.vecPosition, vecSpawnPosition));
	SpaceshipsPostRender::Spawn(rFrame, rPreviousFrame, fDeltaTime, vecSpawnPosition, vecDirectionToPlayer);
}

void FramePostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::Spawn(rFrame, rPreviousFrame, fDeltaTime);

	// Player
	PlayerPostRender::Spawn(rFrame, rPreviousFrame, fDeltaTime);

	FrameInterpolate& rInterpolate = rFrame.interpolate;
	if (rFrame.interpolate.flags & FrameFlags::kMainMenu)
	{
		return;
	}

	// Spawn one spaceship every second
	constexpr float kfSpawnInterval = 1.0f;
	while (rInterpolate.fSpawnTimer >= kfSpawnInterval)
	{
		rInterpolate.fSpawnTimer -= kfSpawnInterval;
		SpawnSingleSpaceship(rFrame, rPreviousFrame, fDeltaTime);
	}
}

void FramePostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::PreCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Player
	PlayerPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Collections
	BlastersPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
}

void FramePostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::PostCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Player
	PlayerPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Collections
	BlastersPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);

	engine::Collision::Clear();
}

void FramePostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);

	// Player
	PlayerPostRender::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);

	// Collections
	SpaceshipsPostRender::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);

	engine::Collision::ClearAreaDamage();
}

void FramePostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FramePostRenderBase::Destroy(rFrame, rPreviousFrame, fDeltaTime);

	// Player
	PlayerPostRender::Destroy(rFrame, rPreviousFrame, fDeltaTime);

	// Collections
	BlastersPostRender::Destroy(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::Destroy(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::Destroy(rFrame, rPreviousFrame, fDeltaTime);
}

[[nodiscard]] target_t Frame::GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, TargetFlags_t targetFlags)
{
	static constexpr float kfMaxTargetingRange = 100.0f;
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
