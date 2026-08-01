#pragma once

#include "Workbuffer.h"
#include "Threading/ThreadLocal.h"

namespace common
{

// Assembles a brace-wrapped, comma-separated float list ("{1.5, 2.5}") in shortest round-trip form (matching std::format
// "{}") into a caller stack buffer, so the caller can forward the view through std::formatter<std::string_view> and have
// width/precision/fill specs apply. Allocation-free — the discarded-spec format_to(rContext.out(), ...) form did not honor specs.
inline std::string_view FormatVector(std::span<char> pcBuffer, std::initializer_list<float> values)
{
	char* pWrite = pcBuffer.data();
	char* pEnd = pcBuffer.data() + pcBuffer.size();
	auto put = [&pWrite, pEnd](char c)
	{
		if (pWrite < pEnd)
		{
			*(pWrite++) = c;
		}
	};

	put('{');
	bool bFirst = true;
	for (float fValue : values)
	{
		if (!bFirst)
		{
			put(',');
			put(' ');
		}
		bFirst = false;
		pWrite = std::to_chars(pWrite, pEnd, fValue).ptr;
	}
	put('}');
	return std::string_view(pcBuffer.data(), pWrite - pcBuffer.data());
}

// Integer value with a trailing unit suffix ("123ns"); same stack-buffer + forward rationale as FormatVector.
inline std::string_view FormatSuffixed(std::span<char> pcBuffer, int64_t iValue, std::string_view suffix)
{
	char* pEnd = pcBuffer.data() + pcBuffer.size();
	char* pWrite = std::to_chars(pcBuffer.data(), pEnd, iValue).ptr;
	for (char c : suffix)
	{
		if (pWrite < pEnd)
		{
			*(pWrite++) = c;
		}
	}
	return std::string_view(pcBuffer.data(), pWrite - pcBuffer.data());
}

} // namespace common

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
	auto format(const std::wstring& rString, CONTEXT& rContext) const
	{
		// Hot path: UTF-8-convert into the workbuffer (no heap allocation). The gpThreadLocal guard mirrors Wb/WbV2 and
		// is load-bearing — without it /analyze flags C6011 (null deref) as an error in the Release code-analysis build.
		if (common::gpThreadLocal != nullptr) [[likely]]
		{
			common::ScopedWorkbufferArena arena = common::gpThreadLocal->mWorkbuffer.Push();
			arena.Append(rString);
			return std::formatter<std::string_view>::format(arena.View(), rContext);
		}

		// Pre-ThreadLocal thread (no workbuffer): owning conversion is acceptable — allocation tracking is inactive this early.
		std::string narrow = common::ToString(rString);
		return std::formatter<std::string_view>::format(narrow, rContext);
	}
};

template<>
struct std::formatter<std::filesystem::path> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::filesystem::path& rPath, CONTEXT& rContext) const
	{
		// See the std::wstring formatter above: the gpThreadLocal guard mirrors Wb/WbV2 and is required for /analyze.
		if (common::gpThreadLocal != nullptr) [[likely]]
		{
			common::ScopedWorkbufferArena arena = common::gpThreadLocal->mWorkbuffer.Push();
			arena.Append(rPath.native());
			return std::formatter<std::string_view>::format(arena.View(), rContext);
		}

		std::string narrow = common::ToString(rPath.native());
		return std::formatter<std::string_view>::format(narrow, rContext);
	}
};

template<>
struct std::formatter<XMFLOAT3> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT3 f3, CONTEXT& rContext) const
	{
		char pcBuffer[128];
		return std::formatter<std::string_view>::format(common::FormatVector(pcBuffer, {f3.x, f3.y, f3.z}), rContext);
	}
};

template<>
struct std::formatter<XMFLOAT4> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT4 f4, CONTEXT& rContext) const
	{
		char pcBuffer[128];
		return std::formatter<std::string_view>::format(common::FormatVector(pcBuffer, {f4.x, f4.y, f4.z, f4.w}), rContext);
	}
};

template<>
struct std::formatter<XMFLOAT4A> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const XMFLOAT4A f4, CONTEXT& rContext) const
	{
		char pcBuffer[128];
		return std::formatter<std::string_view>::format(common::FormatVector(pcBuffer, {f4.x, f4.y, f4.z, f4.w}), rContext);
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
		char pcBuffer[128];
		return std::formatter<std::string_view>::format(common::FormatVector(pcBuffer, {f4.x, f4.y, f4.z, f4.w}), rContext);
	}
};

