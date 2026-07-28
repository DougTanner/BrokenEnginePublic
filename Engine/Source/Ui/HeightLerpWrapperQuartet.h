#pragma once

#include "WrapperBase.h"

namespace engine
{

// Four Wrappers (StartHeight, EndHeight, Low, High) grouped as a single eye-height-lerped scalar.
// Resolve() returns std::lerp(Low, High, t) with t clamped from the (StartHeight, EndHeight) band
// at the supplied eye height.
struct HeightLerpWrapperQuartet
{
	Wrapper StartHeight;
	Wrapper EndHeight;
	Wrapper Low;
	Wrapper High;

	float Resolve(float fEyeHeight) const;
};

constexpr float LerpAtHeight(float fEyeHeight, float fStartHeight, float fEndHeight, float fLow, float fHigh)
{
	const float fSpan = std::max(fEndHeight - fStartHeight, 0.001f);
	const float fT = std::clamp((fEyeHeight - fStartHeight) / fSpan, 0.0f, 1.0f);
	return std::lerp(fLow, fHigh, fT);
}

} // namespace engine
