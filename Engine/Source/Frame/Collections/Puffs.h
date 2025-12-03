#pragma once

#include "Frame/Collections/Collection.h"
#include "Shaders/ShaderLayouts.h"

namespace engine
{

struct PuffsInterpolate : public Collection<PuffsInterpolate>,
                          public Renderable<PuffsInterpolate, "Puffs", {RenderableFlags::kSmokeAxisAligned}>
{

	// Types (configuration shared across puffs)
	struct Type
	{
		common::crc_t crc = 0;
		uint32_t uiColor = 0xFFFFFFFF;
	};

	static inline std::vector<Type> sTypes;

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

	// Controller type registry
	static inline std::vector<PuffControllerType> sControllerTypes;
	static uint8_t RegisterControllerType(const PuffControllerType& rType); // DT: TEMP Why does Puffs have these but not point lights?
	static const PuffControllerType& GetControllerType(uint8_t uiIndex);
	static PuffKeyframe InterpolatePuffKeyframes(const PuffControllerType& rController, float fElapsedTime);

	// Interpolate
	static void Update(PuffsInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

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