template<>
struct std::formatter<VkResult> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const VkResult vkResult, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(string_VkResult(vkResult), rContext);
	}
};

template<>
struct std::formatter<VkFilter> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const VkFilter vkFilter, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(string_VkFilter(vkFilter), rContext);
	}
};

template<>
struct std::formatter<VkSamplerAddressMode> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const VkSamplerAddressMode vkSamplerAddressMode, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(string_VkSamplerAddressMode(vkSamplerAddressMode), rContext);
	}
};

template<>
struct std::formatter<std::chrono::nanoseconds> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::chrono::nanoseconds ns, CONTEXT& rContext) const
	{
		char pcBuffer[32];
		return std::formatter<std::string_view>::format(common::FormatSuffixed(pcBuffer, ns.count(), "ns"), rContext);
	}
};

template<>
struct std::formatter<std::chrono::microseconds> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::chrono::microseconds us, CONTEXT& rContext) const
	{
		char pcBuffer[32];
		return std::formatter<std::string_view>::format(common::FormatSuffixed(pcBuffer, us.count(), "us"), rContext);
	}
};

template<>
struct std::formatter<std::chrono::milliseconds> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::chrono::milliseconds ms, CONTEXT& rContext) const
	{
		char pcBuffer[32];
		return std::formatter<std::string_view>::format(common::FormatSuffixed(pcBuffer, ms.count(), "ms"), rContext);
	}
};

template<>
struct std::formatter<std::chrono::seconds> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const std::chrono::seconds s, CONTEXT& rContext) const
	{
		char pcBuffer[32];
		return std::formatter<std::string_view>::format(common::FormatSuffixed(pcBuffer, s.count(), "s"), rContext);
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
		char pcBuffer[128];
		return std::formatter<std::string_view>::format(common::FormatVector(pcBuffer, {f2.x, f2.y}), rContext);
	}
};

template<typename ENUM_TYPE>
struct std::formatter<common::Flags<ENUM_TYPE>> : std::formatter<std::underlying_type_t<ENUM_TYPE>>
{
	template<typename CONTEXT>
	auto format(const common::Flags<ENUM_TYPE> flags, CONTEXT& rContext) const
	{
		return std::formatter<std::underlying_type_t<ENUM_TYPE>>::format(std::to_underlying(flags.meFlags), rContext);
	}
};

template<>
struct std::formatter<common::RandomEngine> : std::formatter<uint64_t>
{
	template<typename CONTEXT>
	auto format(const common::RandomEngine& rEngine, CONTEXT& rContext) const
	{
		return std::formatter<uint64_t>::format(rEngine.State(), rContext);
	}
};

template <>
struct std::formatter<common::Wb> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::Wb& rValue, CONTEXT& rContext) const
	{
		if (common::gpThreadLocal != nullptr) [[likely]]
		{
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			common::ScopedWorkbufferArena arena = rWorkbuffer.Push();
			arena.AppendFloat(rValue.fValue, rValue.iPrecision);
			return std::formatter<std::string_view>::format(arena.View(), rContext);
		}

		char pcBuffer[64] {};
		std::to_chars_result result = std::to_chars(pcBuffer, pcBuffer + sizeof(pcBuffer), rValue.fValue, std::chars_format::fixed, rValue.iPrecision);
		return std::formatter<std::string_view>::format(std::string_view(pcBuffer, result.ptr - pcBuffer), rContext);
	}
};

template <>
struct std::formatter<common::WbV2> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::WbV2& rValue, CONTEXT& rContext) const
	{
		if (common::gpThreadLocal != nullptr) [[likely]]
		{
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			common::ScopedWorkbufferArena arena = rWorkbuffer.Push();
			arena.Append(std::string_view("("));
			arena.AppendFloat(DirectX::XMVectorGetX(rValue.vec), rValue.iPrecision);
			arena.Append(std::string_view(","));
			arena.AppendFloat(DirectX::XMVectorGetY(rValue.vec), rValue.iPrecision);
			arena.Append(std::string_view(")"));
			return std::formatter<std::string_view>::format(arena.View(), rContext);
		}

		char pcBuffer[128] {};
		char* pWrite = pcBuffer;
		char* pEnd = pcBuffer + sizeof(pcBuffer);
		auto put = [&pWrite, pEnd](char c)
		{
			if (pWrite < pEnd)
			{
				*(pWrite++) = c;
			}
		};
		put('(');
		pWrite = std::to_chars(pWrite, pEnd, DirectX::XMVectorGetX(rValue.vec), std::chars_format::fixed, rValue.iPrecision).ptr;
		put(',');
		pWrite = std::to_chars(pWrite, pEnd, DirectX::XMVectorGetY(rValue.vec), std::chars_format::fixed, rValue.iPrecision).ptr;
		put(')');
		return std::formatter<std::string_view>::format(std::string_view(pcBuffer, pWrite - pcBuffer), rContext);
	}
};

