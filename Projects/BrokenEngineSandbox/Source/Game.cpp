#include "Game.h"

#include "Frame/HealthDamage.h"
#include "Frame/Player.h"
#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Missiles.h"
#include "Frame/Collections/Spaceships.h"
#include "Profile/ProfileManager.h"

namespace game
{

namespace
{

struct ActiveFrameRef
{
	Frame* pNext = nullptr;
	Frame* pCurrent = nullptr;
	FrameInput* pFrameInput = nullptr;
};

} // namespace

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

	const PlayersInterpolate& rPlayers = *rCurrentFrame.interpolate.pPlayers;
	const PlayersPostRender& rPlayersPostRender = *rCurrentFrame.postRender.pPlayers;
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
		// Prediction frames: use local input for human player, zero for others
		for (const engine::GridCoord& rCoord : mActiveCoords)
		{
			const Frame& rCurrentFrame = CurrentFrame(rCoord);
			const PlayersInterpolate& rPlayers = *rCurrentFrame.interpolate.pPlayers;
			const PlayersPostRender& rPlayersPostRender = *rCurrentFrame.postRender.pPlayers;
			int64_t iPlayerCount = rPlayers.iCount;

			FrameInput& rFrameInput = mFrameInputs[rCoord];
			rFrameInput.playerInputs.resize(iPlayerCount);

			// Human's coordinate: capture local input for SendNetworkInput and prediction
			if (rCoord == mHumanGridCoord)
			{
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
						// DT: TEMP
						FILE_LOG("[BuildFrameInputs] DEATH: humanId={} not found at ({},{}) count={}", mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, iPlayerCount);
						{
							int64_t iLogCount = 0;
							for (const auto& [rId, rIdx] : rPlayers.idToIndexMap)
							{
								if (iLogCount >= 4)
								{
									break;
								}
								FILE_LOG("[BuildFrameInputs]   mapEntry: id={} idx={}", rId.ToUuid().Value(), rIdx);
								++iLogCount;
							}
						}

						CurrentFrame(mHumanGridCoord).interpolate.gameFlags.Set(GameFlags::kDeathScreen);
						mHumanPlayerId = {};
						mfPreviousHumanArmor = 0.0f;
					}
				}

				// Use local input for human player prediction
				RawInputToFrameInput(engine::gpRawInputManager->mRawInput, rFrameInput, iHumanIndex);

				// Save local input for SendNetworkInput
				if (iHumanIndex >= 0 && iHumanIndex < static_cast<int64_t>(rFrameInput.playerInputs.size()))
				{
					mLocalPlayerInput = rFrameInput.playerInputs.at(iHumanIndex);
				}
				else
				{
					mLocalPlayerInput = {};
				}

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
			// Non-human player inputs remain zeroed (can't predict remote players)
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
	// Heap: Transfer spawns into destination frames, which may grow SOA buffers and update idToIndexMaps
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Local transfer harvesting (intra-subscription in network mode, all transfers offline)
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

			// During replay, recorded transfers handle the human cell via ApplyTransferStatusChanges
			if (!IsNetworkMode() && dest == mHumanGridCoord && mpDifferenceStreamReader != nullptr)
			{
				continue;
			}

			SpawnTransfer(rDestFrame, rRequest.eType, data, mPlayerAlignment);

			// Record transfers into human's frame for replay determinism (offline only)
			if (!IsNetworkMode() && dest == mHumanGridCoord)
			{
				mPendingTransferChanges.push_back({.eType = rRequest.eType, .data = data});
			}

