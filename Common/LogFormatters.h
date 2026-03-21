#pragma once

template<>
struct std::formatter<std::string> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::string& rString, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(rString, rContext);
	}
};

template<>
struct std::formatter<std::wstring> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::wstring& rPath, CONTEXT& rContext) const
	{
		std::string string = common::ToString(rPath);
		return std::formatter<std::string_view>::format(string, rContext);
	}
};

template<>
struct std::formatter<std::filesystem::path> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::filesystem::path& rPath, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(rPath.string(), rContext);
	}
};

template<>
struct std::formatter<XMFLOAT3> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT3 f3, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{{{}, {}, {}}}", f3.x, f3.y, f3.z);
	}
};

template<>
struct std::formatter<XMFLOAT4> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT4 f4, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{{{}, {}, {}, {}}}", f4.x, f4.y, f4.z, f4.w);
	}
};

template<>
struct std::formatter<XMFLOAT4A> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT4A f4, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{{{}, {}, {}, {}}}", f4.x, f4.y, f4.z, f4.w);
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
		return std::format_to(rContext.out(), "{{{}, {}, {}, {}}}", f4.x, f4.y, f4.z, f4.w);
	}
};

template<>
struct std::formatter<VkFilter> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const VkFilter vkSamplerAddressMode, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}", static_cast<int>(vkSamplerAddressMode));
	}
};

template<>
struct std::formatter<VkSamplerAddressMode> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const VkSamplerAddressMode vkSamplerAddressMode, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}", static_cast<int>(vkSamplerAddressMode));
	}
};

template<>
struct std::formatter<std::chrono::nanoseconds> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::chrono::nanoseconds ns, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}ns", ns.count());
	}
};

template<>
struct std::formatter<std::chrono::microseconds> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::chrono::microseconds us, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}us", us.count());
	}
};

template<>
struct std::formatter<std::chrono::milliseconds> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::chrono::milliseconds ms, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}ms", ms.count());
	}
};

template<>
struct std::formatter<std::chrono::seconds> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::chrono::seconds s, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}s", s.count());
	}
};

template<>
struct std::formatter<int8_t> : std::formatter<int>
{
	template<typename CONTEXT>
	auto format(const int8_t value, CONTEXT& rContext) const
	{
		return std::formatter<int>::format(static_cast<int>(value), rContext);
	}
};

template<>
struct std::formatter<uint8_t> : std::formatter<uint32_t>
{
	template<typename CONTEXT>
	auto format(const uint8_t value, CONTEXT& rContext) const
	{
		return std::formatter<uint32_t>::format(static_cast<uint32_t>(value), rContext);
	}
};

template<>
struct std::formatter<XMFLOAT2> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT2 f2, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{{{}, {}}}", f2.x, f2.y);
	}
};

template<typename ENUM_TYPE>
struct std::formatter<common::Flags<ENUM_TYPE>> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const common::Flags<ENUM_TYPE> flags, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}", std::to_underlying(flags.meFlags));
	}
};

template<>
struct std::formatter<common::RandomEngine> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const common::RandomEngine& rEngine, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}", rEngine.uiState);
	}
};
