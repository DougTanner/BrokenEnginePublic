#pragma once

#include "Frame/Collections/Billboards.h"
#include "Frame/Collections/Collection.h"

namespace game
{

struct Frame;
struct FrameInterpolate;

enum class TargetFlags : uint8_t
{
	kDestination    = 0x01,
	kSubscriber     = 0x02,

	// DT: GAMELOGIC
	kTargetIsPlayer = 0x04,
	kTargetIsEnemy  = 0x08,
};
using TargetFlags_t = common::Flags<TargetFlags>;

struct TargetsInterpolate : public engine::Collection<TargetsInterpolate, engine::CollectionFlags::kIdToIndex>
{
	// Type configuration for billboard rendering
	struct Type
	{
		common::crc_t crc = 0;
		float fSize = 0.055f;
		float fAlpha = 2.0f;
		uint8_t uiBillboardTypeIndex = 0; // Set by RegisterType()

		bool operator==(const Type& rOther) const = default;
	};

	static inline std::vector<Type> sTypes;

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	XMVECTOR* __restrict pVecPositions = nullptr;
	uint8_t* __restrict puiTypeIndices = nullptr;
	engine::billboard_t* __restrict puiBillboards = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.puiTypeIndices, rSelf.puiBillboards); }

	// Utility
	bool operator==(const TargetsInterpolate& rOther) const;
};
using target_t = TargetsInterpolate::id_t;

struct TargetsPostRender : public engine::Collection<TargetsPostRender>
{
	// Update
	static void Update(TargetsPostRender& __restrict rCurrent, const TargetsPostRender& __restrict rPrevious);

	// Add/Remove API
	static void Add(Frame& __restrict rFrame, target_t& rId);
	static void Remove(Frame& __restrict rFrame, target_t& rId, TargetFlags_t flags);
	static void AddSubscriber(Frame& __restrict rFrame, target_t id);

	// Type registration
	static uint8_t RegisterType(const TargetsInterpolate::Type& type);
	static const TargetsInterpolate::Type& GetType(uint8_t uiTypeIndex);

	target_t* __restrict puiIds = nullptr;
	TargetFlags_t* __restrict pFlags = nullptr;
	uint8_t* __restrict puiSubscribers = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds, rSelf.pFlags, rSelf.puiSubscribers); }

	// Utility
	bool operator==(const TargetsPostRender& rOther) const;
};

} // namespace game
