#include "Frame.h"

#include "Frame/Collision.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace game
{

using enum FrameFlags;

void InitializeCollisionGroups(engine::FramePostRenderBase& rPostRender)
{
	engine::CollisionGroups& rGroups = rPostRender.collisionGroups;

	// Register player and enemy groups
	gPlayerGroup = rGroups.Add(rPostRender);
	gEnemyGroup = rGroups.Add(rPostRender);

	// Same-alignment groups don't collide with each other
	rGroups.SetCanCollide(gPlayerGroup, gPlayerGroup, false);
	rGroups.SetCanCollide(gEnemyGroup, gEnemyGroup, false);
}

void RestoreCollisionGroupGlobals(const engine::collision_group_t& rPlayerGroup, const engine::collision_group_t& rEnemyGroup)
{
	gPlayerGroup = rPlayerGroup;
	gEnemyGroup = rEnemyGroup;
}

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
	SCOPED_CPU_PROFILE(game::kCpuTimerInterpolateAllocateAndCopy);

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
	SCOPED_CPU_PROFILE(game::kCpuTimerInterpolateUpdate);

	const FrameInterpolate& rPrevious = rPreviousFrame.interpolate;

	// Parent
	FrameInterpolateBase::Update(rCurrent, rPreviousFrame, fDeltaTime);

	// Load
	FrameFlags_t flags = rPrevious.flags;
	float fSpawnTimer = rPrevious.fSpawnTimer;

	// Update spawn timer
	fSpawnTimer += fDeltaTime;

	// Save
	rCurrent.flags = flags;
	rCurrent.fSpawnTimer = fSpawnTimer;

	// Player
	PlayerInterpolate::Update(rCurrent, rPreviousFrame);

	// Collections
	BlastersInterpolate::Update(rCurrent, rPreviousFrame);
	MissilesInterpolate::Update(rCurrent, rPreviousFrame);
	SpaceshipsInterpolate::Update(rCurrent, rPreviousFrame);
	TargetsInterpolate::Update(rCurrent, rPreviousFrame);
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
	SCOPED_CPU_PROFILE(game::kCpuTimerPostRenderAllocateAndCopy);

	// Parent
	engine::FramePostRenderBase::AllocateAndCopy(rCurrent, rPrevious);

	// Collections
	BlastersPostRender::AllocateAndCopy(rCurrent.blasters, rPrevious.blasters);
	MissilesPostRender::AllocateAndCopy(rCurrent.missiles, rPrevious.missiles);
	SpaceshipsPostRender::AllocateAndCopy(rCurrent.spaceships, rPrevious.spaceships);
	TargetsPostRender::AllocateAndCopy(rCurrent.targets, rPrevious.targets);
}

void FramePostRender::Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput)
{
	SCOPED_CPU_PROFILE(game::kCpuTimerPostRenderUpdate);

	// Parent
	FramePostRenderBase::Update(rFrame, rPreviousFrame, rFrameInput);

	// Player
	PlayerPostRender::Update(rFrame, rPreviousFrame, rFrameInput);

	// Collections
	BlastersPostRender::Update(rFrame, rPreviousFrame);
	MissilesPostRender::Update(rFrame, rPreviousFrame);
	SpaceshipsPostRender::Update(rFrame, rPreviousFrame);
	TargetsPostRender::Update(rFrame, rPreviousFrame);
}

void FramePostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	SCOPED_CPU_PROFILE(game::kCpuTimerPostRenderDestroy);

	// Parent
	FramePostRenderBase::Destroy(rFrame);

	// Player
	PlayerPostRender::Destroy(rFrame);

	// Collections
	BlastersPostRender::Destroy(rFrame);
	MissilesPostRender::Destroy(rFrame);
	SpaceshipsPostRender::Destroy(rFrame);
	TargetsPostRender::Destroy(rFrame);
}

