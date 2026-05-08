#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;
struct FrameInput;

using engine::SnapshotIndex;

struct ConfirmedClientState
{
	engine::GridCoord clientGridCoord {};
	engine::global_id_t clientGlobalPlayerId {};
	float fPreviousClientArmor = 0.0f;
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

struct ReconcileInputs
{
	ConfirmedClientState confirmedClientState;
	uint16_t uiNextFrameId = 0;
	int64_t iTargetTick = 0;
	int64_t iJitterUs = 0;
	engine::alignment_t playerAlignment {};
	engine::Alignments alignments;
};

enum class ReconcileScratchFlags : uint8_t
{
	kCrcFastPath        = 0x01,
	kReplayed           = 0x02,
	kShrunkRollback     = 0x04,
	kReSimOccurred      = 0x08,
	kSuppressRepeatLogs = 0x10,
};

struct CoordScratch
{
	std::vector<Frame*> replayStack;
	int64_t iReplayStackCount = 0;
	int64_t iReplayWriteHead = 0;
	int64_t iReplayWriteCount = 0;
	int64_t iLastValidatedIndex = -1;
	int64_t iNewConfirmedTick = -1;
	int64_t iNewConfirmedOffset = -1;       // physical ring index of the new head (NOT necessarily confirmed)
	int64_t iNewConfirmedInnerOffset = 0;   // offset from new head to the confirmed frame; equals kiRenderBehindTicks when render-retention is applied
	int64_t iOutputCount = 0;
	common::Flags<ReconcileScratchFlags> flags;
	int64_t iPreReconcileTailTick = -1;
	ReconcileProfiling profiling;

	int64_t iDesyncTick = -1;
	common::crc_t desyncExpectedCrc = 0;
	common::crc_t desyncActualCrc = 0;
	std::unique_ptr<Frame> pDesyncClientFrame;

	// Reset every field to its declared default while preserving replayStack's allocated capacity.
	void Reset()
	{
		replayStack.clear();
		iReplayStackCount = 0;
		iReplayWriteHead = 0;
		iReplayWriteCount = 0;
		iLastValidatedIndex = -1;
		iNewConfirmedTick = -1;
		iNewConfirmedOffset = -1;
		iNewConfirmedInnerOffset = 0;
		iOutputCount = 0;
		flags.ClearAll();
		iPreReconcileTailTick = -1;
		profiling = {};
		iDesyncTick = -1;
		desyncExpectedCrc = 0;
		desyncActualCrc = 0;
		pDesyncClientFrame.reset();
	}
};

struct CoordWork
{
	engine::GridCoord coord {};
	engine::CoordFrames* pFrames = nullptr;
	CoordScratch scratch;
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

	ClientReconciler() = default;
	~ClientReconciler() = default;

	ReconcileDesyncInfo Run();
	void Reset();
	uint64_t NextGeneration() { return muiNextGeneration++; }

	void InitConfirmedClientState(const ConfirmedClientState& rState)
	{
		if (mConfirmedClientState.fCurrentTime == 0.0f)
		{
			mConfirmedClientState.fCurrentTime = rState.fCurrentTime;
		}
	}

private:

	ConfirmedClientState mConfirmedClientState;
	std::vector<CoordWork> mWorks;
	uint64_t muiNextGeneration = 1;
	float mfLastLoggedVisualErrorDelta = 0.0f;
	int64_t miLastVisualErrorLogTick = -1000;
};

} // namespace game

#endif // BT_CLIENT
