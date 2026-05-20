#include "HeightLerpWrapperQuartet.h"

namespace engine
{

float LerpAtHeight(float fEyeHeight, float fStartHeight, float fEndHeight, float fLow, float fHigh)
{
	const float fSpan = std::max(fEndHeight - fStartHeight, 0.001f);
	const float fT = std::clamp((fEyeHeight - fStartHeight) / fSpan, 0.0f, 1.0f);
	return std::lerp(fLow, fHigh, fT);
}

float HeightLerpWrapperQuartet::Resolve(float fEyeHeight) const
{
	return LerpAtHeight(fEyeHeight, StartHeight.Get(), EndHeight.Get(), Low.Get(), High.Get());
}

} // namespace engine
