#include "Game.h"

#ifdef BT_CLIENT
#include "Audio/AudioManager.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/CommandBufferManager.h"
#include "Graphics/Managers/ParticleManager.h"

#include "Graphics/Camera.h"
#endif
#include "File/DifferenceStream.h"
#include "Graphics/Islands.h"
#include "Input/RawInputManager.h"
#ifdef BT_CLIENT
#include "Network/NetworkClient.h"
#endif
#ifdef BT_SERVER
#include "Network/NetworkServer.h"
#endif

#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
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
#ifdef BT_SERVER
	CreateNewFrame(GameFlags::kGame);
	meUiState = kNone;
#else
	CreateNewFrame(GameFlags::kMainMenu);
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
		if (mSpawnFlags & SpawnFlags::kWaitingForHumanSpawn && !mHumanPlayerId.IsValid() && iPlayerCount > 0
		    && !(rCurrentFrame.interpolate.gameFlags & GameFlags::kDeathScreen))
		{
			mHumanPlayerId = rPlayersPostRender.puiIds[iPlayerCount - 1];
			mfPreviousHumanArmor = rPlayersPostRender.pfArmors[iPlayerCount - 1];
			mSpawnFlags = {};
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
			CurrentFrame(mHumanGridCoord).interpolate.gameFlags.Set(GameFlags::kDeathScreen);
			mHumanPlayerId = {};
			mfPreviousHumanArmor = 0.0f;
			humanFlags.Set(HumanFlags::kJustDied);
		}

		// Camera shake: detect armor damage on human player
		if (humanFlags & HumanFlags::kAlive)
		{
			float fCurrentArmor = rPlayersPostRender.pfArmors[iHumanIndex];
#ifdef BT_CLIENT
			if (fCurrentArmor < mfPreviousHumanArmor)
			{
				mCamera.mfShake = std::min(mCamera.mfShake + kfCameraShakeAdd, kfCameraShakeMax);
			}
#endif
			mfPreviousHumanArmor = fCurrentArmor;
		}
	}

	// --- Human input ---

	FrameInput frameInput {};
	frameInput.playerInputs.resize(iPlayerCount);
#ifdef BT_CLIENT
	RawInputToFrameInput(engine::gpRawInputManager->mRawInput, frameInput, iHumanIndex);
	gpInput->UpdateFrameInputPressed(engine::gpRawInputManager->mRawInput, frameInput);
#endif

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

