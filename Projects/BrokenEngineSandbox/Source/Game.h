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

public:
	engine::alignment_t PlayerAlignment() const { return mPlayerAlignment; }
	void SetHumanPlayerId(player_t id) { mHumanPlayerId = id; }
	void SetPreviousHumanArmor(float f) { mfPreviousHumanArmor = f; }
	const engine::Alignments& Alignments() const { return mAlignments; }

#if defined(BT_CLIENT)
	void StartGameMusic()
	{
		miGameMusicIndex = 0;
		engine::gpAudioManager->PlayMusic(mGameMusicPlaylist.at(0));
	}
#endif

private:
};

inline Game* gpGame = nullptr;

void SpawnTransfer(Frame& rFrame, StatusChangeType eType, const TransferData& data, engine::alignment_t playerAlignment);

} // namespace game
