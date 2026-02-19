#pragma once

#include "Frame/Alignments.h"
#include "Frame/Collections/Collection.h"

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

// Type configuration for targets
struct TargetsType
{
	common::crc_t crc = 0;
	float fSize = 0.055f;
	float fAlpha = 2.0f;

	bool operator==(const TargetsType& rOther) const = default;
};

struct TargetsInterpolate : public engine::Collection<TargetsInterpolate, engine::CollectionFlags::kIdToIndex>,
                            public engine::TypeRegistry<TargetsType>
{
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
		uint8_t uiTypeIndex;
	};

	// Sync owned target with parent-provided data (also syncs owned billboard)
	static void Sync(FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

	// Render
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	XMVECTOR* __restrict pVecPositions = nullptr;
	uint8_t* __restrict puiTypeIndices = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.puiTypeIndices); }

	// Utility
	bool operator==(const TargetsInterpolate& rOther) const;
};
using target_t = TargetsInterpolate::id_t;

struct TargetsPostRender : public engine::Collection<TargetsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(TargetsPostRender& rCurrent, const TargetsPostRender& rPrevious);

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);

	// Add/Remove API
	static void Add(Frame& __restrict rFrame, target_t& rId, uint8_t uiTargetTypeIndex, engine::alignment_t alignment);
	static void Remove(Frame& __restrict rFrame, target_t& rId, TargetFlags_t flags);
	static void AddSubscriber(Frame& __restrict rFrame, target_t id);

	// Type registration (custom - also registers billboard type)
	static void RegisterType(uint8_t& ruiIndex, const TargetsType& rType);

	target_t* __restrict puiIds = nullptr;
	TargetFlags_t* __restrict pFlags = nullptr;
	uint8_t* __restrict puiSubscribers = nullptr;
	engine::alignment_t* __restrict pAlignments = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds, rSelf.pFlags, rSelf.puiSubscribers, rSelf.pAlignments); }

	// Utility
	bool operator==(const TargetsPostRender& rOther) const;
};

} // namespace game
