#include "Game.h"

#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Profile/ProfileManager.h"

namespace game
{

using enum UiState;

// Camera shake
constexpr float kfCameraShakeAdd = 0.25f;
constexpr float kfCameraShakeMax = 1.0f;

Game::Game()
{
	gpGame = this;

	// Set up alignments
	uint32_t uiNextAlignment = 1;
	mPlayerAlignment = engine::alignment_t {uiNextAlignment++};
	mEnemyAlignment = engine::alignment_t {uiNextAlignment++};
	mAlignments.AddAlignment(mPlayerAlignment, mEnemyAlignment, engine::AlignmentFlags::kEnemies);

	// Allocate frames
#ifdef BT_SERVER
	CreateNewFrame(GameFlags::kGame);
	meUiState = kNone;
#else
	CreateNewFrame(GameFlags::kMainMenu);
#endif

	// Start reconcile worker
#ifdef BT_CLIENT
	if constexpr (kbEnableReconcileThread)
	{
		mpReconcileWorker = std::make_unique<common::PersistentWorker>(common::kThreadReconcile, 10 * 1'024 * 1'024);
	}
#endif

	// Start music
#ifdef BT_CLIENT
	engine::gpAudioManager->PlayMusic(mMenuMusicPlaylist.at(0));
	engine::gpAudioManager->Set3dSettings(10.0f, 0.0f, 150.0f, 0.05f);
	engine::gpAudioManager->SetNextMusicTrackCallback([this]()
	{
		return GetNextMusicTrack();
	});
#endif

}

std::optional<int64_t> Game::HumanPlayerIndex(const PlayersInterpolate& rPlayers) const
{
	if (mHumanPlayerId.IsValid())
	{
		auto it = rPlayers.idToIndexMap.find(mHumanPlayerId);
		if (it != rPlayers.idToIndexMap.end())
		{
			return it->second;
		}
	}

	return std::nullopt;
}

void Game::ComputeActiveSet()
{
#ifdef BT_SERVER
	ComputeActiveSetServer();
#else
	// Heap: mActiveCoords vector clear/push_back may allocate. Persists as Game member across frame updates
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	mActiveCoords.clear();

	if (!mSubscribedFrames.contains(mHumanGridCoord))
	{
		return;
	}

	mActiveCoords.push_back(mHumanGridCoord);

	if (!InMainMenu())
	{
		for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
		{
			engine::GridCoord neighbor {mHumanGridCoord.x + rOffset.x, mHumanGridCoord.y + rOffset.y};
			if (!mSubscribedFrames.contains(neighbor))
			{
				CreateFrameAtCoord(neighbor);
			}
			mActiveCoords.push_back(neighbor);
		}

		// Origin is always active
		if (!std::ranges::contains(mActiveCoords, engine::kOriginCoord))
		{
			if (!mSubscribedFrames.contains(engine::kOriginCoord))
			{
				CreateFrameAtCoord(engine::kOriginCoord);
			}
			mActiveCoords.push_back(engine::kOriginCoord);
		}
	}

	// Delete frames outside the active set
	std::erase_if(mSubscribedFrames, [this](const auto& rPair)
	{
		return !std::ranges::contains(mActiveCoords, rPair.first);
	});

	// Update island rendering to match active frames
	engine::gpIslands->UpdateActiveIslands(mSubscribedFrames, mActiveCoords);
#endif
}

void Game::EnsureNextFrames()
{
	// Heap: unordered_map insertion + make_unique<Frame>. Frames persist in mNextFrames across game lifetime
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		if (mSubscribedFrames[rCoord].next == nullptr)
		{
			mSubscribedFrames[rCoord].next = std::make_unique<Frame>();
		}
	}
}

