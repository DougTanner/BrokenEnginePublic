#pragma once

#include "GameBase.h"

#include "PlayerAi.h"
#include "Frame/Frame.h"
#ifdef BT_CLIENT
#include "Graphics/Camera.h"
#endif
#include "Input/Input.h"

#include "Data/Audio.h"

namespace engine
{

class NetworkClient;
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
	void ValidateServerCrcs();
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
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mBroadcastStatusChanges;

	struct SubscriptionUpdate
	{
		int64_t iClientId = 0;
		engine::GridCoord newCoord {};
	};
	std::vector<SubscriptionUpdate> mPendingSubscriptionUpdates;
#endif

#ifdef BT_CLIENT
	void ApplyReceivedFullStates();
	void ApplyReceivedUpdates();

	std::unique_ptr<engine::NetworkClient> mpNetworkClient;
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mServerStatusChanges;
	std::unordered_map<engine::GridCoord, common::crc_t> mServerCrcs;
#endif
};

inline Game* gpGame = nullptr;

} // namespace game
