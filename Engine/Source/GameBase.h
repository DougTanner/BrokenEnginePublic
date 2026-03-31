#pragma once

#include "GameSaveLoad.h"

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
	kMainMenu             = 0x08,
	kDeathScreen          = 0x10,
	kPaused               = 0x20,
};
using GameFlags_t = common::Flags<GameFlags>;

struct CoordFrames
{
	std::unique_ptr<game::Frame> pCurrent;
	std::unique_ptr<game::Frame> pNext;

#if defined(BT_CLIENT)
	std::unique_ptr<game::Frame> snapshots[kiNetworkBufferSize] {};
	int64_t iSnapshotHead = 0;       // physical index of oldest entry
	int64_t iSnapshotCount = 0;      // number of valid entries in ring

	// Confirmed tick = logical offset from head (-1 = none)
	int64_t iConfirmedTick = -1;
	int64_t iConfirmedOffset = -1;

	struct CoordServerUpdate
	{
		common::crc_t sharedCrc = 0;
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

	void ResetClientState()
	{
		iConfirmedTick = -1;
		iConfirmedOffset = -1;
		iSnapshotHead = 0;
		iSnapshotCount = 0;
		serverUpdates.clear();
		pendingFullState.reset();
		uiGeneration = 0;
	}
#endif // BT_CLIENT
};

#if defined(BT_CLIENT)
inline int64_t SnapshotIndex(int64_t iHead, int64_t iLogical)
{
	return (iHead + iLogical) % kiNetworkBufferSize;
}
#endif // BT_CLIENT

class GameBase
{
	friend class GameSaveLoad;

public:

	GameBase();
	virtual ~GameBase();

	virtual void Reset() = 0;
	virtual bool ShouldTrapCursor() = 0;
#if defined(BT_CLIENT)
	virtual bool ShouldUseCrosshair() = 0;
#endif
	virtual std::filesystem::path QuicksaveFile() = 0;
	virtual std::filesystem::path ReplayFile() = 0;
	virtual void ProcessMenuInput(const game::MenuInput& rMenuInput) = 0;

	void ProcessInput(bool bLostFocus, game::MenuInput& rMenuInput);
#if defined(BT_CLIENT)
	void ClientUpdate();
	void Render();
	game::Frame& RenderFrame(GridCoord coord) const;
#endif // BT_CLIENT
#if defined(BT_SERVER)
	void ServerUpdate(const game::MenuInput& rMenuInput);
#endif // BT_SERVER

	uint16_t GenerateFrameId() { return muiNextFrameId++; }
	int64_t TickCounter() const { return miTickCounter; }
	float CurrentTime() const { return mfCurrentTime; }
	void SetTickCounter(int64_t iTickCounter) { miTickCounter = iTickCounter; }
	void SetCurrentTime(float fCurrentTime) { mfCurrentTime = fCurrentTime; }
	uint16_t NextFrameId() const { return muiNextFrameId; }
	void SetNextFrameId(uint16_t uiNextFrameId) { muiNextFrameId = uiNextFrameId; }
	int64_t GenerateGlobalId() { return miNextGlobalId++; }
	int64_t NextGlobalId() const { return miNextGlobalId; }
	void SetNextGlobalId(int64_t iNextGlobalId) { miNextGlobalId = iNextGlobalId; }

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
#if defined(BT_SERVER)
	GameSaveLoad mGameSaveLoad;
#endif // BT_SERVER

	std::unordered_map<GridCoord, CoordFrames> mCoordFrames;

protected:

	void PrepareActiveSet();
	void SwapFrames();
	void BuildAndDispatchFrameTicks(const std::vector<GridCoord>& rActiveCoords, bool bExtrapolating);
	void FinalizeFrameTick(const std::vector<GridCoord>& rActiveCoords, bool bExtrapolating);

	int64_t miTickCounter = 0;
	float mfCurrentTime = 0.0f;

	uint16_t muiNextFrameId = 0;
	int64_t miNextGlobalId = 1;

	MenuFlags_t mMenuFlags {MenuFlags::kMouseVisible};
};

} // namespace engine
