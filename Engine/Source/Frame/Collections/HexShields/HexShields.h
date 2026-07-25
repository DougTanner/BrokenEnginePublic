#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

struct FrameStaticData;

struct HexShieldsType
{
	uint32_t uiColor = 0xFFFFFFFF;
	uint32_t uiLightingColor = 0xFFFFFFFF;
	float fMinimumIntensity = 0.0f;
};

struct HexShieldsInterpolate : public Collection<HexShieldsInterpolate, CollectionFlags::kIdToIndex>,
                               public TypeRegistry<HexShieldsType>
{
	static constexpr const char* kName = "HexShields";
	static constexpr common::crc_t kCrc = common::CrcConsteval("HexShields");

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
	XMFLOAT4* pf4Transforms[3] = {nullptr, nullptr, nullptr};
	XMFLOAT4* pf4TransformNormals[3] = {nullptr, nullptr, nullptr};
	uint8_t* __restrict puiTypeIndices = nullptr;
	XMFLOAT4* pf4Directions[shaders::kiHexShieldDirections] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	float* pfVertIntensities[shaders::kiHexShieldDirections] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	float* pfFragIntensities[shaders::kiHexShieldDirections] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
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
	auto PersistentMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.puiTypeIndices, rSelf.pfLightingIntensities, rSelf.pfSizes, rSelf.pfColorMixes);
	}

	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};
using hex_shields_t = HexShieldsInterpolate::id_t;

struct HexShieldsPostRender : public Collection<HexShieldsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(HexShieldsPostRender& rCurrent, const HexShieldsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);

	// Add/Remove
	static void Add(game::Frame& __restrict rFrame, hex_shields_t& rId, uint8_t uiTypeIndex);
	static void Remove(game::Frame& __restrict rFrame, hex_shields_t& rId);

	hex_shields_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

};

extern template struct Collection<HexShieldsInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<HexShieldsPostRender>;

} // namespace engine

#endif // BT_CLIENT
