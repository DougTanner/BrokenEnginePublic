#pragma once

template<>
struct std::formatter<std::string> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::string& rString, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(std::format("{}", rString.c_str()), rContext);
	}
};

template<>
struct std::formatter<std::wstring> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::wstring& rPath, CONTEXT& rContext) const
	{
		std::string string = common::ToString(rPath);
		return std::formatter<std::string_view>::format(std::format("{}", string), rContext);
	}
};

template<>
struct std::formatter<std::filesystem::path> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::filesystem::path& rPath, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(std::format("{}", rPath.string()), rContext);
	}
};

template<>
struct std::formatter<XMFLOAT3> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT3 f3, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(std::format("{{{}, {}, {}}}", f3.x, f3.y, f3.z), rContext);
	}
};

template<>
struct std::formatter<XMFLOAT4> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT4 f4, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(std::format("{{{}, {}, {}, {}}}", f4.x, f4.y, f4.z, f4.w), rContext);
	}
};

template<>
struct std::formatter<XMFLOAT4A> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT4A f4, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(std::format("{{{}, {}, {}, {}}}", f4.x, f4.y, f4.z, f4.w), rContext);
	}
};

template<>
struct std::formatter<XMVECTOR> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMVECTOR vec, CONTEXT& rContext) const
	{
		XMFLOAT4A f4 {};
		XMStoreFloat4A(&f4, vec);
		return std::formatter<std::string_view>::format(std::format("{{{}, {}, {}, {}}}", f4.x, f4.y, f4.z, f4.w), rContext);
	}
};

template<>
struct std::formatter<VkFilter> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const VkFilter vkSamplerAddressMode, CONTEXT& rContext) const
	{
		std::string string = std::to_string(vkSamplerAddressMode);
		return std::formatter<std::string_view>::format(std::format("{}", string), rContext);
	}
};

template<>
struct std::formatter<VkSamplerAddressMode> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const VkSamplerAddressMode vkSamplerAddressMode, CONTEXT& rContext) const
	{
		std::string string = std::to_string(vkSamplerAddressMode);
		return std::formatter<std::string_view>::format(std::format("{}", string), rContext);
	}
};