void Game::BuildFrameInputs()
{
#ifdef BT_SERVER
	BuildFrameInputsServer();
#else
	// Heap: unordered_map clear/insert for per-coordinate FrameInputs. Map persists as Game member
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mFrameInputs.clear();

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		if (!mSubscribedFrames.contains(rCoord))
		{
			continue;
		}

		mFrameInputs[rCoord];
	}

	// Camera shake
	if (mHumanPlayerId.IsValid() && mSubscribedFrames.contains(mHumanGridCoord))
	{
		const Frame& rCurrentFrame = CurrentFrame(mHumanGridCoord);
		const PlayersInterpolate& rPlayers = *rCurrentFrame.interpolate.pPlayers;
		const PlayersPostRender& rPlayersPostRender = *rCurrentFrame.postRender.pPlayers;

		auto idIt = rPlayers.idToIndexMap.find(mHumanPlayerId);
		if (idIt != rPlayers.idToIndexMap.end())
		{
			int64_t iHumanIndex = idIt->second;

			// Camera shake: detect armor damage on human player
			float fCurrentArmor = rPlayersPostRender.pfArmors[iHumanIndex];
			if (fCurrentArmor < mfPreviousHumanArmor)
			{
				mCamera.mfShake = std::min(mCamera.mfShake + kfCameraShakeAdd, kfCameraShakeMax);
			}
			mfPreviousHumanArmor = fCurrentArmor;
		}
	}
#endif
}

