#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;

} // namespace game

namespace engine
{

class Texture;
struct CoordFrames;
struct GridCoord;

inline constexpr int64_t kiDefaultIslandCapacity = 16;

class Islands
{
public:

	Islands();
	~Islands();

	void UpdateActiveIslands(const std::unordered_map<GridCoord, CoordFrames>& rFrames, const std::vector<GridCoord>& rActiveCoords);

	struct Island
	{
		common::crc_t islandCrc = 0;
		shaders::AxisAlignedQuadLayout quad;
	};

	XMFLOAT4 mf4GlobalArea {};
	std::vector<Island> mIslands;
	int64_t miActiveCount = 0;  // Count of non-zero-width slots at the front of mIslands; remaining slots are zero-width placeholders.
	Buffer mIslandsStorageBuffer;
};

inline Islands* gpIslands = nullptr;

} // namespace engine

#endif // BT_CLIENT
