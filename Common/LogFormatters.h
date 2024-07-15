#pragma once

template<>
struct std::formatter<std::wstring> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::wstring& rWstring, CONTEXT& rContext) const
	{
		std::string string = common::ToString(rWstring);
		return std::formatter<std::string_view>::format(std::format("{}", string), rContext);
	}
};

template<>
struct std::formatter<DirectX::XMFLOAT3> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const DirectX::XMFLOAT3 f3, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(std::format("{{{}, {}, {}}}", f3.x, f3.y, f3.z), rContext);
	}
};

template<>
struct std::formatter<DirectX::XMFLOAT4> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const DirectX::XMFLOAT4 f4, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(std::format("{{{}, {}, {}, {}}}", f4.x, f4.y, f4.z, f4.w), rContext);
	}
};

template<>
struct std::formatter<DirectX::XMFLOAT4A> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const DirectX::XMFLOAT4A f4, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(std::format("{{{}, {}, {}, {}}}", f4.x, f4.y, f4.z, f4.w), rContext);
	}
};

template<>
struct std::formatter<DirectX::XMVECTOR> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const DirectX::XMVECTOR vec, CONTEXT& rContext) const
	{
		DirectX::XMFLOAT4A f4 {};
		DirectX::XMStoreFloat4A(&f4, vec);
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
