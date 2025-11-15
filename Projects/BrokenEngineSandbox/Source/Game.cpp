#include "Game.h"

#include "Audio/AudioManager.h"
#include "File/DifferenceStream.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/DeviceManager.h"
#include "Graphics/Managers/InstanceManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Graphics/Managers/SwapchainManager.h"
#include "Graphics/Managers/TextManager.h"
#include "Input/RawInputManager.h"
#include "Profile/ProfileManager.h"
#include "Ui/UiManager.h"

#include "Frame/Frame.h"
#include "Frame/Pools/Smoke.h"

namespace game
{

using enum UiState;

constexpr float kfZoomMultiplier = 2.0f;

Game::Game()
{
	gpGame = this;

	ResetRealTime();

	mpCurrentFrame = std::make_unique<game::Frame>(game::FrameFlags::kMainMenu);
	mpNextFrame = std::make_unique<game::Frame>(game::FrameFlags::kMainMenu);

	mbSavedFrame = engine::ExistsVersionedFile<game::Frame>({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, AutosaveFile());

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

std::u32string_view Game::WaveText(int64_t iAdd)
{
	// DT: TODO Member not static
	static std::u32string sString;
	// sString = common::ToU32string(std::to_string(CurrentFrame().interpolate.iWave + iAdd));
	return sString;
}

void Game::Reset()
{
	LOG("Game::Reset()");

	meUiState = kNone;
	mpDifferenceStreamWriterHeld.reset();
	mpDifferenceStreamWriterPressed.reset();
	mpDifferenceStreamReaderHeld.reset();
	mpDifferenceStreamReaderPressed.reset();
	engine::gSunAngleOverride.Reset(mpCurrentFrame->interpolate.fSunAngle);
	engine::gbSmokeClear = true;
	engine::gpParticleManager->mbReset = true;
	ResetRealTime();
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
		
#if defined(ENABLE_DEBUG_INPUT)
	if (mTimeStep.mbSingleStep)
	{
		return true;
	}

	return meUiState == kNone || meUiState == kTweaks;
#else
	return meUiState == kNone;
#endif
}

void Game::Restart()
{
	new (&CurrentFrame()) Frame(FrameFlags::kGame);
	
	Reset();

	meUiState = kNone;
}

void Game::ChangeFrame(FrameFlags_t flags)
{
	if ((flags & FrameFlags::kMainMenu && CurrentFrame().flags & FrameFlags::kMainMenu) || (flags & FrameFlags::kGame && CurrentFrame().flags & FrameFlags::kGame))
	{
		DEBUG_BREAK();
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
		new (&CurrentFrame()) Frame(flags);
	}
	else if (flags & FrameFlags::kFirstSpawn)
	{
		new (&CurrentFrame()) Frame(flags);
	}
	else
	{
		if (!engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, AutosaveFile(), CurrentFrame()) || CurrentFrame().flags & FrameFlags::kDeathScreen)
		{
			new (&CurrentFrame()) Frame(flags);
		}
	}

	Reset();
}

void Game::WriteAutosave()
{
	if (InMainMenu())
	{
		return;
	}

	if (CurrentFrame().flags & FrameFlags::kDeathScreen)
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

// DT: TODO Remove this function (at least move to Base?)
bool Game::PreUpdate(const game::MenuInput& rMenuInput, bool bLostFocus)
{
	ProcessMenuInput(rMenuInput);

	bool bUpdateFrame = ShouldUpdateFrame();
	if (bLostFocus || !bUpdateFrame || bUpdateFrame != mbPreviousFrameUpdated) [[unlikely]]
	{
		engine::gpRawInputManager->SetVibration(0, 0.0f, 0.0f);
		ResetRealTime();
	}
	mbPreviousFrameUpdated = bUpdateFrame;

	return bUpdateFrame;
}

void Game::ProcessMenuInput(const MenuInput& rMenuInput)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (InMainMenu() && (rMenuInput.flags & MenuInputFlags::kQuickload || rMenuInput.flags & MenuInputFlags::kResetFrame))
	{
		gpGame->ChangeFrame(FrameFlags::kGame);
		gpGame->meUiState = kNone;
		return;
	}
#endif

	if (rMenuInput.flags & game::MenuInputFlags::kQuit || (rMenuInput.flags & game::MenuInputFlags::kPauseMenu && InMainMenu()))
	{
		mbQuit = true;
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
		if (engine::gpUiManager->mpCapturedWidget != nullptr)
		{
			engine::gpUiManager->mpCapturedWidget = nullptr;
		}
		else if (meUiState == kNone || meUiState == kGraphics || meUiState == kSound)
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

#if defined(ENABLE_DEBUG_INPUT)
	if (rMenuInput.flags & MenuInputFlags::kMenuTweaks)
	{
		meUiState = meUiState == kTweaks ? kNone : kTweaks;
	}

	if (rMenuInput.flags & MenuInputFlags::kMenuGraphics)
	{
		meUiState = meUiState == kGraphics ? kNone : kGraphics;
		engine::gSunAngleOverride.Set(CurrentFrame().interpolate.fSunAngle);
	}

	if (rMenuInput.flags & MenuInputFlags::kToggleProfileText)
	{
		PROFILE_TOGGLE_TEXT();
	}

	if (rMenuInput.flags & MenuInputFlags::kTogglePauseFrame)
	{
		mMenuFlags.Toggle(engine::MenuFlags::kUpdateFrame);
	}

	if (rMenuInput.flags & MenuInputFlags::kSlowTime)
	{
		if (mTimeStep.miTimeMultiply > 1)
		{
			mTimeStep.miTimeMultiply /= 2;
			LOG("Time ratio: {}x", mTimeStep.miTimeMultiply);
			engine::gpTextManager->UpdateTextArea(engine::kTextDebug, std::string("Time ratio: ") + std::to_string(mTimeStep.miTimeMultiply) + "x");
		}
		else
		{
			mTimeStep.miTimeDivide *= 2;
			LOG("Time ratio: 1/{}x", mTimeStep.miTimeDivide);
			engine::gpTextManager->UpdateTextArea(engine::kTextDebug, std::string("Time ratio: ") + std::to_string(mTimeStep.miTimeDivide) + "/x");
		}
	}
	else if (rMenuInput.flags & MenuInputFlags::kSpeedUpTime)
	{
		if (mTimeStep.miTimeDivide > 1)
		{
			mTimeStep.miTimeDivide /= 2;
			LOG("Time ratio: 1/{}x", mTimeStep.miTimeDivide);
			engine::gpTextManager->UpdateTextArea(engine::kTextDebug, std::string("Time ratio: ") + std::to_string(mTimeStep.miTimeDivide) + "/x");
		}
		else
		{
			mTimeStep.miTimeMultiply *= 2;
			LOG("Time ratio: {}x", mTimeStep.miTimeMultiply);
			engine::gpTextManager->UpdateTextArea(engine::kTextDebug, std::string("Time ratio: ") + std::to_string(mTimeStep.miTimeMultiply) + "x");
		}
	}
	if (mTimeStep.miTimeDivide == 1 && mTimeStep.miTimeMultiply == 1)
	{
		engine::gpTextManager->UpdateTextArea(engine::kTextDebug, "");
	}
#endif

#if defined(ENABLE_SCREENSHOTS)
	if (rMenuInput.flags & MenuInputFlags::kToggleScreenshots)
	{
		engine::gpCommandBufferManager->mbSaveScreenshot = !engine::gpCommandBufferManager->mbSaveScreenshot;
	}
#endif
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

#if defined(ENABLE_RECORDING)
	engine::gMusicVolume.Set(0.0f);
#endif
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