#ifdef BT_CLIENT
	if (coord == mHumanGridCoord && rCurrentFrame.interpolate.gameFlags & GameFlags::kGame)
	{
		if (humanFlags.Empty())
		{
			if (!(CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kDeathScreen) && !(mSpawnFlags & SpawnFlags::kWaitingForHumanSpawn))
			{
				// Initial spawn: no human, no death screen
				mPendingStatusChanges.push_back({.eType = StatusChangeType::kSpawnPlayer});
				mSpawnFlags.Set(SpawnFlags::kWaitingForHumanSpawn);
			}
			else if (mSpawnFlags & SpawnFlags::kRespawnRequested && !(mSpawnFlags & SpawnFlags::kWaitingForHumanSpawn))
			{
				// Respawn after death screen
				mPendingStatusChanges.push_back({.eType = StatusChangeType::kRespawnPlayer});
				mHumanGridCoord = engine::kOriginCoord;
				mSpawnFlags.Set(SpawnFlags::kWaitingForHumanSpawn);
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
#endif

	return frameInput;
}

void Game::ComputeActiveSet()
{
#ifdef BT_SERVER
	ComputeActiveSetServer();
#else
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
	if (!std::ranges::contains(mActiveCoords, engine::kOriginCoord))
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
		return !std::ranges::contains(mActiveCoords, rPair.first);
	});

	// Update island rendering to match active frames
	engine::gpIslands->UpdateActiveIslands(mCurrentFrames, mActiveCoords);
#endif
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
#ifdef BT_SERVER
	BuildFrameInputsServer();
#else
	// Heap: unordered_map clear/insert for per-coordinate FrameInputs. Map persists as Game member
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mFrameInputs.clear();

	if (IsNetworkMode())
	{
		for (const engine::GridCoord& rCoord : mActiveCoords)
		{
			const Frame& rCurrentFrame = CurrentFrame(rCoord);
			const PlayersInterpolate& rPlayers = rCurrentFrame.interpolate.players;
			const PlayersPostRender& rPlayersPostRender = rCurrentFrame.postRender.players;
			int64_t iPlayerCount = rPlayers.iCount;

			FrameInput& rFrameInput = mFrameInputs[rCoord];
			rFrameInput.playerInputs.resize(iPlayerCount);

			// Inject server StatusChanges
			auto scIt = mServerStatusChanges.find(rCoord);
			if (scIt != mServerStatusChanges.end())
			{
				rFrameInput.statusChanges = std::move(scIt->second);
			}

			if (rCoord != mHumanGridCoord)
			{
				continue;
			}

			// Map human input to the correct player index
			int64_t iHumanIndex = -1;
			if (mHumanPlayerId.IsValid())
			{
				auto idIt = rPlayers.idToIndexMap.find(mHumanPlayerId);
				if (idIt != rPlayers.idToIndexMap.end())
				{
					iHumanIndex = idIt->second;
				}
				else
				{
					// Human died: was valid but no longer in collection
					CurrentFrame(mHumanGridCoord).interpolate.gameFlags.Set(GameFlags::kDeathScreen);
					mHumanPlayerId = {};
					mfPreviousHumanArmor = 0.0f;
				}
			}

			RawInputToFrameInput(engine::gpRawInputManager->mRawInput, rFrameInput, iHumanIndex);
			gpInput->UpdateFrameInputPressed(engine::gpRawInputManager->mRawInput, rFrameInput);

			// Camera shake: detect armor damage on human player
			if (iHumanIndex >= 0)
			{
				float fCurrentArmor = rPlayersPostRender.pfArmors[iHumanIndex];
				if (fCurrentArmor < mfPreviousHumanArmor)
				{
					mCamera.mfShake = std::min(mCamera.mfShake + kfCameraShakeAdd, kfCameraShakeMax);
				}
				mfPreviousHumanArmor = fCurrentArmor;
			}
		}
		return;
	}

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		mFrameInputs[rCoord] = BuildFrameInput(CurrentFrame(rCoord), rCoord);
	}
#endif
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

static void SpawnTransfer(Frame& rFrame, StatusChangeType eType, const TransferData& data, engine::alignment_t playerAlignment)
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
#else
	if (IsNetworkMode())
	{
		mHumanGridCoord = mpNetworkClient->GetAssignedGridCoord();
		return;
	}

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

			// During replay, recorded transfers handle the human cell via ApplyTransferStatusChanges
			if (dest == mHumanGridCoord && mpDifferenceStreamReader != nullptr)
			{
				continue;
			}

			SpawnTransfer(rDestFrame, rRequest.eType, data, mPlayerAlignment);

			// Record transfers into human's frame for replay determinism
			if (dest == mHumanGridCoord)
			{
				mPendingTransferChanges.push_back({.eType = rRequest.eType, .data = data});
			}

			// Track human player transfer
			if (rRequest.eType == StatusChangeType::kTransferPlayer &&
				mHumanPlayerId.IsValid() && rRequest.iEntityId == mHumanPlayerId.ToUuid().Value())
			{
				mHumanGridCoord = dest;
				ASSERT(mNextFrames.contains(mHumanGridCoord));
				mHumanPlayerId = rDestFrame.postRender.players.puiIds[rDestFrame.postRender.players.iCount - 1];
				mfPreviousHumanArmor = data.fHealth;
			}
		}
	}
#endif
}

