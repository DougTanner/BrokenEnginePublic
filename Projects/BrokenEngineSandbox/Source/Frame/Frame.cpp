#include "Frame.h"

#include "Frame/Collision.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace game
{

using enum FrameFlags;

void FrameInterpolate::Allocate(FrameInterpolate& __restrict rCurrent, const FrameInterpolate& __restrict rPrevious)
{
	// Player doesn't need allocation (not a Collection)
	engine::Allocate(rCurrent.blasters, rPrevious.blasters, rCurrent.blasters.Members());
	engine::Allocate(rCurrent.missiles, rPrevious.missiles, rCurrent.missiles.Members());
	engine::Allocate(rCurrent.spaceships, rPrevious.spaceships, rCurrent.spaceships.Members());
	engine::Allocate(rCurrent.targets, rPrevious.targets, rCurrent.targets.Members());
}

void FramePostRender::Allocate(FramePostRender& __restrict rCurrent, const FramePostRender& __restrict rPrevious)
{
	// Player doesn't need allocation (not a Collection)
	engine::Allocate(rCurrent.blasters, rPrevious.blasters, rCurrent.blasters.Members());
	engine::Allocate(rCurrent.missiles, rPrevious.missiles, rCurrent.missiles.Members());
	engine::Allocate(rCurrent.spaceships, rPrevious.spaceships, rCurrent.spaceships.Members());
	engine::Allocate(rCurrent.targets, rPrevious.targets, rCurrent.targets.Members());
}

void FrameInterpolate::Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	const FrameInterpolate& rPrevious = rPreviousFrame.interpolate;

	// Parent
	FrameInterpolateBase::Update(rCurrent, rPreviousFrame, fDeltaTime);

	// Update sun angle with varying speeds
	if (!(rPreviousFrame.flags & kMainMenu))
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

	// Update spawn timer
	rCurrent.fSpawnTimer = rPrevious.fSpawnTimer + fDeltaTime;

	// Update
	PlayerInterpolate::Update(rCurrent.player, rPreviousFrame, fDeltaTime);
	BlastersInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	MissilesInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	SpaceshipsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
	TargetsInterpolate::Update(rCurrent, rPreviousFrame, fDeltaTime);
}

void FrameInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	engine::FrameInterpolateBase::Render(rFrameInterpolate, iCommandBuffer);

	// Children
	PlayerInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	MissilesInterpolate::Render(rFrameInterpolate, iCommandBuffer);
	SpaceshipsInterpolate::Render(rFrameInterpolate, iCommandBuffer);
}

void FramePostRender::Update(game::Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime, const game::FrameInput& __restrict rFrameInput)
{
	game::FramePostRender& rCurrent = rFrame.postRender;

	// Parent
	FramePostRenderBase::Update(rFrame, rPreviousFrame, fDeltaTime, rFrameInput);

	// Update
	PlayerPostRender::Update(rCurrent.player, rPreviousFrame, fDeltaTime, rFrameInput);
	BlastersPostRender::Update(rCurrent.blasters, rPreviousFrame, fDeltaTime);
	MissilesPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::Update(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::AvoidTerrain(rFrame, rPreviousFrame, fDeltaTime, 0, rFrame.interpolate.spaceships.iCount);
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

	// Avoid islands - retry with expanded radius
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
	PlayerPostRender::Spawn(rFrame, rPreviousFrame, fDeltaTime);

	FrameInterpolate& rInterpolate = rFrame.interpolate;

	if (rFrame.flags & FrameFlags::kMainMenu)
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
	// Bind all collection positions to collision system
	PlayerPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	BlastersPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
}

void FramePostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Process collision results for all collections
	PlayerPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);
	BlastersPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);
}

void FramePostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Process area damage for all collections that can receive it
	SpaceshipsPostRender::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);
}

void FramePostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Clean up destroyed objects in all collections
	PlayerPostRender::Destroy(rFrame, rPreviousFrame, fDeltaTime);
	BlastersPostRender::Destroy(rFrame, rPreviousFrame, fDeltaTime);
	MissilesPostRender::Destroy(rFrame, rPreviousFrame, fDeltaTime);
	SpaceshipsPostRender::Destroy(rFrame, rPreviousFrame, fDeltaTime);
}