			// Track human player transfer
			if (rRequest.eType == StatusChangeType::kTransferPlayer &&
				mHumanPlayerId.IsValid() && rRequest.iEntityId == mHumanPlayerId.ToUuid().Value())
			{
				mHumanGridCoord = dest;
				ASSERT(mNextFrames.contains(mHumanGridCoord));
				mHumanPlayerId = rDestFrame.postRender.pPlayers->puiIds[rDestFrame.postRender.pPlayers->iCount - 1];
				mfPreviousHumanArmor = data.fHealth;

				// DT: TEMP
				FILE_LOG("[HarvestTransfers] Human transfer: entityId={} newId={} to=({},{})", rRequest.iEntityId, mHumanPlayerId.ToUuid().Value(), dest.x, dest.y);
			}
		}
	}

	// Apply external server transfers (from outside client's subscription)
	if (IsNetworkMode())
	{
		for (auto& [rCoord, rTransfers] : mServerTransferStatusChanges)
		{
			auto it = mNextFrames.find(rCoord);
			if (it == mNextFrames.end() || it->second == nullptr)
			{
				continue;
			}

			Frame& rDestFrame = *it->second;
			for (const StatusChange& rChange : rTransfers)
			{
				SpawnTransfer(rDestFrame, rChange.eType, rChange.data, mPlayerAlignment);
			}
		}
		mServerTransferStatusChanges.clear();
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
	mpDiscoveryScanner.reset();
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

void Game::StartServerDiscovery()
{
	// Heap: NetworkDiscoveryScanner creates a UDP socket
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpDiscoveryScanner = std::make_unique<engine::NetworkDiscoveryScanner>();
	mpDiscoveryScanner->StartScan();
}

void Game::DisconnectFromServer()
{
	// Heap: NetworkClient destructor triggers ENet disconnect and cleanup
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpNetworkClient.reset();
	mServerUpdateBuffer.clear();
	mConfirmedState = {};
	mServerTransferStatusChanges.clear();
}

void Game::PollNetworkClient()
{
	// Poll LAN discovery scanner
	if (mpDiscoveryScanner != nullptr)
	{
		mpDiscoveryScanner->Poll();

		if (mpDiscoveryScanner->IsFound())
		{
			const char* pAddress = mpDiscoveryScanner->GetFoundAddress();
			mpDiscoveryScanner.reset();
			ChangeFrame(GameFlags::kGame);
			meUiState = UiState::kNone;
			ConnectToServer(pAddress);
		}
		else if (!mpDiscoveryScanner->IsScanning())
		{
			mpDiscoveryScanner.reset();
		}
	}

	if (mpNetworkClient == nullptr)
	{
		return;
	}

	// Heap: ENet polling allocates packets, DrainReceived* moves vectors, stringstream serialization
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mpNetworkClient->Poll();

	if (mpNetworkClient->WasDisconnected())
	{
		ChangeFrame(GameFlags::kMainMenu);
		meUiState = UiState::kPause;
		return;
	}

	// Check for player assignment
	game::player_t assignedId = mpNetworkClient->GetAssignedPlayerId();
	if (assignedId.IsValid())
	{
		if (assignedId != mHumanPlayerId)
		{
			player_t oldHumanPlayerId = mHumanPlayerId; // DT: TEMP
			mHumanPlayerId = assignedId;
			mHumanGridCoord = mpNetworkClient->GetAssignedGridCoord();
			mSpawnFlags = {};

			// DT: TEMP
			FILE_LOG("[PollNetworkClient] Assignment: old={} new={} grid=({},{}) frame={}", oldHumanPlayerId.ToUuid().Value(), mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, miFrameCounter);
		}
		mpNetworkClient->ClearAssignment();
	}

	int64_t iFullStateFrame = ApplyReceivedFullStates();

	// After receiving full state, establish it as confirmed state
	if (iFullStateFrame >= 0)
	{
		// Prune stale coords before establishing confirmed state
		mActiveCoords.clear();
		mActiveCoords.push_back(mHumanGridCoord);
		for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
		{
			engine::GridCoord neighbor {mHumanGridCoord.x + rOffset.x, mHumanGridCoord.y + rOffset.y};
			if (mCurrentFrames.contains(neighbor))
			{
				mActiveCoords.push_back(neighbor);
			}
		}
		if (!std::ranges::contains(mActiveCoords, engine::kOriginCoord) && mCurrentFrames.contains(engine::kOriginCoord))
		{
			mActiveCoords.push_back(engine::kOriginCoord);
		}
		std::erase_if(mCurrentFrames, [this](const auto& rPair)
		{
			return !std::ranges::contains(mActiveCoords, rPair.first);
		});

		mConfirmedState.iFrame = iFullStateFrame;
		mConfirmedState.fCurrentTime = mfCurrentTime;
		mConfirmedState.serializedFrames.clear();
		for (auto& [rCoord, pFrame] : mCurrentFrames)
		{
			std::ostringstream oss;
			oss << *pFrame;
			mConfirmedState.serializedFrames[rCoord] = oss.str();
		}
		mConfirmedState.humanGridCoord = mHumanGridCoord;
		mConfirmedState.humanPlayerId = mHumanPlayerId;
		mConfirmedState.fPreviousHumanArmor = mfPreviousHumanArmor;

		// DT: TEMP
		FILE_LOG("[PollNetworkClient] ConfirmedState: frame={} humanId={} grid=({},{}) coordCount={}", mConfirmedState.iFrame, mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, mConfirmedState.serializedFrames.size());

		// Prune stale entries from buffer
		std::erase_if(mServerUpdateBuffer, [iFullStateFrame](const auto& rPair)
		{
			return rPair.first <= iFullStateFrame;
		});
	}

	// DT: TEMP
	{
		int64_t iMinFrame = mServerUpdateBuffer.empty() ? -1 : mServerUpdateBuffer.begin()->first;
		int64_t iMaxFrame = mServerUpdateBuffer.empty() ? -1 : mServerUpdateBuffer.rbegin()->first;
		FILE_LOG("[PollNetworkClient] BufferAfterPrune: size={} min={} max={} confirmedFrame={}", mServerUpdateBuffer.size(), iMinFrame, iMaxFrame, mConfirmedState.iFrame);
	}

	ApplyReceivedUpdates();
}

int64_t Game::ApplyReceivedFullStates()
{
	// Heap: Moving unique_ptr<Frame> into mCurrentFrames, make_unique<Frame> for mNextFrames
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	int64_t iFullStateFrame = -1;

	std::vector<engine::ReceivedFullState>& rFullStates = mpNetworkClient->DrainReceivedFullStates();
	for (engine::ReceivedFullState& rFullState : rFullStates)
	{
		iFullStateFrame = rFullState.iFrame;
		mCurrentFrames[rFullState.coord] = std::move(rFullState.pFrame);

		// DT: TEMP
		FILE_LOG("[ApplyReceivedFullStates] Replaced coord=({},{}) frame={}", rFullState.coord.x, rFullState.coord.y, iFullStateFrame);

		if (!mNextFrames.contains(rFullState.coord))
		{
			mNextFrames[rFullState.coord] = std::make_unique<Frame>();
		}

		Frame& rFrame = *mCurrentFrames[rFullState.coord];
		BlastersInterpolate::HydrateClientObjects(rFrame);
		MissilesInterpolate::HydrateClientObjects(rFrame);
		SpaceshipsInterpolate::HydrateClientObjects(rFrame);

		// DT: TEMP
		if (rFullState.coord == mHumanGridCoord && mHumanPlayerId.IsValid())
		{
			auto idIt = rFrame.interpolate.pPlayers->idToIndexMap.find(mHumanPlayerId);
			if (idIt != rFrame.interpolate.pPlayers->idToIndexMap.end())
			{
				XMVECTOR vecPos = rFrame.interpolate.pPlayers->pVecPositions[idIt->second];
				FILE_LOG("[ApplyReceivedFullStates] HumanPos: ({:.1f},{:.1f},{:.1f}) idx={} count={} coord=({},{})", XMVectorGetX(vecPos), XMVectorGetY(vecPos), XMVectorGetZ(vecPos), idIt->second, rFrame.interpolate.pPlayers->iCount, rFullState.coord.x, rFullState.coord.y);
			}
			else
			{
				FILE_LOG("[ApplyReceivedFullStates] HumanNotFound: id={} count={} coord=({},{})", mHumanPlayerId.ToUuid().Value(), rFrame.interpolate.pPlayers->iCount, rFullState.coord.x, rFullState.coord.y);
			}
		}
	}

	if (!rFullStates.empty())
	{
		const engine::GridCoord& rCoord = rFullStates.front().coord;
		miFrameCounter = mCurrentFrames[rCoord]->interpolate.iFrame;
		mfCurrentTime = mCurrentFrames[rCoord]->interpolate.fCurrentTime;
	}

	return iFullStateFrame;
}

void Game::ApplyReceivedUpdates()
{
	// Heap: map insertion for per-frame server updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::ReceivedUpdate>& rUpdates = mpNetworkClient->DrainReceivedUpdates();
	for (engine::ReceivedUpdate& rUpdate : rUpdates)
	{
		// Skip frames at or before confirmed state (already processed)
		if (rUpdate.iFrame <= mConfirmedState.iFrame)
		{
			continue;
		}

		mServerUpdateBuffer[rUpdate.iFrame] = std::move(rUpdate);
	}
}

void Game::SendNetworkInput()
{
	if (mpNetworkClient == nullptr || !mpNetworkClient->IsConnected() || !mHumanPlayerId.IsValid())
	{
		return;
	}

	mpNetworkClient->SendInput(static_cast<uint16_t>(mHumanPlayerId.ToUuid().Value()), mLocalPlayerInput, false, 0.0f);
}

void Game::BuildFrameInputForFrame(int64_t iServerFrame)
{
	// Heap: unordered_map clear/insert, vector resize for per-coordinate FrameInputs
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mFrameInputs.clear();
	mServerTransferStatusChanges.clear();

	auto bufIt = mServerUpdateBuffer.find(iServerFrame);
	if (bufIt == mServerUpdateBuffer.end())
	{
		return;
	}

	engine::ReceivedUpdate& rUpdate = bufIt->second;

	// Initialize FrameInputs for all active coordinates
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		const Frame& rCurrentFrame = CurrentFrame(rCoord);
		int64_t iPlayerCount = rCurrentFrame.interpolate.pPlayers->iCount;
		mFrameInputs[rCoord].playerInputs.resize(iPlayerCount);
	}

	// Populate from server update
	for (engine::ReceivedGridUpdate& rGridUpdate : rUpdate.gridUpdates)
	{
		auto fiIt = mFrameInputs.find(rGridUpdate.coord);
		if (fiIt == mFrameInputs.end())
		{
			continue;
		}

		FrameInput& rFrameInput = fiIt->second;

		// Separate transfers from non-transfers
		for (StatusChange& rChange : rGridUpdate.statusChanges)
		{
			bool bIsTransfer = (rChange.eType == StatusChangeType::kTransferPlayer ||
			                    rChange.eType == StatusChangeType::kTransferSpaceship ||
			                    rChange.eType == StatusChangeType::kTransferBlaster ||
			                    rChange.eType == StatusChangeType::kTransferMissile);
			if (bIsTransfer)
			{
				// DT: TEMP
				if (rChange.eType == StatusChangeType::kTransferPlayer)
				{
					FILE_LOG("[BuildFrameInputForFrame] ServerTransfer: kTransferPlayer at ({},{}) frame={}", rGridUpdate.coord.x, rGridUpdate.coord.y, iServerFrame);
				}

				mServerTransferStatusChanges[rGridUpdate.coord].push_back(std::move(rChange));
			}
			else
			{
				rFrameInput.statusChanges.push_back(std::move(rChange));
			}
		}

		// Apply server player inputs
		int64_t iCopyCount = std::min(static_cast<int64_t>(rGridUpdate.playerInputs.size()), static_cast<int64_t>(rFrameInput.playerInputs.size()));
		for (int64_t i = 0; i < iCopyCount; ++i)
		{
			rFrameInput.playerInputs.at(i) = rGridUpdate.playerInputs.at(i);
		}
	}
}

