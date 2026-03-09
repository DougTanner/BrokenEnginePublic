#pragma once

#include "Data/Audio.h"

namespace engine
{

struct RawInput;

}

namespace game
{

#include "Version.h"

inline constexpr std::string_view kGameName = "Broken Engine Sandbox";
inline constexpr int64_t kiDesiredCoordSlots = 5; // 4 subscriptions + 1 spare for grid transitions

enum class UiState
{
	kNone,

	kGraphics,
	kModal,
	kPause,
	kSound,

	kTweaks,
};

struct ReplayMeta
{
	static constexpr int64_t kiVersion = 1;
	engine::GridCoord humanGridCoord {};
	int64_t iHumanPlayerIdValue = 0;
	float fPreviousHumanArmor = 0.0f;
};

class Game : public engine::GameBase
{
public:

	Game();
	~Game() override;

	void Reset() override;
	bool ShouldTrapCursor() override;
	bool ShouldUseCrosshair() override;

	void ChangeFrame(GameFlags_t gameFlags);
	void CreateNewFrame(GameFlags_t gameFlags);

	bool InMainMenu()
	{
		if (!mCoordFrames.contains(mHumanGridCoord))
		{
			return false;
		}
		return CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kMainMenu;
	}

	void ProcessMenuInput(const MenuInput& rMenuInput) override;

	// Multi-frame grid
	void ComputeActiveSet();
	void EnsureNextFrames();
	void BuildFrameInputs();
	void CreateFrameAtCoord(engine::GridCoord coord);
	void HarvestTransfers();

#if defined(BT_SERVER)
	// Server-mode methods
	void ComputeActiveSetServer();
	void BuildFrameInputsServer();
	void ProcessSpawnRequestsServer();
	void HarvestTransfersServer();
	void BroadcastStatusChangesServer(int64_t iTick);
	void HandleDisconnectsServer();
	void HandleNewClientsServer();
	void FinalizeNewClientsServer(int64_t iTick);
	void DetectPlayerDeathsServer();
	void HandleSubscriptionUpdatesServer(int64_t iTick);
	void RefreshPreSpawnSnapshot();
#endif

#if defined(BT_CLIENT)
	// Network client methods
	bool IsNetworkMode() const { return mpNetworkClient != nullptr; }
	void ConnectToServer(const char* pServerAddress);
	void DisconnectFromServer();
	void PollNetworkClient();
	void WaitForReconcile();
	void TryKickReconcile();
	void UpdateSubscriptions();
	bool IsExtrapolating() const
	{
		if (!IsNetworkMode()) return false;
		for (const auto& [rCoord, rFrame] : mCoordFrames)
		{
			if (rFrame.iConfirmedTick >= 0) return true;
		}
		return false;
	}
	void PrepareExtrapolationTick(const std::vector<engine::GridCoord>& rActiveCoords);
	void BuildExtrapolationFrameRef(const engine::GridCoord& rCoord, game::Frame*& rpNext, game::Frame*& rpCurrent);
	void RecordExtrapolationSnapshot(const std::vector<engine::GridCoord>& rActiveCoords, int64_t iTick);
	Frame* GetSnapshotFrame(engine::GridCoord coord) const;
	void TrySubscribeNext();

	// LAN discovery
	void StartServerDiscovery();
	std::unique_ptr<engine::NetworkDiscoveryScanner> mpDiscoveryScanner;

	int64_t GetConfirmedTick() const;
	int64_t GetServerUpdateBufferSize() const;
	int64_t GetDesyncTick() const { return mDesyncDebugState.iTick; }

	std::chrono::nanoseconds ComputeClockCorrectionNs(int64_t iPreReconcileTick);
	int64_t miClockError = 0;
#endif

	// Human player tracking
	player_t HumanPlayerId() const { return mHumanPlayerId; }
	float PreviousHumanArmor() const { return mfPreviousHumanArmor; }
	bool IsHumanPlayer(player_t id) const { return id.IsValid() && id == mHumanPlayerId; }
	void RestoreReplayMeta(const ReplayMeta& rMeta);
	std::optional<int64_t> HumanPlayerIndex(const PlayersInterpolate& rPlayers) const;
	void ApplyTransferStatusChanges(Frame& rFrame, FrameInput& rFrameInput);

	XMVECTOR GetHumanPlayerPosition() const;

	static void SaveSoundSettings();
	static void LoadSoundSettings();
	static void ResetSoundSettings();

#if defined(BT_CLIENT)
	common::crc_t GetNextMusicTrack();
#endif

#if defined(BT_CLIENT)
	Camera mCamera {};
#endif

	UiState meUiState = UiState::kPause;
	char mModalMessage[256] = {};

	bool mbShowImGui = false;

	engine::GridCoord mHumanGridCoord {};
	std::vector<engine::GridCoord> mActiveCoords;
	std::unordered_map<engine::GridCoord, FrameInput> mFrameInputs;

private:

	std::filesystem::path QuicksaveFile() override
	{
		return std::filesystem::path("Quicksave.save");
	}

	std::filesystem::path ReplayFile() override
	{
		return std::filesystem::path("F7.replay");
	}

#if defined(BT_CLIENT)
	std::vector<common::crc_t> mMenuMusicPlaylist {data::kAudioMusicdoodlewavCrc, data::kAudioMusicMandatoryOvertimewavCrc, data::kAudioMusicsong18wavCrc, data::kAudioMusicTyhosibzzzzwavCrc};
	std::vector<common::crc_t> mGameMusicPlaylist {data::kAudioMusicS31UnexpectedTroublewavCrc, data::kAudioMusicS31HighAlertwavCrc, data::kAudioMusicS31OnPatrolwavCrc, data::kAudioMusicS31TheGearsofProgresswavCrc};

	int64_t miMenuMusicIndex = 0;
	int64_t miGameMusicIndex = 0;
#endif

	player_t mHumanPlayerId {};
	float mfPreviousHumanArmor = 0.0f;
	engine::alignment_t mPlayerAlignment {};
	engine::alignment_t mEnemyAlignment {};
	engine::Alignments mAlignments {};

#if defined(BT_SERVER)
	struct ClientSpawnInfo
	{
		int64_t iClientId = 0;
		engine::GridCoord spawnCoord {};
	};
	struct PendingPlayerDestroy
	{
		engine::GridCoord coord {};
		player_t playerId {};
	};
	std::vector<PendingPlayerDestroy> mPendingPlayerDestroys;

	std::vector<ClientSpawnInfo> mClientsWaitingForSpawn;
	std::unordered_set<int64_t> mDeadClientIds;
	std::vector<player_t> mPreSpawnPlayerIds;
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mBroadcastSpawns;

	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mBroadcastTransfers;

	struct SubscriptionUpdate
	{
		int64_t iClientId = 0;
		engine::GridCoord newCoord {};
		player_t newPlayerId {};
	};
	std::vector<SubscriptionUpdate> mPendingSubscriptionUpdates;
#endif

#if defined(BT_CLIENT)
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

	std::unique_ptr<engine::NetworkClient> mpNetworkClient;
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
#endif
};

inline Game* gpGame = nullptr;

void SpawnTransfer(Frame& rFrame, StatusChangeType eType, const TransferData& data, engine::alignment_t playerAlignment);

} // namespace game