static void SpawnSingleSpaceship(Frame& __restrict rFrame)
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
	if (!common::InsideArea(vecSpawnPosition, rFrame.postRender.vecArea))
	{
		vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(-kfSpawnRadius), vecDirection, rInterpolate.player.vecPosition);
	}

	XMVECTOR vecDirectionToPlayer = XMVector3Normalize(XMVectorSubtract(rInterpolate.player.vecPosition, vecSpawnPosition));
	SpaceshipsPostRender::Spawn(rFrame,
	{
		.vecPosition = vecSpawnPosition,
		.vecDirection = vecDirectionToPlayer,
	});
}

void FramePostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
	SCOPED_CPU_PROFILE(game::kCpuTimerPostRenderSpawn);

	// Parent
	FramePostRenderBase::Spawn(rFrame);

	// Player
	PlayerPostRender::Spawn(rFrame);

	// Collections
	BlastersPostRender::Spawn(rFrame);
	MissilesPostRender::Spawn(rFrame);
	SpaceshipsPostRender::Spawn(rFrame);
	TargetsPostRender::Spawn(rFrame);

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
		SpawnSingleSpaceship(rFrame);
	}
}

void FramePostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SCOPED_CPU_PROFILE(game::kCpuTimerPostRenderPreCollision);

	// Parent
	FramePostRenderBase::PreCollision(rFrame, rPreviousFrame);

	// Player
	PlayerPostRender::PreCollision(rFrame, rPreviousFrame);

	// Collections
	BlastersPostRender::PreCollision(rFrame, rPreviousFrame);
	MissilesPostRender::PreCollision(rFrame, rPreviousFrame);
	SpaceshipsPostRender::PreCollision(rFrame, rPreviousFrame);
	TargetsPostRender::PreCollision(rFrame, rPreviousFrame);
}

void FramePostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SCOPED_CPU_PROFILE(game::kCpuTimerPostRenderPostCollision);

	// Parent
	FramePostRenderBase::PostCollision(rFrame, rPreviousFrame);

	// Player
	PlayerPostRender::PostCollision(rFrame, rPreviousFrame);

	// Collections
	BlastersPostRender::PostCollision(rFrame, rPreviousFrame);
	MissilesPostRender::PostCollision(rFrame, rPreviousFrame);
	SpaceshipsPostRender::PostCollision(rFrame, rPreviousFrame);
	TargetsPostRender::PostCollision(rFrame, rPreviousFrame);

	engine::Collision::Clear();
}

void FramePostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SCOPED_CPU_PROFILE(game::kCpuTimerPostRenderAreaDamage);

	// Parent
	FramePostRenderBase::AreaDamage(rFrame, rPreviousFrame);

	// Player
	PlayerPostRender::AreaDamage(rFrame, rPreviousFrame);

	// Collections
	BlastersPostRender::AreaDamage(rFrame, rPreviousFrame);
	MissilesPostRender::AreaDamage(rFrame, rPreviousFrame);
	SpaceshipsPostRender::AreaDamage(rFrame, rPreviousFrame);
	TargetsPostRender::AreaDamage(rFrame, rPreviousFrame);

	engine::Collision::ClearAreaDamage();
}

[[nodiscard]] target_t Frame::GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, TargetFlags_t targetFlags)
{
	static constexpr float kfMaxTargetingRange = 45.0f;
	static constexpr float kfMaxTargetingRangeSquared = kfMaxTargetingRange * kfMaxTargetingRange;

	TargetsInterpolate& rTargetsInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rTargetsPostRender = rFrame.postRender.targets;

	target_t uiTarget {};
	float fSmallestAngle = std::numeric_limits<float>::max();
	uint8_t uiBestSubscribers = std::numeric_limits<uint8_t>::max();

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

		uint8_t uiSubscribers = rTargetsPostRender.puiSubscribers[i];
		if (uiSubscribers < uiBestSubscribers ||
		    (uiSubscribers == uiBestSubscribers && fAngle < fSmallestAngle))
		{
			uiBestSubscribers = uiSubscribers;
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
