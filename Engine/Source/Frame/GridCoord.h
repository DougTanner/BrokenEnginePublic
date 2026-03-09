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

	inline common::crc_t Crc() const
	{
		return common::Crc(ToKey());
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, x);
		common::Write(rStream, y);
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, x);
		common::Read(rStream, y);
	}
};

inline constexpr GridCoord kOriginCoord {0, 0};

inline constexpr GridCoord kNeighborOffsets[] =
{
	{-1, -1}, {0, -1}, {1, -1},
	{-1,  0},          {1,  0},
	{-1,  1}, {0,  1}, {1,  1},
};

inline void ComputeQuadrantOffsets(float fPositionX, float fPositionY, float fCenterX, float fCenterY, GridCoord (&pOffsets)[3])
{
	int32_t iDirX = (fPositionX >= fCenterX) ? 1 : -1;
	int32_t iDirY = (fPositionY >= fCenterY) ? 1 : -1;
	pOffsets[0] = {iDirX, 0};
	pOffsets[1] = {0, iDirY};
	pOffsets[2] = {iDirX, iDirY};
}

} // namespace engine

template<>
struct std::hash<engine::GridCoord>
{
	std::size_t operator()(const engine::GridCoord& rCoord) const noexcept
	{
		return std::hash<uint64_t>{}(rCoord.ToKey());
	}
};
