#pragma once

#include "Frame/Collections/Collections.h"
#include "Frame/Collections/PointLights.h"

namespace engine
{

inline constexpr int64_t kMaxControllerKeyframes = 4;

// Animatable properties for one keyframe
struct ControllerKeyframe
{
	float fVisibleArea = 0.0f;
	float fVisibleIntensity = 0.0f;
	float fLightingArea = 0.0f;
	float fLightingIntensity = 0.0f;
	float fRotation = 0.0f;

	static ControllerKeyframe Lerp(const ControllerKeyframe& rA, const ControllerKeyframe& rB, float fPercent);
	bool operator==(const ControllerKeyframe& rOther) const = default;
};

// Controller type with variable keyframe count (up to kMaxControllerKeyframes)
struct ControllerType
{
	uint8_t uiTypeIndex = 0;                                    // Base Type for color/texture (from PointLightsInterpolate::sTypes)
	uint8_t uiKeyframeCount = 2;                                // Actual keyframes used (2-4)
	bool bDestroysSelf = true;                                  // Auto-remove when animation ends
	float pfTimes[kMaxControllerKeyframes] {};                  // Keyframe times (relative to start)
	ControllerKeyframe keyframes[kMaxControllerKeyframes] {};   // Keyframe states

	bool operator==(const ControllerType& rOther) const = default;
};

struct ControlledPointLightsInterpolate : public Collection<ControlledPointLightsInterpolate>
{
	static constexpr int64_t kiVersion = 2;
	static constexpr char kpcName[] = "ControlledPointLights";

	// Static controller type registry
	static inline std::vector<ControllerType> sControllerTypes;

	// Interpolate - writes interpolated values to PointLights
	static void Update(ControlledPointLightsInterpolate& __restrict rCurrent, const ControlledPointLightsInterpolate& __restrict rPrevious, PointLightsInterpolate& __restrict rPointLights, float fCurrentTime);
	static void Sync(ControlledPointLightsInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Per-instance controller metadata
	uint8_t* __restrict puiControllerTypeIndices = nullptr;
	float* __restrict pfStartTimes = nullptr;
	float* __restrict pfBaseRotations = nullptr;

	// Reference to actual PointLight (instead of storing position ourselves)
	point_lights_t* __restrict pPointLightIds = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(
			rSelf.puiControllerTypeIndices,
			rSelf.pfStartTimes,
			rSelf.pfBaseRotations,
			rSelf.pPointLightIds
		);
	}

	// No Render() - PointLights handles rendering

	// Utility
	bool operator==(const ControlledPointLightsInterpolate& rOther) const;
};

struct ControlledPointLightsPostRender : public Collection<ControlledPointLightsPostRender>
{
	static constexpr int64_t kiVersion = 1;

	// Controller type registration
	static uint8_t RegisterControllerType(const ControllerType& rType);
	static const ControllerType& GetControllerType(uint8_t uiIndex);

	// Update
	static void Update(ControlledPointLightsPostRender& __restrict rCurrent, const ControlledPointLightsPostRender& __restrict rPrevious);

	// Add controlled point light
	static void XM_CALLCONV Add(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fRotation);

	// Destroy - handles bDestroysSelf auto-removal
	static void Destroy(game::Frame& __restrict rFrame, float fCurrentTime);

	// Per-instance flags (reserved for future use, e.g., manual destroy request)
	uint8_t* __restrict puiFlags = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiFlags);
	}

	// Utility
	bool operator==(const ControlledPointLightsPostRender& rOther) const;
};

} // namespace engine
