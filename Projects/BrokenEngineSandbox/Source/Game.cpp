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

Game::Game()
{
	gpGame = this;

	mpCurrentFrame = std::make_unique<game::Frame>(game::FrameFlags::kMainMenu, engine::kFlipNone);
	mpNextFrame = std::make_unique<game::Frame>(game::FrameFlags::kMainMenu, engine::kFlipNone);

	mbSavedFrame = engine::ExistsVersionedFile<game::Frame>({kAppDataDirectory, kRead}, AutosaveFile());
}

Game::~Game()
{
	if (!(mMenuFlags & kMouseVisible))
	{
		ShowCursor(true);
	}

	gpGame = nullptr;
}

void Game::Quit()
{
	WriteAutosave();
	gbQuit = true;
}

bool Game::Update(const engine::RawInput& rRawInput, bool bLostFocus)
{
	auto [menuInput, frameInput] = ProcessRawInput(rRawInput);

	if (gbQuit || menuInput.flags & kQuit || (menuInput.flags & kPauseMenu && InMainMenu() && meUiState == kPause))
	{
		Quit();
		return false;
	}

	engine::gpUiManager->Update(menuInput);

	ProcessMenuInput(menuInput);
	ProcessSavesAndReplays(menuInput, frameInput);

	CPU_PROFILE_STOP(engine::kCpuTimerMessagesAndInput);

	static bool sbDidUpdateFrame = false;
	bool bUpdateFrame = ShouldUpdateFrame();
	if (bUpdateFrame != sbDidUpdateFrame)
	{
		ResetRealTime();
	}
	sbDidUpdateFrame = bUpdateFrame;

#if defined(ENABLE_DEBUG_INPUT)
	if (gpGame->meUiState == UiState::kTweaks)
	{
		CurrentFrame().fSunAngle = engine::gSunAngleOverride.Get();
	}

	if (!bUpdateFrame && !(menuInput.flags & kSingleStep)) [[unlikely]]
#else
	if (!bUpdateFrame) [[unlikely]]
#endif
	{
		engine::gpRawInputManager->SetVibration(0, 0.0f, 0.0f);
		engine::gpGraphics->RenderMainImagePresentAcquire(CurrentFrame());
		return true;
	}

#if defined(ENABLE_DEBUG_INPUT)
	bool bQuit = GameBase::Update(menuInput.flags & kSingleStep, bLostFocus, frameInput);
#else
	bool bQuit = GameBase::Update(false, bLostFocus, frameInput);
#endif

	float fVibration = menuInput.bGamepad ? std::pow(CurrentFrame().camera.fCameraShake, 0.5f) : 0.0f;
	engine::gpRawInputManager->SetVibration(0, fVibration, fVibration);

	return bQuit;
}