void Game::ApplyTransferStatusChanges(Frame& rFrame, FrameInput& rFrameInput)
{
	// Heap: Spawns into frame may grow SOA buffers
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		switch (rStatusChange.eType)
		{
			case StatusChangeType::kTransferSpaceship:
			case StatusChangeType::kTransferBlaster:
			case StatusChangeType::kTransferMissile:
			case StatusChangeType::kTransferPlayer:
				SpawnTransfer(rFrame, rStatusChange.eType, rStatusChange.data, rFrame.postRender.playerAlignment);
				break;
			default:
				break;
		}
	}

	// Remove transfer StatusChanges so Spawn phase doesn't see them
	std::erase_if(rFrameInput.statusChanges, [](const StatusChange& rStatusChange)
	{
		return rStatusChange.eType == StatusChangeType::kTransferSpaceship ||
			rStatusChange.eType == StatusChangeType::kTransferBlaster ||
			rStatusChange.eType == StatusChangeType::kTransferMissile ||
			rStatusChange.eType == StatusChangeType::kTransferPlayer;
	});
}

Game::~Game()
{
#ifdef BT_CLIENT
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
	mPlayerAi.Reset();
	mfSpawnTimer = 0.0f;
	engine::ResetRealTime();

	mHumanPlayerId = {};
	mSpawnFlags = {};
	mfPreviousHumanArmor = 0.0f;
	mHumanGridCoord = engine::kOriginCoord;
	mPendingStatusChanges.clear();
	mPendingTransferChanges.clear();
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
	return CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kGame && meUiState == kNone;
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
	meUiState = kNone;
}

void Game::ChangeFrame(GameFlags_t gameFlags)
{
#ifdef BT_CLIENT
	DisconnectFromServer();
#endif

	if ((gameFlags & GameFlags::kMainMenu && CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kMainMenu) ||
	    (gameFlags & GameFlags::kGame && CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kGame))
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
	mSpawnFlags = {};
}

#ifdef BT_CLIENT

void Game::ConnectToServer(const char* pServerAddress)
{
	// Heap: NetworkClient allocates ENet host and peer
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpNetworkClient = std::make_unique<engine::NetworkClient>(pServerAddress, engine::kuiDefaultPort);
}

void Game::DisconnectFromServer()
{
	// Heap: NetworkClient destructor triggers ENet disconnect and cleanup
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpNetworkClient.reset();
	mServerStatusChanges.clear();
	mServerCrcs.clear();
}

void Game::PollNetworkClient()
{
	if (mpNetworkClient == nullptr)
	{
		return;
	}

	// Heap: ENet polling allocates packets, DrainReceived* moves vectors
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mpNetworkClient->Poll();

	// Check for player assignment
	game::player_t assignedId = mpNetworkClient->GetAssignedPlayerId();
	if (assignedId.IsValid() && assignedId != mHumanPlayerId)
	{
		mHumanPlayerId = assignedId;
		mHumanGridCoord = mpNetworkClient->GetAssignedGridCoord();
		mSpawnFlags = {};
	}

	ApplyReceivedFullStates();
	ApplyReceivedUpdates();
}

void Game::ApplyReceivedFullStates()
{
	// Heap: Moving unique_ptr<Frame> into mCurrentFrames, make_unique<Frame> for mNextFrames
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (engine::ReceivedFullState& rFullState : mpNetworkClient->DrainReceivedFullStates())
	{
		mCurrentFrames[rFullState.coord] = std::move(rFullState.pFrame);
		if (!mNextFrames.contains(rFullState.coord))
		{
			mNextFrames[rFullState.coord] = std::make_unique<Frame>();
		}
	}
}

void Game::ApplyReceivedUpdates()
{
	// Heap: unordered_map clear/insert for per-coordinate status changes and CRCs
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mServerStatusChanges.clear();
	mServerCrcs.clear();

	for (engine::ReceivedUpdate& rUpdate : mpNetworkClient->DrainReceivedUpdates())
	{
		for (engine::ReceivedGridUpdate& rGridUpdate : rUpdate.gridUpdates)
		{
			mServerCrcs[rGridUpdate.coord] = rGridUpdate.serverCrc;
			if (!rGridUpdate.statusChanges.empty())
			{
				std::vector<StatusChange>& rExisting = mServerStatusChanges[rGridUpdate.coord];
				rExisting.insert(rExisting.end(), std::make_move_iterator(rGridUpdate.statusChanges.begin()), std::make_move_iterator(rGridUpdate.statusChanges.end()));
			}
		}
	}
}

