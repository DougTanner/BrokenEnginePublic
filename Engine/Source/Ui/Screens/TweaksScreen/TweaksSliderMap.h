#pragma once

namespace engine
{

class TweaksSliderMap
{
public:
	static std::unordered_map<std::string_view, Wrapper*>& Get();
};

} // namespace engine
