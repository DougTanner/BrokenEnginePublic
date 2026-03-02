#pragma once

#include "PlayerAi.h"

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

enum class SpawnFlags : uint64_t
{
	kWaitingForHumanSpawn = 0x01,
	kRespawnRequested     = 0x02,
};
using SpawnFlags_t = common::Flags<SpawnFlags>;

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

	void Restart();
	void ChangeFrame(GameFlags_t gameFlags);
	void CreateNewFrame(GameFlags_t gameFlags);

	bool InMainMenu()
	{
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
	void HandleSubscriptionUpdatesServer(int64_t iFrame);
#endif

#ifdef BT_CLIENT
	// Network client methods
	bool IsNetworkMode() const { return mpNetworkClient != nullptr; }
	void ConnectToServer(const char* pServerAddress);
	void DisconnectFromServer();
	void PollNetworkClient();
	void CaptureLocalInput();
	void SendNetworkInput();
	void WaitForReconcile();
	void TryKickReconcile();
	void StoreExtrapolatedSnapshot(int64_t iFrame, float fCurrentTime);

	// LAN discovery
	void StartServerDiscovery();
	std::unique_ptr<engine::NetworkDiscoveryScanner> mpDiscoveryScanner;

	int64_t GetConfirmedFrame() const { return mConfirmedState.iFrame; }
	int64_t GetServerUpdateBufferSize() const { return static_cast<int64_t>(mServerUpdateBuffer.size()); }
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
	std::vector<StatusChange> DrainPendingStatusChanges() { return std::exchange(mPendingStatusChanges, {}); }
	std::vector<StatusChange> DrainPendingTransferChanges() { return std::exchange(mPendingTransferChanges, {}); }
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
	PlayerAi mPlayerAi {};
	float mfSpawnTimer = 0.0f;

	UiState meUiState = UiState::kPause;
	char mModalMessage[256] = {};

	bool mbShowImGui = false;
	SpawnFlags_t mSpawnFlags;

	engine::GridCoord mHumanGridCoord {};
	std::vector<engine::GridCoord> mActiveCoords;
	std::unordered_map<engine::GridCoord, FrameInput> mFrameInputs;

private:

	FrameInput BuildFrameInput(const Frame& rCurrentFrame, engine::GridCoord coord);

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
	std::vector<StatusChange> mPendingStatusChanges;
	std::vector<StatusChange> mPendingTransferChanges;

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
	struct ConfirmedState
	{
		int64_t iFrame = -1;
		float fCurrentTime = 0.0f;
		std::unordered_map<engine::GridCoord, std::string> serializedFrames;
		engine::GridCoord humanGridCoord {};
		player_t humanPlayerId {};
		float fPreviousHumanArmor = 0.0f;
	};

	struct PendingFullState
	{
		int64_t iFrame = -1;
		std::unordered_map<engine::GridCoord, std::string> serializedFrames;
	};

	int64_t ApplyReceivedFullStates();
	void ApplyReceivedUpdates();
	void BuildFrameInputForFrame(int64_t iServerFrame);

	struct DesyncDebugState
	{
		int64_t iFrame = -1;
		engine::GridCoord coord {};
		std::unique_ptr<Frame> pClientFrame;
	};
	DesyncDebugState mDesyncDebugState;

	void CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, int64_t iFrame, engine::GridCoord coord);

	std::unique_ptr<engine::NetworkClient> mpNetworkClient;
	std::map<int64_t, engine::ReceivedUpdate> mServerUpdateBuffer;
	ConfirmedState mConfirmedState;
	PendingFullState mPendingFullState;
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mServerTransferStatusChanges;
	std::unordered_map<engine::GridCoord, std::vector<PlayerInput>> mLastServerPlayerInputs;
	PlayerInput mLocalPlayerInput {};
	int64_t miLatestServerFrame = -1;

	struct ExtrapolatedSnapshot
	{
		std::unordered_map<engine::GridCoord, common::crc_t> coordCrcs;
		std::unordered_map<engine::GridCoord, std::string> serializedFrames;
		engine::GridCoord humanGridCoord {};
		player_t humanPlayerId {};
		float fPreviousHumanArmor = 0.0f;
		float fCurrentTime = 0.0f;
	};
	std::unordered_map<int64_t, ExtrapolatedSnapshot> mExtrapolatedSnapshots;

	struct ReconcileContext
	{
		// Input (snapshot from main thread before Wake)
		const ConfirmedState* pConfirmedState = nullptr;
		PendingFullState pendingFullState;
		std::map<int64_t, engine::ReceivedUpdate> serverUpdates; // Consecutive subset
		std::unordered_map<engine::GridCoord, std::vector<PlayerInput>> lastServerPlayerInputs;
		std::unordered_map<int64_t, ExtrapolatedSnapshot> extrapolatedSnapshots; // For CRC fast-path
		uint16_t uiNextFrameId = 0;
		int64_t iTargetFrame = 0; // Frame counter at kick time; worker predicts up to this

		// Working data (owned by worker during execution)
		std::unordered_map<engine::GridCoord, std::unique_ptr<Frame>> currentFrames;
		std::unordered_map<engine::GridCoord, std::unique_ptr<Frame>> nextFrames;
		std::unordered_map<engine::GridCoord, FrameInput> frameInputs;
		std::unordered_map<engine::GridCoord, std::vector<StatusChange>> serverTransferStatusChanges;
		std::vector<engine::GridCoord> activeCoords;
		engine::GridCoord humanGridCoord {};
		player_t humanPlayerId {};
		float fPreviousHumanArmor = 0.0f;
		int64_t iFrameCounter = 0;
		float fCurrentTime = 0.0f;
		engine::alignment_t playerAlignment {};

		// Output (read by main thread after Wait)
		ConfirmedState newConfirmedState;
		std::unordered_map<engine::GridCoord, std::vector<PlayerInput>> newLastServerPlayerInputs;
		int64_t iLastProcessedServerFrame = -1;
		bool bPendingConsumed = false;
		int64_t iPendingConsumedFrame = -1;
		bool bCrcFastPathHandledAll = false; // True if CRC matched everything (no replay needed)
		bool bNoChange = false; // True if no server data available (confirmed unchanged)

		// DT: TEMP diagnostic - main thread's PlayersInterpolate ptr for corruption monitoring
		PlayersInterpolate* pDiagMainPI = nullptr;

		// Deferred desync info (network ops not thread-safe, deferred to main thread)
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
	static void ReconcileEnsureNextFrames(ReconcileContext& rReconcileContext);
	static void ReconcileBuildFrameInput(ReconcileContext& rReconcileContext, int64_t iServerFrame);
	static void ReconcileHarvestTransfers(ReconcileContext& rReconcileContext);
	static void ReconcileComputeActiveCoords(ReconcileContext& rReconcileContext);
#endif
};

inline Game* gpGame = nullptr;

} // namespace game
