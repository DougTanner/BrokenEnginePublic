#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

struct FrameStaticData;

struct PuffsType
{
	common::crc_t crc = 0;
	uint32_t uiColor = 0xFFFFFFFF;
};

// Puff-specific keyframe with semantically correct names
struct PuffKeyframe
{
	float fArea = 0.0f;       // Puff size/radius
	float fIntensity = 0.0f;  // Puff opacity/brightness
	float fRotation = 0.0f;   // Puff rotation

	static PuffKeyframe Lerp(const PuffKeyframe& rA, const PuffKeyframe& rB, float fPercent)
	{
		return
		{
			.fArea = std::lerp(rA.fArea, rB.fArea, fPercent),
			.fIntensity = std::lerp(rA.fIntensity, rB.fIntensity, fPercent),
			.fRotation = std::lerp(rA.fRotation, rB.fRotation, fPercent),
		};
	}

	bool operator==(const PuffKeyframe& rOther) const = default;
};

// Puff controller type
struct PuffControllerType
{
	uint8_t uiBaseTypeIndex = 0;
	uint8_t uiKeyframeCount = 2;
	bool bDestroysSelf = true;
	float pfTimes[kMaxControllerKeyframes] {};
	PuffKeyframe keyframes[kMaxControllerKeyframes] {};

	// Per-keyframe wrapper scaling: keyframe values are multiplied by wrapper.Get() at interpolation time
	Wrapper* ppAreaScales[kMaxControllerKeyframes] {};
	Wrapper* ppIntensityScales[kMaxControllerKeyframes] {};

	bool operator==(const PuffControllerType& rOther) const = default;
};

struct PuffsInterpolate : public Collection<PuffsInterpolate>,
                          public TypeRegistry<PuffsType>,
                          public ControllerTypeRegistry<PuffsInterpolate, PuffControllerType>
{
	static constexpr const char* kName = "Puffs";
	static constexpr common::crc_t kCrc = common::CrcConsteval("Puffs");

	// Allocate and copy
	static void AllocateAndCopy(PuffsInterpolate& rCurrent, const PuffsInterpolate& rPrevious);

	// Interpolate
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

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
	auto PersistentMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.puiTypeIndices, rSelf.puiControllerTypeIndices, rSelf.pfStartTimes);
	}

	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};

struct PuffsPostRender : public Collection<PuffsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(PuffsPostRender& rCurrent, const PuffsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);

	// Add controlled puff (fire-and-forget, auto-destroys when animation ends)
	static void XM_CALLCONV AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition);

	// Destroy handles auto-removal of expired controlled puffs
	static void Destroy(game::Frame& __restrict rFrame, const FrameStaticData& rStaticData);

	auto Members([[maybe_unused]] this auto&& rSelf) { return std::tie(); }

};

extern template struct Collection<PuffsInterpolate>;
extern template struct Collection<PuffsPostRender>;

} // namespace engine

#endif // BT_CLIENT
