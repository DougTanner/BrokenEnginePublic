#pragma once

#include "Frame/Collections/Collections.h"

namespace engine
{

struct AreaLightType
{
	common::crc_t crc = 0;
	uint32_t puiColors[4] {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
	XMFLOAT2 pf2Texcoords[4] {};
	float fLightingSize = 0.5f;
	float fVisibleIntensity = 1.0f;
	float fLightingIntensity = 1.0f;

	bool operator==(const AreaLightType& rOther) const = default;

	static inline std::vector<AreaLightType> sTypes;

	static uint8_t RegisterType(const AreaLightType& rType);
	static const AreaLightType& GetType(uint8_t uiTypeIndex);
};

struct AreaLightsInterpolate : public Collection<AreaLightsInterpolate, true>, public Version<5>
{
	// Interpolate
	static void Update(game::FrameInterpolate& __restrict rCurrentFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	#define AREA_LIGHTS_INTERPOLATE_LIST(a) a.puiTypeIndices, a.pVecVisiblePositions, a.pVecDirectionMultipliers
	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecVisiblePositions[4] = {nullptr, nullptr, nullptr, nullptr};
	XMVECTOR* __restrict pVecDirectionMultipliers = nullptr;

	// Render
	static void CreatePipelines();
	static void Render(const game::Frame& __restrict rFrame, int64_t iCommandBuffer);

	static inline int64_t siBufferIndex = -1;
	static inline size_t sPipelineIndex = SIZE_MAX;

	// Utility
	bool operator==(const AreaLightsInterpolate& rOther) const;
};
using area_lights_t = AreaLightsInterpolate::id_t;

struct AreaLightsPostRender : public Collection<AreaLightsPostRender>, public Version<1>
{
	// Update
	static void Update(game::FramePostRender& __restrict rCurrentFramePostRender, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static area_lights_t Add(game::Frame& __restrict rFrame, uint8_t uiTypeIndex);
	static void Remove(game::Frame& __restrict rFrame, area_lights_t id);

	#define AREA_LIGHTS_POST_RENDER_LIST(a) a.puiIds
	area_lights_t* __restrict puiIds = nullptr;

	// Utility
	bool operator==(const AreaLightsPostRender& rOther) const;
};

} // namespace engine
