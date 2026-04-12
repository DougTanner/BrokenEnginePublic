#pragma once

#include "Frame/FrameStaticData.h"
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
	kPaused               = 0x20,
};
using GameFlags_t = common::Flags<GameFlags>;

struct CoordFrames
{
	FrameStaticData staticData;

#if defined(BT_SERVER)
	std::unique_ptr<game::Frame> pCurrent;
	std::unique_ptr<game::Frame> pNext;
#endif

#if defined(BT_CLIENT)
	std::unique_ptr<game::Frame> snapshots[kiNetworkBufferSize] {};
	int64_t iSnapshotHead = 0;       // physical index of oldest entry
	int64_t iSnapshotCount = 0;      // number of valid entries in ring

	// Confirmed tick = logical offset from head (-1 = none)
	int64_t iConfirmedTick = -1;
	int64_t iConfirmedOffset = -1;

	// Monotonic high-water mark: highest tick whose CRC has matched the server. Re-simulating
	// any tick <= this value is an invariant violation and trips DEBUG_BREAK in ReconcileRunTickCoord.
	int64_t iHighWaterValidatedTick = -1;

	// Monotonic guard for the rendered frame: the renderer must never regress to an older tick
	// or an earlier interpolated time for the same coord. Trips DEBUG_BREAK in Render().
	int64_t iLastRenderedTick = -1;
	float fLastRenderedTime = 0.0f;

	struct CoordServerUpdate
	{
		common::crc_t sharedCrc = 0;
		std::vector<game::StatusChange> statusChanges;
	};
	std::map<int64_t, CoordServerUpdate> serverUpdates;

	struct PendingFullState
	{
		int64_t iTick = -1;
		std::unique_ptr<game::Frame> pFrame;
	};
	std::optional<PendingFullState> pendingFullState;
	int64_t iLastFullStateTick = -1;

	uint64_t uiGeneration = 0;

	// Log deduplication: suppress repeated mismatch logging when reconcile is stuck on same desync
	int64_t iLastLoggedConfirmedTick = -1;
	int64_t iLastLoggedFirstMismatch = -1;
	int64_t iStuckFrameCount = 0;
	static constexpr int64_t kiStuckLogInterval = 64;

	// Per-coord log cooldowns: prevent redundant detail logging across multiple Run() calls
	// within the same render frame (multiple ClientUpdates can fire at the same or adjacent ticks)
	int64_t iLastMismatchDetailLogTick = -1;
	int64_t iLastSpawnTransferLogTick = -1;
	static constexpr int64_t kiMismatchDetailLogCooldown = 32;

	// Repeated-work guard: detect if full replay is entered with identical state as last attempt.
	// Set when entering full replay; checked on next entry to trip DEBUG_BREAK.
	int64_t iLastReplayConfirmedTick = -1;
	int64_t iLastReplayServerUpdateCount = -1;

	// Delta-only throttle logging: only log when the computed max replay changes
	int64_t iLastLoggedMaxReplay = -1;
	bool bLastLoggedGapOverride = false;

	void ResetClientState()
	{
		iConfirmedTick = -1;
		iConfirmedOffset = -1;
		iHighWaterValidatedTick = -1;
		iLastRenderedTick = -1;
		fLastRenderedTime = 0.0f;
		iSnapshotHead = 0;
		iSnapshotCount = 0;
		serverUpdates.clear();
		pendingFullState.reset();
		iLastFullStateTick = -1;
		uiGeneration = 0;
		iLastLoggedConfirmedTick = -1;
		iLastLoggedFirstMismatch = -1;
		iStuckFrameCount = 0;
		iLastMismatchDetailLogTick = -1;
		iLastSpawnTransferLogTick = -1;
		iLastReplayConfirmedTick = -1;
		iLastReplayServerUpdateCount = -1;
		iLastLoggedMaxReplay = -1;
		bLastLoggedGapOverride = false;
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

#if defined(BT_SERVER)
	game::Frame& CurrentFrame(GridCoord coord) const
	{
		return *mCoordFrames.at(coord).pCurrent;
	}

	game::Frame& NextFrame(GridCoord coord)
	{
		return *mCoordFrames.at(coord).pNext;
	}
#endif // BT_SERVER

	GameFlags_t mGameFlags;

	TimeStep mTimeStep;
#if defined(BT_SERVER)
	GameSaveLoad mGameSaveLoad;
#endif // BT_SERVER

	std::unordered_map<GridCoord, CoordFrames> mCoordFrames;

protected:

	void PrepareActiveSet();
#if defined(BT_SERVER)
	void SwapFrames();
	void BuildAndDispatchFrameTicks(const std::vector<GridCoord>& rActiveCoords);
	void FinalizeFrameTick(const std::vector<GridCoord>& rActiveCoords);
#endif // BT_SERVER

	int64_t miTickCounter = 0;
	float mfCurrentTime = 0.0f;

	uint16_t muiNextFrameId = 0;
	int64_t miNextGlobalId = 1;

	MenuFlags_t mMenuFlags {MenuFlags::kMouseVisible};
};

} // namespace engine
