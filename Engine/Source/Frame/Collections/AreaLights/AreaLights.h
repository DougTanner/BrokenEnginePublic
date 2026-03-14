#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace engine
{

struct AreaLightsType
{
	common::crc_t crc = 0;
	uint32_t puiColors[4] {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
	XMFLOAT2 pf2Texcoords[4] {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}};
	float fVisibleIntensity = 1.0f;
	float fLightingSize = 1.0f;
	float fLightingIntensity = 1.0f;
};

struct AreaLightsInterpolate : public Collection<AreaLightsInterpolate, CollectionFlags::kIdToIndex>,
                               public TypeRegistry<AreaLightsType>
{
	static constexpr const char* kName = "AreaLights";
	static constexpr common::crc_t kCrc = common::CrcConsteval("AreaLights");

	// Register
	static void Register();

	// Allocate and copy
	static void AllocateAndCopy(AreaLightsInterpolate& rCurrent, const AreaLightsInterpolate& rPrevious);

	// SyncData for parent-provided values
	struct SyncData
	{
		uint8_t uiTypeIndex;
		XMVECTOR vecVisiblePositions[4];
		float fIntensityMultiplier = 1.0f;
	};

	// Sync owned area light with parent-provided data
	static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// Interpolate
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecVisiblePositions[4] = {nullptr, nullptr, nullptr, nullptr};
	float* __restrict pfIntensityMultipliers = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiTypeIndices, rSelf.pVecVisiblePositions, rSelf.pfIntensityMultipliers); }

	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};
using area_lights_t = AreaLightsInterpolate::id_t;

struct AreaLightsPostRender : public Collection<AreaLightsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(AreaLightsPostRender& rCurrent, const AreaLightsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);

	static void Add(game::Frame& __restrict rFrame, area_lights_t& rId, uint8_t uiTypeIndex);
	static void Remove(game::Frame& __restrict rFrame, area_lights_t& rId);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Transfer(game::Frame& __restrict rFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	area_lights_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

};

extern template struct Collection<AreaLightsInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<AreaLightsPostRender>;

} // namespace engine

#endif // BT_CLIENT