void Game::SendNetworkInput()
{
	if (mpNetworkClient == nullptr || !mpNetworkClient->IsConnected() || !mHumanPlayerId.IsValid())
	{
		return;
	}

	const Frame& rCurrentFrame = CurrentFrame(mHumanGridCoord);
	const PlayersInterpolate& rPlayers = rCurrentFrame.interpolate.players;
	auto idIt = rPlayers.idToIndexMap.find(mHumanPlayerId);
	if (idIt == rPlayers.idToIndexMap.end())
	{
		return;
	}

	int64_t iHumanIndex = idIt->second;
	auto frameIt = mFrameInputs.find(mHumanGridCoord);
	if (frameIt == mFrameInputs.end())
	{
		return;
	}

	const FrameInput& rFrameInput = frameIt->second;
	const PlayerInput& rPlayerInput = rFrameInput.playerInputs.at(iHumanIndex);

	mpNetworkClient->SendInput(static_cast<uint16_t>(mHumanPlayerId.ToUuid().Value()), rPlayerInput, rFrameInput.bGamepad, rFrameInput.fRotateEye);
}

void Game::ValidateServerCrcs()
{
	if (mpNetworkClient == nullptr || mServerCrcs.empty())
	{
		return;
	}

	for (const auto& [rCoord, serverCrc] : mServerCrcs)
	{
		if (!mCurrentFrames.contains(rCoord))
		{
			continue;
		}

		common::crc_t clientCrc = CurrentFrame(rCoord).ServerCrc();
		if (clientCrc != serverCrc)
		{
			Log("Desync at ({},{}): server={} client={}", rCoord.x, rCoord.y, serverCrc, clientCrc);
			mpNetworkClient->SendDesyncReport(miFrameCounter, rCoord, serverCrc, clientCrc);
		}
	}

	mServerCrcs.clear();
}

#endif // BT_CLIENT

#ifdef BT_SERVER

static constexpr float kfSpawnInterval = 2.0f;

void Game::ComputeActiveSetServer()
{
	// Heap: mActiveCoords vector clear/push_back may allocate. Persists as Game member across frame updates
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	mActiveCoords.clear();

	// Union all clients' activeCoords
	const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (const engine::ClientConnection& rClient : rClients)
	{
		for (const engine::GridCoord& rCoord : rClient.activeCoords)
		{
			if (!std::ranges::contains(mActiveCoords, rCoord))
			{
				mActiveCoords.push_back(rCoord);
			}
		}
	}

	// Origin is always active
	if (!std::ranges::contains(mActiveCoords, engine::kOriginCoord))
	{
		mActiveCoords.push_back(engine::kOriginCoord);
	}

	// Create frames at missing coordinates
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		if (!mCurrentFrames.contains(rCoord))
		{
			CreateFrameAtCoord(rCoord);
		}
	}

	// Delete frames outside the active set
	std::erase_if(mCurrentFrames, [this](const auto& rPair)
	{
		return !std::ranges::contains(mActiveCoords, rPair.first);
	});

	engine::gpIslands->UpdateActiveIslands(mCurrentFrames, mActiveCoords);
}

