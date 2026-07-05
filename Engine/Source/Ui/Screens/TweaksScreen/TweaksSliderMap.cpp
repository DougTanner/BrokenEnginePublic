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

	// Insert singly (not the void initializer_list overload, which silently drops keys already present) so a duplicate
	// key fails loud with the offending key. A dropped key otherwise succeeds silently and never crashes on its own,
	// and the first-open drift audit is structurally blind to it (key present + lookup succeeds), so this is the only
	// backstop. Static-init-safe (pre-main): common::Assert's LOG takes the gpThreadLocal==nullptr fallback buffer and
	// writes to constant-initialized ring buffers; DEBUG_BREAK breaks at the collision under a debugger, and the
	// post-break throw is the intended terminate() halt without one. Message is formatted (tracker not yet armed).
	for (const std::pair<const std::string_view, Wrapper*>& rEntry : entries)
	{
		bool bInserted = TweaksSliderMap::Get().insert(rEntry).second;
		if (!bInserted) [[unlikely]]
		{
			common::Assert(bInserted, std::format("Duplicate Tweaks slider key: \"{}\"", rEntry.first));
		}
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