void Game::Reset()
{
	LOG("\n\n\nGame::Reset()\n\n\n");

	mpDifferenceStreamWriter.reset();
	mpDifferenceStreamReader.reset();

	engine::gSunAngleOverride.Reset(mpCurrentFrame->fSunAngle);

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

void Game::EndReplay(FrameInput& rFrameInput)
{
	Reset();
	mpDifferenceStreamReader = std::make_unique<engine::DifferenceStreamReader<Frame, FrameInput>>(engine::FileFlags_t {kAppDataDirectory, kRead}, ReplayFile(), CurrentFrame(), rFrameInput);
}

void Game::Restart()
{
	new (&CurrentFrame()) Frame(kGame, NextIslandsFlip());
	
	Reset();

	meUiState = kNone;
}

void Game::ChangeFrame(FrameFlags_t flags)
{
	if ((flags & kMainMenu && CurrentFrame().flags & kMainMenu) || (flags & kGame && CurrentFrame().flags & kGame))
	{
		DEBUG_BREAK();
		return;
	}

	mbMainMenuMusic = flags & kMainMenu;

	WriteAutosave();

	if (flags & kMainMenu)
	{
		new (&CurrentFrame()) Frame(flags, engine::kFlipNone);
	}
	else if (flags & kFirstSpawn)
	{
		new (&CurrentFrame()) Frame(flags, NextIslandsFlip());
	}
	else
	{
		if (!engine::ReadVersionedFile({kAppDataDirectory, kRead}, AutosaveFile(), CurrentFrame()) || CurrentFrame().flags & kDeathScreen)
		{
			new (&CurrentFrame()) Frame(flags, NextIslandsFlip());
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

	if (CurrentFrame().flags & kDeathScreen)
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

void Game::ProcessMenuInput(const MenuInput& rMenuInput)
{
	if (rMenuInput.bGamepad && mMenuFlags & kMouseVisible)
	{
		ShowCursor(false);
		mMenuFlags &= kMouseVisible;
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
		engine::gSunAngleOverride.Set(CurrentFrame().fSunAngle);
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
		if (miTimeMultiply > 1)
		{
			miTimeMultiply /= 2;
			LOG("Time ratio: {}x", miTimeMultiply);
			engine::gpTextManager->UpdateTextArea(engine::kTextDebug, std::string("Time ratio: ") + std::to_string(miTimeMultiply) + "x");
		}
		else
		{
			miTimeDivide *= 2;
			LOG("Time ratio: 1/{}x", miTimeDivide);
			engine::gpTextManager->UpdateTextArea(engine::kTextDebug, std::string("Time ratio: ") + std::to_string(miTimeDivide) + "/x");
		}
	}
	else if (rMenuInput.flags & kSpeedUpTime)
	{
		if (miTimeDivide > 1)
		{
			miTimeDivide /= 2;
			LOG("Time ratio: 1/{}x", miTimeDivide);
			engine::gpTextManager->UpdateTextArea(engine::kTextDebug, std::string("Time ratio: ") + std::to_string(miTimeDivide) + "/x");
		}
		else
		{
			miTimeMultiply *= 2;
			LOG("Time ratio: {}x", miTimeMultiply);
			engine::gpTextManager->UpdateTextArea(engine::kTextDebug, std::string("Time ratio: ") + std::to_string(miTimeMultiply) + "x");
		}
	}
	if (miTimeDivide == 1 && miTimeMultiply == 1)
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

void Game::ProcessSavesAndReplays([[maybe_unused]] const MenuInput& rMenuInput, [[maybe_unused]] FrameInput& rFrameInput)
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

#if defined(ENABLE_DEBUG_INPUT)
	if (rMenuInput.flags & kSaveReplay && mpDifferenceStreamWriter == nullptr)
	{
		mpDifferenceStreamReader.reset();

		LOG("Start recording replay at {}", CurrentFrame().iFrame);
		mpDifferenceStreamWriter = std::make_unique<engine::DifferenceStreamWriter<Frame, FrameInput>>(CurrentFrame(), rFrameInput);
	}
	else if (rMenuInput.flags & kSaveReplay && mpDifferenceStreamWriter != nullptr)
	{
		LOG("Saving replay at {}", CurrentFrame().iFrame);
		mpDifferenceStreamWriter->Save({kAppDataDirectory, kWrite, kBackup}, ReplayFile(), CurrentFrame());
		mpDifferenceStreamWriter.reset();
	}

	if (rMenuInput.flags & kLoadReplay)
	{
		if (mpDifferenceStreamReader != nullptr)
		{
			mpDifferenceStreamReader.reset();
		}
		else
		{
			Reset();

			mpDifferenceStreamWriter.reset();
			mpDifferenceStreamReader = std::make_unique<engine::DifferenceStreamReader<Frame, FrameInput>>(engine::FileFlags_t {kAppDataDirectory, kRead}, ReplayFile(), CurrentFrame(), rFrameInput);
			memcpy(&NextFrame(), &CurrentFrame(), sizeof(NextFrame()));

			if (mpDifferenceStreamReader->Loaded())
			{
				LOG("Loaded replay at {}", CurrentFrame().iFrame);
			}
			else
			{
				mpDifferenceStreamReader.reset();
			}
		}
	}

	if (rMenuInput.flags & kQuicksave)
	{
		engine::WriteVersionedFile({kAppDataDirectory, kWrite}, QuicksaveFile(), CurrentFrame());
	}

	if (rMenuInput.flags & kQuickload || rMenuInput.flags & kResetFrame)
	{
		if (rMenuInput.flags & kQuickload)
		{
			engine::ReadVersionedFile({kAppDataDirectory, kRead}, QuicksaveFile(), CurrentFrame());
		}
		else
		{
			new (mpCurrentFrame.get()) Frame(kGame, NextIslandsFlip());
		}

		meUiState = kNone;
		Reset();
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

} // namespace game