void Game::BuildFrameInputsServer()
{
	// Heap: unordered_map clear/insert, vector resize for playerInputs and statusChanges
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mFrameInputs.clear();
	mBroadcastStatusChanges.clear();

	const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();

	// Initialize FrameInputs for all active coordinates
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		const Frame& rCurrentFrame = CurrentFrame(rCoord);
		int64_t iPlayerCount = rCurrentFrame.interpolate.players.iCount;

		FrameInput& rFrameInput = mFrameInputs[rCoord];
		rFrameInput.playerInputs.resize(iPlayerCount);
	}

	// Map client inputs to their human player's coordinate
	for (const engine::PendingInput& rInput : engine::gpNetworkServer->DrainPendingInputs())
	{
		// Find which client this input belongs to
		const engine::ClientConnection* pClient = nullptr;
		for (const engine::ClientConnection& rClient : rClients)
		{
			if (rClient.iClientId == rInput.iClientId)
			{
				pClient = &rClient;
				break;
			}
		}
		if (pClient == nullptr || !pClient->humanPlayerId.IsValid())
		{
			continue;
		}

		// Find the player index in the frame
		engine::GridCoord coord = pClient->humanGridCoord;
		auto frameIt = mFrameInputs.find(coord);
		if (frameIt == mFrameInputs.end())
		{
			continue;
		}

		const Frame& rCurrentFrame = CurrentFrame(coord);
		auto idIt = rCurrentFrame.interpolate.players.idToIndexMap.find(pClient->humanPlayerId);
		if (idIt == rCurrentFrame.interpolate.players.idToIndexMap.end())
		{
			continue;
		}

		int64_t iHumanIndex = idIt->second;
		FrameInput& rFrameInput = frameIt->second;

		if (iHumanIndex < static_cast<int64_t>(rFrameInput.playerInputs.size()))
		{
			rFrameInput.playerInputs[iHumanIndex].flags = rInput.heldFlags;
			rFrameInput.playerInputs[iHumanIndex].f3Move = rInput.f3Move;
			rFrameInput.playerInputs[iHumanIndex].vecDirection = rInput.vecDirection;
		}

		rFrameInput.pressedFlags = rInput.pressedFlags;
		rFrameInput.bGamepad = rInput.bGamepad;
		rFrameInput.fRotateEye = rInput.fRotateEye;
	}

	// AI input for all non-human players
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		const Frame& rCurrentFrame = CurrentFrame(rCoord);
		FrameInput& rFrameInput = mFrameInputs.at(rCoord);

		if (!(rCurrentFrame.interpolate.gameFlags & GameFlags::kGame))
		{
			continue;
		}

		int64_t iPlayerCount = rCurrentFrame.interpolate.players.iCount;
		const PlayersPostRender& rPlayersPostRender = rCurrentFrame.postRender.players;

		auto isHumanIndex = [&](int64_t i) -> bool
		{
			for (const engine::ClientConnection& rClient : rClients)
			{
				if (rClient.humanGridCoord == rCoord && rClient.humanPlayerId.IsValid())
				{
					auto idIt = rCurrentFrame.interpolate.players.idToIndexMap.find(rClient.humanPlayerId);
					if (idIt != rCurrentFrame.interpolate.players.idToIndexMap.end() && idIt->second == i)
					{
						return true;
					}
				}
			}
			return false;
		};

		for (int64_t i = 0; i < iPlayerCount; ++i)
		{
			if (isHumanIndex(i))
			{
				continue;
			}

			if (rPlayersPostRender.pFlags[i] & PlayerFlags::kExploding)
			{
				continue;
			}

			mPlayerAi.UpdatePlayer(rCurrentFrame, i, rFrameInput.playerInputs.at(i));
		}
	}

	// AI wingmen spawning: decrement timer once per tick, spawn into a coordinate with a living human
	mfSpawnTimer -= kfDeltaTime;
	if (mfSpawnTimer <= 0.0f)
	{
		for (const engine::GridCoord& rCoord : mActiveCoords)
		{
			const Frame& rCurrentFrame = CurrentFrame(rCoord);
			if (!(rCurrentFrame.interpolate.gameFlags & GameFlags::kGame))
			{
				continue;
			}

			if (rCurrentFrame.interpolate.players.iCount >= kiMaxSpawnedPlayers)
			{
				continue;
			}

			bool bHumanAlive = false;
			for (const engine::ClientConnection& rClient : rClients)
			{
				if (rClient.humanGridCoord == rCoord && rClient.humanPlayerId.IsValid())
				{
					auto idIt = rCurrentFrame.interpolate.players.idToIndexMap.find(rClient.humanPlayerId);
					if (idIt != rCurrentFrame.interpolate.players.idToIndexMap.end())
					{
						bHumanAlive = true;
						break;
					}
				}
			}

			if (bHumanAlive)
			{
				mFrameInputs.at(rCoord).statusChanges.push_back({.eType = StatusChangeType::kSpawnPlayer});
				mfSpawnTimer = kfSpawnInterval;
				break;
			}
		}
	}

	// Add spawn StatusChanges for clients waiting for initial spawn
	for (const ClientSpawnInfo& rInfo : mClientsWaitingForSpawn)
	{
		mFrameInputs[rInfo.spawnCoord].statusChanges.push_back({.eType = StatusChangeType::kSpawnPlayer});
	}

	// Save StatusChanges for broadcasting
	for (const auto& [rCoord, rFrameInput] : mFrameInputs)
	{
		if (!rFrameInput.statusChanges.empty())
		{
			mBroadcastStatusChanges[rCoord] = rFrameInput.statusChanges;
		}
	}

	// Take snapshot of player IDs at spawn coordinates for FinalizeNewClientsServer
	mPreSpawnPlayerIds.clear();
	if (!mClientsWaitingForSpawn.empty() && mCurrentFrames.contains(engine::kOriginCoord))
	{
		const PlayersPostRender& rPlayers = CurrentFrame(engine::kOriginCoord).postRender.players;
		for (int64_t i = 0; i < rPlayers.iCount; ++i)
		{
			mPreSpawnPlayerIds.push_back(rPlayers.puiIds[i]);
		}
	}
}

