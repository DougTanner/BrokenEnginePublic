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

struct CoordScratch
{
	std::vector<Frame*> replayStack;
	int64_t iReplayStackCount = 0;
	int64_t iReplayWriteHead = 0;
	int64_t iReplayWriteCount = 0;
	int64_t iLastValidatedIndex = -1;
	int64_t iNewConfirmedTick = -1;
	int64_t iNewConfirmedOffset = -1;
	int64_t iOutputCount = 0;
	bool bCrcFastPath = false;
	bool bReplayed = false;
	bool bShrunkRollback = false;
	bool bReSimOccurred = false;
	bool bSuppressRepeatLogs = false;
	int64_t iPreReconcileTailTick = -1;
	int64_t iTickCounter = 0;
	float fCurrentTime = 0.0f;
	ReconcileProfiling profiling;

	int64_t iDesyncTick = -1;
	common::crc_t desyncExpectedCrc = 0;
	common::crc_t desyncActualCrc = 0;
	std::unique_ptr<Frame> pDesyncClientFrame;
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
};

} // namespace game

#endif // BT_CLIENT
