#pragma once

#if defined(BT_CLIENT)

#include "Network/ClientNetwork/ClientSessionBase.h"

namespace game
{

struct Frame;

inline int64_t SnapshotIndex(int64_t iHead, int64_t iLogical)
{
	return (iHead + iLogical) % engine::kiTickRate;
}

class ClientSession : public engine::ClientSessionBase
{
public:

	ClientSession();
	~ClientSession() override;

	// Connection
	bool IsNetworkMode() const { return mpClientNetwork != nullptr; }
	void ConnectToServer(const char* pServerAddress);
	void DisconnectFromServer();
	void StartServerDiscovery();

	// Main-loop integration
	void PollNetwork();
	void PollAndReconcile();
	void PostTick();
	void PostRender();

	// Reconciliation
	void WaitForReconcile();
	void TryKickReconcile();

	// Subscriptions
	void UpdateSubscriptions();
	void TrySubscribeNext();

	// Extrapolation
	bool IsExtrapolating() const;
	void PrepareExtrapolationTick(const std::vector<engine::GridCoord>& rActiveCoords);
	void BuildExtrapolationFrameRef(const engine::GridCoord& rCoord, Frame*& rpNext, Frame*& rpCurrent);
	void RecordExtrapolationSnapshot(const std::vector<engine::GridCoord>& rActiveCoords, int64_t iTick);
	Frame* GetSnapshotFrame(engine::GridCoord coord) const;

	// Queries
	int64_t GetConfirmedTick() const;
	int64_t GetServerUpdateBufferSize() const;
	int64_t GetDesyncTick() const { return mDesyncDebugState.iTick; }

	// Clock correction
	std::chrono::nanoseconds ComputeClockCorrectionNs(int64_t iPreReconcileTick);
	int64_t miClockError = 0;

private:

	// Confirmed human tracking state (global, not per-coord)
	struct ConfirmedHumanState
	{
		engine::GridCoord humanGridCoord {};
		player_t humanPlayerId {};
		float fPreviousHumanArmor = 0.0f;
		float fCurrentTime = 0.0f;
	};

	void ApplyReceivedFullStates();
	void ApplyReceivedUpdates();

	struct DesyncDebugState
	{
		int64_t iTick = -1;
		engine::GridCoord coord {};
		std::unique_ptr<Frame> pClientFrame;
	};
	DesyncDebugState mDesyncDebugState;

	void CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, int64_t iTick, engine::GridCoord coord);

	uint64_t muiNextReconcileGeneration = 1;
	ConfirmedHumanState mConfirmedHumanState;
	int64_t miLatestServerTick = -1;

	// Subscription management
	std::vector<engine::GridCoord> mSubscriptionQueue;

	// Per-coord reconcile work item
	struct CoordReconcileWork
	{
		engine::GridCoord coord {};
		uint64_t uiGeneration = 0;

		// Input
		int64_t iConfirmedTick = -1;
		int64_t iConfirmedOffset = -1;
		int64_t iSnapshotHead = 0;
		std::map<int64_t, engine::CoordFrames::CoordServerUpdate> serverUpdates;
		std::array<std::unique_ptr<Frame>, engine::kiTickRate> snapshots {};
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

		// Working data (owned by worker during execution)
		std::unordered_map<engine::GridCoord, size_t> coordWorkIndex;
		std::unordered_map<engine::GridCoord, FrameInput> frameInputs;
		std::vector<engine::GridCoord> activeCoords;
		engine::GridCoord humanGridCoord {};
		player_t humanPlayerId {};
		float fPreviousHumanArmor = 0.0f;
		int64_t iTickCounter = 0;
		float fCurrentTime = 0.0f;

		// Output
		ConfirmedHumanState newConfirmedHumanState;
		bool bCrcFastPathHandledAll = false;

		// Profiling counters
		int64_t iCrcValidatedFrameTicks = 0;
		int64_t iAssumedFrameTicks = 0;
		int64_t iCrcFastPathEvents = 0;
		int64_t iStatusChangeReplayTicks = 0;
		int64_t iKnockOnReplayTicks = 0;

		// Deferred desync info (from any coord)
		int64_t iDesyncTick = -1;
		engine::GridCoord desyncCoord {};
		common::crc_t desyncServerCrc = 0;
		common::crc_t desyncClientCrc = 0;
		std::unique_ptr<Frame> pDesyncClientFrame;
	};

	std::unique_ptr<common::PersistentWorker> mpReconcileWorker;
	std::unique_ptr<ReconcileContext> mpReconcileContext;
	bool mbReconcileInFlight = false;
	bool mbReconcileHasNewData = false;

	void KickReconcile();
	static void Reconcile(ReconcileContext& rReconcileContext, const engine::Alignments& rAlignments);
	void ApplyReconcileResult();
	static std::pair<bool, int64_t> ReconcileCrcFastPath(ReconcileContext& rReconcileContext);
	static void ReconcileRollback(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick);
	static int64_t ReconcileFindReplayRange(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick);
	static void ReconcileReplay(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick, int64_t iMaxConsecutive);
	static void ReconcileCatchUp(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick);
	static void ReconcileBuildFrameInput(ReconcileContext& rReconcileContext, int64_t iServerTick, const std::unordered_map<engine::GridCoord, engine::CoordFrames::CoordServerUpdate>& rCoordUpdates);
	static void ReconcileRunTick(ReconcileContext& rReconcileContext);
	static void ReconcileComputeActiveCoords(ReconcileContext& rReconcileContext);
	static void ReconcileInjectPendingFullState(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork);
	static void ReconcilePruneInactiveFrames(ReconcileContext& rReconcileContext);
};

inline ClientSession* gpClientSession = nullptr;

} // namespace game

#endif // BT_CLIENT
