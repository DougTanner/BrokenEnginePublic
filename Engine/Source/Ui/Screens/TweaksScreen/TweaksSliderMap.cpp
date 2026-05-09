#include "TweaksSliderMap.h"

#if defined(BT_CLIENT)

namespace engine
{

std::unordered_map<std::string_view, Wrapper*>& TweaksSliderMap::Get()
{
	// Heap: static unordered_map built once on first call, lives forever. Can't use workbuffer (data lost on Pop)
	// and can't pre-allocate (STL map manages its own hash buckets internally)
	ScopedSuppressAllocationTracking suppress;

	static std::unordered_map<std::string_view, Wrapper*> sSliderMap = {};
	return sSliderMap;
}

TweaksSliderMapRegistrar::TweaksSliderMapRegistrar(std::initializer_list<std::pair<const std::string_view, Wrapper*>> entries)
{
	// Heap: STL hash buckets allocate. Static-init runs before main-loop tracking begins; suppression mirrors TweaksSliderMap::Get() and stays explicit.
	ScopedSuppressAllocationTracking suppress;
	TweaksSliderMap::Get().insert(entries);
}

} // namespace engine

#endif // defined(BT_CLIENT)
