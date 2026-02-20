#include "Game.h"

#include "Audio/AudioManager.h"
#include "File/DifferenceStream.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Input/RawInputManager.h"
#include "Graphics/Managers/CommandBufferManager.h"
#include "Graphics/Managers/ParticleManager.h"

#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Graphics/Camera.h"
#include "Profile/ProfileManager.h"

namespace game
{

using enum UiState;

constexpr float kfZoomMultiplier = 2.0f;

// Camera shake
constexpr float kfCameraShakeAdd = 0.25f;
constexpr float kfCameraShakeMax = 1.0f;

// Human player tracking state within BuildFrameInput
enum class HumanFlags : uint64_t
{
	kAlive    = 0x01,
	kJustDied = 0x02,
};
using HumanFlags_t = common::Flags<HumanFlags>;

Game::Game()
{
	gpGame = this;

	// Set up alignments
	uint32_t uiNextAlignment = 1;
	mPlayerAlignment = engine::alignment_t {uiNextAlignment++};
	mEnemyAlignment = engine::alignment_t {uiNextAlignment++};
	mAlignments.AddAlignment(mPlayerAlignment, mEnemyAlignment, engine::AlignmentFlags::kEnemies);

	// Allocate frames
	CreateNewFrame(GameFlags::kMainMenu);
	mpNextFrame = std::make_unique<Frame>();

	// Check for an autosave
	mbSavedFrame = engine::ExistsVersionedFile<Frame>({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, AutosaveFile());

	// Start music
	engine::gpAudioManager->PlayMusic(mMenuMusicPlaylist.at(0));
	engine::gpAudioManager->Set3dSettings(10.0f, 0.0f, 150.0f, 0.05f);
	engine::gpAudioManager->SetNextMusicTrackCallback([this]()
	{
		return GetNextMusicTrack();
	});

	// Prepare for first frame
	engine::ResetRealTime();
}

int64_t Game::HumanPlayerIndex(const PlayersInterpolate& rPlayers) const
{
	if (mHumanPlayerId.IsValid())
	{
		auto it = rPlayers.idToIndexMap.find(mHumanPlayerId);
		if (it != rPlayers.idToIndexMap.end())
		{
			return it->second;
		}
	}

	return 0;
}

FrameInput Game::BuildFrameInput(const Frame& rCurrentFrame)
{
	static constexpr float kfSpawnInterval = 2.0f;

	// Heap: vector::resize on playerInputs/statusChanges in FrameInput. Data must outlive this call
	// (consumed by frame update phases), so workbuffer won't work. Player count varies, so can't pre-allocate.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const PlayersInterpolate& rPlayers = rCurrentFrame.interpolate.players;
	const PlayersPostRender& rPlayersPostRender = rCurrentFrame.postRender.players;
	int64_t iPlayerCount = rPlayers.iCount;

	// --- Human player tracking ---

	// Identify newly-spawned human player after a spawn event
	if (mbWaitingForHumanSpawn && !mHumanPlayerId.IsValid() && iPlayerCount > 0
	    && !(rCurrentFrame.interpolate.gameFlags & GameFlags::kDeathScreen))
	{
		mHumanPlayerId = rPlayersPostRender.puiIds[iPlayerCount - 1];
		mfPreviousHumanArmor = rPlayersPostRender.pfArmors[iPlayerCount - 1];
		mbWaitingForHumanSpawn = false;
		mbRespawnRequested = false;
	}

	// Check if human player exists in current frame
	HumanFlags_t humanFlags;
	if (mHumanPlayerId.IsValid() && rPlayers.idToIndexMap.contains(mHumanPlayerId))
	{
		humanFlags.Set(HumanFlags::kAlive);
	}
	int64_t iHumanIndex = (humanFlags & HumanFlags::kAlive) ? HumanPlayerIndex(rPlayers) : -1;

	// Detect human death: was valid but no longer in collection
	if (mHumanPlayerId.IsValid() && !(humanFlags & HumanFlags::kAlive))
	{
		static_cast<Frame*>(mpCurrentFrame.get())->interpolate.gameFlags.Set(GameFlags::kDeathScreen);
		mHumanPlayerId = {};
		mfPreviousHumanArmor = 0.0f;
		humanFlags.Set(HumanFlags::kJustDied);
	}

	// Camera shake: detect armor damage on human player
	if (humanFlags & HumanFlags::kAlive)
	{
		float fCurrentArmor = rPlayersPostRender.pfArmors[iHumanIndex];
		if (fCurrentArmor < mfPreviousHumanArmor)
		{
			mCamera.mfShake = std::min(mCamera.mfShake + kfCameraShakeAdd, kfCameraShakeMax);
		}
		mfPreviousHumanArmor = fCurrentArmor;
	}

	// --- Human input ---

	FrameInput frameInput {};
	frameInput.playerInputs.resize(iPlayerCount);
	RawInputToFrameInput(engine::gpRawInputManager->mRawInput, frameInput, iHumanIndex);
	gpInput->UpdateFrameInputPressed(engine::gpRawInputManager->mRawInput, frameInput);

	// --- AI input ---

	if (rCurrentFrame.interpolate.gameFlags & GameFlags::kGame)
	{
		for (int64_t i = 0; i < iPlayerCount; ++i)
		{
			if (i == iHumanIndex)
			{
				continue;
			}

			if (rPlayersPostRender.pFlags[i] & PlayerFlags::kExploding)
			{
				continue;
			}

			mPlayerAi.UpdatePlayer(rCurrentFrame, i, frameInput.playerInputs.at(i));
		}
	}

	// --- Spawn management ---

	if (rCurrentFrame.interpolate.gameFlags & GameFlags::kGame)
	{
		if (humanFlags.Empty())
		{
			if (!(rCurrentFrame.interpolate.gameFlags & GameFlags::kDeathScreen) && !mbWaitingForHumanSpawn)
			{
				// Initial spawn: no human, no death screen
				mPendingStatusChanges.push_back({.eType = StatusChangeType::kSpawnPlayer});
				mbWaitingForHumanSpawn = true;
			}
			else if (mbRespawnRequested && !mbWaitingForHumanSpawn)
			{
				// Respawn after death screen
				mPendingStatusChanges.push_back({.eType = StatusChangeType::kRespawnPlayer});
				mbWaitingForHumanSpawn = true;
			}
		}
		else if (humanFlags & HumanFlags::kAlive && iPlayerCount < kiMaxPlayers)
		{
			// AI wingmen spawning (only when human is alive)
			mfSpawnTimer -= kfDeltaTime;
			if (mfSpawnTimer <= 0.0f)
			{
				mPendingStatusChanges.push_back({.eType = StatusChangeType::kSpawnPlayer});
				mfSpawnTimer = kfSpawnInterval;
			}
		}
	}

	return frameInput;
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

	mpDifferenceStreamWriter.reset();
	mpDifferenceStreamReader.reset();
	game::gpCamera->ResetSunAngle();
	engine::gSunAngleOverride.Reset(game::gpCamera->SunAngle(true));
	engine::gbSmokeClear = true;
	engine::gpParticleManager->mbReset = true;
	engine::SmokeTrailsInterpolate::ResetRenderState();
	engine::WindTrailsInterpolate::ResetRenderState();
	mPlayerAi.Reset();
	mfSpawnTimer = 0.0f;
	engine::ResetRealTime();
}

void Game::CreateNewFrame(GameFlags_t gameFlags)
{
	// Heap: make_unique<Frame> with all its SOA collections. The frame must persist as mpCurrentFrame
	// across the entire game state lifetime, so workbuffer (lost on Pop) can't hold it.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mpCurrentFrame = std::make_unique<Frame>();
	mpCurrentFrame->interpolate.gameFlags.Set(gameFlags.meFlags);
	mpCurrentFrame->postRender.uiFrameId = GenerateFrameId();
	mpCurrentFrame->postRender.playerAlignment = mPlayerAlignment;
	mpCurrentFrame->postRender.enemyAlignment = mEnemyAlignment;
	mpCurrentFrame->postRender.alignments = mAlignments;
}

bool Game::ShouldTrapCursor()
{
	return !InMainMenu() && ShouldUpdateFrame();
}

bool Game::ShouldUseCrosshair()
{
	return CurrentFrame().interpolate.gameFlags & GameFlags::kGame && meUiState == kNone;
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
	CreateNewFrame(GameFlags::kGame);
	Reset();

	mHumanPlayerId = {};
	mbRespawnRequested = false;
	mbWaitingForHumanSpawn = false;
	mfPreviousHumanArmor = 0.0f;
	mPendingStatusChanges.clear();

	meUiState = kNone;
}

void Game::ChangeFrame(GameFlags_t gameFlags)
{
	if ((gameFlags & GameFlags::kMainMenu && CurrentFrame().interpolate.gameFlags & GameFlags::kMainMenu) ||
	    ((gameFlags & GameFlags::kGame || gameFlags & GameFlags::kContinue) && CurrentFrame().interpolate.gameFlags & GameFlags::kGame))
	{
		DEBUG_BREAK();
		return;
	}

	// Start appropriate music playlist for menu or game mode
	if (gameFlags & GameFlags::kMainMenu)
	{
		miMenuMusicIndex = 0;
		engine::gpAudioManager->PlayMusic(mMenuMusicPlaylist.at(0));
	}
	else
	{
		miGameMusicIndex = 0;
		engine::gpAudioManager->PlayMusic(mGameMusicPlaylist.at(0));
	}

	WriteAutosave();

	// Clear human tracking on frame transitions
	mHumanPlayerId = {};
	mbRespawnRequested = false;
	mbWaitingForHumanSpawn = false;
	mfPreviousHumanArmor = 0.0f;
	mPendingStatusChanges.clear();

	if (gameFlags & GameFlags::kMainMenu)
	{
		CreateNewFrame(gameFlags);
	}
	else if (gameFlags & GameFlags::kGame)
	{
		CreateNewFrame(gameFlags);
	}
	else if (gameFlags & GameFlags::kContinue)
	{
		if (!engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, AutosaveFile(), CurrentFrame()) || CurrentFrame().interpolate.gameFlags & GameFlags::kDeathScreen)
		{
			// Load failed or on death screen - create new game
			CreateNewFrame(GameFlags::kGame);
		}
		else if (CurrentFrame().postRender.players.iCount > 0)
		{
			// Backward compat: assign first player as human after loading
			mHumanPlayerId = CurrentFrame().postRender.players.puiIds[0];
			mfPreviousHumanArmor = CurrentFrame().postRender.players.pfArmors[0];
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

	// Heap: fstream internal buffers and filesystem::path strings from WriteVersionedFile/RemoveFile.
	// Stream internals can't use workbuffer. Only called on state transitions, not per-frame.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if (CurrentFrame().interpolate.gameFlags & GameFlags::kDeathScreen)
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
	// Heap: filesystem::path construction and std::filesystem::remove() allocate internally.
	// Can't replace OS filesystem calls with workbuffer. Only called on player death.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mbSavedFrame = false;
	engine::gpFileManager->RemoveFile({engine::FileFlags::kAppDataDirectory}, AutosaveFile());
}

void Game::ProcessMenuInput(const MenuInput& rMenuInput)
{
	if constexpr (kbEnableDebugInput)
	{
		if (InMainMenu() && (rMenuInput.flags & MenuInputFlags::kQuickload || rMenuInput.flags & MenuInputFlags::kResetFrame))
		{
			gpGame->ChangeFrame(GameFlags::kGame);
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
		mMenuFlags.Set(engine::MenuFlags::kMouseVisible);
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
			engine::gSunAngleOverride.Set(game::gpCamera->SunAngle(true));
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
		return mMenuMusicPlaylist.at(miMenuMusicIndex);
	}
	else
	{
		miGameMusicIndex = (miGameMusicIndex + 1) % mGameMusicPlaylist.size();
		return mGameMusicPlaylist.at(miGameMusicIndex);
	}
}

} // namespace game
