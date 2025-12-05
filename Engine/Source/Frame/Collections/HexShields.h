#pragma once

#include "Frame/Collections/Collection.h"
#include "Shaders/ShaderLayouts.h"

namespace engine
{

struct HexShieldsInterpolate : public Collection<HexShieldsInterpolate, CollectionFlags::kIdToIndex>,
                               public Renderable<HexShieldsInterpolate, "HexShields", {RenderableFlags::kHexShields, RenderableFlags::kHexShieldsLighting}>
{
	// Update
	static void Update(HexShieldsInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	// SOA arrays (decomposed from HexShieldLayout)
	XMVECTOR* __restrict pVecPositions = nullptr;
	XMFLOAT4* __restrict pf4Transforms[3] = {nullptr, nullptr, nullptr};
	XMFLOAT4* __restrict pf4TransformNormals[3] = {nullptr, nullptr, nullptr};
	uint32_t* __restrict puiColors = nullptr;
	uint32_t* __restrict puiLightingColors = nullptr;
	XMFLOAT4* __restrict pf4Directions[shaders::kiHexShieldDirections] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	float* __restrict pfVertIntensities[shaders::kiHexShieldDirections] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	float* __restrict pfFragIntensities[shaders::kiHexShieldDirections] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	float* __restrict pfLightingIntensities = nullptr;
	float* __restrict pfSizes = nullptr;
	float* __restrict pfColorMixes = nullptr;
	float* __restrict pfMinimumIntensities = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pf4Transforms, rSelf.pf4TransformNormals,
		                rSelf.puiColors, rSelf.puiLightingColors,
		                rSelf.pf4Directions, rSelf.pfVertIntensities, rSelf.pfFragIntensities,
		                rSelf.pfLightingIntensities, rSelf.pfSizes, rSelf.pfColorMixes, rSelf.pfMinimumIntensities);
	}

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const HexShieldsInterpolate& rOther) const;
};
using hex_shields_t = HexShieldsInterpolate::id_t;

struct HexShieldsPostRender : public Collection<HexShieldsPostRender>
{
	// Update
	static void Update(HexShieldsPostRender& __restrict rCurrent, const HexShieldsPostRender& __restrict rPrevious);

	// Add/Remove
	static void Add(game::Frame& __restrict rFrame, hex_shields_t& rId);
	static void Remove(game::Frame& __restrict rFrame, hex_shields_t& rId);

	hex_shields_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const HexShieldsPostRender& rOther) const;
};

static_assert(sizeof(shaders::HexShieldLayout) == kHexShieldLayoutSize);

} // namespace engine
