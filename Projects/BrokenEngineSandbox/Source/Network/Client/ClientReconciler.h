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
	int64_t iTargetTick = 0;
};

enum class ReconcileScratchFlags : uint8_t
{
	kCrcFastPath        = 0x01,
	kReplayed           = 0x02,
	kShrunkRollback     = 0x04,
	kReSimOccurred      = 0x08,
	kSuppressRepeatLogs = 0x10,
};

struct RingLayout
{
	int64_t iHead = -1;
	int64_t iCount = 0;
	int64_t iConfirmedInner = 0;
};

constexpr RingLayout ComputeRetention(int64_t iHeadPhysical, int64_t iSnapshotCount, int64_t iConfirmedIndex)
{
	int64_t iHeadAdvance = std::max<int64_t>(0, iConfirmedIndex - engine::kiRenderBehindTicks);
	return {
		.iHead = SnapshotIndex(iHeadPhysical, iHeadAdvance),
		.iCount = iSnapshotCount - iHeadAdvance,
		.iConfirmedInner = iConfirmedIndex - iHeadAdvance,
	};
}

struct CoordScratch
{
	std::vector<Frame*> replayStack;
	int64_t iReplayStackCount = 0;
	int64_t iReplayWriteHead = 0;
	int64_t iReplayWriteCount = 0;
	int64_t iLastValidatedIndex = -1;
	int64_t iNewConfirmedTick = -1;
	RingLayout outputLayout;
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
		outputLayout = {};
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

private:

	ConfirmedClientState mConfirmedClientState;
	std::vector<CoordWork> mWorks;
	float mfLastLoggedVisualErrorDelta = 0.0f;
	int64_t miLastVisualErrorLogTick = -1000;
};

} // namespace game

#endif // BT_CLIENT
