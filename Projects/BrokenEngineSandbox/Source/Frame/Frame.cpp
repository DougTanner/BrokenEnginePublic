#include "Frame.h"

#include "Frame/Collision.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
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
	engine::ForEachRegister(GameInterpolateTypes{});
}

void FrameInterpolate::GraphicsResources()
{
	// Parent
	FrameInterpolateBase::GraphicsResources();

	// Player
	PlayerInterpolate::GraphicsResources();

	// Collections
	engine::ForEachGraphicsResources(GameInterpolateTypes{});
}

void FrameInterpolate::AllocateAndCopy(FrameInterpolate& __restrict rCurrent, const FrameInterpolate& __restrict rPrevious)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerInterpolateAllocateAndCopy);

	// Parent
	FrameInterpolateBase::AllocateAndCopy(rCurrent, rPrevious);

	// Collections
	engine::AllocateAndCopyCollections(rCurrent.Collections(), rPrevious.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(rCurrent.Collections())>>{});
}

void FrameInterpolate::Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerInterpolateUpdate);

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
	engine::ForEachInterpolateUpdate(GameInterpolateTypes{}, rCurrent, rPreviousFrame);
}

void FramePostRender::AllocateAndCopy(FramePostRender& __restrict rCurrent, const FramePostRender& __restrict rPrevious)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderAllocateAndCopy);

	// Parent
	engine::FramePostRenderBase::AllocateAndCopy(rCurrent, rPrevious);

	// Collections
	engine::AllocateAndCopyCollections(rCurrent.Collections(), rPrevious.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(rCurrent.Collections())>>{});
}

void FramePostRender::Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdate);

	// Parent
	FramePostRenderBase::Update(rFrame, rPreviousFrame, rFrameInput);

	// Player
	PlayerPostRender::Update(rFrame, rPreviousFrame, rFrameInput);

	// Collections
	engine::ForEachPostRenderUpdate(GamePostRenderTypes{}, rFrame, rPreviousFrame);
}

void FramePostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderDestroy);

	// Parent
	FramePostRenderBase::Destroy(rFrame);

	// Player
	PlayerPostRender::Destroy(rFrame);

	// Collections
	engine::ForEachPostRenderDestroy(GamePostRenderTypes{}, rFrame);
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
	float fTerrainElevation = engine::gpIslands->GlobalElevation(vecSpawnPosition);
	while (fTerrainElevation > 0.0f)
	{
		fCurrentRadius += 1.0f;
		vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(fCurrentRadius), vecDirection, rInterpolate.player.vecPosition);
		fTerrainElevation = engine::gpIslands->GlobalElevation(vecSpawnPosition);
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
		.alignment = rFrame.postRender.enemyAlignment,
	});
}

void FramePostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderSpawn);

	// Parent
	FramePostRenderBase::Spawn(rFrame);

	// Player
	PlayerPostRender::Spawn(rFrame);

	// Collections
	engine::ForEachPostRenderSpawn(GamePostRenderTypes{}, rFrame);

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
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderPreCollision);

	// Parent
	FramePostRenderBase::PreCollision(rFrame, rPreviousFrame);

	// Player
	PlayerPostRender::PreCollision(rFrame, rPreviousFrame);

	// Collections
	engine::ForEachPostRenderPreCollision(GamePostRenderTypes{}, rFrame, rPreviousFrame);
}

void FramePostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderPostCollision);

	// Parent
	FramePostRenderBase::PostCollision(rFrame, rPreviousFrame);

	// Player
	PlayerPostRender::PostCollision(rFrame, rPreviousFrame);

	// Collections
	engine::ForEachPostRenderPostCollision(GamePostRenderTypes{}, rFrame, rPreviousFrame);

	engine::Collision::Clear();
}

void FramePostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderAreaDamage);

	// Parent
	FramePostRenderBase::AreaDamage(rFrame, rPreviousFrame);

	// Player
	PlayerPostRender::AreaDamage(rFrame, rPreviousFrame);

	// Collections
	engine::ForEachPostRenderAreaDamage(GamePostRenderTypes{}, rFrame, rPreviousFrame);

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

void FrameInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	// Parent
	engine::FrameInterpolateBase::Render(rFrameInterpolate, iCommandBuffer);

	// Player
	PlayerInterpolate::Render(rFrameInterpolate, iCommandBuffer);

	// Collections
	engine::ForEachInterpolateRender(GameInterpolateTypes {}, rFrameInterpolate, iCommandBuffer);
}

} // namespace game