void Game::Reconcile()
{
	if (mpNetworkClient == nullptr || mServerUpdateBuffer.empty() || mConfirmedState.iFrame < 0)
	{
		return;
	}

	// Don't restore confirmed state if we can't replay any consecutive frames (gap)
	int64_t iExpectedFrame = mConfirmedState.iFrame + 1;
	if (mServerUpdateBuffer.begin()->first != iExpectedFrame)
	{
		// DT: TEMP
		FILE_LOG("[Reconcile] Gap: confirmedFrame={} expectedNext={} bufferFirst={} bufferSize={}", mConfirmedState.iFrame, iExpectedFrame, mServerUpdateBuffer.begin()->first, mServerUpdateBuffer.size());
		return;
	}

	int64_t iOriginalConfirmedFrame = mConfirmedState.iFrame; // DT: TEMP

	// Heap: Frame deserialization, stringstream, workbuffer ops, collision, transfers
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Restore mCurrentFrames from confirmed state
	const std::unordered_map<engine::GridCoord, std::string>& rConfirmedFrames = mConfirmedState.serializedFrames;
	std::erase_if(mCurrentFrames, [&rConfirmedFrames](const auto& rPair)
	{
		return !rConfirmedFrames.contains(rPair.first);
	});
	for (auto& [rCoord, rSerializedFrame] : rConfirmedFrames)
	{
		if (!mCurrentFrames.contains(rCoord))
		{
			mCurrentFrames[rCoord] = std::make_unique<Frame>();
		}
		std::istringstream iss(rSerializedFrame);
		iss >> *mCurrentFrames[rCoord];
	}

	miFrameCounter = mConfirmedState.iFrame;
	mfCurrentTime = mConfirmedState.fCurrentTime;
	mHumanGridCoord = mConfirmedState.humanGridCoord;
	mHumanPlayerId = mConfirmedState.humanPlayerId;
	mfPreviousHumanArmor = mConfirmedState.fPreviousHumanArmor;

	// DT: TEMP
	FILE_LOG("[Reconcile] Restore: frame={} humanId={} grid=({},{})", miFrameCounter, mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y);

	// Ensure next frames exist for all restored coords
	for (auto& [rCoord, pFrame] : mCurrentFrames)
	{
		if (!mNextFrames.contains(rCoord))
		{
			mNextFrames[rCoord] = std::make_unique<Frame>();
		}
	}

	// Replay each consecutive server frame
	iExpectedFrame = mConfirmedState.iFrame + 1;
	auto it = mServerUpdateBuffer.begin();
	while (it != mServerUpdateBuffer.end())
	{
		if (it->first != iExpectedFrame)
		{
			break; // Gap in frames, stop replay
		}

		// Compute active set for this tick (matching server's per-tick ComputeActiveSet behavior)
		ASSERT(mCurrentFrames.contains(mHumanGridCoord));
		mActiveCoords.clear();
		mActiveCoords.push_back(mHumanGridCoord);
		for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
		{
			engine::GridCoord neighbor {mHumanGridCoord.x + rOffset.x, mHumanGridCoord.y + rOffset.y};
			if (mCurrentFrames.contains(neighbor))
			{
				mActiveCoords.push_back(neighbor);
			}
		}
		if (!std::ranges::contains(mActiveCoords, engine::kOriginCoord) && mCurrentFrames.contains(engine::kOriginCoord))
		{
			mActiveCoords.push_back(engine::kOriginCoord);
		}

		int64_t iServerFrame = it->first;
		++miFrameCounter;
		mfCurrentTime += kfDeltaTime;

		// Build frame inputs from server data
		BuildFrameInputForFrame(iServerFrame);

		const int64_t iActiveCount = static_cast<int64_t>(mActiveCoords.size());

		// Pre-resolve frame references
		common::gpThreadLocal->mWorkbuffer.Push();
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			const engine::GridCoord& rCoord = mActiveCoords[static_cast<size_t>(j)];
			common::gpThreadLocal->mWorkbuffer.PushBack<ActiveFrameRef>({
				.pNext = &NextFrame(rCoord),
				.pCurrent = &CurrentFrame(rCoord),
				.pFrameInput = &mFrameInputs.at(rCoord),
			});
		}
		std::span<const ActiveFrameRef> activeFrameRefs = common::gpThreadLocal->mWorkbuffer.Span<ActiveFrameRef>();

		// Interpolate phase
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			Frame& rNext = *activeFrameRefs[j].pNext;
			const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
			FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
			FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime);
			rNext.interpolate.iFrame = miFrameCounter;
			rNext.interpolate.fCurrentTime = mfCurrentTime;
			rNext.interpolate.frameFlags.Set(engine::FrameFlags::kRecalculated);
		}

		// PostRender phase
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			Frame& rNext = *activeFrameRefs[j].pNext;
			const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
			FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
			FramePostRender::AllocateAndCopy(rNext.postRender, rCurrent.postRender);

			if (rNext.interpolate.pPlayers->iCount > static_cast<int64_t>(rFrameInput.playerInputs.size()))
			{
				rFrameInput.playerInputs.resize(rNext.interpolate.pPlayers->iCount);
			}

			FramePostRender::Update(rNext, rCurrent, rFrameInput);
		}

		// Collision (sequential, uses static storage)
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			Frame& rNext = *activeFrameRefs[j].pNext;
			const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
			FramePostRender::PreCollision(rNext, rCurrent);
			engine::Collision::Collide(rNext.postRender.alignments, rNext.postRender.vecArea);
			FramePostRender::PostCollision(rNext, rCurrent);
			FramePostRender::AreaDamage(rNext, rCurrent);
		}

		// Transfer
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			FramePostRender::Transfer(*activeFrameRefs[j].pNext);
		}

		// Destroy and Spawn
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			Frame& rNext = *activeFrameRefs[j].pNext;
			FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
			FramePostRender::Destroy(rNext);
			FramePostRender::Spawn(rNext, rFrameInput);
		}

		common::gpThreadLocal->mWorkbuffer.Pop();

		// Harvest transfers
		HarvestTransfers();

		// DT: TEMP
		FILE_LOG("[Reconcile] Post-HarvestTransfers frame={}: humanId={} grid=({},{})", miFrameCounter, mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y);

		// Swap frames
		std::swap(mCurrentFrames, mNextFrames);
		EnsureNextFrames();

		// Clear pressed state
		for (auto& [rCoord, rFrameInput] : mFrameInputs)
		{
			rFrameInput.ClearPressed();
		}

		// Validate CRC against server
		engine::ReceivedUpdate& rUpdate = it->second;
		for (const engine::ReceivedGridUpdate& rGridUpdate : rUpdate.gridUpdates)
		{
			if (!mCurrentFrames.contains(rGridUpdate.coord))
			{
				continue;
			}

			CurrentFrame(rGridUpdate.coord).interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
			common::crc_t clientCrc = CurrentFrame(rGridUpdate.coord).ServerCrc();
			if (clientCrc != rGridUpdate.serverCrc)
			{
				// DT: TEMP - per-component CRC breakdown
				const Frame& rFrame = CurrentFrame(rGridUpdate.coord);
				const auto& rInterp = rFrame.interpolate;
				const auto& rPost = rFrame.postRender;

				// Interpolate base sub-components
				common::crc_t interpFlags = common::Crc(rInterp.frameFlags);
				common::crc_t interpFrame = common::Crc(rInterp.iFrame);
				common::crc_t interpTime = common::Crc(rInterp.fCurrentTime);
				common::crc_t interpDelta = common::Crc(rInterp.fDeltaTime);
				common::crc_t interpExplosions = engine::ServerCollectionCrc(rInterp.explosions);
				common::crc_t interpPushers = engine::ServerCollectionCrc(rInterp.pushers);
				FILE_LOG("[Reconcile] InterpBase frame={}: flags={} iFrame={} time={} delta={} explosions={} pushers={}", iServerFrame, interpFlags, interpFrame, interpTime, interpDelta, interpExplosions, interpPushers);

				// PostRender base sub-components
				common::crc_t postRandom = rPost.randomEngine.Crc();
				common::crc_t postArea = common::Crc(rPost.vecArea);
				common::crc_t postUuid = common::Crc(rPost.uiNextUuid);
				common::crc_t postFrameId = common::Crc(rPost.uiFrameId);
				common::crc_t postIslands = common::Crc(rPost.eIslandsFlip);
				common::crc_t postAlign = rPost.alignments.Crc();
				common::crc_t postExplosions = engine::ServerCollectionCrc(rPost.explosions);
				common::crc_t postPushers = engine::ServerCollectionCrc(rPost.pushers);
				FILE_LOG("[Reconcile] PostBase frame={}: random={} area={} uuid={} frameId={} islands={} align={} explosions={} pushers={}", iServerFrame, postRandom, postArea, postUuid, postFrameId, postIslands, postAlign, postExplosions, postPushers);

				// Game-level
				common::crc_t interpPlayers = engine::CollectionCrc(*rInterp.pPlayers, rInterp.pPlayers->ServerCrcMembers());
				common::crc_t postPlayers = engine::CollectionCrc(*rPost.pPlayers, rPost.pPlayers->Members());
				FILE_LOG("[Reconcile] Game frame={}: gameFlags={} spawnTimer={} interpPlayers={} enemy={} player={} postPlayers={}", iServerFrame, common::Crc(rInterp.gameFlags), common::Crc(rInterp.fSpawnTimer), interpPlayers, common::Crc(rPost.enemyAlignment), common::Crc(rPost.playerAlignment), postPlayers);
				Log("Desync at ({},{}): server={} client={} frame={}", rGridUpdate.coord.x, rGridUpdate.coord.y, rGridUpdate.serverCrc, clientCrc, iServerFrame);
				FILE_LOG("[Reconcile] Desync at ({},{}): server={} client={} frame={}", rGridUpdate.coord.x, rGridUpdate.coord.y, rGridUpdate.serverCrc, clientCrc, iServerFrame);
				mpNetworkClient->SendDesyncReport(iServerFrame, rGridUpdate.coord, rGridUpdate.serverCrc, clientCrc);
				DEBUG_BREAK();
				mpNetworkClient->Disconnect();
				return;
			}
		}

		// Success: remove processed frame and advance
		it = mServerUpdateBuffer.erase(it);
		++iExpectedFrame;
	}

	// Save new confirmed state
	mConfirmedState.iFrame = miFrameCounter;
	mConfirmedState.fCurrentTime = mfCurrentTime;
	mConfirmedState.serializedFrames.clear();
	for (auto& [rCoord, pFrame] : mCurrentFrames)
	{
		std::ostringstream oss;
		oss << *pFrame;
		mConfirmedState.serializedFrames[rCoord] = oss.str();
	}
	mConfirmedState.humanGridCoord = mHumanGridCoord;
	mConfirmedState.humanPlayerId = mHumanPlayerId;
	mConfirmedState.fPreviousHumanArmor = mfPreviousHumanArmor;

	// DT: TEMP
	FILE_LOG("[Reconcile] NewConfirmed: frame={} replayed={} humanId={} grid=({},{}) bufRemaining={}", miFrameCounter, miFrameCounter - iOriginalConfirmedFrame, mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, mServerUpdateBuffer.size());
}

