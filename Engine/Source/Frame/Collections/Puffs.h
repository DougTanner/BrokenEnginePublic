#pragma once

#include "Frame/Collections/Collection.h"
#include "Shaders/ShaderLayouts.h"

namespace engine
{

struct PuffsInterpolate : public Collection<PuffsInterpolate>,
                          public Renderable<PuffsInterpolate, "Puffs", {RenderableFlags::kSmokeAxisAligned}>,
                          public ControllerTypeRegistry<PuffsInterpolate>
{

	// Types (configuration shared across puffs)
	struct Type
	{
		common::crc_t crc = 0;
		uint32_t uiColor = 0xFFFFFFFF;
	};

	static inline std::vector<Type> sTypes;

	// Update
	static void Update(PuffsInterpolate& __restrict rCurrent, const PuffsInterpolate& __restrict rPrevious, float fCurrentTime);
	static void Sync(PuffsInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Member arrays (SOA)
	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;

	// Per-instance animatable properties
	float* __restrict pfIntensities = nullptr;
	float* __restrict pfAreas = nullptr;
	float* __restrict pfRotations = nullptr;

	// Controller fields (kuiInvalidControllerType = not controlled)
	uint8_t* __restrict puiControllerTypeIndices = nullptr;
	float* __restrict pfStartTimes = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiTypeIndices, rSelf.pVecPositions, rSelf.pfIntensities, rSelf.pfAreas, rSelf.pfRotations, rSelf.puiControllerTypeIndices, rSelf.pfStartTimes);
	}

	// Utility
	bool operator==(const PuffsInterpolate& rOther) const;
};

struct PuffsPostRender : public Collection<PuffsPostRender>
{
	// Update
	static void Update(PuffsPostRender& __restrict rCurrent, const PuffsPostRender& __restrict rPrevious);
	static uint8_t RegisterType(const PuffsInterpolate::Type& rType);
	static const PuffsInterpolate::Type& GetType(uint8_t uiIndex);

	// Add controlled puff (fire-and-forget, auto-destroys when animation ends)
	static void XM_CALLCONV AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition);

	// Destroy handles auto-removal of expired controlled puffs
	static void Destroy(game::Frame& __restrict rFrame, float fCurrentTime);

	auto Members([[maybe_unused]] this auto&& rSelf) { return std::tie(); }

	// Utility
	bool operator==(const PuffsPostRender& rOther) const;
};

static_assert(sizeof(shaders::AxisAlignedQuadLayout) == kAxisAlignedQuadLayoutSize);

} // namespace engine
