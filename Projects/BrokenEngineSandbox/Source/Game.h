#pragma once

#include "GameBase.h"

#include "Frame/Frame.h"
#include "Input/Input.h"

namespace engine
{

class FileManager;

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

inline bool gbQuit = false;

class Game : public engine::GameBase
{
public:

	Game();
	virtual ~Game();

	virtual void Reset();
	virtual bool ShouldUpdateFrame();
	virtual void EndReplay(FrameInput& rFrameInput);

	void Restart();
	void ChangeFrame(FrameFlags_t flags);
	bool Update(const engine::RawInput& rRawInput, bool bLostFocus);
	void Quit();

	std::u32string_view WaveText(int64_t iAdd = 0)
	{
		std::string wave = std::to_string(CurrentFrame().iWave + iAdd);

		static std::u32string sString;
		sString = common::ToU32string(wave);
		return sString;
	}

	bool InMainMenu()
	{
		return CurrentFrame().flags & FrameFlags::kMainMenu;
	}

	void WriteAutosave();
	void RemoveAutosave();

	static void SaveSoundSettings();
	static void LoadSoundSettings();
	static void ResetSoundSettings();

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

	void ProcessMenuInput(const MenuInput& rMenuInput);
	void ProcessSavesAndReplays(const MenuInput& rMenuInput, FrameInput& rFrameInput);

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
};

inline Game* gpGame = nullptr;

} // namespace game
