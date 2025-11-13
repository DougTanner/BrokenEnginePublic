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
#include "Ui/Wrapper.h"

#include "Frame/Frame.h"

using enum engine::FileFlags;
using enum engine::MenuFlags;

namespace game
{

using enum FrameFlags;
using enum MenuInputFlags;
using enum UiState;

constexpr float kfZoomMultiplier = 2.0f;

Game::Game()
{
	gpGame = this;

	ResetRealTime();

	mpCurrentFrame = std::make_unique<game::Frame>(game::FrameFlags::kMainMenu);
	mpNextFrame = std::make_unique<game::Frame>(game::FrameFlags::kMainMenu);

	mbSavedFrame = engine::ExistsVersionedFile<game::Frame>({kAppDataDirectory, kRead}, AutosaveFile());

	engine::gpAudioManager->PlayMusic(mMenuMusicPlaylist[0]);
	engine::gpAudioManager->SetNextMusicTrackCallback([this]()
	{
		return GetNextMusicTrack();
	});
}

Game::~Game()
{
	engine::gpAudioManager->SetNextMusicTrackCallback(nullptr);

	if (!(mMenuFlags & kMouseVisible))
	{
		ShowCursor(true);
	}

	gpGame = nullptr;
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

	if (!(mMenuFlags & kUpdateFrame))
	{
		return false;
	}
		
#if defined(ENABLE_DEBUG_INPUT)
	return meUiState == kNone || meUiState == kTweaks;
#else
	return meUiState == kNone;
#endif
}

void Game::Restart()
{
	new (&CurrentFrame()) Frame(kGame);
	
	Reset();

	meUiState = kNone;
}

void Game::ChangeFrame(FrameFlags_t flags)
{
	if ((flags & kMainMenu && CurrentFrame().interpolate.flags & kMainMenu) || (flags & kGame && CurrentFrame().interpolate.flags & kGame))
	{
		DEBUG_BREAK();
		return;
	}

	// Start appropriate music playlist for menu or game mode
	if (flags & kMainMenu)
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

	if (flags & kMainMenu)
	{
		new (&CurrentFrame()) Frame(flags);
	}
	else if (flags & kFirstSpawn)
	{
		new (&CurrentFrame()) Frame(flags);
	}
	else
	{
		if (!engine::ReadVersionedFile({kAppDataDirectory, kRead}, AutosaveFile(), CurrentFrame()) || CurrentFrame().interpolate.flags & kDeathScreen)
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

	if (CurrentFrame().interpolate.flags & kDeathScreen)
	{
		gpGame->RemoveAutosave();
	}
	else
	{
		engine::WriteVersionedFile({kAppDataDirectory, kWrite}, AutosaveFile(), CurrentFrame());
	}
}

void Game::RemoveAutosave()
{
	mbSavedFrame = false;
	engine::gpFileManager->RemoveFile({engine::FileFlags::kAppDataDirectory}, AutosaveFile());
}

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
	if (rMenuInput.flags & game::MenuInputFlags::kQuit || (rMenuInput.flags & game::MenuInputFlags::kPauseMenu && InMainMenu() && meUiState == game::UiState::kPause))
	{
		return;
	}

	if (rMenuInput.bGamepad && mMenuFlags & kMouseVisible)
	{
		ShowCursor(false);
		mMenuFlags.Clear(kMouseVisible);
	}
	else if (!rMenuInput.bGamepad && !(mMenuFlags & kMouseVisible))
	{
		ShowCursor(true);
		mMenuFlags |= kMouseVisible;
	}

	if (rMenuInput.flags & kPauseMenu) [[unlikely]]
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

	if (rMenuInput.flags & kToggleFullscreen)
	{
		engine::gFullscreen.Toggle();
	}

#if defined(ENABLE_DEBUG_INPUT)
	if (rMenuInput.flags & kMenuTweaks)
	{
		meUiState = meUiState == kTweaks ? kNone : kTweaks;
	}

	if (rMenuInput.flags & kMenuGraphics)
	{
		meUiState = meUiState == kGraphics ? kNone : kGraphics;
		engine::gSunAngleOverride.Set(CurrentFrame().interpolate.fSunAngle);
	}

	if (rMenuInput.flags & kToggleProfileText)
	{
		PROFILE_TOGGLE_TEXT();
	}

	if (rMenuInput.flags & kTogglePauseFrame)
	{
		mMenuFlags.Toggle(kUpdateFrame);
	}

	if (rMenuInput.flags & kSlowTime)
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
	else if (rMenuInput.flags & kSpeedUpTime)
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
	if (rMenuInput.flags & kToggleScreenshots)
	{
		engine::gpCommandBufferManager->mbSaveScreenshots = !engine::gpCommandBufferManager->mbSaveScreenshots;
	}
#endif
}

void Game::ProcessSavesAndReplays([[maybe_unused]] const MenuInput& rMenuInput, [[maybe_unused]] const FrameInputHeld& rFrameInputHeld, [[maybe_unused]] const FrameInputPressed& rFrameInputPressed)
{
	if (InMainMenu())
	{
	#if defined(ENABLE_DEBUG_INPUT)
		if (rMenuInput.flags & kQuickload || rMenuInput.flags & kResetFrame)
		{
			gpGame->ChangeFrame(FrameFlags::kGame);
			gpGame->meUiState = kNone;
		}
	#endif

		return;
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

	engine::WriteVersionedFile({kAppDataDirectory, kWrite}, kpcSoundSettingsPath, soundSettings);
}

void Game::LoadSoundSettings()
{
	SoundSettings soundSettings {};

	if (engine::ReadVersionedFile({kAppDataDirectory, kRead}, kpcSoundSettingsPath, soundSettings))
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
