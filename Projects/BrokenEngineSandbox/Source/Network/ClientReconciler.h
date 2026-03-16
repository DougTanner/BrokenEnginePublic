#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;
struct FrameInput;

using engine::SnapshotIndex;

struct ConfirmedHumanState
{
	engine::GridCoord humanGridCoord {};
	player_t humanPlayerId {};
	float fPreviousHumanArmor = 0.0f;
	float fCurrentTime = 0.0f;
};

struct ReconcileProfiling
{
	int64_t iCrcValidatedFrameTicks = 0;
	int64_t iAssumedFrameTicks = 0;
	int64_t iCrcFastPathEvents = 0;
	int64_t iStatusChangeReplayTicks = 0;
	int64_t iKnockOnReplayTicks = 0;
};

struct CoordReconcileWork
{
	engine::GridCoord coord {};
	uint64_t uiGeneration = 0;

	// Input
	int64_t iConfirmedTick = -1;
	int64_t iConfirmedOffset = -1;
	int64_t iSnapshotHead = 0;
	std::map<int64_t, engine::CoordFrames::CoordServerUpdate> serverUpdates;
	std::unique_ptr<Frame> snapshots[engine::kiNetworkBufferSize] {};
	int64_t iSnapshotCount = 0;
	std::optional<engine::CoordFrames::PendingFullState> pendingFullState;

	// Replay stack: raw pointers (non-owning), referencing snapshots in ring
	std::vector<Frame*> replayStack;
	int64_t iReplayStackCount = 0;
	int64_t iReplayWriteHead = 0; // first ring slot written during replay
	int64_t iReplayWriteCount = 0; // number of ring slots written

	// Index of last CRC-validated replay stack entry (-1 if none)
	int64_t iLastValidatedIndex = -1;

	// Output
	int64_t iNewConfirmedTick = -1;
	int64_t iNewConfirmedOffset = -1; // physical ring index of new confirmed frame
	int64_t iOutputCount = 0; // total snapshot count for writeback (confirmed + catch-up)
	bool bCrcFastPath = false;
	bool bFullReplay = false;
	int64_t iTickCounter = 0;
	float fCurrentTime = 0.0f;

	// Per-coord profiling counters
	ReconcileProfiling profiling;

	// Desync (if any)
	int64_t iDesyncTick = -1;
	common::crc_t desyncExpectedCrc = 0;
	common::crc_t desyncActualCrc = 0;
	std::unique_ptr<Frame> pDesyncClientFrame;
};

struct ReconcileContext
{
	// Per-coord work items
	std::vector<CoordReconcileWork> coordWork;

	// Global input
	ConfirmedHumanState confirmedHumanState;
	uint16_t uiNextFrameId = 0;
	int64_t iTargetTick = 0;
	engine::alignment_t playerAlignment {};
	engine::Alignments alignments;

	// Working data (set during post-dispatch merge)
	int64_t iTickCounter = 0;
	float fCurrentTime = 0.0f;

	// Output
	ConfirmedHumanState newConfirmedHumanState;
	bool bAnyFullReplay = false;

	// Profiling counters
	using Profiling = ReconcileProfiling;
	Profiling profiling;

	// Deferred desync info (from any coord)
	int64_t iDesyncTick = -1;
	engine::GridCoord desyncCoord {};
	common::crc_t desyncExpectedCrc = 0;
	common::crc_t desyncActualCrc = 0;
	std::unique_ptr<Frame> pDesyncClientFrame;
};

struct ReconcileDesyncInfo
{
	bool bDesync = false;
	int64_t iDesyncTick = -1;
	engine::GridCoord desyncCoord {};
	common::crc_t desyncExpectedCrc = 0;
	common::crc_t desyncActualCrc = 0;
	std::unique_ptr<Frame> pDesyncClientFrame;
};

class ClientReconciler
{
public:

	ClientReconciler();
	~ClientReconciler();

	void TryKick();
	ReconcileDesyncInfo Wait();
	void Reset();
	uint64_t NextGeneration() { return muiNextGeneration++; }

	void SetHasNewData() { mbHasNewData = true; }
	void InitConfirmedHumanState(const ConfirmedHumanState& rState)
	{
		if (mConfirmedHumanState.fCurrentTime == 0.0f)
		{
			mConfirmedHumanState.fCurrentTime = rState.fCurrentTime;
		}
	}

private:

	bool mbHasNewData = false;
	ConfirmedHumanState mConfirmedHumanState;

	void Kick();
	ReconcileDesyncInfo ApplyResult();
	static void ApplyCoordWriteback(CoordReconcileWork& rWork, engine::CoordFrames& rSub);
	void Reconcile(ReconcileContext& rReconcileContext, const engine::Alignments& rAlignments);

	std::unique_ptr<common::PersistentWorker> mpWorker;
	std::unique_ptr<common::Multithreading> mpDispatch; // Per-coord parallel reconciliation
	std::unique_ptr<ReconcileContext> mpContext;
	bool mbInFlight = false;
	uint64_t muiNextGeneration = 1;
};

} // namespace game

#endif // BT_CLIENT
