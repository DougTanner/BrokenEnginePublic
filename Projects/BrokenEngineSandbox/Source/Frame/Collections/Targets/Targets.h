#pragma once

#include "Frame/Alignments.h"
#include "Frame/Collections/Collection.h"

namespace engine { struct FrameStaticData; }
#include "Frame/GridCoord.h"

namespace game
{

struct Frame;
struct FrameInterpolate;

enum class TargetFlags : uint8_t
{
	kDestination    = 0x01,
	kSubscriber     = 0x02,
};
using TargetFlags_t = common::Flags<TargetFlags>;

struct TargetsInterpolate : public engine::Collection<TargetsInterpolate, engine::CollectionFlags::kIdToIndex>
{
	static constexpr int64_t kiVersion = 2;

	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources() {}

	// Allocate and copy
	static void AllocateAndCopy(TargetsInterpolate& rCurrent, const TargetsInterpolate& rPrevious);

	// SyncData for parent-provided values
	struct SyncData
	{
		XMVECTOR vecPosition;
	};

	// Sync owned target with parent-provided data
	static void Sync(FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

	// Render
	static void BeginRender(int64_t, const std::unordered_map<engine::GridCoord, FrameInterpolate>&, const std::vector<engine::GridCoord>&) {}
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t) {}

	XMVECTOR* __restrict pVecPositions = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }

	// Utility
	bool LogDifferences(const TargetsInterpolate& rOther) const;
};

struct TargetsPostRender : public engine::Collection<TargetsPostRender>
{
	static constexpr int64_t kiVersion = 1;

	// Allocate and copy
	static void AllocateAndCopy(TargetsPostRender& rCurrent, const TargetsPostRender& rPrevious);

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void Transfer(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Destroy(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Spawn(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);

	// Add/Remove API
	static void Add(Frame& __restrict rFrame, target_t& rId, engine::alignment_t alignment);
	static void Remove(Frame& __restrict rFrame, target_t& rId, TargetFlags_t flags);
	static void AddSubscriber(Frame& __restrict rFrame, target_t id);

	target_t* __restrict puiIds = nullptr;
	TargetFlags_t* __restrict pFlags = nullptr;
	uint8_t* __restrict puiSubscribers = nullptr;
	engine::alignment_t* __restrict pAlignments = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds, rSelf.pFlags, rSelf.puiSubscribers, rSelf.pAlignments); }

	// Utility
	bool LogDifferences(const TargetsPostRender& rOther) const;
};

} // namespace game

namespace engine
{
extern template struct Collection<game::TargetsInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<game::TargetsPostRender>;
}