#endif // BT_CLIENT

#ifdef BT_SERVER

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
	mBroadcastSpawns.clear();
	mBroadcastTransfers.clear();

	const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();

	// Initialize FrameInputs for all active coordinates
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		const Frame& rCurrentFrame = CurrentFrame(rCoord);
		int64_t iPlayerCount = rCurrentFrame.interpolate.pPlayers->iCount;

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
		auto idIt = rCurrentFrame.interpolate.pPlayers->idToIndexMap.find(pClient->humanPlayerId);
		if (idIt == rCurrentFrame.interpolate.pPlayers->idToIndexMap.end())
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

	// Add spawn StatusChanges for clients waiting for initial spawn
	for (const ClientSpawnInfo& rInfo : mClientsWaitingForSpawn)
	{
		mFrameInputs[rInfo.spawnCoord].statusChanges.push_back({.eType = StatusChangeType::kSpawnPlayer});
	}

	// Save StatusChanges for broadcasting (spawns only, transfers handled separately in HarvestTransfersServer)
	for (const auto& [rCoord, rFrameInput] : mFrameInputs)
	{
		if (!rFrameInput.statusChanges.empty())
		{
			mBroadcastSpawns[rCoord] = rFrameInput.statusChanges;
		}
	}

	// Take snapshot of player IDs at spawn coordinates for FinalizeNewClientsServer
	mPreSpawnPlayerIds.clear();
	if (!mClientsWaitingForSpawn.empty() && mCurrentFrames.contains(engine::kOriginCoord))
	{
		const PlayersPostRender& rPlayers = *CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
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

			// Record transfer for broadcasting with source coord for per-client filtering
			mBroadcastTransfers[dest].push_back({.change = {.eType = rRequest.eType, .data = data}, .sourceCoord = rCoord});

			// Track human player transfers for subscription updates
			if (rRequest.eType == StatusChangeType::kTransferPlayer && rRequest.iEntityId != 0)
			{
				player_t transferredPlayerId {engine::uuid_t {rRequest.iEntityId}};
				player_t newPlayerId = rDestFrame.postRender.pPlayers->puiIds[rDestFrame.postRender.pPlayers->iCount - 1];

				const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
				for (const engine::ClientConnection& rClient : rClients)
				{
					if (rClient.humanPlayerId.IsValid() && transferredPlayerId == rClient.humanPlayerId)
					{
						mPendingSubscriptionUpdates.push_back({.iClientId = rClient.iClientId, .newCoord = dest, .newPlayerId = newPlayerId});

						// DT: TEMP
						FILE_LOG("[HarvestTransfersServer] Human transfer: client={} oldId={} newId={} dest=({},{})", rClient.iClientId, transferredPlayerId.ToUuid().Value(), newPlayerId.ToUuid().Value(), dest.x, dest.y);

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

	// DT: TEMP
	FILE_LOG("[BroadcastServer] frame={} activeCoords={}", iFrame, mActiveCoords.size());

	// Compute CRCs for all active coords (once)
	std::unordered_map<engine::GridCoord, common::crc_t> crcs;
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		crcs[rCoord] = CurrentFrame(rCoord).ServerCrc();

		// DT: TEMP - per-component CRC breakdown (only for (0,0) to reduce log volume)
		if (rCoord.x == 0 && rCoord.y == 0)
		{
			const Frame& rFrame = CurrentFrame(rCoord);
			const auto& rInterp = rFrame.interpolate;
			const auto& rPost = rFrame.postRender;
			FILE_LOG("[BroadcastServer] InterpBase coord=({},{}) frame={}: flags={} iFrame={} time={} delta={} explosions={} pushers={}", rCoord.x, rCoord.y, iFrame, common::Crc(rInterp.frameFlags), common::Crc(rInterp.iFrame), common::Crc(rInterp.fCurrentTime), common::Crc(rInterp.fDeltaTime), engine::ServerCollectionCrc(rInterp.explosions), engine::ServerCollectionCrc(rInterp.pushers));
			FILE_LOG("[BroadcastServer] PostBase coord=({},{}) frame={}: random={} area={} uuid={} frameId={} islands={} align={} explosions={} pushers={}", rCoord.x, rCoord.y, iFrame, rPost.randomEngine.Crc(), common::Crc(rPost.vecArea), common::Crc(rPost.uiNextUuid), common::Crc(rPost.uiFrameId), common::Crc(rPost.eIslandsFlip), rPost.alignments.Crc(), engine::ServerCollectionCrc(rPost.explosions), engine::ServerCollectionCrc(rPost.pushers));
		}
	}

	// Build unfiltered data for ring buffer: spawns + all transfers (regardless of source)
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> allChanges;
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		auto spawnIt = mBroadcastSpawns.find(rCoord);
		if (spawnIt != mBroadcastSpawns.end())
		{
			allChanges[rCoord] = spawnIt->second;
		}

		auto transferIt = mBroadcastTransfers.find(rCoord);
		if (transferIt != mBroadcastTransfers.end())
		{
			for (const BroadcastTransfer& rTransfer : transferIt->second)
			{
				allChanges[rCoord].push_back(rTransfer.change);
			}
		}
	}

	// Buffer unfiltered frame data (allGridUpdates holds spans into allChanges)
	std::vector<std::pair<engine::GridCoord, engine::GridUpdateData>> allGridUpdates;
	allGridUpdates.reserve(mActiveCoords.size());
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		engine::GridUpdateData updateData {};
		updateData.serverCrc = crcs[rCoord];

		auto it = allChanges.find(rCoord);
		if (it != allChanges.end())
		{
			updateData.statusChanges = std::span<const StatusChange>(it->second);
		}

		auto fiIt = mFrameInputs.find(rCoord);
		if (fiIt != mFrameInputs.end())
		{
			updateData.playerInputs = std::span<const PlayerInput>(fiIt->second.playerInputs);
		}

		allGridUpdates.push_back({rCoord, updateData});
	}
	engine::gpNetworkServer->BufferFrame(iFrame, allGridUpdates);

	// Build per-client filtered data and send
	std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		std::unordered_map<engine::GridCoord, std::vector<StatusChange>> clientChanges;

		for (const engine::GridCoord& rCoord : rClient.activeCoords)
		{
			// Add spawns for this coord
			auto spawnIt = mBroadcastSpawns.find(rCoord);
			if (spawnIt != mBroadcastSpawns.end())
			{
				clientChanges[rCoord] = spawnIt->second;
			}

			// Add external transfers (source NOT in client's active set)
			auto transferIt = mBroadcastTransfers.find(rCoord);
			if (transferIt != mBroadcastTransfers.end())
			{
				for (const BroadcastTransfer& rTransfer : transferIt->second)
				{
					if (!std::ranges::contains(rClient.activeCoords, rTransfer.sourceCoord))
					{
						clientChanges[rCoord].push_back(rTransfer.change);

						// DT: TEMP
						if (rTransfer.change.eType == StatusChangeType::kTransferPlayer)
						{
							FILE_LOG("[BroadcastServer] External transfer: client={} coord=({},{}) source=({},{})", rClient.iClientId, rCoord.x, rCoord.y, rTransfer.sourceCoord.x, rTransfer.sourceCoord.y);
						}
					}
					else
					{
						// DT: TEMP
						if (rTransfer.change.eType == StatusChangeType::kTransferPlayer)
						{
							FILE_LOG("[BroadcastServer] Filtered (intra): client={} coord=({},{}) source=({},{})", rClient.iClientId, rCoord.x, rCoord.y, rTransfer.sourceCoord.x, rTransfer.sourceCoord.y);
						}
					}
				}
			}
		}

		// Build grid updates for this client
		std::vector<std::pair<engine::GridCoord, engine::GridUpdateData>> gridUpdates;
		gridUpdates.reserve(rClient.activeCoords.size());
		for (const engine::GridCoord& rCoord : rClient.activeCoords)
		{
			engine::GridUpdateData updateData {};

			// Only send CRCs for coords the server actively simulated this frame
			auto crcIt = crcs.find(rCoord);
			if (crcIt == crcs.end())
			{
				continue;
			}
			updateData.serverCrc = crcIt->second;

			auto it = clientChanges.find(rCoord);
			if (it != clientChanges.end())
			{
				updateData.statusChanges = std::span<const StatusChange>(it->second);
			}

			auto fiIt = mFrameInputs.find(rCoord);
			if (fiIt != mFrameInputs.end())
			{
				updateData.playerInputs = std::span<const PlayerInput>(fiIt->second.playerInputs);
			}

			gridUpdates.push_back({rCoord, updateData});
		}

		engine::gpNetworkServer->SendUpdate(rClient, iFrame, gridUpdates);
	}

	mBroadcastSpawns.clear();
	mBroadcastTransfers.clear();
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
	const PlayersPostRender& rPlayers = *CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
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
		int64_t iClientId = mClientsWaitingForSpawn.at(i).iClientId;
		player_t playerId = newPlayerIds.at(i);

		engine::gpNetworkServer->SendAssignPlayer(iClientId, playerId, engine::kOriginCoord);

		// Gather frames for client's active set (computed by SendAssignPlayer)
		const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
		for (const engine::ClientConnection& rClient : rClients)
		{
			if (rClient.iClientId != iClientId)
			{
				continue;
			}

			// Ensure frames exist at all active coords (server needs them for simulation)
			for (const engine::GridCoord& rCoord : rClient.activeCoords)
			{
				if (!mCurrentFrames.contains(rCoord))
				{
					CreateFrameAtCoord(rCoord);
				}
			}

			// Only send frames the client doesn't already have
			std::vector<std::pair<engine::GridCoord, const Frame*>> clientFrames;
			clientFrames.reserve(rClient.pendingFullStateCoords.size());
			for (const engine::GridCoord& rCoord : rClient.pendingFullStateCoords)
			{
				clientFrames.push_back({rCoord, &CurrentFrame(rCoord)});
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
		engine::gpNetworkServer->SendAssignPlayer(rUpdate.iClientId, rUpdate.newPlayerId, rUpdate.newCoord);

		// DT: TEMP
		{
			const std::vector<engine::ClientConnection>& rTempClients = engine::gpNetworkServer->GetClients();
			for (const engine::ClientConnection& rTempClient : rTempClients)
			{
				if (rTempClient.iClientId == rUpdate.iClientId)
				{
					FILE_LOG("[HandleSubscriptionUpdatesServer] client={} newId={} newCoord=({},{}) pendingCount={}", rUpdate.iClientId, rUpdate.newPlayerId.ToUuid().Value(), rUpdate.newCoord.x, rUpdate.newCoord.y, rTempClient.pendingFullStateCoords.size());
					for (const engine::GridCoord& rPendingCoord : rTempClient.pendingFullStateCoords)
					{
						FILE_LOG("[HandleSubscriptionUpdatesServer]   pendingFullState: ({},{})", rPendingCoord.x, rPendingCoord.y);
					}
					break;
				}
			}
		}

		// Ensure frames exist at newly-subscribed coords
		const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
		for (const engine::ClientConnection& rClient : rClients)
		{
			if (rClient.iClientId != rUpdate.iClientId)
			{
				continue;
			}

			for (const engine::GridCoord& rCoord : rClient.pendingFullStateCoords)
			{
				if (!mCurrentFrames.contains(rCoord))
				{
					CreateFrameAtCoord(rCoord);
				}
			}

			std::vector<std::pair<engine::GridCoord, const Frame*>> newCellFrames;
			newCellFrames.reserve(rClient.pendingFullStateCoords.size());
			for (const engine::GridCoord& rCoord : rClient.pendingFullStateCoords)
			{
				newCellFrames.push_back({rCoord, &CurrentFrame(rCoord)});
			}

			engine::gpNetworkServer->SendFullState(rUpdate.iClientId, iFrame, newCellFrames);
			break;
		}
	}

	mPendingSubscriptionUpdates.clear();
}

#endif // BT_SERVER

} // namespace game