void Game::ProcessSpawnRequestsServer()
{
	// Heap: vector push_back for spawn StatusChanges
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::PendingSpawnRequest& rRequest : engine::gpNetworkServer->DrainPendingSpawnRequests())
	{
		const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
		for (const engine::ClientConnection& rClient : rClients)
		{
			if (rClient.iClientId != rRequest.iClientId)
			{
				continue;
			}

			if (rRequest.flags & engine::ClientRequestFlags::kRespawnRequested ||
			    rRequest.flags & engine::ClientRequestFlags::kSpawnRequested)
			{
				mClientsWaitingForSpawn.push_back({rRequest.iClientId, engine::kOriginCoord});
			}
			break;
		}
	}
}

void Game::HarvestTransfersServer()
{
	// Heap: Transfer spawns into destination frames, which may grow SOA buffers and update idToIndexMaps
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mPendingSubscriptionUpdates.clear();

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

			TransferData data = rRequest.data;

			auto it = mNextFrames.find(dest);
			if (it == mNextFrames.end() || it->second == nullptr)
			{
				continue;
			}

			Frame& rDestFrame = *it->second;
			SpawnTransfer(rDestFrame, rRequest.eType, data, mPlayerAlignment);

			// Record transfer for broadcasting
			mBroadcastStatusChanges[dest].push_back({.eType = rRequest.eType, .data = data});

			// Track human player transfers for subscription updates
			if (rRequest.eType == StatusChangeType::kTransferPlayer && rRequest.iEntityId != 0)
			{
				player_t transferredPlayerId {engine::uuid_t {rRequest.iEntityId}};

				const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
				for (const engine::ClientConnection& rClient : rClients)
				{
					if (rClient.humanPlayerId.IsValid() && transferredPlayerId == rClient.humanPlayerId)
					{
						mPendingSubscriptionUpdates.push_back({rClient.iClientId, dest});
						break;
					}
				}
			}
		}
	}
}

void Game::BroadcastStatusChangesServer(int64_t iFrame)
{
	// Heap: vector construction for grid updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<std::pair<engine::GridCoord, std::pair<common::crc_t, std::span<const StatusChange>>>> gridUpdates;

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		common::crc_t serverCrc = CurrentFrame(rCoord).ServerCrc();

		auto it = mBroadcastStatusChanges.find(rCoord);
		if (it != mBroadcastStatusChanges.end())
		{
			gridUpdates.push_back({rCoord, {serverCrc, std::span<const StatusChange>(it->second)}});
		}
		else
		{
			gridUpdates.push_back({rCoord, {serverCrc, {}}});
		}
	}

	engine::gpNetworkServer->BroadcastUpdate(iFrame, gridUpdates);
}

