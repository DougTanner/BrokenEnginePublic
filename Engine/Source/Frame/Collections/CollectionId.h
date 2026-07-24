#pragma once

namespace engine
{

struct FramePostRenderBase;

// Stable entity identity across transfers and reconnects
// Assigned by server at first spawn, carried in TransferData
struct global_id_t
{
	int64_t iValue = 0;
	constexpr bool IsValid() const { return iValue != 0; }
	bool operator==(const global_id_t&) const = default;
};

// Global unique identifier with counter stored in FramePostRenderBase
// 0 = invalid/uninitialized, counter starts at 1
struct uuid_t
{
	int64_t iValue = 0;

	constexpr uuid_t() = default;
	constexpr explicit uuid_t(int64_t iVal) : iValue(iVal) {}

	// Generate next unique ID (counter stored in FramePostRenderBase)
	static uuid_t Generate(FramePostRenderBase& rFramePostRender);
#if defined(BT_CLIENT)
	static uuid_t GenerateVisual(FramePostRenderBase& rFramePostRender);
#endif

	constexpr bool IsValid() const
	{
		return iValue != 0;
	}

	constexpr int64_t Value() const
	{
		return iValue;
	}

	constexpr bool operator==(const uuid_t& other) const = default;
	constexpr auto operator<=>(const uuid_t& other) const = default;

	void Write(std::ostream& stream) const { common::Write(stream, iValue); }
	void Read(std::istream& stream) { common::Read(stream, iValue); }
};

// Strong-typed ID wrapper preventing implicit conversions between different collection types
// Tag parameter ensures AreaLights::id_t cannot be mixed with Sounds::id_t
template <typename T>
struct id_t
{
	uuid_t uuid {};

	constexpr id_t() = default;
	constexpr explicit id_t(uuid_t u)
	: uuid(u)
	{
	}

	// Generate next unique ID (counter stored in FramePostRenderBase)
	static id_t Generate(FramePostRenderBase& rFramePostRender)
	{
		return id_t {uuid_t::Generate(rFramePostRender)};
	}

#if defined(BT_CLIENT)
	static id_t GenerateVisual(FramePostRenderBase& rFramePostRender)
	{
		return id_t {uuid_t::GenerateVisual(rFramePostRender)};
	}
#endif

	constexpr bool IsValid() const { return uuid.IsValid(); }

	// Explicit conversion to uuid_t for generic comparisons
	constexpr uuid_t ToUuid() const { return uuid; }

	constexpr bool operator==(const id_t& other) const = default;
	constexpr auto operator<=>(const id_t& other) const = default;

	void Write(std::ostream& stream) const { uuid.Write(stream); }
	void Read(std::istream& stream) { uuid.Read(stream); }
};

} // namespace engine

// Hash specializations in std namespace for unordered_map support
namespace std
{

template <>
struct hash<engine::uuid_t>
{
	size_t operator()(const engine::uuid_t& id) const noexcept
	{
		return std::hash<int64_t>{}(id.iValue);
	}
};

template <typename T>
struct hash<engine::id_t<T>>
{
	size_t operator()(const engine::id_t<T>& id) const noexcept
	{
		return std::hash<engine::uuid_t>{}(id.uuid);
	}
};

// Renders global_id_t in logs; prints "(none)" for the 0 sentinel (see global_id_t::IsValid).
template <>
struct formatter<engine::global_id_t> : formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const engine::global_id_t id, CONTEXT& rContext) const
	{
		if (id.iValue == 0)
		{
			return formatter<std::string_view>::format("(none)", rContext);
		}

		char pcBuffer[24];
		char* pWrite = std::to_chars(pcBuffer, pcBuffer + sizeof(pcBuffer), id.iValue).ptr;
		return formatter<std::string_view>::format(std::string_view(pcBuffer, pWrite - pcBuffer), rContext);
	}
};

} // namespace std
