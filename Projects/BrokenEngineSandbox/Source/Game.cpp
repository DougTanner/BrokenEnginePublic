#include "Game.h"

#include "Audio/AudioManager.h"
#include "File/DifferenceStream.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
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

FrameInput Game::BuildFrameInput(const Frame& rCurrentFrame, engine::GridCoord coord)
{
	static constexpr float kfSpawnInterval = 2.0f;

	// Heap: vector::resize on playerInputs/statusChanges in FrameInput. Data must outlive this call
	// (consumed by frame update phases), so workbuffer won't work. Player count varies, so can't pre-allocate.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const PlayersInterpolate& rPlayers = rCurrentFrame.interpolate.players;
	const PlayersPostRender& rPlayersPostRender = rCurrentFrame.postRender.players;
	int64_t iPlayerCount = rPlayers.iCount;

	// --- Human player tracking (only on human's grid coordinate) ---

	HumanFlags_t humanFlags;
	int64_t iHumanIndex = -1;

	if (coord == mHumanGridCoord)
	{
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
		if (mHumanPlayerId.IsValid() && rPlayers.idToIndexMap.contains(mHumanPlayerId))
		{
			humanFlags.Set(HumanFlags::kAlive);
		}
		iHumanIndex = (humanFlags & HumanFlags::kAlive) ? HumanPlayerIndex(rPlayers) : -1;

		// Detect human death: was valid but no longer in collection
		if (mHumanPlayerId.IsValid() && !(humanFlags & HumanFlags::kAlive))
		{
			CurrentFrame().interpolate.gameFlags.Set(GameFlags::kDeathScreen);
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

	// --- Spawn management (only on human's grid coordinate) ---

	if (coord == mHumanGridCoord && rCurrentFrame.interpolate.gameFlags & GameFlags::kGame)
	{
		if (humanFlags.Empty())
		{
			if (!(CurrentFrame().interpolate.gameFlags & GameFlags::kDeathScreen) && !mbWaitingForHumanSpawn)
			{
				// Initial spawn: no human, no death screen
				mPendingStatusChanges.push_back({.eType = StatusChangeType::kSpawnPlayer});
				mbWaitingForHumanSpawn = true;
			}
			else if (mbRespawnRequested && !mbWaitingForHumanSpawn)
			{
				// Respawn after death screen
				mPendingStatusChanges.push_back({.eType = StatusChangeType::kRespawnPlayer});
				mHumanGridCoord = engine::kOriginCoord;
				mbWaitingForHumanSpawn = true;
			}
		}
		else if (humanFlags & HumanFlags::kAlive && iPlayerCount < kiMaxSpawnedPlayers)
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

void Game::ComputeActiveSet()
{
	// Heap: mActiveCoords vector clear/push_back may allocate. Persists as Game member across frame updates
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	mActiveCoords.clear();

	ASSERT(mCurrentFrames.contains(mHumanGridCoord));

	mActiveCoords.push_back(mHumanGridCoord);

	for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
	{
		engine::GridCoord neighbor {mHumanGridCoord.x + rOffset.x, mHumanGridCoord.y + rOffset.y};
		if (!mCurrentFrames.contains(neighbor))
		{
			CreateFrameAtCoord(neighbor);
		}
		mActiveCoords.push_back(neighbor);
	}

	// Origin is always active
	if (std::find(mActiveCoords.begin(), mActiveCoords.end(), engine::kOriginCoord) == mActiveCoords.end())
	{
		if (!mCurrentFrames.contains(engine::kOriginCoord))
		{
			CreateFrameAtCoord(engine::kOriginCoord);
		}
		mActiveCoords.push_back(engine::kOriginCoord);
	}

	// Delete frames outside the active set
	std::erase_if(mCurrentFrames, [this](const auto& rPair)
	{
		const engine::GridCoord& rCoord = rPair.first;
		return std::find(mActiveCoords.begin(), mActiveCoords.end(), rCoord) == mActiveCoords.end();
	});

	// Update island rendering to match active frames
	engine::gpIslands->UpdateActiveIslands(mCurrentFrames, mActiveCoords);
}

void Game::EnsureNextFrames()
{
	// Heap: unordered_map insertion + make_unique<Frame>. Frames persist in mNextFrames across game lifetime
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		if (!mNextFrames.contains(rCoord))
		{
			mNextFrames[rCoord] = std::make_unique<Frame>();
		}
	}
}

void Game::BuildFrameInputs()
{
	// Heap: unordered_map clear/insert for per-coordinate FrameInputs. Map persists as Game member
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mFrameInputs.clear();
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		mFrameInputs[rCoord] = BuildFrameInput(CurrentFrame(rCoord), rCoord);
	}
}

void Game::CreateFrameAtCoord(engine::GridCoord coord)
{
	// Heap: unordered_map insertion + make_unique<Frame>. Frame persists in mCurrentFrames across game lifetime
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::unique_ptr<Frame>& pFrame = mCurrentFrames[coord];
	pFrame = std::make_unique<Frame>();
	pFrame->interpolate.gameFlags.Set(GameFlags::kGame);
	pFrame->postRender.uiFrameId = GenerateFrameId();
	pFrame->postRender.playerAlignment = mPlayerAlignment;
	pFrame->postRender.enemyAlignment = mEnemyAlignment;
	pFrame->postRender.alignments = mAlignments;

	XMVECTOR vecBaseArea = XMVectorSet(Frame::kfBaseAreaMinX, Frame::kfBaseAreaMaxY, Frame::kfBaseAreaMaxX, Frame::kfBaseAreaMinY);
	pFrame->postRender.vecArea = ComputeFrameArea(vecBaseArea, coord);

	// Compute flip from grid coordinate parity for seamless tiling
	bool bFlipX = (std::abs(coord.x) % 2) == 1;
	bool bFlipY = (std::abs(coord.y) % 2) == 1;
	pFrame->postRender.eIslandsFlip = static_cast<engine::IslandsFlip>((bFlipX ? engine::kFlipX : 0) | (bFlipY ? engine::kFlipY : 0));
}

void Game::HarvestTransfers()
{
	// Heap: Transfer spawns into destination frames, which may grow SOA buffers and update idToIndexMaps
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		Frame& rNextFrame = NextFrame(rCoord);
		if (rNextFrame.postRender.transferRequests.empty())
		{
			continue;
		}

		for (const TransferRequest& rRequest : rNextFrame.postRender.transferRequests)
		{
			engine::GridCoord dest {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};

			// World coordinates: position is preserved as-is across frames
			TransferData data = rRequest.data;

			// Drop transfer if destination frame doesn't exist
			auto it = mNextFrames.find(dest);
			if (it == mNextFrames.end() || it->second == nullptr)
			{
				continue;
			}

			Frame& rDestFrame = *it->second;

			// Spawn transferred entity into destination frame
			switch (rRequest.eType)
			{
				case StatusChangeType::kTransferSpaceship:
					SpaceshipsPostRender::Spawn(rDestFrame, {
						.vecPosition = data.vecPosition,
						.vecDirection = data.vecDirection,
						.vecVelocity = data.vecVelocity,
						.alignment = data.alignment,
						.fHealth = data.fHealth,
						.fNextBlasterSpawnTime = data.fNextBlasterSpawnTime,
					});
					break;

				case StatusChangeType::kTransferBlaster:
					BlastersPostRender::Spawn(rDestFrame, {
						.vecPosition = data.vecPosition,
						.vecVelocity = data.vecVelocity,
						.uiTypeIndex = data.uiTypeIndex,
						.alignment = data.alignment,
						.fWindTrailIntensity = data.fWindTrailIntensity,
						.fWindTrailWidth = data.fWindTrailWidth,
						.fWindTrailLengthMultiplier = data.fWindTrailLengthMultiplier,
					});
					break;

				case StatusChangeType::kTransferMissile:
				{
					MissileFlags_t missileFlags;
					if (data.alignment == mPlayerAlignment)
					{
						missileFlags.Set(MissileFlags::kTargetEnemy);
					}
					else
					{
						missileFlags.Set(MissileFlags::kTargetPlayer);
					}

					MissilesPostRender::Spawn(rDestFrame, {
						.vecPosition = data.vecPosition,
						.vecDirection = data.vecDirection,
						.vecVelocity = data.vecVelocity,
						.vecStoredDirection = data.vecDirection,
						.uiTarget = {},
						.fAcceleration = data.fAcceleration,
						.flags = missileFlags,
						.alignment = data.alignment,
						.fDeltaRotationDelay = data.fDeltaRotationDelay,
						.fTime = data.fTime,
						.fExhaustDelay = data.fExhaustDelay,
						.fNextJitter = data.fNextJitter,
					});
					break;
				}

				case StatusChangeType::kTransferPlayer:
					PlayersPostRender::Spawn(rDestFrame, {
						.vecPosition = data.vecPosition,
						.vecDirection = data.vecDirection,
						.vecVelocity = data.vecVelocity,
						.alignment = data.alignment,
						.fArmor = data.fHealth,
						.fShield = data.fShield,
						.fNextBlasterFireTime = data.fNextBlasterFireTime,
						.fNextSecondarySpawnTime = data.fNextSecondarySpawnTime,
						.fShieldCooldown = data.fShieldCooldown,
						.fShieldDownSoundCooldown = data.fShieldDownSoundCooldown,
						.fAnimationTime = data.fAnimationTime,
						.fShieldRotation = data.fShieldRotation,
						.fShieldShrink = data.fShieldShrink,
						.flags = PlayerFlags_t {static_cast<PlayerFlags>(data.uiPlayerFlags)},
					});

					// Track human player transfer
					if (mHumanPlayerId.IsValid() && rRequest.iEntityId == mHumanPlayerId.ToUuid().Value())
					{
						mHumanGridCoord = dest;
						ASSERT(mNextFrames.contains(mHumanGridCoord));
						mHumanPlayerId = rDestFrame.postRender.players.puiIds[rDestFrame.postRender.players.iCount - 1];
						mfPreviousHumanArmor = data.fHealth;
					}
					break;

				default:
					break;
			}
		}
	}
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

	miFrameCounter = 0;
	mfCurrentTime = 0.0f;

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
	// Heap: make_unique<Frame> with all its SOA collections. The frame must persist in mCurrentFrames
	// across the entire game state lifetime, so workbuffer (lost on Pop) can't hold it.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mCurrentFrames.clear();
	mNextFrames.clear();
	std::unique_ptr<Frame>& pFrame = mCurrentFrames[engine::kOriginCoord];
	pFrame = std::make_unique<Frame>();
	pFrame->interpolate.gameFlags.Set(gameFlags.meFlags);
	pFrame->postRender.uiFrameId = GenerateFrameId();
	pFrame->postRender.playerAlignment = mPlayerAlignment;
	pFrame->postRender.enemyAlignment = mEnemyAlignment;
	pFrame->postRender.alignments = mAlignments;
	pFrame->postRender.vecArea = XMVectorSet(Frame::kfBaseAreaMinX, Frame::kfBaseAreaMaxY, Frame::kfBaseAreaMaxX, Frame::kfBaseAreaMinY);
	pFrame->postRender.eIslandsFlip = engine::kFlipNone;

	mNextFrames[engine::kOriginCoord] = std::make_unique<Frame>();
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
		engine::GridCoord humanGridCoord;
		if (!ReadGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, AutosaveFile(), humanGridCoord) || CurrentFrame(humanGridCoord).interpolate.gameFlags & GameFlags::kDeathScreen)
		{
			// Load failed or on death screen - create new game
			CreateNewFrame(GameFlags::kGame);
		}
		else
		{
			mHumanGridCoord = humanGridCoord;
			ASSERT(mCurrentFrames.contains(mHumanGridCoord));
			if (CurrentFrame(mHumanGridCoord).postRender.players.iCount > 0)
			{
				mHumanPlayerId = CurrentFrame(mHumanGridCoord).postRender.players.puiIds[0];
				mfPreviousHumanArmor = CurrentFrame(mHumanGridCoord).postRender.players.pfArmors[0];
			}
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

	// Heap: fstream internal buffers and filesystem::path strings from WriteGrid/RemoveFile.
	// Stream internals can't use workbuffer. Only called on state transitions, not per-frame.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if (CurrentFrame().interpolate.gameFlags & GameFlags::kDeathScreen)
	{
		gpGame->RemoveAutosave();
	}
	else
	{
		WriteGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, AutosaveFile(), mHumanGridCoord);
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
