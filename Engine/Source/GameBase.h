#pragma once

#if defined(BT_CLIENT)
#include "Graphics/Camera.h"
#endif

namespace game
{

struct Frame;
struct MenuInput;
struct FrameInput;
struct StatusChange;

}

namespace engine
{

struct RawInput;

enum class MenuFlags : uint64_t
{
	kMouseVisible = 0x01,
};
using MenuFlags_t = common::Flags<MenuFlags>;

enum class GameFlags : uint64_t
{
	kQuit                 = 0x01,
	kSaveReplay           = 0x02,
	kLoadReplay           = 0x04,
};
using GameFlags_t = common::Flags<GameFlags>;

struct CoordFrames
{
	std::unique_ptr<game::Frame> pCurrent;
	std::unique_ptr<game::Frame> pNext;

#if defined(BT_CLIENT)
	std::array<std::unique_ptr<game::Frame>, kiTickRate> snapshots {};
	int64_t iSnapshotCount = 0;

	// Confirmed tick = index into snapshots (no separate unique_ptr)
	int64_t iConfirmedTick = -1;
	int64_t iConfirmedSnapshotIndex = -1;

	struct CoordServerUpdate
	{
		common::crc_t serverCrc = 0;
		common::crc_t inputCrc = 0;
		std::vector<game::StatusChange> statusChanges;
	};
	std::map<int64_t, CoordServerUpdate> serverUpdates;

	struct PendingFullState
	{
		int64_t iTick = -1;
		std::unique_ptr<game::Frame> pFrame;
	};
	std::optional<PendingFullState> pendingFullState;

	uint64_t uiGeneration = 0;
#endif
};

class GameBase
{
public:

	GameBase();
	virtual ~GameBase() = default;

	virtual void Reset() = 0;
	virtual bool ShouldTrapCursor() = 0;
	virtual bool ShouldUseCrosshair() = 0;
	virtual std::filesystem::path QuicksaveFile() = 0;
	virtual std::filesystem::path ReplayFile() = 0;
	virtual void ProcessMenuInput(const game::MenuInput& rMenuInput) = 0;

	void PreUpdate(const game::MenuInput& rMenuInput);
	void TickFrames(const game::MenuInput& rMenuInput);
#if defined(BT_CLIENT)
	void TickFramesAndRender(const game::MenuInput& rMenuInput);
	void Render();
	game::Frame& RenderFrame(GridCoord coord) const;
#endif

	void Quicksave(const game::MenuInput& rMenuInput);
	bool Quickload(const game::MenuInput& rMenuInput);
	void SaveLoadReplay(const game::MenuInput& rMenuInput);
	void SyncReplay(game::Frame& rFrame, game::FrameInput& rFrameInput);

	uint16_t GenerateFrameId() { return muiNextFrameId++; }
	int64_t TickCounter() const { return miTickCounter; }

	game::Frame& CurrentFrame(GridCoord coord) const
	{
		return *mCoordFrames.at(coord).pCurrent;
	}

	game::Frame& NextFrame(GridCoord coord)
	{
		return *mCoordFrames.at(coord).pNext;
	}

	GameFlags_t mGameFlags;

	TimeStep mTimeStep;
	std::unique_ptr<DifferenceStreamWriter<game::Frame, game::FrameInput>> mpDifferenceStreamWriter;
	std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInput>> mpDifferenceStreamReader;

	std::unordered_map<GridCoord, CoordFrames> mCoordFrames;

protected:

	void WriteGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord humanGridCoord);
	bool ReadGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord& rHumanGridCoord);

	int64_t miTickCounter = 0;
	float mfCurrentTime = 0.0f;

	uint16_t muiNextFrameId = 0;

	MenuFlags_t mMenuFlags {MenuFlags::kMouseVisible};
};

} // namespace engine
