#include "Game.h"

#include "Audio/AudioManager.h"
#include "File/DifferenceStream.h"
#include "Graphics/Graphics.h"
#include "Input/RawInputManager.h"
#include "Profile/ProfileManager.h"

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Graphics/Camera.h"

namespace game
{

using enum UiState;

constexpr float kfZoomMultiplier = 2.0f;

Game::Game()
{
	gpGame = this;

	engine::ResetRealTime();

	mpCurrentFrame = std::make_unique<Frame>();
	mpCurrentFrame->postRender.uiFrameId = GenerateFrameId();
	mpCurrentFrame->interpolate.flags |= FrameFlags::kMainMenu;
	mpNextFrame = std::make_unique<Frame>();
	mpNextFrame->postRender.uiFrameId = GenerateFrameId();
	mpNextFrame->interpolate.flags |= FrameFlags::kMainMenu;

	// Initialize alignments once at startup
	InitializeAlignments(mpCurrentFrame->postRender);

	mbSavedFrame = engine::ExistsVersionedFile<Frame>({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, AutosaveFile());

	engine::gpAudioManager->PlayMusic(mMenuMusicPlaylist[0]);
	engine::gpAudioManager->SetNextMusicTrackCallback([this]()
	{
		return GetNextMusicTrack();
	});
}

Game::~Game()
{
	engine::gpAudioManager->SetNextMusicTrackCallback(nullptr);

	if (!(mMenuFlags & engine::MenuFlags::kMouseVisible))
	{
		ShowCursor(true);
	}

	gpGame = nullptr;
}

void Game::Reset()
{
	Log("Game::Reset()");

	// DT: TEMP meUiState = kNone;
	mpDifferenceStreamWriter.reset();
	mpDifferenceStreamReader.reset();
	engine::gSunAngleOverride.Reset(game::gpCamera->mfSunAngle);
	engine::gbSmokeClear = true;
	engine::gpParticleManager->mbReset = true;
	engine::ResetRealTime();
}

bool Game::ShouldUpdateFrame()
{
	if (InMainMenu())
	{
		return true;
	}

	if (!(mMenuFlags & engine::MenuFlags::kUpdateFrame))
	{
		return false;
	}

	if constexpr (kbEnableDebugInput)
	{
		if (mTimeStep.mbSingleStep)
		{
			return true;
		}

		return meUiState == kNone || mbShowImGui;
	}
	else
	{
		return meUiState == kNone;
	}
}

void Game::Restart()
{
	mpCurrentFrame = std::make_unique<Frame>();
	mpCurrentFrame->postRender.uiFrameId = GenerateFrameId();
	mpCurrentFrame->interpolate.flags |= FrameFlags::kGame;

	Reset();

	meUiState = kNone;
}

void Game::ChangeFrame(FrameFlags_t flags)
{
	if ((flags & FrameFlags::kMainMenu && CurrentFrame().interpolate.flags & FrameFlags::kMainMenu) ||
	    ((flags & FrameFlags::kGame || flags & FrameFlags::kContinue) && CurrentFrame().interpolate.flags & FrameFlags::kGame))
	{
		common::DebugBreak();
		return;
	}

	// Start appropriate music playlist for menu or game mode
	if (flags & FrameFlags::kMainMenu)
	{
		miMenuMusicIndex = 0;
		engine::gpAudioManager->PlayMusic(mMenuMusicPlaylist[0]);
	}
	else
	{
		miGameMusicIndex = 0;
		engine::gpAudioManager->PlayMusic(mGameMusicPlaylist[0]);
	}

	WriteAutosave();

	if (flags & FrameFlags::kMainMenu)
	{
		mpCurrentFrame = std::make_unique<Frame>();
		mpCurrentFrame->postRender.uiFrameId = GenerateFrameId();
		mpCurrentFrame->interpolate.flags |= flags;
	}
	else if (flags & FrameFlags::kGame)
	{
		mpCurrentFrame = std::make_unique<Frame>();
		mpCurrentFrame->postRender.uiFrameId = GenerateFrameId();
		mpCurrentFrame->interpolate.flags |= flags;
	}
	else if (flags & FrameFlags::kContinue)
	{
		if (!engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, AutosaveFile(), CurrentFrame()) ||
		    CurrentFrame().interpolate.flags & FrameFlags::kDeathScreen)
		{
			mpCurrentFrame = std::make_unique<Frame>();
			mpCurrentFrame->postRender.uiFrameId = GenerateFrameId();
		}
		mpCurrentFrame->interpolate.flags |= FrameFlags::kGame;
	}

	Reset();
}

void Game::WriteAutosave()
{
	if (InMainMenu())
	{
		return;
	}

	if (CurrentFrame().interpolate.flags & FrameFlags::kDeathScreen)
	{
		gpGame->RemoveAutosave();
	}
	else
	{
		engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, AutosaveFile(), CurrentFrame());
	}
}

