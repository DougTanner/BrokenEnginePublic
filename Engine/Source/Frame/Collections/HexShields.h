#pragma once

#include "Frame/Collections/Collection.h"

namespace engine
{

struct HexShieldsType
{
	uint32_t uiColor = 0xFFFFFFFF;
	uint32_t uiLightingColor = 0xFFFFFFFF;
	float fMinimumIntensity = 0.0f;
};

struct HexShieldsInterpolate : public Collection<HexShieldsInterpolate, CollectionFlags::kIdToIndex>,
                               public Renderable<HexShieldsInterpolate, "HexShields", {RenderableFlags::kHexShields, RenderableFlags::kHexShieldsLighting}>,
                               public TypeRegistry<HexShieldsType>
{
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// Allocate and copy
	static void AllocateAndCopy(HexShieldsInterpolate& rCurrent, const HexShieldsInterpolate& rPrevious);

	// Update
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// Sync data (owner-provided values written every frame)
	struct SyncData
	{
		XMVECTOR vecPosition;
		XMFLOAT4 pf4Transforms[3];
		XMFLOAT4 pf4TransformNormals[3];
		XMFLOAT4 pf4Directions[shaders::kiHexShieldDirections];
		float pfVertIntensities[shaders::kiHexShieldDirections];
		float pfFragIntensities[shaders::kiHexShieldDirections];
		float fLightingIntensity;
		float fSize;
		float fColorMix;
	};

	// Sync
	static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// SOA arrays (decomposed from HexShieldLayout)
	XMVECTOR* __restrict pVecPositions = nullptr;
	XMFLOAT4* __restrict pf4Transforms[3] = {nullptr, nullptr, nullptr};
	XMFLOAT4* __restrict pf4TransformNormals[3] = {nullptr, nullptr, nullptr};
	uint8_t* __restrict puiTypeIndices = nullptr;
	XMFLOAT4* __restrict pf4Directions[shaders::kiHexShieldDirections] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	float* __restrict pfVertIntensities[shaders::kiHexShieldDirections] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	float* __restrict pfFragIntensities[shaders::kiHexShieldDirections] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	float* __restrict pfLightingIntensities = nullptr;
	float* __restrict pfSizes = nullptr;
	float* __restrict pfColorMixes = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pf4Transforms, rSelf.pf4TransformNormals,
		                rSelf.puiTypeIndices,
		                rSelf.pf4Directions, rSelf.pfVertIntensities, rSelf.pfFragIntensities,
		                rSelf.pfLightingIntensities, rSelf.pfSizes, rSelf.pfColorMixes);
	}

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const HexShieldsInterpolate& rOther) const;
};
using hex_shields_t = HexShieldsInterpolate::id_t;

struct HexShieldsPostRender : public Collection<HexShieldsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(HexShieldsPostRender& rCurrent, const HexShieldsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);

	// Add/Remove
	static void Add(game::Frame& __restrict rFrame, hex_shields_t& rId, uint8_t uiTypeIndex);
	static void Remove(game::Frame& __restrict rFrame, hex_shields_t& rId);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	hex_shields_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const HexShieldsPostRender& rOther) const;
};

static_assert(sizeof(shaders::HexShieldLayout) == kHexShieldLayoutSize);

} // namespace engine
