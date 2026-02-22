#pragma once

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

struct WindRadialKeyframe
{
	float fIntensity = 0.0f;
	float fSize = 0.0f;

	static WindRadialKeyframe Lerp(const WindRadialKeyframe& rA, const WindRadialKeyframe& rB, float fPercent)
	{
		return
		{
			.fIntensity = std::lerp(rA.fIntensity, rB.fIntensity, fPercent),
			.fSize = std::lerp(rA.fSize, rB.fSize, fPercent),
		};
	}

	bool operator==(const WindRadialKeyframe& rOther) const = default;
};

struct WindRadialControllerType
{
	uint8_t uiKeyframeCount = 2;
	bool bDestroysSelf = true;
	float pfTimes[kMaxControllerKeyframes] {};
	WindRadialKeyframe keyframes[kMaxControllerKeyframes] {};

	bool operator==(const WindRadialControllerType& rOther) const = default;
};

inline WindRadialKeyframe InterpolateWindRadialKeyframes(const WindRadialControllerType& rController, float fElapsedTime)
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
			return WindRadialKeyframe::Lerp(rController.keyframes[j - 1], rController.keyframes[j], fPercent);
		}
	}

	return rController.keyframes[iKeyframeCount - 1];
}

struct WindRadialsInterpolate : public Collection<WindRadialsInterpolate>,
                                public ControllerTypeRegistry<WindRadialsInterpolate, WindRadialControllerType>
{
	static constexpr const char* kName = "WindRadials";
	static constexpr common::crc_t kCrc = common::CrcConsteval("WindRadials");

	// Register
	static void Register();

	// Allocate and copy
	static void AllocateAndCopy(WindRadialsInterpolate& rCurrent, const WindRadialsInterpolate& rPrevious);

	// Update
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// Member arrays (SOA)
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;
	float* __restrict pfSizes = nullptr;

	// Controller fields
	uint8_t* __restrict puiControllerTypeIndices = nullptr;
	float* __restrict pfStartTimes = nullptr;
	float* __restrict pfBaseIntensities = nullptr;
	float* __restrict pfBaseSizes = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pfIntensities, rSelf.pfSizes,
		                rSelf.puiControllerTypeIndices, rSelf.pfStartTimes, rSelf.pfBaseIntensities, rSelf.pfBaseSizes);
	}

	// Utility
	bool operator==(const WindRadialsInterpolate& rOther) const;

	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};

struct WindRadialsPostRender : public Collection<WindRadialsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(WindRadialsPostRender& rCurrent, const WindRadialsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Transfer(game::Frame& __restrict rFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	// Add controlled wind radial (fire-and-forget, auto-destroys when animation ends)
	static void XM_CALLCONV AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fBaseIntensity, float fBaseSize);

	auto Members([[maybe_unused]] this auto&& rSelf) { return std::tie(); }

	// Utility
	bool operator==(const WindRadialsPostRender& rOther) const;
};

} // namespace engine
