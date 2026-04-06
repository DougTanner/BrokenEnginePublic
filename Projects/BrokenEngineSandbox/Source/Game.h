#pragma once

#include "Data/Audio.h"

#if defined(BT_SERVER)
#include "Network/ServerSession.h"
#endif
#if defined(BT_CLIENT)
#include "Network/ClientSession.h"
#endif

namespace engine
{

struct RawInput;

}

namespace game
{

#include "Version.h"

#if defined(BT_CLIENT)
inline constexpr std::string_view kGameName = "Broken Engine Sandbox";
#else
inline constexpr std::string_view kGameName = "Broken Engine Sandbox Server";
#endif
inline constexpr int64_t kiDesiredCoordSlots = 8; // 4 subscriptions + 3 sticky + 1 spare for grid transitions

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
	engine::GridCoord clientGridCoord {};
	int64_t iClientPlayerIdValue = 0;
	float fPreviousClientArmor = 0.0f;
};

class Game : public engine::GameBase
{
public:

	Game();
	~Game() override;

	void Reset() override;
	bool ShouldTrapCursor() override;
#if defined(BT_CLIENT)
	bool ShouldUseCrosshair() override;
#endif

	void ChangeFrame(GameFlags_t gameFlags);
	void CreateNewFrame(GameFlags_t gameFlags);

	bool InMainMenu()
	{
		return mGameFlags & engine::GameFlags::kMainMenu;
	}

	void ProcessMenuInput(const MenuInput& rMenuInput) override;

	// Multi-frame grid
	void ComputeActiveSet();
	void EnsureNextFrames();
	void BuildFrameInputs();
	void CreateFrameAtCoord(engine::GridCoord coord);
	void HarvestTransfers();

#if defined(BT_SERVER)
	std::unique_ptr<ServerSession> mpServerSession;
#endif
#if defined(BT_CLIENT)
	std::unique_ptr<ClientSession> mpClientSession;
#endif

	// Client player tracking (multi-player per client)
	engine::global_player_t ClientPlayerId() const;
	float PreviousClientArmor() const { return mfPreviousClientArmor; }
	bool IsClientPlayer(engine::global_player_t id) const;
	void AddClientPlayer(engine::global_player_t id, engine::GridCoord coord);
	void RemoveClientPlayer(engine::global_player_t id);
	int64_t PlayerCount() const;
	int64_t FocusedPlayerIndex() const;
	void FocusNext();
	void FocusPrev();
	bool CanFocusNext() const;
	bool CanFocusPrev() const;
	void RestoreReplayMeta(const ReplayMeta& rMeta);
	std::optional<int64_t> ClientPlayerIndex(const PlayersPostRender& rPlayers) const;
	void ApplyTransferStatusChanges(Frame& rFrame, FrameInput& rFrameInput);

	XMVECTOR GetClientPlayerPosition() const;

#if defined(BT_CLIENT)
	static void SaveSoundSettings();
	static void LoadSoundSettings();
	static void ResetSoundSettings();

	static void SaveTweaksSettings();
	static void LoadTweaksSettings();
#endif

#if defined(BT_CLIENT)
	common::crc_t GetNextMusicTrack();
#endif

#if defined(BT_CLIENT)
	Camera mCamera {};
	XMVECTOR mVecVisualErrorOffset {};
	engine::NetworkUiControl<bool> mWeaponModeToggle {};
	engine::NetworkUiControl<float> mNavigationDelayControl {};
	engine::NetworkUiControl<int64_t> mSpawnToggle {};

	static constexpr float kfVisualErrorDecayRate = 15.0f;
	static constexpr float kfVisualErrorMaxDistance = 5.0f;
	static constexpr float kfVisualErrorMinDistance = 0.001f;
#endif

	UiState meUiState = UiState::kPause;
	char mModalMessage[256] = {};

	bool mbShowImGui = false;

	engine::GridCoord mClientGridCoord {};
	int32_t miQuadrantDirX = 0;
	int32_t miQuadrantDirY = 0;
	std::vector<engine::GridCoord> mActiveCoords;
	std::unordered_map<engine::GridCoord, FrameInput> mFrameInputs;

private:

	std::filesystem::path QuicksaveFile() override
	{
		return std::filesystem::path("ServerQuicksave.save");
	}

	std::filesystem::path ReplayFile() override
	{
		return std::filesystem::path("F7.replay");
	}

#if defined(BT_CLIENT)
	static constexpr common::crc_t mMenuMusicPlaylist[4] {data::kAudioMusicdoodlewavCrc, data::kAudioMusicMandatoryOvertimewavCrc, data::kAudioMusicsong18wavCrc, data::kAudioMusicTyhosibzzzzwavCrc};
	static constexpr common::crc_t mGameMusicPlaylist[4] {data::kAudioMusicS31UnexpectedTroublewavCrc, data::kAudioMusicS31HighAlertwavCrc, data::kAudioMusicS31OnPatrolwavCrc, data::kAudioMusicS31TheGearsofProgresswavCrc};

	int64_t miMenuMusicIndex = 0;
	int64_t miGameMusicIndex = 0;
#endif

public:
	std::vector<engine::global_player_t> mClientPlayerIds;
	std::vector<engine::GridCoord> mClientPlayerCoords;
	int64_t miFocusedPlayerIndex = -1;
private:
	float mfPreviousClientArmor = 0.0f;
	engine::alignment_t mPlayerAlignment {};
	engine::alignment_t mEnemyAlignment {};
	engine::Alignments mAlignments {};

public:
	engine::alignment_t PlayerAlignment() const { return mPlayerAlignment; }
	void SetPreviousClientArmor(float fArmor) { mfPreviousClientArmor = fArmor; }
	const engine::Alignments& Alignments() const { return mAlignments; }

#if defined(BT_CLIENT)
	void StartMenuMusic()
	{
		miMenuMusicIndex = 0;
		engine::gpAudioManager->PlayMusic(mMenuMusicPlaylist[0]);
	}

	void StartGameMusic()
	{
		miGameMusicIndex = 0;
		engine::gpAudioManager->PlayMusic(mGameMusicPlaylist[0]);
	}
#endif

	void InitFramePostRender(Frame& rFrame);
	void ProcessDebugInput(const MenuInput& rMenuInput);

private:
};

inline Game* gpGame = nullptr;

void SpawnTransfer(Frame& rFrame, StatusChangeType eType, const TransferData& rData, engine::alignment_t playerAlignment);

} // namespace game
