#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

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

	bool operator==(const PuffControllerType& rOther) const = default;
};

// Interpolates between puff keyframes based on elapsed time
inline PuffKeyframe InterpolatePuffKeyframes(const PuffControllerType& rController, float fElapsedTime)
{
	int64_t iKeyframeCount = rController.uiKeyframeCount;

	if (fElapsedTime <= rController.pfTimes[0])
	{
		return rController.keyframes[0];
	}
	if (fElapsedTime >= rController.pfTimes[iKeyframeCount - 1])
	{
		return rController.keyframes[iKeyframeCount - 1];
	}

	for (int64_t j = 1; j < iKeyframeCount; ++j)
	{
		if (fElapsedTime < rController.pfTimes[j])
		{
			float fPreviousTime = rController.pfTimes[j - 1];
			float fPercent = (fElapsedTime - fPreviousTime) / (rController.pfTimes[j] - fPreviousTime);
			return PuffKeyframe::Lerp(rController.keyframes[j - 1], rController.keyframes[j], fPercent);
		}
	}

	return rController.keyframes[iKeyframeCount - 1];
}

struct PuffsInterpolate : public Collection<PuffsInterpolate>,
                          public TypeRegistry<PuffsType>,
                          public ControllerTypeRegistry<PuffsInterpolate, PuffControllerType>
{
	static constexpr const char* kName = "Puffs";
	static constexpr common::crc_t kCrc = common::CrcConsteval("Puffs");

	// Register
	static void Register();

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

	// Utility
	bool operator==(const PuffsInterpolate& rOther) const;

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
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);

	// Add controlled puff (fire-and-forget, auto-destroys when animation ends)
	static void XM_CALLCONV AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition);

	// Destroy handles auto-removal of expired controlled puffs
	static void Destroy(game::Frame& __restrict rFrame);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Transfer(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	auto Members([[maybe_unused]] this auto&& rSelf) { return std::tie(); }

	// Utility
	bool operator==(const PuffsPostRender& rOther) const;
};

extern template struct Collection<PuffsInterpolate>;
extern template struct Collection<PuffsPostRender>;

} // namespace engine

#endif // BT_CLIENT