void Game::RemoveAutosave()
{
	mbSavedFrame = false;
	engine::gpFileManager->RemoveFile({engine::FileFlags::kAppDataDirectory}, AutosaveFile());
}

void Game::ProcessMenuInput(const MenuInput& rMenuInput)
{
	if constexpr (kbEnableDebugInput)
	{
		if (InMainMenu() && (rMenuInput.flags & MenuInputFlags::kQuickload || rMenuInput.flags & MenuInputFlags::kResetFrame))
		{
			gpGame->ChangeFrame(FrameFlags::kGame);
			gpGame->meUiState = kNone;
			return;
		}
	}

	if (rMenuInput.flags & MenuInputFlags::kQuit || (rMenuInput.flags & MenuInputFlags::kPauseMenu && InMainMenu()))
	{
		mGameFlags.Set(engine::GameFlags::kQuit);
	}

	if (rMenuInput.bGamepad && mMenuFlags & engine::MenuFlags::kMouseVisible)
	{
		ShowCursor(false);
		mMenuFlags.Clear(engine::MenuFlags::kMouseVisible);
	}
	else if (!rMenuInput.bGamepad && !(mMenuFlags & engine::MenuFlags::kMouseVisible))
	{
		ShowCursor(true);
		mMenuFlags |= engine::MenuFlags::kMouseVisible;
	}

	if (rMenuInput.flags & MenuInputFlags::kPauseMenu) [[unlikely]]
	{
		if (meUiState == kNone || meUiState == kGraphics || meUiState == kSound)
		{
			meUiState = kPause;
		}
		else if (!InMainMenu())
		{
			meUiState = kNone;
		}
	}

	if (rMenuInput.flags & MenuInputFlags::kToggleFullscreen)
	{
		engine::gFullscreen.Toggle();
	}

	if constexpr (kbEnableDebugInput)
	{
		if (rMenuInput.flags & MenuInputFlags::kMenuTweaks)
		{
			mbShowImGui = !mbShowImGui;
		}

		if (rMenuInput.flags & MenuInputFlags::kMenuGraphics)
		{
			meUiState = meUiState == kGraphics ? kNone : kGraphics;
			engine::gSunAngleOverride.Set(game::gpCamera->mfSunAngle);
		}

		if (rMenuInput.flags & MenuInputFlags::kToggleProfileText)
		{
			gpProfileManager->ToggleProfileText();
		}

		if (rMenuInput.flags & MenuInputFlags::kTogglePauseFrame)
		{
			mMenuFlags.Toggle(engine::MenuFlags::kUpdateFrame);
		}

		if (rMenuInput.flags & MenuInputFlags::kSlowTime)
		{
			mTimeStep.DecreaseTimeScale();
		}
		else if (rMenuInput.flags & MenuInputFlags::kSpeedUpTime)
		{
			mTimeStep.IncreaseTimeScale();
		}
	}

	if constexpr (kbEnableScreenshots)
	{
		if (rMenuInput.flags & MenuInputFlags::kToggleScreenshots)
		{
			engine::gpCommandBufferManager->mbSaveScreenshot = !engine::gpCommandBufferManager->mbSaveScreenshot;
		}
	}
}

struct SoundSettings
{
	static constexpr int64_t kiVersion = 1;

	float fMasterVolume = 0.0f;
	float fMusicVolume = 0.0f;
	float fSoundVolume = 0.0f;
};
constexpr char kpcSoundSettingsPath[] = "SoundSettings.bin";

void Game::SaveSoundSettings()
{
	SoundSettings soundSettings
	{
		.fMasterVolume = engine::gMasterVolume.Get(),
		.fMusicVolume = engine::gMusicVolume.Get(),
		.fSoundVolume = engine::gSoundVolume.Get(),
	};

	engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, kpcSoundSettingsPath, soundSettings);
}

void Game::LoadSoundSettings()
{
	SoundSettings soundSettings {};

	if (engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, kpcSoundSettingsPath, soundSettings))
	{
		engine::gMasterVolume.Set(soundSettings.fMasterVolume);
		engine::gMusicVolume.Set(soundSettings.fMusicVolume);
		engine::gSoundVolume.Set(soundSettings.fSoundVolume);
	}

	if constexpr (kbEnableRecording)
	{
		engine::gMusicVolume.Set(0.0f);
	}
}

void Game::ResetSoundSettings()
{
	engine::gMasterVolume.ResetToDefault();
	engine::gMusicVolume.ResetToDefault();
	engine::gSoundVolume.ResetToDefault();

	SaveSoundSettings();
}

common::crc_t Game::GetNextMusicTrack()
{
	if (InMainMenu())
	{
		miMenuMusicIndex = (miMenuMusicIndex + 1) % mMenuMusicPlaylist.size();
		return mMenuMusicPlaylist[miMenuMusicIndex];
	}
	else
	{
		miGameMusicIndex = (miGameMusicIndex + 1) % mGameMusicPlaylist.size();
		return mGameMusicPlaylist[miGameMusicIndex];
	}
}

} // namespace game
