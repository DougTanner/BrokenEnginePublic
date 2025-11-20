#pragma once

#include "Frame/Collections/Collections.h"

namespace engine
{

inline constexpr int64_t kiAreaLightsInterpolateVersion = 1;

struct AreaLightsInterpolate : public engine::Collection<kiAreaLightsInterpolateVersion, true>
{
	AreaLightsInterpolate() = default;
	virtual ~AreaLightsInterpolate() = default;
	static void Update(game::FrameInterpolate& __restrict rCurrentFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	void Render(int64_t iCommandBuffer) const;
	bool operator==(const AreaLightsInterpolate& rOther) const;

	#define AREA_LIGHTS_INTERPOLATE_LIST(a) a.pVecPositions
	XMVECTOR* __restrict pVecPositions = nullptr;
};

inline constexpr int64_t kiAreaLightsPostRenderVersion = 1;

struct AreaLightsPostRender : public engine::Collection<kiAreaLightsPostRenderVersion>
{
	AreaLightsPostRender() = default;
	virtual ~AreaLightsPostRender() = default;
	bool operator==(const AreaLightsPostRender& rOther) const;

	static void Update(game::FramePostRender& __restrict rCurrentFramePostRender, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static area_light_t Add(game::Frame& __restrict rFrame);
	static void Remove(game::Frame& __restrict rFrame, area_light_t uiId);

	#define AREA_LIGHTS_POST_RENDER_LIST(a) a.puiIds
	area_light_t* __restrict puiIds = nullptr;
};

} // namespace engine