// Calculate enemy spawn position adjusted for current terrain base height
FXMVECTOR XM_CALLCONV Frame::EnemySpawnPosition()
{
	auto vecSpawnPosition = kVecEnemySpawnPosition;
	return XMVectorSetZ(vecSpawnPosition, engine::gBaseHeight.Get());
}

// Register shared type configurations for game objects
void Frame::Register()
{
	FrameBase::Register();

	PlayerInterpolate::Register();
	MissilesInterpolate::Register();
}

// Allocate graphics pipelines for game objects
void Frame::AllocateGraphicsResources()
{
	FrameBase::AllocateGraphicsResources();

	PlayerInterpolate::AllocatePipelines();
	MissilesInterpolate::AllocatePipelines();
	SpaceshipsInterpolate::AllocatePipelines();
}

// Initialize frame with main menu flag
Frame::Frame()
{
	flags |= kMainMenu;

	interpolate.player.vecPosition = XMVECTOR {45.0f, -12.0f, 0.0f, 1.0f};
}

// Initialize frame with specified flags
Frame::Frame(FrameFlags_t initialFlags)
{
	flags = initialFlags;

	interpolate.player.vecPosition = XMVECTOR {45.0f, -12.0f, 0.0f, 1.0f};
}

void Frame::InterpolateAllocate(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	// Parent
	FrameBase::InterpolateAllocate(rCurrent, rPreviousFrame, fDeltaTime);

	// Children
	FrameInterpolate::Allocate(rCurrent.interpolate, rPreviousFrame.interpolate);
}

void Frame::InterpolateUpdate(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	SCOPED_CPU_PROFILE(engine::kCpuTimerFrameInterpolate);

	// Parent
	FrameBase::InterpolateUpdate(rCurrent, rPreviousFrame, fDeltaTime);

	// Load
	FrameFlags_t flags = rPreviousFrame.flags;

	// Save
	rCurrent.flags = flags;

	// Children
	FrameInterpolate::Update(rCurrent.interpolate, rPreviousFrame, fDeltaTime);
}

void Frame::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	FrameInterpolate::Render(rFrameInterpolate, iCommandBuffer);
}

void Frame::PostRenderAllocate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame)
{
	// Parent
	FrameBase::PostRenderAllocate(rFrame, rPreviousFrame);

	// Children
	FramePostRender::Allocate(rFrame.postRender, rPreviousFrame.postRender);
}

void Frame::PostRenderUpdate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime, const game::FrameInput& __restrict rFrameInput)
{
	SCOPED_CPU_PROFILE(engine::kCpuTimerFramePostRender);

	// Parent
	FrameBase::PostRenderUpdate(rFrame, rPreviousFrame, fDeltaTime, rFrameInput);

	// Children
	FramePostRender::Update(rFrame, rPreviousFrame, fDeltaTime, rFrameInput);
}

void Frame::PostRenderPreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FrameBase::PostRenderPreCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Children
	FramePostRender::PreCollision(rFrame, rPreviousFrame, fDeltaTime);
}

void Frame::PostRenderCollide()
{
	// Centralized collision detection
	FrameBase::PostRenderCollide();
}

void Frame::PostRenderPostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FrameBase::PostRenderPostCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Children
	FramePostRender::PostCollision(rFrame, rPreviousFrame, fDeltaTime);

	// Clear collision layers for next frame
	engine::Collision::Clear();
}

void Frame::PostRenderAreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FrameBase::PostRenderAreaDamage(rFrame, rPreviousFrame, fDeltaTime);

	// Children
	FramePostRender::AreaDamage(rFrame, rPreviousFrame, fDeltaTime);

	// Clear area damage sources for next frame
	engine::Collision::ClearAreaDamage();
}

void Frame::PostRenderSpawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FrameBase::PostRenderSpawn(rFrame, rPreviousFrame, fDeltaTime);

	// Children
	FramePostRender::Spawn(rFrame, rPreviousFrame, fDeltaTime);
}

void Frame::PostRenderDestroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Parent
	FrameBase::PostRenderDestroy(rFrame, rPreviousFrame, fDeltaTime);

	// Children
	FramePostRender::Destroy(rFrame, rPreviousFrame, fDeltaTime);
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
