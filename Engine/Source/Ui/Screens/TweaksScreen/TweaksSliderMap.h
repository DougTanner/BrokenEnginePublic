#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class TweaksSliderMap
{
public:
	static std::unordered_map<std::string_view, Wrapper*>& Get();
};

// RAII helper: each translation unit declares one of these at namespace scope to register its labels into TweaksSliderMap::Get() at static-init time, co-locating registration with use-site.
struct TweaksSliderMapRegistrar
{
	TweaksSliderMapRegistrar(std::initializer_list<std::pair<const std::string_view, Wrapper*>> entries);
};

} // namespace engine

#endif // BT_CLIENT
