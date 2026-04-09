#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class TweaksSliderMap
{
public:
	static std::unordered_map<std::string_view, Wrapper*>& Get();
};

} // namespace engine

#endif // BT_CLIENT
