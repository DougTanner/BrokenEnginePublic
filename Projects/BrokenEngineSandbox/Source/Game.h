#pragma once

#include "GameBase.h"

#include "Frame/Frame.h"
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

	virtual void Reset();
	virtual bool ShouldUpdateFrame();

	void Restart();
	void ChangeFrame(FrameFlags_t flags);
	void Quit();

	std::u32string_view WaveText(int64_t iAdd = 0)
	{
		std::string wave = std::to_string(CurrentFrame().global.iWave + iAdd);

		static std::u32string sString;
		sString = common::ToU32string(wave);
		return sString;
	}

	bool InMainMenu()
	{
		return CurrentFrame().global.flags & FrameFlags::kMainMenu;
	}

	void WriteAutosave();
	void RemoveAutosave();

	bool PreUpdate(game::MenuInput& rMenuInput, game::FrameInput& rFrameInput, bool bLostFocus);
	void ProcessMenuInput(const MenuInput& rMenuInput);
	void ProcessSavesAndReplays(const MenuInput& rMenuInput, FrameInput& rFrameInput);

	static void SaveSoundSettings();
	static void LoadSoundSettings();
	static void ResetSoundSettings();

	common::crc_t GetNextMusicTrack();

	UiState meUiState = UiState::kPause;
	engine::IslandsFlip meLastIslandsFlip = engine::kFlipXY;

	engine::IslandsFlip NextIslandsFlip()
	{
		meLastIslandsFlip = static_cast<engine::IslandsFlip>(meLastIslandsFlip + 1);
		if (meLastIslandsFlip == engine::kFlipCount)
		{
			meLastIslandsFlip = engine::kFlipNone;
		}

		return meLastIslandsFlip;
	}

	bool mbSavedFrame = false;

private:

	std::filesystem::path AutosaveFile()
	{
		return std::filesystem::path("Autosave.save");
	}

	std::filesystem::path QuicksaveFile()
	{
		return std::filesystem::path("Quicksave.save");
	}

	std::filesystem::path ReplayFile()
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
