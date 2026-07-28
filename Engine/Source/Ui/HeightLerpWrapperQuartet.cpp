#include "HeightLerpWrapperQuartet.h"

namespace engine
{

float HeightLerpWrapperQuartet::Resolve(float fEyeHeight) const
{
	return LerpAtHeight(fEyeHeight, StartHeight.Get(), EndHeight.Get(), Low.Get(), High.Get());
}

} // namespace engine