void Game::CreateFrameAtCoord(engine::GridCoord coord)
{
	// Heap: unordered_map insertion + make_unique<Frame>. Frame persists in mCurrentFrames across game lifetime
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::unique_ptr<Frame>& pFrame = mSubscribedFrames[coord].current;
	pFrame = std::make_unique<Frame>();
	pFrame->interpolate.iFrame = miFrameCounter;
	pFrame->interpolate.fCurrentTime = mfCurrentTime;
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

void SpawnTransfer(Frame& rFrame, StatusChangeType eType, const TransferData& data, engine::alignment_t playerAlignment)
{
	switch (eType)
	{
		case StatusChangeType::kTransferSpaceship:
			SpaceshipsPostRender::Spawn(rFrame, {
				.vecPosition = data.vecPosition,
				.vecDirection = data.vecDirection,
				.vecVelocity = data.vecVelocity,
				.alignment = data.alignment,
				.fHealth = data.fHealth,
				.fNextBlasterSpawnTime = data.fNextBlasterSpawnTime,
			});
			break;

		case StatusChangeType::kTransferBlaster:
			BlastersPostRender::Spawn(rFrame, {
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
			if (data.alignment == playerAlignment)
			{
				missileFlags.Set(MissileFlags::kTargetEnemy);
			}
			else
			{
				missileFlags.Set(MissileFlags::kTargetPlayer);
			}

			MissilesPostRender::Spawn(rFrame, {
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
#ifdef BT_CLIENT
				.smokeTrailId = data.smokeTrailId,
#endif
			});
			break;
		}

		case StatusChangeType::kTransferPlayer:
			FILE_LOG(0, "[SpawnTransfer] kTransferPlayer: fNextSecondarySpawnTime={:.6f}", data.fNextSecondarySpawnTime);
			PlayersPostRender::Spawn(rFrame, {
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
			break;

		default:
			break;
	}
}

void Game::HarvestTransfers()
{
#ifdef BT_SERVER
	HarvestTransfersServer();
#endif
}

void Game::ApplyTransferStatusChanges(Frame& rFrame, FrameInput& rFrameInput)
{
	// Heap: Spawns into frame may grow SOA buffers
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		if (IsTransferType(rStatusChange.eType))
		{
			SpawnTransfer(rFrame, rStatusChange.eType, rStatusChange.data, rFrame.postRender.playerAlignment);
		}
	}

	// Remove transfer StatusChanges so Spawn phase doesn't see them
	std::erase_if(rFrameInput.statusChanges, [](const StatusChange& rStatusChange)
	{
		return IsTransferType(rStatusChange.eType);
	});
}

Game::~Game()
{
#ifdef BT_CLIENT
	if constexpr (kbEnableReconcileThread)
	{
		if (mbReconcileInFlight)
		{
			mpReconcileWorker->Wait();
			mbReconcileInFlight = false;
		}
	}
	engine::gpAudioManager->SetNextMusicTrackCallback(nullptr);
#endif

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
#ifdef BT_CLIENT
	game::gpCamera->ResetSunAngle();
	engine::gSunAngleOverride.Reset(game::gpCamera->SunAngle(true));
	engine::gbSmokeClear = true;
	engine::gpParticleManager->mbReset = true;
	engine::SmokeTrailsInterpolate::ResetRenderState();
	engine::WindTrailsInterpolate::ResetRenderState();
#endif

	mHumanPlayerId = {};
	mfPreviousHumanArmor = 0.0f;
	mHumanGridCoord = engine::kOriginCoord;
	mActiveCoords.clear();
	mActiveCoords.push_back(mHumanGridCoord);
}

void Game::CreateNewFrame(GameFlags_t gameFlags)
{
	// Heap: make_unique<Frame> with all its SOA collections. The frame must persist in mCurrentFrames
	// across the entire game state lifetime, so workbuffer (lost on Pop) can't hold it.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mSubscribedFrames.clear();
	std::unique_ptr<Frame>& pFrame = mSubscribedFrames[engine::kOriginCoord].current;
	pFrame = std::make_unique<Frame>();
	pFrame->interpolate.gameFlags.Set(gameFlags.meFlags);
	pFrame->postRender.uiFrameId = GenerateFrameId();
	pFrame->postRender.playerAlignment = mPlayerAlignment;
	pFrame->postRender.enemyAlignment = mEnemyAlignment;
	pFrame->postRender.alignments = mAlignments;
	pFrame->postRender.vecArea = XMVectorSet(Frame::kfBaseAreaMinX, Frame::kfBaseAreaMaxY, Frame::kfBaseAreaMaxX, Frame::kfBaseAreaMinY);
	pFrame->postRender.eIslandsFlip = engine::kFlipNone;

	mSubscribedFrames[engine::kOriginCoord].next = std::make_unique<Frame>();
}

bool Game::ShouldTrapCursor()
{
	return !InMainMenu();
}

bool Game::ShouldUseCrosshair()
{
	if (!mSubscribedFrames.contains(mHumanGridCoord))
	{
		return false;
	}
	return CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kGame && meUiState == kNone;
}

void Game::ChangeFrame(GameFlags_t gameFlags)
{
#ifdef BT_CLIENT
	mpDiscoveryScanner.reset();
	DisconnectFromServer();
#endif

	if (mSubscribedFrames.contains(mHumanGridCoord) &&
	    ((gameFlags & GameFlags::kMainMenu && CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kMainMenu) ||
	     (gameFlags & GameFlags::kGame && CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kGame)))
	{
		DEBUG_BREAK();
		return;
	}

	// Start appropriate music playlist for menu or game mode
#ifdef BT_CLIENT
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
#endif

	CreateNewFrame(gameFlags);
	Reset();
}

void Game::ProcessMenuInput(const MenuInput& rMenuInput)
{
	if (meUiState == UiState::kModal)
	{
		return;
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

	if constexpr (kbEnableProfiling)
	{
		if (rMenuInput.flags & MenuInputFlags::kToggleProfileText)
		{
			gpProfileManager->ToggleProfileText();
		}
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
#ifdef BT_CLIENT
			engine::gSunAngleOverride.Set(game::gpCamera->SunAngle(true));
#endif
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

#ifdef BT_CLIENT
	if constexpr (kbEnableScreenshots)
	{
		if (rMenuInput.flags & MenuInputFlags::kToggleScreenshots)
		{
			engine::gpCommandBufferManager->mbSaveScreenshot = !engine::gpCommandBufferManager->mbSaveScreenshot;
		}
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

#ifdef BT_CLIENT
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
#endif

void Game::RestoreReplayMeta(const ReplayMeta& rMeta)
{
	mHumanGridCoord = rMeta.humanGridCoord;
	if (rMeta.iHumanPlayerIdValue != 0)
	{
		mHumanPlayerId = player_t {engine::uuid_t {rMeta.iHumanPlayerIdValue}};
	}
	mfPreviousHumanArmor = rMeta.fPreviousHumanArmor;
}

} // namespace game
