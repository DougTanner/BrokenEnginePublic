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
	virtual ~Game();

	virtual void Reset() override;
	virtual bool ShouldUpdateFrame() override;
	virtual bool ShouldTrapCursor() override;
	virtual bool ShouldUseCrosshair() override;

	void ChangeFrame(GameFlags_t gameFlags);
	void CreateNewFrame(GameFlags_t gameFlags);

	bool InMainMenu()
	{
		if (!mCurrentFrames.contains(mHumanGridCoord))
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

#ifdef BT_SERVER
	// Server-mode methods
	void ComputeActiveSetServer();
	void BuildFrameInputsServer();
	void ProcessSpawnRequestsServer();
	void HarvestTransfersServer();
	void BroadcastStatusChangesServer(int64_t iFrame);
	void HandleDisconnectsServer();
	void HandleNewClientsServer();
	void FinalizeNewClientsServer(int64_t iFrame);
	void DetectPlayerDeathsServer();
	void HandleSubscriptionUpdatesServer(int64_t iFrame);
	void RefreshPreSpawnSnapshot();
#endif

#ifdef BT_CLIENT
	// Network client methods
	bool IsNetworkMode() const { return mpNetworkClient != nullptr; }
	void ConnectToServer(const char* pServerAddress);
	void DisconnectFromServer();
	void PollNetworkClient();
	void WaitForReconcile();
	void TryKickReconcile();
	void StoreExtrapolatedSnapshot(int64_t iFrame);
	void UpdateSubscriptions();
	void TrySubscribeNext();

	// LAN discovery
	void StartServerDiscovery();
	std::unique_ptr<engine::NetworkDiscoveryScanner> mpDiscoveryScanner;

	int64_t GetConfirmedFrame() const;
	int64_t GetServerUpdateBufferSize() const;
	int64_t GetDesyncFrame() const { return mDesyncDebugState.iFrame; }

	std::chrono::nanoseconds ComputeClockCorrectionNs(int64_t iPreReconcileFrame);
	int64_t miClockError = 0;
#endif

	// Human player tracking
	player_t HumanPlayerId() const { return mHumanPlayerId; }
	float PreviousHumanArmor() const { return mfPreviousHumanArmor; }
	bool IsHumanPlayer(player_t id) const { return id.IsValid() && id == mHumanPlayerId; }
	void RestoreReplayMeta(const ReplayMeta& rMeta);
	std::optional<int64_t> HumanPlayerIndex(const PlayersInterpolate& rPlayers) const;
	void ApplyTransferStatusChanges(Frame& rFrame, FrameInput& rFrameInput);

	static void SaveSoundSettings();
	static void LoadSoundSettings();
	static void ResetSoundSettings();

#ifdef BT_CLIENT
	common::crc_t GetNextMusicTrack();
#endif

#ifdef BT_CLIENT
	Camera mCamera {};
#endif

	UiState meUiState = UiState::kPause;
	char mModalMessage[256] = {};

	bool mbShowImGui = false;

	engine::GridCoord mHumanGridCoord {};
	std::vector<engine::GridCoord> mActiveCoords;
	std::unordered_map<engine::GridCoord, FrameInput> mFrameInputs;

private:

	virtual std::filesystem::path QuicksaveFile() override
	{
		return std::filesystem::path("Quicksave.save");
	}

	virtual std::filesystem::path ReplayFile() override
	{
		return std::filesystem::path("F7.replay");
	}

#ifdef BT_CLIENT
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

#ifdef BT_SERVER
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

#ifdef BT_CLIENT
	// Per-coord extrapolated snapshot for CRC fast-path
	struct CoordExtrapolatedSnapshot
	{
		common::crc_t crc = 0;
		common::crc_t inputCrc = 0;
		std::string serializedFrame;
	};

	// Per-coord reconciliation state (replaces unified ConfirmedState + mServerUpdateBuffer + mExtrapolatedSnapshots)
	struct CoordReconcileState
	{
		int64_t iConfirmedFrame = -1;
		std::string confirmedSerializedFrame;

		struct CoordServerUpdate
		{
			common::crc_t serverCrc = 0;
			common::crc_t inputCrc = 0;
			std::vector<StatusChange> statusChanges;
		};
		std::map<int64_t, CoordServerUpdate> serverUpdates;

		std::map<int64_t, CoordExtrapolatedSnapshot> extrapolatedSnapshots;

		// Pending full state from subscription
		std::optional<std::pair<int64_t, std::string>> pendingFullState;

		uint64_t uiGeneration = 0;
	};

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
		int64_t iFrame = -1;
		engine::GridCoord coord {};
		std::unique_ptr<Frame> pClientFrame;
	};
	DesyncDebugState mDesyncDebugState;

	void CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, int64_t iFrame, engine::GridCoord coord);

	std::unique_ptr<engine::NetworkClient> mpNetworkClient;
	std::unordered_map<engine::GridCoord, CoordReconcileState> mCoordReconcileStates;
	uint64_t muiNextReconcileGeneration = 1;
	ConfirmedHumanState mConfirmedHumanState;
	int64_t miLatestServerFrame = -1;

	// Subscription management
	std::vector<engine::GridCoord> mSubscriptionQueue;

	// Per-coord reconcile work item
	struct CoordReconcileWork
	{
		engine::GridCoord coord {};
		uint64_t uiGeneration = 0;

		// Input
		int64_t iConfirmedFrame = -1;
		std::string confirmedSerializedFrame;
		std::map<int64_t, CoordReconcileState::CoordServerUpdate> serverUpdates;
		std::map<int64_t, CoordExtrapolatedSnapshot> extrapolatedSnapshots;
		std::optional<std::pair<int64_t, std::string>> pendingFullState;

		// Output
		int64_t iNewConfirmedFrame = -1;
		std::string newConfirmedSerializedFrame;
		std::map<int64_t, CoordExtrapolatedSnapshot> newExtrapolatedSnapshots;
		bool bCrcFastPath = false;

		// Desync (if any)
		int64_t iDesyncFrame = -1;
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
		int64_t iTargetFrame = 0;
		engine::alignment_t playerAlignment {};

		// Working data (owned by worker during execution)
		std::unordered_map<engine::GridCoord, std::unique_ptr<Frame>> currentFrames;
		std::unordered_map<engine::GridCoord, std::unique_ptr<Frame>> nextFrames;
		std::unordered_map<engine::GridCoord, FrameInput> frameInputs;
		std::vector<engine::GridCoord> activeCoords;
		engine::GridCoord humanGridCoord {};
		player_t humanPlayerId {};
		float fPreviousHumanArmor = 0.0f;
		int64_t iFrameCounter = 0;
		float fCurrentTime = 0.0f;

		// Output
		ConfirmedHumanState newConfirmedHumanState;
		bool bCrcFastPathHandledAll = false;

		// Deferred desync info (from any coord)
		int64_t iDesyncFrame = -1;
		engine::GridCoord desyncCoord {};
		common::crc_t desyncServerCrc = 0;
		common::crc_t desyncClientCrc = 0;
		std::unique_ptr<Frame> pDesyncClientFrame;
	};

	std::unique_ptr<common::PersistentWorker> mpReconcileWorker;
	std::unique_ptr<ReconcileContext> mpReconcileContext;
	bool mbReconcileInFlight = false;

	void KickReconcile();
	static void Reconcile(ReconcileContext& rReconcileContext, const engine::Alignments& rAlignments);
	void ApplyReconcileResult();
	static std::pair<bool, int64_t> ReconcileCrcFastPath(ReconcileContext& rReconcileContext);
	static void ReconcileRollback(ReconcileContext& rReconcileContext, int64_t iMinConfirmedFrame);
	static int64_t ReconcileFindReplayRange(ReconcileContext& rReconcileContext, int64_t iMinConfirmedFrame);
	static void ReconcileReplay(ReconcileContext& rReconcileContext, int64_t iMinConfirmedFrame, int64_t iMaxConsecutive, const std::unordered_map<engine::GridCoord, size_t>& rCoordWorkIndex);
	static void ReconcileCatchUp(ReconcileContext& rReconcileContext, int64_t iMinConfirmedFrame);
	static void ReconcileEnsureNextFrames(ReconcileContext& rReconcileContext);
	static void ReconcileBuildFrameInput(ReconcileContext& rReconcileContext, int64_t iServerFrame, const std::unordered_map<engine::GridCoord, CoordReconcileState::CoordServerUpdate>& rCoordUpdates);
	static void ReconcileRunPhysics(ReconcileContext& rReconcileContext);
	static void ReconcileComputeActiveCoords(ReconcileContext& rReconcileContext);
	static void ReconcileInjectPendingFullState(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork);
	static void ReconcilePruneInactiveFrames(ReconcileContext& rReconcileContext);
#endif
};

inline Game* gpGame = nullptr;

void SpawnTransfer(Frame& rFrame, StatusChangeType eType, const TransferData& data, engine::alignment_t playerAlignment);

} // namespace game
