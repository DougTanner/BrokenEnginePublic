#pragma once

namespace engine
{

#if defined(BT_CLIENT)
class Wrapper;
#endif

// ============================================================================
// CONTROLLER TYPES FOR KEYFRAME ANIMATION
// ============================================================================
// Reusable keyframe animation system for collections that need time-based property interpolation.

inline constexpr int64_t kMaxControllerKeyframes = 4;
inline constexpr uint8_t kuiInvalidControllerType = 0xFF;

// Keyframe state with lerp-able properties for light animation
struct ControllerKeyframe
{
	float fVisibleArea = 0.0f;
	float fVisibleIntensity = 0.0f;
	float fLightingArea = 0.0f;
	float fLightingIntensity = 0.0f;
	float fRotation = 0.0f;

	static ControllerKeyframe Lerp(const ControllerKeyframe& rA, const ControllerKeyframe& rB, float fPercent)
	{
		return
		{
			.fVisibleArea = std::lerp(rA.fVisibleArea, rB.fVisibleArea, fPercent),
			.fVisibleIntensity = std::lerp(rA.fVisibleIntensity, rB.fVisibleIntensity, fPercent),
			.fLightingArea = std::lerp(rA.fLightingArea, rB.fLightingArea, fPercent),
			.fLightingIntensity = std::lerp(rA.fLightingIntensity, rB.fLightingIntensity, fPercent),
			.fRotation = std::lerp(rA.fRotation, rB.fRotation, fPercent),
		};
	}

	bool operator==(const ControllerKeyframe& rOther) const = default;
};

// Controller type defining animation behavior
struct ControllerType
{
	uint8_t uiBaseTypeIndex = 0;                                // Base Type for color/texture
	uint8_t uiKeyframeCount = 2;                                // Actual keyframes used (2-4)
	bool bDestroysSelf = true;                                  // Auto-remove when animation ends
	float pfTimes[kMaxControllerKeyframes] {};                  // Keyframe times (relative to start)
	ControllerKeyframe keyframes[kMaxControllerKeyframes] {};   // Keyframe states (normalized when wrappers present)

#if defined(BT_CLIENT)
	// Per-keyframe wrapper scaling: keyframe values are multiplied by wrapper.Get() at interpolation time
	Wrapper* ppVisibleAreaScales[kMaxControllerKeyframes] {};
	Wrapper* ppVisibleIntensityScales[kMaxControllerKeyframes] {};
	Wrapper* ppLightingAreaScales[kMaxControllerKeyframes] {};
	Wrapper* ppLightingIntensityScales[kMaxControllerKeyframes] {};
#endif

	bool operator==(const ControllerType& rOther) const = default;
};

// Interpolates between keyframes based on elapsed time
template <typename TControllerType>
inline auto InterpolateKeyframes(const TControllerType& rController, float fElapsedTime)
	-> std::remove_extent_t<decltype(TControllerType::keyframes)>
{
	using KeyframeType = std::remove_extent_t<decltype(TControllerType::keyframes)>;
	int64_t iKeyframeCount = rController.uiKeyframeCount;

	if (fElapsedTime <= rController.pfTimes[0])
	{
		return rController.keyframes[0];
	}
	// NOLINTNEXTLINE(clang-analyzer-security.ArrayBound) — registered controllers always have uiKeyframeCount >= 2; the analyzer's count==0 path cannot occur
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
			return KeyframeType::Lerp(rController.keyframes[j - 1], rController.keyframes[j], fPercent);
		}
	}

	return rController.keyframes[iKeyframeCount - 1];
}

// Mixin providing static controller type registry for collections with keyframe animation.
// TControllerType defaults to ControllerType for standard keyframe animation (PointLights).
// Collections with custom keyframes (Puffs) can specify their own controller type.
// Threading contract: registration is startup-only (single-threaded, before Dispatch() workers fan out);
// sControllerTypes is immutable afterward, so parallel frame-tick .at() reads need no synchronization.
template <typename TCollection, typename TControllerType = ControllerType>
struct ControllerTypeRegistry
{
	static inline std::vector<TControllerType> sControllerTypes;

	static void RegisterControllerType(uint8_t& ruiIndex, const TControllerType& rType)
	{
		ASSERT(ruiIndex == 0xFF);
		ASSERT(sControllerTypes.size() < kuiInvalidControllerType);
		ruiIndex = static_cast<uint8_t>(sControllerTypes.size());
		sControllerTypes.push_back(rType);
	}

	static const TControllerType& GetControllerType(uint8_t uiIndex)
	{
		return sControllerTypes.at(uiIndex);
	}
};

// Removes controlled elements whose keyframe animation has expired.
// removeFn signature: void(TInterpolate&, TPostRender&, int64_t& i)
template <typename TInterpolate, typename TPostRender, typename TRemoveFn>
void DestroyExpiredControlled(TInterpolate& rInterpolate, TPostRender& rPostRender, float fCurrentTime, TRemoveFn removeFn)
{
	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiControllerTypeIndex = rInterpolate.puiControllerTypeIndices[i];
		if (uiControllerTypeIndex == kuiInvalidControllerType)
			continue;

		const auto& rController = TInterpolate::GetControllerType(uiControllerTypeIndex);
		if (!rController.bDestroysSelf)
			continue;

		float fElapsedTime = fCurrentTime - rInterpolate.pfStartTimes[i];
		if (fElapsedTime > rController.pfTimes[rController.uiKeyframeCount - 1]) [[unlikely]]
		{
			removeFn(rInterpolate, rPostRender, i);
		}
	}
}

} // namespace engine
