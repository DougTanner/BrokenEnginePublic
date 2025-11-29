#pragma once

#include "GameBase.h"

#include "Frame/Frame.h"
#include "Graphics/Camera.h"
#include "Input/Input.h"

namespace engine
{

struct RawInput;

}

namespace game
{

#include "Version.h"

inline constexpr std::string_view kpcGameName = "Broken Engine Sandbox";

enum class UiState
{
	kNone,

	kGraphics,
	kPause,
	kSound,

#if defined(ENABLE_DEBUG_INPUT)
	kTweaks,
#endif
};

class Game : public engine::GameBase
{
public:

	Game();
	virtual ~Game();

	virtual void Reset() override;
	virtual bool ShouldUpdateFrame() override;

	void Restart();
	void ChangeFrame(FrameFlags_t flags);

	bool InMainMenu()
	{
		return CurrentFrame().flags & FrameFlags::kMainMenu;
	}

	void WriteAutosave();
	void RemoveAutosave();

	bool PreUpdate(const game::MenuInput& rMenuInput, bool bLostFocus);
	void ProcessMenuInput(const MenuInput& rMenuInput);

	static void SaveSoundSettings();
	static void LoadSoundSettings();
	static void ResetSoundSettings();

	common::crc_t GetNextMusicTrack();

	Camera mCamera {};

	UiState meUiState = UiState::kPause;

	bool mbSavedFrame = false;

private:

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
};

inline Game* gpGame = nullptr;

} // namespace game