void Game::HandleNewClientsServer()
{
	// Heap: vector push_back for waiting clients
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (const engine::ClientConnection& rClient : rClients)
	{
		if (rClient.humanPlayerId.IsValid())
		{
			continue;
		}

		// Check if already waiting for spawn
		bool bAlreadyWaiting = false;
		for (const ClientSpawnInfo& rInfo : mClientsWaitingForSpawn)
		{
			if (rInfo.iClientId == rClient.iClientId)
			{
				bAlreadyWaiting = true;
				break;
			}
		}
		if (bAlreadyWaiting)
		{
			continue;
		}

		mClientsWaitingForSpawn.push_back({rClient.iClientId, engine::kOriginCoord});
	}
}

void Game::FinalizeNewClientsServer(int64_t iFrame)
{
	if (mClientsWaitingForSpawn.empty())
	{
		return;
	}

	// Heap: vector construction for full state frames
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Find newly spawned player IDs (present now but not in pre-spawn snapshot)
	const PlayersPostRender& rPlayers = CurrentFrame(engine::kOriginCoord).postRender.players;
	std::vector<player_t> newPlayerIds;
	for (int64_t i = 0; i < rPlayers.iCount; ++i)
	{
		if (!std::ranges::contains(mPreSpawnPlayerIds, rPlayers.puiIds[i]))
		{
			newPlayerIds.push_back(rPlayers.puiIds[i]);
		}
	}

	// Assign new players to waiting clients (in order)
	size_t iAssignCount = std::min(mClientsWaitingForSpawn.size(), newPlayerIds.size());
	for (size_t i = 0; i < iAssignCount; ++i)
	{
		int64_t iClientId = mClientsWaitingForSpawn[i].iClientId;
		player_t playerId = newPlayerIds[i];

		engine::gpNetworkServer->SendAssignPlayer(iClientId, playerId, engine::kOriginCoord);

		// Gather frames for client's active set (computed by SendAssignPlayer)
		const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
		for (const engine::ClientConnection& rClient : rClients)
		{
			if (rClient.iClientId != iClientId)
			{
				continue;
			}

			std::vector<std::pair<engine::GridCoord, const Frame*>> clientFrames;
			for (const engine::GridCoord& rCoord : rClient.activeCoords)
			{
				if (mCurrentFrames.contains(rCoord))
				{
					clientFrames.push_back({rCoord, &CurrentFrame(rCoord)});
				}
			}

			engine::gpNetworkServer->SendFullState(iClientId, iFrame, clientFrames);
			break;
		}
	}

	mClientsWaitingForSpawn.erase(mClientsWaitingForSpawn.begin(), mClientsWaitingForSpawn.begin() + static_cast<int64_t>(iAssignCount));
}

void Game::HandleSubscriptionUpdatesServer(int64_t iFrame)
{
	if (mPendingSubscriptionUpdates.empty())
	{
		return;
	}

	// Heap: vector construction for new cell frames
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const SubscriptionUpdate& rUpdate : mPendingSubscriptionUpdates)
	{
		// Ensure frames exist at the new active set before sending
		// (ComputeActiveSetServer on next tick will create them, but we need them now)
		engine::GridCoord newCoord = rUpdate.newCoord;
		for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
		{
			engine::GridCoord neighbor {newCoord.x + rOffset.x, newCoord.y + rOffset.y};
			if (!mCurrentFrames.contains(neighbor))
			{
				CreateFrameAtCoord(neighbor);
			}
		}
		if (!mCurrentFrames.contains(newCoord))
		{
			CreateFrameAtCoord(newCoord);
		}

		// Gather frames for newly-visible cells
		std::vector<std::pair<engine::GridCoord, const Frame*>> newCellFrames;
		newCellFrames.push_back({newCoord, &CurrentFrame(newCoord)});
		for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
		{
			engine::GridCoord neighbor {newCoord.x + rOffset.x, newCoord.y + rOffset.y};
			if (mCurrentFrames.contains(neighbor))
			{
				newCellFrames.push_back({neighbor, &CurrentFrame(neighbor)});
			}
		}

		engine::gpNetworkServer->UpdateClientSubscription(rUpdate.iClientId, newCoord, iFrame, newCellFrames);
	}

	mPendingSubscriptionUpdates.clear();
}

#endif // BT_SERVER

} // namespace game
