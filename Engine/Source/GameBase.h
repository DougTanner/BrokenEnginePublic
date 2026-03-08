#pragma once

#ifdef BT_CLIENT
#include "Graphics/Camera.h"
#endif

namespace game
{

struct Frame;
struct MenuInput;
struct FrameInput;

}

namespace engine
{

struct RawInput;

enum class MenuFlags : uint64_t
{
	kMouseVisible = 0x01,
	kUpdateFrame  = 0x02,
};
using MenuFlags_t = common::Flags<MenuFlags>;

enum class GameFlags : uint64_t
{
	kQuit                 = 0x01,
	kSaveReplay           = 0x02,
	kLoadReplay           = 0x04,
	kPreviousFrameUpdated = 0x08,
};
using GameFlags_t = common::Flags<GameFlags>;

class GameBase
{
public:

	GameBase();
	virtual ~GameBase() = default;

	virtual void Reset() = 0;
	virtual bool ShouldUpdateFrame() = 0;
	virtual bool ShouldTrapCursor() = 0;
	virtual bool ShouldUseCrosshair() = 0;
	virtual std::filesystem::path QuicksaveFile() = 0;
	virtual std::filesystem::path ReplayFile() = 0;
	virtual void ProcessMenuInput(const game::MenuInput& rMenuInput) = 0;

	bool PreUpdate(const game::MenuInput& rMenuInput, bool bLostFocus);
	void UpdateFrames(const game::MenuInput& rMenuInput, bool bUpdateFrames);
#ifdef BT_CLIENT
	void UpdateFramesAndRender(const game::MenuInput& rMenuInput, bool bUpdateFrames);
	void Render(bool bUpdateFrames);
#endif

	void Quicksave(const game::MenuInput& rMenuInput);
	bool Quickload(const game::MenuInput& rMenuInput);
	void SaveLoadReplay(const game::MenuInput& rMenuInput);
	void SyncReplay(game::Frame& rFrame, game::FrameInput& rFrameInput);
	
	uint16_t GenerateFrameId() { return muiNextFrameId++; }
	int64_t FrameCounter() const { return miFrameCounter; }

	game::Frame& CurrentFrame(GridCoord coord) const
	{
		return *mCurrentFrames.at(coord);
	}

	game::Frame& NextFrame(GridCoord coord)
	{
		return *mNextFrames.at(coord);
	}

	const std::unordered_map<GridCoord, std::unique_ptr<game::Frame>>& CurrentFrames() const { return mCurrentFrames; }

	GameFlags_t mGameFlags;

	TimeStep mTimeStep;
	std::unique_ptr<DifferenceStreamWriter<game::Frame, game::FrameInput>> mpDifferenceStreamWriter;
	std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInput>> mpDifferenceStreamReader;

protected:

	void WriteGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord humanGridCoord);
	bool ReadGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord& rHumanGridCoord);

	int64_t miFrameCounter = 0;
	float mfCurrentTime = 0.0f;

	uint16_t muiNextFrameId = 0;

	MenuFlags_t mMenuFlags {MenuFlags::kMouseVisible, MenuFlags::kUpdateFrame};

	std::unordered_map<GridCoord, std::unique_ptr<game::Frame>> mCurrentFrames;
	std::unordered_map<GridCoord, std::unique_ptr<game::Frame>> mNextFrames;
};

void ResetRealTime();

} // namespace engine
