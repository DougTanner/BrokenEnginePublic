#pragma once

#include "Frame/Pools/ObjectControllerPool.h"
#include "Frame/Pools/ObjectPool.h"

namespace game
{

struct Frame;

}

namespace engine
{

struct AreaLightInfo
{
	bool bAlwaysVisible = false; // DT: TODO Flags?

	common::crc_t crc = 0;
	uint32_t puiColors[4] {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
	XMFLOAT2 pf2Texcoords[4] {};

	XMVECTOR pVecVisiblePositions[4] {}; // DT: TODO Can this be common:AreaVertices?
	float fVisibleIntensity = 0.0f;

	XMVECTOR pVecLightingPositions[4] {};
	float fLightingIntensity = 0.0f;

	bool operator==(const AreaLightInfo& rOther) const = default;
};
struct AreaLight
{
	bool operator==(const AreaLight& rOther) const = default;
};
using AreaLights = ObjectPool<AreaLightInfo, AreaLight, uint16_t, kuiMaxAreaLights>;
static_assert(std::is_trivially_copyable_v<AreaLights>);

struct PointLightInfo
{
	XMVECTOR vecPosition {};
	uint32_t uiColor = 0xFFFFFFFF;

	float fVisibleArea = 0.0f;
	float fVisibleIntensity = 0.0f;

	float fLightingArea = 0.0f;
	float fLightingIntensity = 0.0f;

	common::crc_t crc = 0;
	float fRotation = 0.0f;

	static PointLightInfo Lerp(const PointLightInfo& rOne, const PointLightInfo& rTwo, float fPercent)
	{
		return
		{
			.vecPosition = XMVectorLerp(rOne.vecPosition, rTwo.vecPosition, fPercent),
			.uiColor = common::ColorLerp(rOne.uiColor, rTwo.uiColor, fPercent),
			.fVisibleArea = (1.0f - fPercent) * rOne.fVisibleArea + fPercent * rTwo.fVisibleArea,
			.fVisibleIntensity = (1.0f - fPercent) * rOne.fVisibleIntensity + fPercent * rTwo.fVisibleIntensity,
			.fLightingArea = (1.0f - fPercent) * rOne.fLightingArea + fPercent * rTwo.fLightingArea,
			.fLightingIntensity = (1.0f - fPercent) * rOne.fLightingIntensity + fPercent * rTwo.fLightingIntensity,
			.crc = rOne.crc,
			.fRotation = (1.0f - fPercent)* rOne.fRotation + fPercent * rTwo.fRotation,
		};
	}

	bool operator==(const PointLightInfo& rOther) const = default;
};
struct PointLight
{
	bool operator==(const PointLight& rOther) const = default;
};
using PointLights = ObjectPool<PointLightInfo, PointLight, point_light_t, kuiMaxPointLights>;
static_assert(std::is_trivially_copyable_v<PointLights>);

template<point_light_t CONTROLLER_LERP_SIZE, point_light_controller_t CONTROLLER_SIZE>
using PointLightControllers = ObjectControllerPool<PointLightInfo, PointLight, point_light_t, kuiMaxPointLights, CONTROLLER_LERP_SIZE, point_light_controller_t, CONTROLLER_SIZE>;

XMVECTOR XM_CALLCONV DirectionToDirectionMultipliers(FXMVECTOR vecDirection);
void RenderLightingGlobal(int64_t iCommandBuffer);
void RenderLightingMain(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);

inline constexpr int64_t kiLightingVersion = 3 + sizeof(AreaLights) + sizeof(PointLights) + shaders::kiLightingCookieCount + shaders::kiLightingTextures;

}
