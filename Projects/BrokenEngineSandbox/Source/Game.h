#pragma once

#include "GameBase.h"

#include "PlayerAi.h"
#include "Frame/Frame.h"
#include "Graphics/Camera.h"
#include "Input/Input.h"

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

	void WriteAutosave();
	void RemoveAutosave();

	void ProcessMenuInput(const MenuInput& rMenuInput) override;

	// Multi-frame grid
	void ComputeActiveSet();
	void EnsureNextFrames();
	void BuildFrameInputs();
	void CreateFrameAtCoord(engine::GridCoord coord);
	void HarvestTransfers();

	// Human player tracking
	player_t HumanPlayerId() const { return mHumanPlayerId; }
	bool IsHumanPlayer(player_t id) const { return id.IsValid() && id == mHumanPlayerId; }
	int64_t HumanPlayerIndex(const PlayersInterpolate& rPlayers) const;
	std::vector<StatusChange> DrainPendingStatusChanges() { return std::exchange(mPendingStatusChanges, {}); }

	static void SaveSoundSettings();
	static void LoadSoundSettings();
	static void ResetSoundSettings();

	common::crc_t GetNextMusicTrack();

	Camera mCamera {};
	PlayerAi mPlayerAi {};
	float mfSpawnTimer = 0.0f;

	UiState meUiState = UiState::kPause;

	bool mbShowImGui = false;
	bool mbSavedFrame = false;
	bool mbRespawnRequested = false;

	engine::GridCoord mHumanGridCoord {};
	std::vector<engine::GridCoord> mActiveCoords;
	std::unordered_map<engine::GridCoord, FrameInput> mFrameInputs;

private:

	FrameInput BuildFrameInput(const Frame& rCurrentFrame, engine::GridCoord coord);

	virtual std::filesystem::path AutosaveFile() override
	{
		return std::filesystem::path("Autosave.save");
	}

	virtual std::filesystem::path QuicksaveFile() override
	{
		return std::filesystem::path("Quicksave.save");
	}

	virtual std::filesystem::path ReplayFile() override
	{
		return std::filesystem::path("F7.replay");
	}

	std::vector<common::crc_t> mMenuMusicPlaylist {data::kAudioMusicdoodlewavCrc, data::kAudioMusicMandatoryOvertimewavCrc, data::kAudioMusicsong18wavCrc, data::kAudioMusicTyhosibzzzzwavCrc};
	std::vector<common::crc_t> mGameMusicPlaylist {data::kAudioMusicS31UnexpectedTroublewavCrc, data::kAudioMusicS31HighAlertwavCrc, data::kAudioMusicS31OnPatrolwavCrc, data::kAudioMusicS31TheGearsofProgresswavCrc};

	int64_t miMenuMusicIndex = 0;
	int64_t miGameMusicIndex = 0;

	player_t mHumanPlayerId {};
	bool mbWaitingForHumanSpawn = false;
	float mfPreviousHumanArmor = 0.0f;
	std::vector<StatusChange> mPendingStatusChanges;

	engine::alignment_t mPlayerAlignment {};
	engine::alignment_t mEnemyAlignment {};
	engine::Alignments mAlignments {};
};

inline Game* gpGame = nullptr;

} // namespace game
