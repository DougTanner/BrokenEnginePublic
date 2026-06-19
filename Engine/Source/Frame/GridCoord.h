#pragma once

namespace engine
{

struct GridCoord
{
	int32_t x = 0;
	int32_t y = 0;

	constexpr bool operator==(const GridCoord&) const = default;

	constexpr uint64_t ToKey() const
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
		        static_cast<uint64_t>(static_cast<uint32_t>(y));
	}

	static constexpr GridCoord FromKey(uint64_t uiKey)
	{
		return {static_cast<int32_t>(uiKey >> 32), static_cast<int32_t>(uiKey)};
	}

	common::crc_t Crc() const
	{
		return common::Crc(ToKey());
	}

	void Write(std::ostream& rStream) const
	{
		common::Write(rStream, x);
		common::Write(rStream, y);
	}

	void Read(std::istream& rStream)
	{
		common::Read(rStream, x);
		common::Read(rStream, y);
	}
};

inline constexpr GridCoord kOriginCoord {0, 0};

} // namespace engine

template<>
struct std::hash<engine::GridCoord>
{
	std::size_t operator()(const engine::GridCoord& rCoord) const noexcept
	{
		return std::hash<uint64_t>{}(rCoord.ToKey());
	}
};