template <>
struct std::formatter<common::WbV3> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::WbV3& rValue, CONTEXT& rContext) const
	{
		if (common::gpThreadLocal != nullptr) [[likely]]
		{
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			common::ScopedWorkbufferArena arena = rWorkbuffer.Push();
			arena.Append(std::string_view("("));
			arena.AppendFloat(DirectX::XMVectorGetX(rValue.vec), rValue.iPrecision);
			arena.Append(std::string_view(","));
			arena.AppendFloat(DirectX::XMVectorGetY(rValue.vec), rValue.iPrecision);
			arena.Append(std::string_view(","));
			arena.AppendFloat(DirectX::XMVectorGetZ(rValue.vec), rValue.iPrecision);
			arena.Append(std::string_view(")"));
			return std::formatter<std::string_view>::format(arena.View(), rContext);
		}

		char pcBuffer[128] {};
		char* pWrite = pcBuffer;
		char* pEnd = pcBuffer + sizeof(pcBuffer);
		auto put = [&pWrite, pEnd](char c)
		{
			if (pWrite < pEnd)
			{
				*(pWrite++) = c;
			}
		};
		put('(');
		pWrite = std::to_chars(pWrite, pEnd, DirectX::XMVectorGetX(rValue.vec), std::chars_format::fixed, rValue.iPrecision).ptr;
		put(',');
		pWrite = std::to_chars(pWrite, pEnd, DirectX::XMVectorGetY(rValue.vec), std::chars_format::fixed, rValue.iPrecision).ptr;
		put(',');
		pWrite = std::to_chars(pWrite, pEnd, DirectX::XMVectorGetZ(rValue.vec), std::chars_format::fixed, rValue.iPrecision).ptr;
		put(')');
		return std::formatter<std::string_view>::format(std::string_view(pcBuffer, pWrite - pcBuffer), rContext);
	}
};

template <>
struct std::formatter<common::WbV4> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::WbV4& rValue, CONTEXT& rContext) const
	{
		if (common::gpThreadLocal != nullptr) [[likely]]
		{
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			common::ScopedWorkbufferArena arena = rWorkbuffer.Push();
			arena.Append(std::string_view("("));
			arena.AppendFloat(DirectX::XMVectorGetX(rValue.vec), rValue.iPrecision);
			arena.Append(std::string_view(","));
			arena.AppendFloat(DirectX::XMVectorGetY(rValue.vec), rValue.iPrecision);
			arena.Append(std::string_view(","));
			arena.AppendFloat(DirectX::XMVectorGetZ(rValue.vec), rValue.iPrecision);
			arena.Append(std::string_view(","));
			arena.AppendFloat(DirectX::XMVectorGetW(rValue.vec), rValue.iPrecision);
			arena.Append(std::string_view(")"));
			return std::formatter<std::string_view>::format(arena.View(), rContext);
		}

		char pcBuffer[128] {};
		char* pWrite = pcBuffer;
		char* pEnd = pcBuffer + sizeof(pcBuffer);
		auto put = [&pWrite, pEnd](char c)
		{
			if (pWrite < pEnd)
			{
				*(pWrite++) = c;
			}
		};
		put('(');
		pWrite = std::to_chars(pWrite, pEnd, DirectX::XMVectorGetX(rValue.vec), std::chars_format::fixed, rValue.iPrecision).ptr;
		put(',');
		pWrite = std::to_chars(pWrite, pEnd, DirectX::XMVectorGetY(rValue.vec), std::chars_format::fixed, rValue.iPrecision).ptr;
		put(',');
		pWrite = std::to_chars(pWrite, pEnd, DirectX::XMVectorGetZ(rValue.vec), std::chars_format::fixed, rValue.iPrecision).ptr;
		put(',');
		pWrite = std::to_chars(pWrite, pEnd, DirectX::XMVectorGetW(rValue.vec), std::chars_format::fixed, rValue.iPrecision).ptr;
		put(')');
		return std::formatter<std::string_view>::format(std::string_view(pcBuffer, pWrite - pcBuffer), rContext);
	}
};
