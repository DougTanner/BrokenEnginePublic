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
	void SendNetworkInput();
	void Reconcile();

	// LAN discovery
	void StartServerDiscovery();
	std::unique_ptr<engine::NetworkDiscoveryScanner> mpDiscoveryScanner;
#endif

	// Human player tracking
	player_t HumanPlayerId() const { return mHumanPlayerId; }
	float PreviousHumanArmor() const { return mfPreviousHumanArmor; }
	bool IsHumanPlayer(player_t id) const { return id.IsValid() && id == mHumanPlayerId; }
	void RestoreReplayMeta(const ReplayMeta& rMeta);
	int64_t HumanPlayerIndex(const PlayersInterpolate& rPlayers) const;
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

	std::unique_ptr<engine::NetworkClient> mpNetworkClient;
	std::map<int64_t, engine::ReceivedUpdate> mServerUpdateBuffer;
	ConfirmedState mConfirmedState;
	PendingFullState mPendingFullState;
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mServerTransferStatusChanges;
	std::unordered_map<engine::GridCoord, std::vector<PlayerInput>> mLastServerPlayerInputs;
	PlayerInput mLocalPlayerInput {};
#endif
};

inline Game* gpGame = nullptr;

} // namespace game
