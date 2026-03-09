#pragma once

#ifdef BT_CLIENT

namespace game
{

struct Frame;

} // namespace game

namespace engine
{

class Texture;
struct SubscribedFrame;
struct GridCoord;

inline constexpr int64_t kiDefaultIslandCapacity = 16;

class Islands
{
public:

	Islands();
	~Islands();

	void UpdateActiveIslands(const std::unordered_map<GridCoord, SubscribedFrame>& rFrames, const std::vector<GridCoord>& rActiveCoords);

	struct Island
	{
		shaders::AxisAlignedQuadLayout quad;
	};

	XMFLOAT4 mf4GlobalArea {};
	std::vector<Island> mIslands;
	Buffer mIslandsStorageBuffer;
};

inline Islands* gpIslands = nullptr;

} // namespace engine

#endif // BT_CLIENT
