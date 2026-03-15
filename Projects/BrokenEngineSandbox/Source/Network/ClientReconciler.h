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

struct CoordReconcileWork
{
	engine::GridCoord coord {};
	uint64_t uiGeneration = 0;

	// Input
	int64_t iConfirmedTick = -1;
	int64_t iConfirmedOffset = -1;
	int64_t iSnapshotHead = 0;
	std::map<int64_t, engine::CoordFrames::CoordServerUpdate> serverUpdates;
	std::unique_ptr<Frame> snapshots[engine::kiTickRate] {};
	int64_t iSnapshotCount = 0;
	std::optional<engine::CoordFrames::PendingFullState> pendingFullState;

	// Replay stack: raw pointers (non-owning), referencing snapshots or workspace
	std::vector<Frame*> replayStack;
	int64_t iReplayStackCount = 0;
	std::vector<std::unique_ptr<Frame>> replayWorkspace; // owns scratch Frames
	int64_t iReplayWorkspaceUsed = 0;

	// Index of last CRC-validated replay stack entry (-1 if none)
	int64_t iLastValidatedIndex = -1;

	// Output
	int64_t iNewConfirmedTick = -1;
	int64_t iNewConfirmedOffset = -1; // logical offset into snapshots ring (fast-path)
	int64_t iNewConfirmedNewSnapshotIndex = -1; // index into newSnapshots (replay)
	std::vector<std::unique_ptr<Frame>> newSnapshots;
	bool bCrcFastPath = false;

	// Desync (if any)
	int64_t iDesyncTick = -1;
	common::crc_t desyncServerCrc = 0;
	common::crc_t desyncClientCrc = 0;
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

	// Working data (set during per-coord processing)
	int64_t iTickCounter = 0;
	float fCurrentTime = 0.0f;

	// Output
	ConfirmedHumanState newConfirmedHumanState;
	bool bAnyFullReplay = false;

	// Profiling counters
	struct Profiling
	{
		int64_t iCrcValidatedFrameTicks = 0;
		int64_t iAssumedFrameTicks = 0;
		int64_t iCrcFastPathEvents = 0;
		int64_t iStatusChangeReplayTicks = 0;
		int64_t iKnockOnReplayTicks = 0;
	};
	Profiling profiling;

	// Deferred desync info (from any coord)
	int64_t iDesyncTick = -1;
	engine::GridCoord desyncCoord {};
	common::crc_t desyncServerCrc = 0;
	common::crc_t desyncClientCrc = 0;
	std::unique_ptr<Frame> pDesyncClientFrame;
};

struct ReconcileDesyncInfo
{
	bool bDesync = false;
	int64_t iDesyncTick = -1;
	engine::GridCoord desyncCoord {};
	common::crc_t desyncServerCrc = 0;
	common::crc_t desyncClientCrc = 0;
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
		mConfirmedHumanState.humanGridCoord = rState.humanGridCoord;
		mConfirmedHumanState.humanPlayerId = rState.humanPlayerId;
		mConfirmedHumanState.fPreviousHumanArmor = rState.fPreviousHumanArmor;
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
	static void Reconcile(ReconcileContext& rReconcileContext, const engine::Alignments& rAlignments);

	std::unique_ptr<common::PersistentWorker> mpWorker;
	std::unique_ptr<ReconcileContext> mpContext;
	bool mbInFlight = false;
	uint64_t muiNextGeneration = 1;
};

} // namespace game

#endif // BT_CLIENT
