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
constexpr int64_t kiReconcileCeiling = 4;

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
	if constexpr (!kbEnableAutoInput)
	{
		RawInputToFrameInput(engine::gpRawInputManager->mRawInput, frameInput, iHumanIndex);
		gpInput->UpdateFrameInputPressed(engine::gpRawInputManager->mRawInput, frameInput);
	}
#endif

	// --- AI input ---

	if (rCurrentFrame.interpolate.gameFlags & GameFlags::kGame)
	{
		for (int64_t i = 0; i < iPlayerCount; ++i)
		{
			if constexpr (!kbEnableAutoInput)
			{
				if (i == iHumanIndex)
				{
					continue;
				}
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

	if (!InMainMenu())
	{
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
		// Populate mFrameInputs with last server-confirmed player inputs (not local input)
		for (const engine::GridCoord& rCoord : mActiveCoords)
		{
			const Frame& rCurrentFrame = CurrentFrame(rCoord);
			int64_t iPlayerCount = rCurrentFrame.interpolate.pPlayers->iCount;

			FrameInput& rFrameInput = mFrameInputs[rCoord];
			rFrameInput.playerInputs.resize(iPlayerCount);

			// Apply last server-confirmed inputs (held state continues)
			auto serverIt = mLastServerPlayerInputs.find(rCoord);
			if (serverIt != mLastServerPlayerInputs.end())
			{
				int64_t iCopyCount = std::min(static_cast<int64_t>(serverIt->second.size()), iPlayerCount);
				for (int64_t i = 0; i < iCopyCount; ++i)
				{
					rFrameInput.playerInputs.at(i) = serverIt->second.at(i);
				}
			}
		}

		// Camera shake and death detection (local input capture is in CaptureLocalInput)
		if (mHumanPlayerId.IsValid() && mCurrentFrames.contains(mHumanGridCoord))
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
			else
			{
				// Human died (existing death detection logic)
				CurrentFrame(mHumanGridCoord).interpolate.gameFlags.Set(GameFlags::kDeathScreen);
				mHumanPlayerId = {};
				mfPreviousHumanArmor = 0.0f;
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

	bool bUseServerTransfers = IsNetworkMode() && !mServerTransferStatusChanges.empty();

	if (bUseServerTransfers)
	{
		// Sort by server sequence to restore original spawn order
		for (auto& [rCoord, rTransfers] : mServerTransferStatusChanges)
		{
			std::ranges::sort(rTransfers, {}, &StatusChange::uiSequence);
		}

		// Save pre-spawn player counts per destination for human ID matching
		std::unordered_map<engine::GridCoord, int64_t> preSpawnPlayerCounts;
		for (const auto& [rCoord, rTransfers] : mServerTransferStatusChanges)
		{
			auto it = mNextFrames.find(rCoord);
			if (it != mNextFrames.end() && it->second != nullptr)
			{
				preSpawnPlayerCounts[rCoord] = it->second->postRender.pPlayers->iCount;
			}
		}

		// Reconciliation: apply ALL transfers from server
		for (const auto& [rCoord, rTransfers] : mServerTransferStatusChanges)
		{
			auto it = mNextFrames.find(rCoord);
			if (it == mNextFrames.end() || it->second == nullptr)
			{
				continue;
			}

			Frame& rDestFrame = *it->second;
			for (const StatusChange& rChange : rTransfers)
			{
				// DT: TEMP
				FILE_LOG(0, "[HarvestTransfers] Server: type={} dest=({},{}) align={}", static_cast<int>(rChange.eType), rCoord.x, rCoord.y, rChange.data.alignment.uiValue);

				SpawnTransfer(rDestFrame, rChange.eType, rChange.data, mPlayerAlignment);
			}
		}
		mServerTransferStatusChanges.clear();

		// Track human player transfer from local transferRequests (no spawning)
		for (const engine::GridCoord& rCoord : mActiveCoords)
		{
			Frame& rNextFrame = NextFrame(rCoord);
			for (const TransferRequest& rRequest : rNextFrame.postRender.transferRequests)
			{
				if (rRequest.eType == StatusChangeType::kTransferPlayer &&
					mHumanPlayerId.IsValid() && rRequest.iEntityId == mHumanPlayerId.ToUuid().Value())
				{
					engine::GridCoord dest {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};
					mHumanGridCoord = dest;
					ASSERT(mNextFrames.contains(mHumanGridCoord));
					Frame& rDestFrame = *mNextFrames[dest];

					// Find the human's new ID by matching position among recently-spawned players
					int64_t iPreCount = preSpawnPlayerCounts.contains(dest) ? preSpawnPlayerCounts[dest] : 0;
					bool bFound = false;
					for (int64_t i = iPreCount; i < rDestFrame.postRender.pPlayers->iCount; ++i)
					{
						if (XMVector4Equal(rDestFrame.interpolate.pPlayers->pVecPositions[i], rRequest.data.vecPosition))
						{
							mHumanPlayerId = rDestFrame.postRender.pPlayers->puiIds[i];
							bFound = true;
							break;
						}
					}
					if (!bFound)
					{
						common::Log("HarvestTransfers: No position match for human transfer to ({},{}), falling back to last player", dest.x, dest.y);
						mHumanPlayerId = rDestFrame.postRender.pPlayers->puiIds[rDestFrame.postRender.pPlayers->iCount - 1];
					}
					mfPreviousHumanArmor = rRequest.data.fHealth;

					// DT: TEMP
					FILE_LOG(0, "[HarvestTransfers] Human transfer: entityId={} newId={} to=({},{})", rRequest.iEntityId, mHumanPlayerId.ToUuid().Value(), dest.x, dest.y);
				}
			}
		}
	}
	else
	{
		// Extrapolation or offline: local transfer harvesting
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

				// DT: TEMP
				FILE_LOG(0, "[HarvestTransfers] Local: type={} src=({},{}) dest=({},{}) align={}", static_cast<int>(rRequest.eType), rCoord.x, rCoord.y, dest.x, dest.y, data.alignment.uiValue);

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
					FILE_LOG(0, "[HarvestTransfers] Human transfer: entityId={} newId={} to=({},{})", rRequest.iEntityId, mHumanPlayerId.ToUuid().Value(), dest.x, dest.y);
				}
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
	miSkipSnapshotSteps = 0;

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
	FILE_LOG(0, "ConnectToServer: connecting to {}", pServerAddress);
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

void Game::StoreExtrapolatedSnapshot(int64_t iFrame, float fCurrentTime)
{
	ExtrapolatedSnapshot& rSnapshot = mExtrapolatedSnapshots[iFrame];
	rSnapshot.coordCrcs.clear();
	rSnapshot.serializedFrames.clear();
	rSnapshot.humanGridCoord = mHumanGridCoord;
	rSnapshot.humanPlayerId = mHumanPlayerId;
	rSnapshot.fPreviousHumanArmor = mfPreviousHumanArmor;
	rSnapshot.fCurrentTime = fCurrentTime;

	// Capture CRCs and serialized frames for CRC fast-path comparison
	for (const auto& [rCoord, pFrame] : mCurrentFrames)
	{
		rSnapshot.coordCrcs[rCoord] = pFrame->ServerCrc();
		std::ostringstream oss;
		oss << *pFrame;
		rSnapshot.serializedFrames[rCoord] = oss.str();
	}
}

std::chrono::nanoseconds Game::ComputeClockCorrectionNs(int64_t iPreReconcileFrame)
{
	if (miLatestServerFrame < 0)
	{
		return 0ns;
	}

	int64_t iRttUs = mpNetworkClient->GetPipelineRttUs();

	// Target: client should be ceil(RTT/2 / frameTime) + 1 frames behind server
	static constexpr int64_t kiFrameTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(kUpdateStepNs).count();
	int64_t iTargetBehind = (iRttUs > 0) ? ((iRttUs / 2 + kiFrameTimeUs - 1) / kiFrameTimeUs + 1) : 1;

	// Offset: positive = client ahead, negative = client behind
	int64_t iOffset = iPreReconcileFrame - miLatestServerFrame;

	// Error: positive = too far ahead (slow down), negative = too far behind (speed up)
	int64_t iError = iOffset + iTargetBehind;
	miClockError = iError;

	// Proportional correction capped at ±4, scaled to 1/64th of a frame step per error unit
	int64_t iCorrectionSteps = std::clamp(iError, -4LL, 4LL);
	std::chrono::nanoseconds correction(-iCorrectionSteps * kUpdateStepNs.count() / 64);

	gpProfileManager->SetClockCorrection(iOffset, iTargetBehind, iError);

	FILE_LOG(0, "[ClockCorrection] offset={} target={} error={} correction={}ns", iOffset, iTargetBehind, iError, correction.count());
	return correction;
}

void Game::DisconnectFromServer()
{
	// Heap: NetworkClient destructor triggers ENet disconnect and cleanup
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbEnableReconcileThread)
	{
		if (mbReconcileInFlight)
		{
			mpReconcileWorker->Wait();
			mbReconcileInFlight = false;
		}
		mpReconcileContext.reset();
	}

	miLatestServerFrame = -1;
	mpNetworkClient.reset();
	mServerUpdateBuffer.clear();
	mConfirmedState = {};
	mPendingFullState = {};
	mDesyncDebugState = {};
	mServerTransferStatusChanges.clear();
	mLastServerPlayerInputs.clear();
	mExtrapolatedSnapshots.clear();
	miSkipSnapshotSteps = 0;
	miClockError = 0;
}

void Game::PollNetworkClient()
{
	// Poll LAN discovery scanner
	if (mpDiscoveryScanner != nullptr)
	{
		mpDiscoveryScanner->Poll();

		if (mpDiscoveryScanner->IsFound())
		{
			char pcAddress[16];
			snprintf(pcAddress, sizeof(pcAddress), "%s", mpDiscoveryScanner->GetFoundAddress());
			FILE_LOG(0, "Discovery: found server at {}", pcAddress);
			ChangeFrame(GameFlags::kGame);
			meUiState = UiState::kNone;
			ConnectToServer(pcAddress);
		}
		else if (!mpDiscoveryScanner->IsScanning())
		{
			FILE_LOG(0, "Discovery: scan timed out, no server found");
			DEBUG_BREAK();
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

	// Check for debug frame response
	std::unique_ptr<engine::ReceivedDebugFrame> pDebugFrame = mpNetworkClient->DrainReceivedDebugFrame();
	if (pDebugFrame != nullptr && mDesyncDebugState.pClientFrame != nullptr)
	{
		CompareWithServerFrame(*mDesyncDebugState.pClientFrame, *pDebugFrame->pFrame, mDesyncDebugState.iFrame, mDesyncDebugState.coord);
		mDesyncDebugState = {};
		DEBUG_BREAK();
		mpNetworkClient->Disconnect();
		return;
	}

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
			FILE_LOG(0, "[PollNetworkClient] Assignment: old={} new={} grid=({},{}) frame={}", oldHumanPlayerId.ToUuid().Value(), mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, miFrameCounter);
			if (mCurrentFrames.contains(mHumanGridCoord))
			{
				FILE_LOG(0, "[PollNetworkClient] PostAssign pPlayers={} pVecPos={} count={}", (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers.get(), (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->pVecPositions, mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->iCount);
			}
		}
		mpNetworkClient->ClearAssignment();
	}

	int64_t iFullStateFrame = ApplyReceivedFullStates();

	// Initial connection: establish confirmed state immediately
	if (iFullStateFrame >= 0)
	{
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
		FILE_LOG(0, "[PollNetworkClient] ConfirmedState: frame={} humanId={} grid=({},{}) coordCount={}", mConfirmedState.iFrame, mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, mConfirmedState.serializedFrames.size());

		// Prune stale entries from buffer
		std::erase_if(mServerUpdateBuffer, [iFullStateFrame](const auto& rPair)
		{
			return rPair.first <= iFullStateFrame;
		});
	}

	ApplyReceivedUpdates();
}

int64_t Game::ApplyReceivedFullStates()
{
	// Heap: Moving unique_ptr<Frame> into mCurrentFrames, stringstream serialization
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::ReceivedFullState>& rFullStates = mpNetworkClient->DrainReceivedFullStates();
	if (rFullStates.empty())
	{
		return -1;
	}

	int64_t iFullStateFrame = rFullStates.front().iFrame;

	// Subscription update: defer new coords as pending (don't touch mCurrentFrames/miFrameCounter/mfCurrentTime)
	if (mConfirmedState.iFrame >= 0)
	{
		mPendingFullState.iFrame = iFullStateFrame;
		mPendingFullState.serializedFrames.clear();

		for (engine::ReceivedFullState& rFullState : rFullStates)
		{
			// Hydrate before serializing so pending data includes client-only objects
			Frame& rFrame = *rFullState.pFrame;
			BlastersInterpolate::HydrateClientObjects(rFrame);
			MissilesInterpolate::HydrateClientObjects(rFrame);
			SpaceshipsInterpolate::HydrateClientObjects(rFrame);

			std::ostringstream oss;
			oss << rFrame;
			mPendingFullState.serializedFrames[rFullState.coord] = oss.str();

			FILE_LOG(0, "[ApplyReceivedFullStates] Deferred coord=({},{}) frame={}", rFullState.coord.x, rFullState.coord.y, iFullStateFrame);
		}

		return -1;
	}

	// Initial connection: apply all received coords to mCurrentFrames immediately
	for (engine::ReceivedFullState& rFullState : rFullStates)
	{
		mCurrentFrames[rFullState.coord] = std::move(rFullState.pFrame);

		FILE_LOG(0, "[ApplyReceivedFullStates] Applied coord=({},{}) frame={} interpPVecPos={} postPuiIds={}", rFullState.coord.x, rFullState.coord.y, iFullStateFrame, (void*)mCurrentFrames[rFullState.coord]->interpolate.pPlayers->pVecPositions, (void*)mCurrentFrames[rFullState.coord]->postRender.pPlayers->puiIds);

		if (!mNextFrames.contains(rFullState.coord))
		{
			mNextFrames[rFullState.coord] = std::make_unique<Frame>();
		}

		Frame& rFrame = *mCurrentFrames[rFullState.coord];
		BlastersInterpolate::HydrateClientObjects(rFrame);
		MissilesInterpolate::HydrateClientObjects(rFrame);
		SpaceshipsInterpolate::HydrateClientObjects(rFrame);

		FILE_LOG(0, "[ApplyReceivedFullStates] PostHydrate coord=({},{}) interpPVecPos={} postPuiIds={}", rFullState.coord.x, rFullState.coord.y, (void*)rFrame.interpolate.pPlayers->pVecPositions, (void*)rFrame.postRender.pPlayers->puiIds);
	}

	miFrameCounter = mCurrentFrames[rFullStates.front().coord]->interpolate.iFrame;
	mfCurrentTime = mCurrentFrames[rFullStates.front().coord]->interpolate.fCurrentTime;

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
			FILE_LOG(0, "[ApplyReceivedUpdates] Skipped stale: frame={} confirmed={}", rUpdate.iFrame, mConfirmedState.iFrame);
			continue;
		}

		miLatestServerFrame = std::max(miLatestServerFrame, rUpdate.iFrame);
		mServerUpdateBuffer[rUpdate.iFrame] = std::move(rUpdate);
	}
}

void Game::CaptureLocalInput()
{
	if (!IsNetworkMode() || !mHumanPlayerId.IsValid() || !mCurrentFrames.contains(mHumanGridCoord))
	{
		mLocalPlayerInput = {};
		return;
	}

	// Find human player index in current frame
	const Frame& rCurrentFrame = CurrentFrame(mHumanGridCoord);
	const PlayersInterpolate& rPlayers = *rCurrentFrame.interpolate.pPlayers;

	auto idIt = rPlayers.idToIndexMap.find(mHumanPlayerId);
	if (idIt == rPlayers.idToIndexMap.end())
	{
		mLocalPlayerInput = {};
		return;
	}

	int64_t iHumanIndex = idIt->second;

	if constexpr (kbEnableAutoInput)
	{
		mLocalPlayerInput = {};
		mPlayerAi.UpdatePlayer(rCurrentFrame, iHumanIndex, mLocalPlayerInput);
	}
	else
	{
		// Heap: temporary FrameInput for raw input conversion
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		FrameInput tempInput {};
		tempInput.playerInputs.resize(rPlayers.iCount);
		RawInputToFrameInput(engine::gpRawInputManager->mRawInput, tempInput, iHumanIndex);
		mLocalPlayerInput = tempInput.playerInputs.at(iHumanIndex);
	}
}

void Game::SendNetworkInput()
{
	if (mpNetworkClient == nullptr || !mpNetworkClient->IsConnected())
	{
		return;
	}

	uint16_t uiPlayerId = mHumanPlayerId.IsValid() ? static_cast<uint16_t>(mHumanPlayerId.ToUuid().Value()) : 0;
	mpNetworkClient->SendInput(uiPlayerId, mLocalPlayerInput, false, 0.0f);
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
				FILE_LOG(0, "[BuildFrameInputForFrame] ServerTransfer: type={} at ({},{}) frame={}", static_cast<int>(rChange.eType), rGridUpdate.coord.x, rGridUpdate.coord.y, iServerFrame);

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

void Game::WaitForReconcile()
{
	if (mpNetworkClient == nullptr)
	{
		return;
	}

	if constexpr (kbEnableReconcileThread)
	{
		// Async: wait for worker result from previous tick
		if (!mbReconcileInFlight)
		{
			return;
		}
		mpReconcileWorker->Wait();
		ApplyReconcileResult();
		mbReconcileInFlight = false;
	}
	else
	{
	// Sync: full reconcile inline
	if (mServerUpdateBuffer.empty() || mConfirmedState.iFrame < 0)
	{
		return;
	}

	// Waiting for debug frame response from server
	if (mDesyncDebugState.iFrame >= 0)
	{
		return;
	}

	FILE_LOG(0, "[Reconcile] Entry: confirmedFrame={} bufferSize={} bufferFirst={} ackFloor={}", mConfirmedState.iFrame, mServerUpdateBuffer.size(), mServerUpdateBuffer.begin()->first, mpNetworkClient->GetAckFloor());

	// Don't restore confirmed state if we can't replay any consecutive frames (gap)
	int64_t iExpectedFrame = mConfirmedState.iFrame + 1;
	if (mServerUpdateBuffer.begin()->first != iExpectedFrame)
	{
		// Gap fallback: restore confirmed state and replay missing frames (with or without pending full state)
		FILE_LOG(0, "[Reconcile] Gap fallback: pending={} confirmed={} bufferFirst={}", mPendingFullState.iFrame, mConfirmedState.iFrame, mServerUpdateBuffer.begin()->first);

		// Heap: Frame deserialization, stringstream, workbuffer ops
		ScopedSuppressAllocationTracking suppressAllocationTracking;

		// Restore mCurrentFrames from confirmed state (not extrapolated state)
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

		// Inject pending new coords into restored state (only when pending full state exists)
		if (mPendingFullState.iFrame >= 0)
		{
			for (const auto& [rCoord, rSerializedFrame] : mPendingFullState.serializedFrames)
			{
				if (!mCurrentFrames.contains(rCoord))
				{
					mCurrentFrames[rCoord] = std::make_unique<Frame>();
				}
				std::istringstream iss(rSerializedFrame);
				iss >> *mCurrentFrames[rCoord];

				// Also populate next frames so interpolation has valid data
				if (!mNextFrames.contains(rCoord))
				{
					mNextFrames[rCoord] = std::make_unique<Frame>();
				}
				std::istringstream issNext(rSerializedFrame);
				issNext >> *mNextFrames[rCoord];
			}
		}

		// Restore human tracking state and simulation counters from confirmed state
		mHumanGridCoord = mConfirmedState.humanGridCoord;
		mHumanPlayerId = mConfirmedState.humanPlayerId;
		mfPreviousHumanArmor = mConfirmedState.fPreviousHumanArmor;
		miFrameCounter = mConfirmedState.iFrame;
		mfCurrentTime = mConfirmedState.fCurrentTime;

		int64_t iFallbackFrame = mServerUpdateBuffer.begin()->first - 1;

		// Replay missing frames with extrapolated inputs
		const int64_t iMaxGapReplay = std::min((iFallbackFrame - mConfirmedState.iFrame + 1) / 2, kiReconcileCeiling);
		int64_t iGapReplayCount = 0;
		for (int64_t iMissingFrame = mConfirmedState.iFrame + 1; iMissingFrame <= iFallbackFrame; ++iMissingFrame)
		{
			if (iGapReplayCount >= iMaxGapReplay)
			{
				break;
			}

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

			EnsureNextFrames();

			++miFrameCounter;
			mfCurrentTime += kfDeltaTime;

			// Build extrapolated inputs
			mFrameInputs.clear();
			for (const engine::GridCoord& rCoord : mActiveCoords)
			{
				const Frame& rCurrentFrame = CurrentFrame(rCoord);
				int64_t iPlayerCount = rCurrentFrame.interpolate.pPlayers->iCount;
				FrameInput& rFrameInput = mFrameInputs[rCoord];
				rFrameInput.playerInputs.resize(iPlayerCount);
				auto serverIt = mLastServerPlayerInputs.find(rCoord);
				if (serverIt != mLastServerPlayerInputs.end())
				{
					int64_t iCopyCount = std::min(static_cast<int64_t>(serverIt->second.size()), iPlayerCount);
					for (int64_t j = 0; j < iCopyCount; ++j)
					{
						rFrameInput.playerInputs.at(j) = serverIt->second.at(j);
					}
				}
			}

			// Run physics pipeline
			const int64_t iActiveCount = static_cast<int64_t>(mActiveCoords.size());

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

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				Frame& rNext = *activeFrameRefs[j].pNext;
				const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
				FramePostRender::PreCollision(rNext, rCurrent);
				engine::Collision::Collide(rNext.postRender.alignments, rNext.postRender.vecArea);
				FramePostRender::PostCollision(rNext, rCurrent);
				FramePostRender::AreaDamage(rNext, rCurrent);
			}

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				FramePostRender::Transfer(*activeFrameRefs[j].pNext);
			}

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				Frame& rNext = *activeFrameRefs[j].pNext;
				FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
				FramePostRender::Destroy(rNext);
				FramePostRender::Spawn(rNext, rFrameInput);
			}

			common::gpThreadLocal->mWorkbuffer.Pop();

			HarvestTransfers();

			std::swap(mCurrentFrames, mNextFrames);
			EnsureNextFrames();

			for (auto& [rCoord, rFrameInput] : mFrameInputs)
			{
				rFrameInput.ClearPressed();
			}

			++iGapReplayCount;
		}

		// Diag: warn on high gap replay count
		if (iGapReplayCount > 12)
		{
			FILE_LOG(0, "[Reconcile] WARNING: High gap replay count: replayed={} bufRemaining={}", iGapReplayCount, mServerUpdateBuffer.size());
			DEBUG_BREAK();
		}

		// Establish confirmed state at last replayed frame
		mConfirmedState.iFrame = miFrameCounter;
		mConfirmedState.fCurrentTime = mfCurrentTime;
		mConfirmedState.humanGridCoord = mHumanGridCoord;
		mConfirmedState.humanPlayerId = mHumanPlayerId;
		mConfirmedState.fPreviousHumanArmor = mfPreviousHumanArmor;
		mConfirmedState.serializedFrames.clear();
		for (const auto& [rCoord, pFrame] : mCurrentFrames)
		{
			std::ostringstream oss;
			oss << *pFrame;
			mConfirmedState.serializedFrames[rCoord] = oss.str();
		}

		// Prune buffer entries at or before confirmed frame
		int64_t iConfirmedFrame = mConfirmedState.iFrame;
		std::erase_if(mServerUpdateBuffer, [iConfirmedFrame](const auto& rPair)
		{
			return rPair.first <= iConfirmedFrame;
		});

		// Only clear pending full state if gap replay completed
		if (miFrameCounter >= iFallbackFrame)
		{
			mPendingFullState = {};
		}
		return; // Next tick replays from the new confirmed state
	}

	// Generalized CRC fast-path: check all consecutive buffered updates against extrapolated CRCs
	if (mPendingFullState.iFrame < 0)
	{
		bool bAllMatch = true;
		int64_t iLastMatchedFrame = -1;
		auto bufIt = mServerUpdateBuffer.begin();
		int64_t iCheckFrame = iExpectedFrame;

		while (bufIt != mServerUpdateBuffer.end() && bufIt->first == iCheckFrame)
		{
			auto snapshotIt = mExtrapolatedSnapshots.find(iCheckFrame);
			if (snapshotIt == mExtrapolatedSnapshots.end())
			{
				bAllMatch = false;
				break;
			}

			for (const engine::ReceivedGridUpdate& rGridUpdate : bufIt->second.gridUpdates)
			{
				auto crcIt = snapshotIt->second.coordCrcs.find(rGridUpdate.coord);
				if (crcIt == snapshotIt->second.coordCrcs.end() || crcIt->second != rGridUpdate.serverCrc)
				{
					bAllMatch = false;
					break;
				}
			}
			if (!bAllMatch)
		{
			break;
		}

			iLastMatchedFrame = iCheckFrame;
			++iCheckFrame;
			++bufIt;
		}

		if (bAllMatch && iLastMatchedFrame >= 0)
		{
			ScopedSuppressAllocationTracking suppressAllocationTracking;

			const ExtrapolatedSnapshot& rSnapshot = mExtrapolatedSnapshots.at(iLastMatchedFrame);

			// Update last server player inputs from the last matched update
			mLastServerPlayerInputs.clear();
			auto lastUpdateIt = mServerUpdateBuffer.find(iLastMatchedFrame);
			for (const engine::ReceivedGridUpdate& rGridUpdate : lastUpdateIt->second.gridUpdates)
			{
				mLastServerPlayerInputs[rGridUpdate.coord] = rGridUpdate.playerInputs;
			}

			// Save new confirmed state from the stored snapshot
			mConfirmedState.iFrame = iLastMatchedFrame;
			mConfirmedState.fCurrentTime = rSnapshot.fCurrentTime;
			mConfirmedState.serializedFrames = rSnapshot.serializedFrames;
			mConfirmedState.humanGridCoord = rSnapshot.humanGridCoord;
			mConfirmedState.humanPlayerId = rSnapshot.humanPlayerId;
			mConfirmedState.fPreviousHumanArmor = rSnapshot.fPreviousHumanArmor;

			// Clear stale pending full state if replayed past it
			if (mPendingFullState.iFrame >= 0 && mPendingFullState.iFrame <= mConfirmedState.iFrame)
			{
				mPendingFullState = {};
			}

			// Erase processed updates
			mServerUpdateBuffer.erase(mServerUpdateBuffer.begin(), bufIt);

			// Clean up stale snapshots
			std::erase_if(mExtrapolatedSnapshots, [iLastMatchedFrame](const auto& rPair)
			{
				return rPair.first <= iLastMatchedFrame;
			});

			FILE_LOG(0, "[Reconcile] FastPath: frame={} matched={} humanId={} grid=({},{})", iLastMatchedFrame, iLastMatchedFrame - iExpectedFrame + 1, mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y);
			if (mHumanPlayerId.IsValid() && mCurrentFrames.contains(mHumanGridCoord))
			{
				int64_t iIdx = HumanPlayerIndex(*CurrentFrame(mHumanGridCoord).interpolate.pPlayers);
				XMVECTOR vecPos = CurrentFrame(mHumanGridCoord).interpolate.pPlayers->pVecPositions[iIdx];
				FILE_LOG(1, "[PostReconcile] pos=({:.1f},{:.1f}) frame={} grid=({},{})", XMVectorGetX(vecPos), XMVectorGetY(vecPos), miFrameCounter, mHumanGridCoord.x, mHumanGridCoord.y);
			}
			return;
		}
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
	mExtrapolatedSnapshots.clear();

	// DT: TEMP
	FILE_LOG(0, "[Reconcile] Restore: frame={} humanId={} grid=({},{})", miFrameCounter, mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y);

	// Race condition: full state arrived after Reconcile already replayed past its frame
	if (mPendingFullState.iFrame >= 0 && mPendingFullState.iFrame <= mConfirmedState.iFrame)
	{
		for (const auto& [rCoord, rSerializedFrame] : mPendingFullState.serializedFrames)
		{
			if (!mCurrentFrames.contains(rCoord))
			{
				mCurrentFrames[rCoord] = std::make_unique<Frame>();
			}
			std::istringstream iss(rSerializedFrame);
			iss >> *mCurrentFrames[rCoord];

			// Also populate next frames so interpolation has valid data
			if (!mNextFrames.contains(rCoord))
			{
				mNextFrames[rCoord] = std::make_unique<Frame>();
			}
			std::istringstream issNext(rSerializedFrame);
			issNext >> *mNextFrames[rCoord];

			FILE_LOG(0, "[Reconcile] Race-injected coord=({},{}) pending={} confirmed={}", rCoord.x, rCoord.y, mPendingFullState.iFrame, mConfirmedState.iFrame);
		}
		mPendingFullState = {};
	}

	// Ensure next frames exist for all restored coords (non-injected)
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
	const int64_t iMaxReplay = std::min((static_cast<int64_t>(mServerUpdateBuffer.size()) + 1) / 2, kiReconcileCeiling);
	int64_t iReplayCount = 0;
	while (it != mServerUpdateBuffer.end())
	{
		if (it->first != iExpectedFrame)
		{
			break; // Gap in frames, stop replay
		}

		if (iReplayCount >= iMaxReplay)
		{
			break;
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

		EnsureNextFrames();

		// DT: TEMP
		FILE_LOG(0, "[Reconcile] ActiveCoords frame={}: count={} [0]=({},{}) [1]=({},{})", miFrameCounter + 1, mActiveCoords.size(), mActiveCoords[0].x, mActiveCoords[0].y, mActiveCoords.size() > 1 ? mActiveCoords[1].x : 0, mActiveCoords.size() > 1 ? mActiveCoords[1].y : 0);

		int64_t iServerFrame = it->first;
		++miFrameCounter;
		mfCurrentTime += kfDeltaTime;

		// Build frame inputs from server data
		BuildFrameInputForFrame(iServerFrame);

		// DT: TEMP
		{
			auto originFiIt = mFrameInputs.find(engine::kOriginCoord);
			if (originFiIt != mFrameInputs.end())
			{
				const FrameInput& rFI = originFiIt->second;
				FILE_LOG(0, "[Reconcile] FrameInput frame={}: playerInputs={} statusChanges={}", iServerFrame, rFI.playerInputs.size(), rFI.statusChanges.size());
				for (size_t sc = 0; sc < rFI.statusChanges.size(); ++sc)
				{
					FILE_LOG(0, "[Reconcile] StatusChange[{}] type={}", sc, static_cast<int>(rFI.statusChanges[sc].eType));
				}
			}
		}

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
		FILE_LOG(0, "[Reconcile] Post-HarvestTransfers frame={}: humanId={} grid=({},{})", miFrameCounter, mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y);

		// DT: TEMP
		{
			const Frame& rOrigin = NextFrame(engine::kOriginCoord);
			const PlayersInterpolate& rPI = *rOrigin.interpolate.pPlayers;
			const PlayersPostRender& rPR = *rOrigin.postRender.pPlayers;
			FILE_LOG(0, "[Reconcile] Pre-swap frame={} playerCount={}", miFrameCounter, rPI.iCount);
			for (int64_t iP = 0; iP < rPI.iCount; ++iP)
			{
				FILE_LOG(0, "[Reconcile] Player[{}] pos=({:.6f},{:.6f},{:.6f}) id={}", iP, XMVectorGetX(rPI.pVecPositions[iP]), XMVectorGetY(rPI.pVecPositions[iP]), XMVectorGetZ(rPI.pVecPositions[iP]), rPR.puiIds[iP].ToUuid().Value());
			}
		}

		// Swap frames
		std::swap(mCurrentFrames, mNextFrames);
		EnsureNextFrames();

		// Inject pending full state at the transfer frame
		if (mPendingFullState.iFrame >= 0 && iServerFrame == mPendingFullState.iFrame)
		{
			for (const auto& [rCoord, rSerializedFrame] : mPendingFullState.serializedFrames)
			{
				if (!mCurrentFrames.contains(rCoord))
				{
					mCurrentFrames[rCoord] = std::make_unique<Frame>();
				}
				std::istringstream iss(rSerializedFrame);
				iss >> *mCurrentFrames[rCoord];

				// Also populate next frames so interpolation has valid data
				if (!mNextFrames.contains(rCoord))
				{
					mNextFrames[rCoord] = std::make_unique<Frame>();
				}
				std::istringstream issNext(rSerializedFrame);
				issNext >> *mNextFrames[rCoord];

				FILE_LOG(0, "[Reconcile] Injected coord=({},{}) frame={}", rCoord.x, rCoord.y, iServerFrame);
			}
			mPendingFullState = {};
		}

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
				FILE_LOG(0, "[Reconcile] CRC skip: coord=({},{}) not in mCurrentFrames frame={}", rGridUpdate.coord.x, rGridUpdate.coord.y, iServerFrame);
				continue;
			}

			CurrentFrame(rGridUpdate.coord).interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
			common::crc_t clientCrc = CurrentFrame(rGridUpdate.coord).ServerCrc();
			if (clientCrc != rGridUpdate.serverCrc)
			{
				Log("Desync at ({},{}): server={} client={} frame={}", rGridUpdate.coord.x, rGridUpdate.coord.y, rGridUpdate.serverCrc, clientCrc, iServerFrame);
				FILE_LOG(0, "[Reconcile] Desync at ({},{}): server={} client={} frame={}", rGridUpdate.coord.x, rGridUpdate.coord.y, rGridUpdate.serverCrc, clientCrc, iServerFrame);
				mpNetworkClient->SendDesyncReport(iServerFrame, rGridUpdate.coord, rGridUpdate.serverCrc, clientCrc);

				// Request server's full frame for side-by-side comparison
				mpNetworkClient->SendDebugFrameRequest(iServerFrame, rGridUpdate.coord);

				// Deep-copy the client's Frame via serialize/deserialize round-trip
				mDesyncDebugState.iFrame = iServerFrame;
				mDesyncDebugState.coord = rGridUpdate.coord;
				{
					std::ostringstream oss(std::ios::binary);
					oss << CurrentFrame(rGridUpdate.coord);
					std::istringstream iss(oss.str(), std::ios::binary);
					mDesyncDebugState.pClientFrame = std::make_unique<Frame>();
					iss >> *mDesyncDebugState.pClientFrame;
				}
				return;
			}
		}

		// Success: remove processed frame and advance
		it = mServerUpdateBuffer.erase(it);
		++iExpectedFrame;
		++iReplayCount;
	}

	// Diag: warn on high replay count
	int64_t iReplayed = miFrameCounter - iOriginalConfirmedFrame;
	if (iReplayed > 12)
	{
		FILE_LOG(0, "[Reconcile] WARNING: High replay count: replayed={} bufRemaining={}", iReplayed, mServerUpdateBuffer.size());
		DEBUG_BREAK();
	}

	// Save last server inputs for client extrapolation
	mLastServerPlayerInputs.clear();
	for (const auto& [rCoord, rFrameInput] : mFrameInputs)
	{
		mLastServerPlayerInputs[rCoord] = rFrameInput.playerInputs;
	}

	// Prune stale coords before saving confirmed state
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
	for (const auto& [rCoord, pFrame] : mCurrentFrames)
	{
		if (!std::ranges::contains(mActiveCoords, rCoord))
		{
			FILE_LOG(0, "[Reconcile] Pruned coord=({},{}) frame={}", rCoord.x, rCoord.y, miFrameCounter);
		}
	}
	std::erase_if(mCurrentFrames, [this](const auto& rPair)
	{
		return !std::ranges::contains(mActiveCoords, rPair.first);
	});

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

	// Stale pending cleanup: clear if Reconcile replayed past the pending frame
	if (mPendingFullState.iFrame >= 0 && mPendingFullState.iFrame <= mConfirmedState.iFrame)
	{
		FILE_LOG(0, "[Reconcile] Stale pending cleared: pending={} confirmed={}", mPendingFullState.iFrame, mConfirmedState.iFrame);
		mPendingFullState = {};
	}

	// DT: TEMP
	FILE_LOG(0, "[Reconcile] NewConfirmed: frame={} replayed={} humanId={} grid=({},{}) bufRemaining={}", miFrameCounter, miFrameCounter - iOriginalConfirmedFrame, mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, mServerUpdateBuffer.size());

	if (mHumanPlayerId.IsValid() && mCurrentFrames.contains(mHumanGridCoord))
	{
		int64_t iIdx = HumanPlayerIndex(*CurrentFrame(mHumanGridCoord).interpolate.pPlayers);
		XMVECTOR vecPos = CurrentFrame(mHumanGridCoord).interpolate.pPlayers->pVecPositions[iIdx];
		FILE_LOG(1, "[PostReconcile] pos=({:.1f},{:.1f}) frame={} grid=({},{})", XMVectorGetX(vecPos), XMVectorGetY(vecPos), miFrameCounter, mHumanGridCoord.x, mHumanGridCoord.y);
	}

	} // else (synchronous path)
}

void Game::TryKickReconcile()
{
	if constexpr (!kbEnableReconcileThread)
	{
		return;
	}

	if (mpNetworkClient == nullptr || mConfirmedState.iFrame < 0)
	{
		return;
	}

	if (mDesyncDebugState.iFrame >= 0)
	{
		return;
	}

	KickReconcile();
	mbReconcileInFlight = true;
}

void Game::CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, [[maybe_unused]] int64_t iFrame, [[maybe_unused]] engine::GridCoord coord)
{
	// DT: TEMP - Per-field BreakOnNotEqual comparison (shared fields only, same as ServerCrc)
	rClientFrame.ServerCompare(rServerFrame);
}

#endif // BT_CLIENT

#ifdef BT_SERVER

void Game::HandleDisconnectsServer()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::PendingDisconnect& rDisconnect : engine::gpNetworkServer->DrainPendingDisconnects())
	{
		// Queue player destruction for the next physics frame
		if (rDisconnect.playerId.IsValid())
		{
			mPendingPlayerDestroys.push_back({.coord = rDisconnect.coord, .playerId = rDisconnect.playerId});
		}

		// Remove from spawn queue if waiting
		std::erase_if(mClientsWaitingForSpawn, [&](const ClientSpawnInfo& rInfo)
		{
			return rInfo.iClientId == rDisconnect.iClientId;
		});
	}
}

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

	// Ensure destroy coords are active so the StatusChange is processed and broadcast
	for (const PendingPlayerDestroy& rDestroy : mPendingPlayerDestroys)
	{
		if (!std::ranges::contains(mActiveCoords, rDestroy.coord))
		{
			mActiveCoords.push_back(rDestroy.coord);
		}
	}

	// DT: TEMP
	FILE_LOG(0, "[ComputeActiveSetServer] count={} [0]=({},{}) [1]=({},{})", mActiveCoords.size(), mActiveCoords[0].x, mActiveCoords[0].y, mActiveCoords.size() > 1 ? mActiveCoords[1].x : 0, mActiveCoords.size() > 1 ? mActiveCoords[1].y : 0);

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
}

void Game::BuildFrameInputsServer()
{
	// Heap: unordered_map clear/insert, vector resize for playerInputs and statusChanges
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mFrameInputs.clear();
	mBroadcastSpawns.clear();

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

	// Add destroy StatusChanges for disconnected players
	for (const PendingPlayerDestroy& rDestroy : mPendingPlayerDestroys)
	{
		auto fiIt = mFrameInputs.find(rDestroy.coord);
		if (fiIt == mFrameInputs.end())
		{
			continue;
		}

		StatusChange destroyChange {.eType = StatusChangeType::kDestroyPlayer};
		int64_t iPlayerUuid = rDestroy.playerId.ToUuid().Value();
		XMFLOAT4A f4 {};
		std::memcpy(&f4, &iPlayerUuid, sizeof(int64_t));
		destroyChange.data.vecPosition = XMLoadFloat4A(&f4);
		fiIt->second.statusChanges.push_back(destroyChange);
	}
	mPendingPlayerDestroys.clear();

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

	mBroadcastTransfers.clear();

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

			// DT: TEMP
			FILE_LOG(0, "[HarvestTransfersServer] Transfer: type={} src=({},{}) dest=({},{}) align={}", static_cast<int>(rRequest.eType), rCoord.x, rCoord.y, dest.x, dest.y, data.alignment.uiValue);

			// Record transfer for broadcasting
			mBroadcastTransfers[dest].push_back({.eType = rRequest.eType, .data = data});

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
						FILE_LOG(0, "[HarvestTransfersServer] Human transfer: client={} oldId={} newId={} dest=({},{})", rClient.iClientId, transferredPlayerId.ToUuid().Value(), newPlayerId.ToUuid().Value(), dest.x, dest.y);

						break;
					}
				}
			}
		}
	}

	// Assign sequences per-destination (matches server spawn order)
	for (auto& [rCoord, rTransfers] : mBroadcastTransfers)
	{
		for (size_t i = 0; i < rTransfers.size(); ++i)
		{
			rTransfers[i].uiSequence = static_cast<uint16_t>(i);
		}
	}
}

void Game::BroadcastStatusChangesServer(int64_t iFrame)
{
	// Heap: vector construction for grid updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// DT: TEMP
	FILE_LOG(0, "[BroadcastServer] frame={} activeCoords={}", iFrame, mActiveCoords.size());

	// Compute CRCs for all active coords (once)
	std::unordered_map<engine::GridCoord, common::crc_t> crcs;
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		crcs[rCoord] = CurrentFrame(rCoord).ServerCrc();
	}

	// DT: TEMP
	FILE_LOG(0, "[BroadcastServer] frame={} originCrc={} playerCount={}", iFrame, crcs.contains(engine::kOriginCoord) ? crcs[engine::kOriginCoord] : 0, CurrentFrame(engine::kOriginCoord).interpolate.pPlayers->iCount);
	{
		const PlayersInterpolate& rPI = *CurrentFrame(engine::kOriginCoord).interpolate.pPlayers;
		const PlayersPostRender& rPR = *CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
		for (int64_t iP = 0; iP < rPI.iCount; ++iP)
		{
			FILE_LOG(0, "[BroadcastServer] frame={} Player[{}] pos=({:.6f},{:.6f},{:.6f}) id={}", iFrame, iP, XMVectorGetX(rPI.pVecPositions[iP]), XMVectorGetY(rPI.pVecPositions[iP]), XMVectorGetZ(rPI.pVecPositions[iP]), rPR.puiIds[iP].ToUuid().Value());
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
			for (const StatusChange& rTransfer : transferIt->second)
			{
				allChanges[rCoord].push_back(rTransfer);
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

	// Buffer full frame snapshots for debug frame requests
	{
		std::vector<std::pair<engine::GridCoord, const game::Frame*>> fullFrames;
		fullFrames.reserve(mActiveCoords.size());
		for (const engine::GridCoord& rCoord : mActiveCoords)
		{
			fullFrames.push_back({rCoord, &CurrentFrame(rCoord)});
		}
		engine::gpNetworkServer->BufferFullFrame(iFrame, fullFrames);
	}

	// Build per-client data and send (all changes sent to all clients)
	std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		std::vector<std::pair<engine::GridCoord, engine::GridUpdateData>> gridUpdates;
		gridUpdates.reserve(rClient.activeCoords.size());
		for (const engine::GridCoord& rCoord : rClient.activeCoords)
		{
			auto crcIt = crcs.find(rCoord);
			if (crcIt == crcs.end())
			{
				continue;
			}

			engine::GridUpdateData updateData {};
			updateData.serverCrc = crcIt->second;

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

			gridUpdates.push_back({rCoord, updateData});
		}

		// Send current frame update, then proactively re-send unacknowledged frames
		engine::gpNetworkServer->SendUpdate(rClient, iFrame, gridUpdates);
		engine::gpNetworkServer->SendResends(rClient, iFrame);
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

	// Refresh snapshot for subsequent physics frames in this tick
	mPreSpawnPlayerIds.clear();
	const PlayersPostRender& rPlayersRefresh = *CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
	for (int64_t i = 0; i < rPlayersRefresh.iCount; ++i)
	{
		mPreSpawnPlayerIds.push_back(rPlayersRefresh.puiIds[i]);
	}
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
					FILE_LOG(0, "[HandleSubscriptionUpdatesServer] client={} newId={} newCoord=({},{}) pendingCount={}", rUpdate.iClientId, rUpdate.newPlayerId.ToUuid().Value(), rUpdate.newCoord.x, rUpdate.newCoord.y, rTempClient.pendingFullStateCoords.size());
					for (const engine::GridCoord& rPendingCoord : rTempClient.pendingFullStateCoords)
					{
						FILE_LOG(0, "[HandleSubscriptionUpdatesServer]   pendingFullState: ({},{})", rPendingCoord.x, rPendingCoord.y);
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

#ifdef BT_CLIENT

void Game::KickReconcile()
{
	mpReconcileContext = std::make_unique<ReconcileContext>();
	ReconcileContext& rReconcileContext = *mpReconcileContext;

	// Deep-copy confirmed state and pending full state
	rReconcileContext.confirmedState = mConfirmedState;
	rReconcileContext.pendingFullState = mPendingFullState;

	// Copy consecutive server update entries starting at confirmed+1
	int64_t iExpected = mConfirmedState.iFrame + 1;
	for (auto it = mServerUpdateBuffer.find(iExpected); it != mServerUpdateBuffer.end(); ++it)
	{
		if (it->first != iExpected)
		{
			break;
		}
		rReconcileContext.serverUpdates[it->first] = it->second;
		iExpected = it->first + 1;
	}

	// Copy extrapolation state
	rReconcileContext.lastServerPlayerInputs = mLastServerPlayerInputs;
	rReconcileContext.extrapolatedSnapshots.insert(mExtrapolatedSnapshots.begin(), mExtrapolatedSnapshots.end());

	// Target frame and frame ID
	rReconcileContext.iTargetFrame = miFrameCounter;
	rReconcileContext.uiNextFrameId = muiNextFrameId;

	// Human tracking and alignment
	rReconcileContext.humanGridCoord = mHumanGridCoord;
	rReconcileContext.humanPlayerId = mHumanPlayerId;
	rReconcileContext.fPreviousHumanArmor = mfPreviousHumanArmor;
	rReconcileContext.playerAlignment = mPlayerAlignment;

	if (mCurrentFrames.contains(mHumanGridCoord))
	{
		rReconcileContext.pDiagMainPI = mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers.get();
		FILE_LOG(0, "[KickReconcile] pre-wake pPlayers={} pVecPos={} count={}", (void*)rReconcileContext.pDiagMainPI, (void*)rReconcileContext.pDiagMainPI->pVecPositions, rReconcileContext.pDiagMainPI->iCount);
	}

	// Dispatch to worker
	mpReconcileWorker->Wake([this]()
	{
		Reconcile(*mpReconcileContext, mAlignments);
	});
}

void Game::ReconcileComputeActiveCoords(ReconcileContext& rReconcileContext)
{
	// Human's cell plus existing neighbors
	rReconcileContext.activeCoords.clear();
	rReconcileContext.activeCoords.push_back(rReconcileContext.humanGridCoord);

	for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
	{
		engine::GridCoord neighbor {rReconcileContext.humanGridCoord.x + rOffset.x, rReconcileContext.humanGridCoord.y + rOffset.y};
		if (rReconcileContext.currentFrames.contains(neighbor))
		{
			rReconcileContext.activeCoords.push_back(neighbor);
		}
	}

	// Origin is always active
	if (!std::ranges::contains(rReconcileContext.activeCoords, engine::kOriginCoord) && rReconcileContext.currentFrames.contains(engine::kOriginCoord))
	{
		rReconcileContext.activeCoords.push_back(engine::kOriginCoord);
	}
}

void Game::ReconcileEnsureNextFrames(ReconcileContext& rReconcileContext)
{
	for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
	{
		if (!rReconcileContext.nextFrames.contains(rCoord))
		{
			rReconcileContext.nextFrames[rCoord] = std::make_unique<Frame>();
		}
	}
}

void Game::ReconcileBuildFrameInput(ReconcileContext& rReconcileContext, int64_t iServerFrame)
{
	rReconcileContext.frameInputs.clear();
	rReconcileContext.serverTransferStatusChanges.clear();

	auto bufferIt = rReconcileContext.serverUpdates.find(iServerFrame);
	if (bufferIt == rReconcileContext.serverUpdates.end())
	{
		return;
	}

	engine::ReceivedUpdate& rUpdate = bufferIt->second;

	// Initialize frame inputs with correct player counts
	for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
	{
		auto frameIt = rReconcileContext.currentFrames.find(rCoord);
		if (frameIt == rReconcileContext.currentFrames.end())
		{
			continue;
		}
		int64_t iPlayerCount = frameIt->second->interpolate.pPlayers->iCount;
		rReconcileContext.frameInputs[rCoord].playerInputs.resize(iPlayerCount);
	}

	// Separate transfers from other status changes and copy player inputs
	for (engine::ReceivedGridUpdate& rGridUpdate : rUpdate.gridUpdates)
	{
		auto frameInputIt = rReconcileContext.frameInputs.find(rGridUpdate.coord);
		if (frameInputIt == rReconcileContext.frameInputs.end())
		{
			continue;
		}

		FrameInput& rFrameInput = frameInputIt->second;

		for (StatusChange& rChange : rGridUpdate.statusChanges)
		{
			bool bIsTransfer = (rChange.eType == StatusChangeType::kTransferPlayer ||
			                    rChange.eType == StatusChangeType::kTransferSpaceship ||
			                    rChange.eType == StatusChangeType::kTransferBlaster ||
			                    rChange.eType == StatusChangeType::kTransferMissile);
			if (bIsTransfer)
			{
				FILE_LOG(0, "[ReconcileBuildFrameInput] ServerTransfer: type={} at ({},{}) frame={}", static_cast<int>(rChange.eType), rGridUpdate.coord.x, rGridUpdate.coord.y, iServerFrame);
				rReconcileContext.serverTransferStatusChanges[rGridUpdate.coord].push_back(std::move(rChange));
			}
			else
			{
				rFrameInput.statusChanges.push_back(std::move(rChange));
			}
		}

		int64_t iCopyCount = std::min(static_cast<int64_t>(rGridUpdate.playerInputs.size()), static_cast<int64_t>(rFrameInput.playerInputs.size()));
		for (int64_t i = 0; i < iCopyCount; ++i)
		{
			rFrameInput.playerInputs.at(i) = rGridUpdate.playerInputs.at(i);
		}
	}
}

void Game::ReconcileHarvestTransfers(ReconcileContext& rReconcileContext)
{
	bool bHasServerTransfers = !rReconcileContext.serverTransferStatusChanges.empty();

	if (bHasServerTransfers)
	{
		// Sort by server sequence to restore original spawn order
		for (auto& [rCoord, rTransfers] : rReconcileContext.serverTransferStatusChanges)
		{
			std::ranges::sort(rTransfers, {}, &StatusChange::uiSequence);
		}

		// Save pre-spawn player counts per destination for human ID matching
		std::unordered_map<engine::GridCoord, int64_t> preSpawnPlayerCounts;
		for (const auto& [rCoord, rTransfers] : rReconcileContext.serverTransferStatusChanges)
		{
			auto it = rReconcileContext.nextFrames.find(rCoord);
			if (it != rReconcileContext.nextFrames.end() && it->second != nullptr)
			{
				preSpawnPlayerCounts[rCoord] = it->second->postRender.pPlayers->iCount;
			}
		}

		// Apply ALL transfers from server
		for (const auto& [rCoord, rTransfers] : rReconcileContext.serverTransferStatusChanges)
		{
			auto it = rReconcileContext.nextFrames.find(rCoord);
			if (it == rReconcileContext.nextFrames.end() || it->second == nullptr)
			{
				continue;
			}

			Frame& rDestFrame = *it->second;
			for (const StatusChange& rChange : rTransfers)
			{
				FILE_LOG(0, "[ReconcileHarvestTransfers] Server: type={} dest=({},{}) align={}", static_cast<int>(rChange.eType), rCoord.x, rCoord.y, rChange.data.alignment.uiValue);
				SpawnTransfer(rDestFrame, rChange.eType, rChange.data, rReconcileContext.playerAlignment);
			}
		}
		rReconcileContext.serverTransferStatusChanges.clear();

		// Track human player transfer from local transferRequests (no spawning)
		for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
		{
			auto nextIt = rReconcileContext.nextFrames.find(rCoord);
			if (nextIt == rReconcileContext.nextFrames.end() || nextIt->second == nullptr)
			{
				continue;
			}
			Frame& rNextFrame = *nextIt->second;
			for (const TransferRequest& rRequest : rNextFrame.postRender.transferRequests)
			{
				if (rRequest.eType == StatusChangeType::kTransferPlayer &&
					rReconcileContext.humanPlayerId.IsValid() && rRequest.iEntityId == rReconcileContext.humanPlayerId.ToUuid().Value())
				{
					engine::GridCoord dest {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};
					rReconcileContext.humanGridCoord = dest;
					auto destIt = rReconcileContext.nextFrames.find(dest);
					ASSERT(destIt != rReconcileContext.nextFrames.end());
					Frame& rDestFrame = *destIt->second;

					// Find the human's new ID by matching position among recently-spawned players
					int64_t iPreCount = preSpawnPlayerCounts.contains(dest) ? preSpawnPlayerCounts[dest] : 0;
					bool bFound = false;
					for (int64_t i = iPreCount; i < rDestFrame.postRender.pPlayers->iCount; ++i)
					{
						if (XMVector4Equal(rDestFrame.interpolate.pPlayers->pVecPositions[i], rRequest.data.vecPosition))
						{
							rReconcileContext.humanPlayerId = rDestFrame.postRender.pPlayers->puiIds[i];
							bFound = true;
							break;
						}
					}
					if (!bFound)
					{
						common::Log("ReconcileHarvestTransfers: No position match for human transfer to ({},{}), falling back to last player", dest.x, dest.y);
						rReconcileContext.humanPlayerId = rDestFrame.postRender.pPlayers->puiIds[rDestFrame.postRender.pPlayers->iCount - 1];
					}
					rReconcileContext.fPreviousHumanArmor = rRequest.data.fHealth;

					FILE_LOG(0, "[ReconcileHarvestTransfers] Human transfer: entityId={} newId={} to=({},{})", rRequest.iEntityId, rReconcileContext.humanPlayerId.ToUuid().Value(), dest.x, dest.y);
				}
			}
		}
	}
	else
	{
		// Catch-up / gap: local transfer harvesting (spawn entities + track human)
		for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
		{
			auto nextIt = rReconcileContext.nextFrames.find(rCoord);
			if (nextIt == rReconcileContext.nextFrames.end() || nextIt->second == nullptr)
			{
				continue;
			}
			Frame& rNextFrame = *nextIt->second;
			if (rNextFrame.postRender.transferRequests.empty())
			{
				continue;
			}

			for (const TransferRequest& rRequest : rNextFrame.postRender.transferRequests)
			{
				engine::GridCoord dest {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};
				TransferData data = rRequest.data;

				auto destIt = rReconcileContext.nextFrames.find(dest);
				if (destIt == rReconcileContext.nextFrames.end() || destIt->second == nullptr)
				{
					continue;
				}

				Frame& rDestFrame = *destIt->second;
				SpawnTransfer(rDestFrame, rRequest.eType, data, rReconcileContext.playerAlignment);

				FILE_LOG(0, "[ReconcileHarvestTransfers] Local: type={} src=({},{}) dest=({},{})", static_cast<int>(rRequest.eType), rCoord.x, rCoord.y, dest.x, dest.y);

				// Track human player transfer
				if (rRequest.eType == StatusChangeType::kTransferPlayer &&
					rReconcileContext.humanPlayerId.IsValid() && rRequest.iEntityId == rReconcileContext.humanPlayerId.ToUuid().Value())
				{
					rReconcileContext.humanGridCoord = dest;
					rReconcileContext.humanPlayerId = rDestFrame.postRender.pPlayers->puiIds[rDestFrame.postRender.pPlayers->iCount - 1];
					rReconcileContext.fPreviousHumanArmor = data.fHealth;

					FILE_LOG(0, "[ReconcileHarvestTransfers] Local human transfer: entityId={} newId={} to=({},{})", rRequest.iEntityId, rReconcileContext.humanPlayerId.ToUuid().Value(), dest.x, dest.y);
				}
			}
		}
	}
}

void Game::Reconcile(ReconcileContext& rReconcileContext, [[maybe_unused]] const engine::Alignments& rAlignments)
{
	// DT: TEMP
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	FILE_LOG(0, "[ReconcileImpl] Entry: confirmedFrame={} serverUpdates={} pendingFrame={} targetFrame={}", rReconcileContext.confirmedState.iFrame, rReconcileContext.serverUpdates.size(), rReconcileContext.pendingFullState.iFrame, rReconcileContext.iTargetFrame);

	// DT: TEMP - monitor main thread's PlayersInterpolate for corruption
	auto diagCheck = [&](const char* pLabel)
	{
		if (rReconcileContext.pDiagMainPI)
		{
			FILE_LOG(0, "[WorkerDiag:{}] mainPI={} pVecPos={} count={}",
				pLabel,
				(void*)rReconcileContext.pDiagMainPI,
				(void*)rReconcileContext.pDiagMainPI->pVecPositions,
				rReconcileContext.pDiagMainPI->iCount);
		}
	};
	diagCheck("Entry");

	// ------ 1. CRC FAST-PATH ------
	int64_t iExpectedFrame = rReconcileContext.confirmedState.iFrame + 1;

	if (rReconcileContext.pendingFullState.iFrame < 0)
	{
		bool bAllMatch = true;
		int64_t iLastMatchedFrame = -1;
		auto bufIt = rReconcileContext.serverUpdates.begin();
		int64_t iCheckFrame = iExpectedFrame;

		while (bufIt != rReconcileContext.serverUpdates.end() && bufIt->first == iCheckFrame)
		{
			auto snapshotIt = rReconcileContext.extrapolatedSnapshots.find(iCheckFrame);
			if (snapshotIt == rReconcileContext.extrapolatedSnapshots.end())
			{
				bAllMatch = false;
				break;
			}

			for (const engine::ReceivedGridUpdate& rGridUpdate : bufIt->second.gridUpdates)
			{
				auto crcIt = snapshotIt->second.coordCrcs.find(rGridUpdate.coord);
				if (crcIt == snapshotIt->second.coordCrcs.end() || crcIt->second != rGridUpdate.serverCrc)
				{
					bAllMatch = false;
					break;
				}
			}
			if (!bAllMatch)
			{
				break;
			}

			iLastMatchedFrame = iCheckFrame;
			++iCheckFrame;
			++bufIt;
		}

		if (bAllMatch && iLastMatchedFrame >= 0)
		{
			const ExtrapolatedSnapshot& rSnapshot = rReconcileContext.extrapolatedSnapshots.at(iLastMatchedFrame);

			// Update last server player inputs from the last matched update
			rReconcileContext.newLastServerPlayerInputs.clear();
			auto lastUpdateIt = rReconcileContext.serverUpdates.find(iLastMatchedFrame);
			for (const engine::ReceivedGridUpdate& rGridUpdate : lastUpdateIt->second.gridUpdates)
			{
				rReconcileContext.newLastServerPlayerInputs[rGridUpdate.coord] = rGridUpdate.playerInputs;
			}

			// Save new confirmed state from the stored snapshot
			rReconcileContext.newConfirmedState.iFrame = iLastMatchedFrame;
			rReconcileContext.newConfirmedState.fCurrentTime = rSnapshot.fCurrentTime;
			rReconcileContext.newConfirmedState.serializedFrames = rSnapshot.serializedFrames;
			rReconcileContext.newConfirmedState.humanGridCoord = rSnapshot.humanGridCoord;
			rReconcileContext.newConfirmedState.humanPlayerId = rSnapshot.humanPlayerId;
			rReconcileContext.newConfirmedState.fPreviousHumanArmor = rSnapshot.fPreviousHumanArmor;

			rReconcileContext.bCrcFastPathHandledAll = true;

			FILE_LOG(0, "[ReconcileImpl] FastPath: frame={} matched={}", iLastMatchedFrame, iLastMatchedFrame - iExpectedFrame + 1);
			return;
		}
	}

	// No server data and no pending full state: nothing to reconcile
	if (rReconcileContext.serverUpdates.empty() && rReconcileContext.pendingFullState.iFrame < 0)
	{
		rReconcileContext.bNoChange = true;
		FILE_LOG(0, "[ReconcileImpl] NoData: confirmed unchanged at frame={}", rReconcileContext.confirmedState.iFrame);
		return;
	}

	diagCheck("PreRestore");

	// ------ 2. RESTORE FROM CONFIRMED STATE ------
	rReconcileContext.currentFrames.clear();
	for (const auto& [rCoord, rSerializedFrame] : rReconcileContext.confirmedState.serializedFrames)
	{
		rReconcileContext.currentFrames[rCoord] = std::make_unique<Frame>();
		std::istringstream iss(rSerializedFrame);
		iss >> *rReconcileContext.currentFrames[rCoord];
	}
	rReconcileContext.humanGridCoord = rReconcileContext.confirmedState.humanGridCoord;
	rReconcileContext.humanPlayerId = rReconcileContext.confirmedState.humanPlayerId;
	rReconcileContext.fPreviousHumanArmor = rReconcileContext.confirmedState.fPreviousHumanArmor;
	rReconcileContext.iFrameCounter = rReconcileContext.confirmedState.iFrame;
	rReconcileContext.fCurrentTime = rReconcileContext.confirmedState.fCurrentTime;

	diagCheck("PostRestore");

	FILE_LOG(0, "[ReconcileImpl] Restore: frame={} humanId={} grid=({},{})", rReconcileContext.iFrameCounter, rReconcileContext.humanPlayerId.ToUuid().Value(), rReconcileContext.humanGridCoord.x, rReconcileContext.humanGridCoord.y);

	// ------ 3. DETERMINE GAP VS MAIN ------
	iExpectedFrame = rReconcileContext.confirmedState.iFrame + 1;
	bool bHasGap = rReconcileContext.serverUpdates.empty() || rReconcileContext.serverUpdates.begin()->first != iExpectedFrame;
	bool bSkipMainReplay = false;

	// ------ 4. GAP REPLAY ------
	if (bHasGap)
	{
		// Inject pending UNCONDITIONALLY if pending.iFrame >= 0
		if (rReconcileContext.pendingFullState.iFrame >= 0)
		{
			for (const auto& [rCoord, rSerializedFrame] : rReconcileContext.pendingFullState.serializedFrames)
			{
				rReconcileContext.currentFrames[rCoord] = std::make_unique<Frame>();
				std::istringstream iss(rSerializedFrame);
				iss >> *rReconcileContext.currentFrames[rCoord];

				rReconcileContext.nextFrames[rCoord] = std::make_unique<Frame>();
				std::istringstream issNext(rSerializedFrame);
				issNext >> *rReconcileContext.nextFrames[rCoord];
			}
		}

		int64_t iFallbackFrame = rReconcileContext.serverUpdates.empty() ? rReconcileContext.iFrameCounter : rReconcileContext.serverUpdates.begin()->first - 1;
		const int64_t iMaxGapReplay = std::min((iFallbackFrame - rReconcileContext.confirmedState.iFrame + 1) / 2, kiReconcileCeiling);
		int64_t iGapReplayCount = 0;

		FILE_LOG(0, "[ReconcileImpl] Gap: fallback={} maxReplay={} pending={}", iFallbackFrame, iMaxGapReplay, rReconcileContext.pendingFullState.iFrame);

		for (int64_t iMissingFrame = rReconcileContext.confirmedState.iFrame + 1; iMissingFrame <= iFallbackFrame; ++iMissingFrame)
		{
			if (iGapReplayCount >= iMaxGapReplay)
			{
				break;
			}

			ReconcileComputeActiveCoords(rReconcileContext);
			ReconcileEnsureNextFrames(rReconcileContext);

			++rReconcileContext.iFrameCounter;
			rReconcileContext.fCurrentTime += kfDeltaTime;

			// Build extrapolated inputs
			rReconcileContext.frameInputs.clear();
			for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
			{
				auto frameIt = rReconcileContext.currentFrames.find(rCoord);
				if (frameIt == rReconcileContext.currentFrames.end())
				{
					continue;
				}
				int64_t iPlayerCount = frameIt->second->interpolate.pPlayers->iCount;
				FrameInput& rFrameInput = rReconcileContext.frameInputs[rCoord];
				rFrameInput.playerInputs.resize(iPlayerCount);
				auto serverIt = rReconcileContext.lastServerPlayerInputs.find(rCoord);
				if (serverIt != rReconcileContext.lastServerPlayerInputs.end())
				{
					int64_t iCopyCount = std::min(static_cast<int64_t>(serverIt->second.size()), iPlayerCount);
					for (int64_t j = 0; j < iCopyCount; ++j)
					{
						rFrameInput.playerInputs.at(j) = serverIt->second.at(j);
					}
				}
			}

			// PHYSICS PIPELINE
			const int64_t iActiveCount = static_cast<int64_t>(rReconcileContext.activeCoords.size());

			common::gpThreadLocal->mWorkbuffer.Push();
			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				const engine::GridCoord& rCoord = rReconcileContext.activeCoords[static_cast<size_t>(j)];
				common::gpThreadLocal->mWorkbuffer.PushBack<ActiveFrameRef>({
					.pNext = rReconcileContext.nextFrames.at(rCoord).get(),
					.pCurrent = rReconcileContext.currentFrames.at(rCoord).get(),
					.pFrameInput = &rReconcileContext.frameInputs.at(rCoord),
				});
			}
			std::span<const ActiveFrameRef> activeFrameRefs = common::gpThreadLocal->mWorkbuffer.Span<ActiveFrameRef>();

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				Frame& rNext = *activeFrameRefs[j].pNext;
				const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
				FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
				FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime);
				rNext.interpolate.iFrame = rReconcileContext.iFrameCounter;
				rNext.interpolate.fCurrentTime = rReconcileContext.fCurrentTime;
				rNext.interpolate.frameFlags.Set(engine::FrameFlags::kRecalculated);
			}

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

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				Frame& rNext = *activeFrameRefs[j].pNext;
				const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
				FramePostRender::PreCollision(rNext, rCurrent);
				engine::Collision::Collide(rNext.postRender.alignments, rNext.postRender.vecArea);
				FramePostRender::PostCollision(rNext, rCurrent);
				FramePostRender::AreaDamage(rNext, rCurrent);
			}

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				FramePostRender::Transfer(*activeFrameRefs[j].pNext);
			}

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				Frame& rNext = *activeFrameRefs[j].pNext;
				FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
				FramePostRender::Destroy(rNext);
				FramePostRender::Spawn(rNext, rFrameInput);
			}

			common::gpThreadLocal->mWorkbuffer.Pop();

			ReconcileHarvestTransfers(rReconcileContext);

			std::swap(rReconcileContext.currentFrames, rReconcileContext.nextFrames);
			ReconcileEnsureNextFrames(rReconcileContext);

			for (auto& [rCoord, rFrameInput] : rReconcileContext.frameInputs)
			{
				rFrameInput.ClearPressed();
			}

			++iGapReplayCount;
		}

		// Check if gap replay completed
		if (rReconcileContext.iFrameCounter < iFallbackFrame)
		{
			bSkipMainReplay = true;
		}
		else
		{
			rReconcileContext.bPendingConsumed = (rReconcileContext.pendingFullState.iFrame >= 0);
			rReconcileContext.iPendingConsumedFrame = rReconcileContext.pendingFullState.iFrame;
		}
	}

	diagCheck("PreReplay");

	// ------ 5. MAIN REPLAY ------
	if (!bSkipMainReplay && !rReconcileContext.serverUpdates.empty())
	{
		// Race condition: pending full state arrived after Reconcile already replayed past its frame
		if (rReconcileContext.pendingFullState.iFrame >= 0 && rReconcileContext.pendingFullState.iFrame <= rReconcileContext.confirmedState.iFrame)
		{
			for (const auto& [rCoord, rSerializedFrame] : rReconcileContext.pendingFullState.serializedFrames)
			{
				rReconcileContext.currentFrames[rCoord] = std::make_unique<Frame>();
				std::istringstream iss(rSerializedFrame);
				iss >> *rReconcileContext.currentFrames[rCoord];

				rReconcileContext.nextFrames[rCoord] = std::make_unique<Frame>();
				std::istringstream issNext(rSerializedFrame);
				issNext >> *rReconcileContext.nextFrames[rCoord];

				FILE_LOG(0, "[ReconcileImpl] Race-injected coord=({},{}) pending={} confirmed={}", rCoord.x, rCoord.y, rReconcileContext.pendingFullState.iFrame, rReconcileContext.confirmedState.iFrame);
			}
			rReconcileContext.bPendingConsumed = true;
			rReconcileContext.iPendingConsumedFrame = rReconcileContext.pendingFullState.iFrame;
		}

		// Ensure next frames exist for all restored coords
		for (const auto& [rCoord, pFrame] : rReconcileContext.currentFrames)
		{
			if (!rReconcileContext.nextFrames.contains(rCoord))
			{
				rReconcileContext.nextFrames[rCoord] = std::make_unique<Frame>();
			}
		}

		iExpectedFrame = rReconcileContext.iFrameCounter + 1;
		auto it = rReconcileContext.serverUpdates.begin();
		// Skip entries at or before current frame counter (already handled by gap)
		while (it != rReconcileContext.serverUpdates.end() && it->first <= rReconcileContext.iFrameCounter)
		{
			++it;
		}
		if (it != rReconcileContext.serverUpdates.end())
		{
			iExpectedFrame = it->first;
		}

		const int64_t iMaxReplay = std::min((static_cast<int64_t>(rReconcileContext.serverUpdates.size()) + 1) / 2, kiReconcileCeiling);
		int64_t iReplayCount = 0;

		while (it != rReconcileContext.serverUpdates.end())
		{
			if (it->first != iExpectedFrame)
			{
				break;
			}

			if (iReplayCount >= iMaxReplay)
			{
				break;
			}

			ReconcileComputeActiveCoords(rReconcileContext);
			ReconcileEnsureNextFrames(rReconcileContext);

			int64_t iServerFrame = it->first;
			++rReconcileContext.iFrameCounter;
			rReconcileContext.fCurrentTime += kfDeltaTime;

			ReconcileBuildFrameInput(rReconcileContext, iServerFrame);

			// PHYSICS PIPELINE
			const int64_t iActiveCount = static_cast<int64_t>(rReconcileContext.activeCoords.size());

			common::gpThreadLocal->mWorkbuffer.Push();
			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				const engine::GridCoord& rCoord = rReconcileContext.activeCoords[static_cast<size_t>(j)];
				common::gpThreadLocal->mWorkbuffer.PushBack<ActiveFrameRef>({
					.pNext = rReconcileContext.nextFrames.at(rCoord).get(),
					.pCurrent = rReconcileContext.currentFrames.at(rCoord).get(),
					.pFrameInput = &rReconcileContext.frameInputs.at(rCoord),
				});
			}
			std::span<const ActiveFrameRef> activeFrameRefs = common::gpThreadLocal->mWorkbuffer.Span<ActiveFrameRef>();

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				Frame& rNext = *activeFrameRefs[j].pNext;
				const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
				FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
				FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime);
				rNext.interpolate.iFrame = rReconcileContext.iFrameCounter;
				rNext.interpolate.fCurrentTime = rReconcileContext.fCurrentTime;
				rNext.interpolate.frameFlags.Set(engine::FrameFlags::kRecalculated);
			}

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

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				Frame& rNext = *activeFrameRefs[j].pNext;
				const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
				FramePostRender::PreCollision(rNext, rCurrent);
				engine::Collision::Collide(rNext.postRender.alignments, rNext.postRender.vecArea);
				FramePostRender::PostCollision(rNext, rCurrent);
				FramePostRender::AreaDamage(rNext, rCurrent);
			}

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				FramePostRender::Transfer(*activeFrameRefs[j].pNext);
			}

			for (int64_t j = 0; j < iActiveCount; ++j)
			{
				Frame& rNext = *activeFrameRefs[j].pNext;
				FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
				FramePostRender::Destroy(rNext);
				FramePostRender::Spawn(rNext, rFrameInput);
			}

			common::gpThreadLocal->mWorkbuffer.Pop();

			ReconcileHarvestTransfers(rReconcileContext);

			std::swap(rReconcileContext.currentFrames, rReconcileContext.nextFrames);
			ReconcileEnsureNextFrames(rReconcileContext);

			// Inject pending full state at the transfer frame
			if (rReconcileContext.pendingFullState.iFrame >= 0 && iServerFrame == rReconcileContext.pendingFullState.iFrame)
			{
				for (const auto& [rCoord, rSerializedFrame] : rReconcileContext.pendingFullState.serializedFrames)
				{
					rReconcileContext.currentFrames[rCoord] = std::make_unique<Frame>();
					std::istringstream iss(rSerializedFrame);
					iss >> *rReconcileContext.currentFrames[rCoord];

					rReconcileContext.nextFrames[rCoord] = std::make_unique<Frame>();
					std::istringstream issNext(rSerializedFrame);
					issNext >> *rReconcileContext.nextFrames[rCoord];

					FILE_LOG(0, "[ReconcileImpl] Injected coord=({},{}) frame={}", rCoord.x, rCoord.y, iServerFrame);
				}
				rReconcileContext.bPendingConsumed = true;
				rReconcileContext.iPendingConsumedFrame = rReconcileContext.pendingFullState.iFrame;
			}

			for (auto& [rCoord, rFrameInput] : rReconcileContext.frameInputs)
			{
				rFrameInput.ClearPressed();
			}

			// CRC validation
			engine::ReceivedUpdate& rUpdate = it->second;
			for (const engine::ReceivedGridUpdate& rGridUpdate : rUpdate.gridUpdates)
			{
				if (!rReconcileContext.currentFrames.contains(rGridUpdate.coord))
				{
					FILE_LOG(0, "[ReconcileImpl] CRC skip: coord=({},{}) not in currentFrames frame={}", rGridUpdate.coord.x, rGridUpdate.coord.y, iServerFrame);
					continue;
				}

				rReconcileContext.currentFrames.at(rGridUpdate.coord)->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
				common::crc_t clientCrc = rReconcileContext.currentFrames.at(rGridUpdate.coord)->ServerCrc();
				if (clientCrc != rGridUpdate.serverCrc)
				{
					common::Log("ReconcileImpl desync at ({},{}): server={} client={} frame={}", rGridUpdate.coord.x, rGridUpdate.coord.y, rGridUpdate.serverCrc, clientCrc, iServerFrame);
					FILE_LOG(0, "[ReconcileImpl] Desync at ({},{}): server={} client={} frame={}", rGridUpdate.coord.x, rGridUpdate.coord.y, rGridUpdate.serverCrc, clientCrc, iServerFrame);

					// Store desync info for main thread to handle
					rReconcileContext.iDesyncFrame = iServerFrame;
					rReconcileContext.desyncCoord = rGridUpdate.coord;
					rReconcileContext.desyncServerCrc = rGridUpdate.serverCrc;
					rReconcileContext.desyncClientCrc = clientCrc;

					// Deep-copy the client's Frame via serialize/deserialize round-trip
					std::ostringstream oss(std::ios::binary);
					oss << *rReconcileContext.currentFrames.at(rGridUpdate.coord);
					std::istringstream iss(oss.str(), std::ios::binary);
					rReconcileContext.pDesyncClientFrame = std::make_unique<Frame>();
					iss >> *rReconcileContext.pDesyncClientFrame;
					return;
				}
			}

			rReconcileContext.iLastProcessedServerFrame = iServerFrame;
			++it;
			++iExpectedFrame;
			++iReplayCount;
		}

		// Save last server inputs from rReconcileContext.frameInputs
		rReconcileContext.newLastServerPlayerInputs.clear();
		for (const auto& [rCoord, rFrameInput] : rReconcileContext.frameInputs)
		{
			rReconcileContext.newLastServerPlayerInputs[rCoord] = rFrameInput.playerInputs;
		}
	}

	diagCheck("PostReplay");

	// ------ 6. SAVE CONFIRMED STATE (before catch-up, at last replayed frame) ------
	rReconcileContext.newConfirmedState.iFrame = rReconcileContext.iFrameCounter;
	rReconcileContext.newConfirmedState.fCurrentTime = rReconcileContext.fCurrentTime;
	rReconcileContext.newConfirmedState.serializedFrames.clear();
	for (const auto& [rCoord, pFrame] : rReconcileContext.currentFrames)
	{
		std::ostringstream oss;
		oss << *pFrame;
		rReconcileContext.newConfirmedState.serializedFrames[rCoord] = oss.str();
	}
	rReconcileContext.newConfirmedState.humanGridCoord = rReconcileContext.humanGridCoord;
	rReconcileContext.newConfirmedState.humanPlayerId = rReconcileContext.humanPlayerId;
	rReconcileContext.newConfirmedState.fPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;

	FILE_LOG(0, "[ReconcileImpl] NewConfirmed: frame={} humanId={} grid=({},{})", rReconcileContext.iFrameCounter, rReconcileContext.humanPlayerId.ToUuid().Value(), rReconcileContext.humanGridCoord.x, rReconcileContext.humanGridCoord.y);

	diagCheck("PreCatchUp");

	// ------ 7. PREDICTIVE CATCH-UP ------
	// Use updated server inputs from main replay if available
	if (!rReconcileContext.newLastServerPlayerInputs.empty())
	{
		rReconcileContext.lastServerPlayerInputs = rReconcileContext.newLastServerPlayerInputs;
	}

	while (rReconcileContext.iFrameCounter < rReconcileContext.iTargetFrame)
	{
		ReconcileComputeActiveCoords(rReconcileContext);
		ReconcileEnsureNextFrames(rReconcileContext);

		++rReconcileContext.iFrameCounter;
		rReconcileContext.fCurrentTime += kfDeltaTime;

		// Build extrapolated inputs from last server player inputs
		rReconcileContext.frameInputs.clear();
		for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
		{
			auto frameIt = rReconcileContext.currentFrames.find(rCoord);
			if (frameIt == rReconcileContext.currentFrames.end())
			{
				continue;
			}
			int64_t iPlayerCount = frameIt->second->interpolate.pPlayers->iCount;
			FrameInput& rFrameInput = rReconcileContext.frameInputs[rCoord];
			rFrameInput.playerInputs.resize(iPlayerCount);
			auto serverIt = rReconcileContext.lastServerPlayerInputs.find(rCoord);
			if (serverIt != rReconcileContext.lastServerPlayerInputs.end())
			{
				int64_t iCopyCount = std::min(static_cast<int64_t>(serverIt->second.size()), iPlayerCount);
				for (int64_t j = 0; j < iCopyCount; ++j)
				{
					rFrameInput.playerInputs.at(j) = serverIt->second.at(j);
				}
			}
		}

		// PHYSICS PIPELINE
		const int64_t iActiveCount = static_cast<int64_t>(rReconcileContext.activeCoords.size());

		common::gpThreadLocal->mWorkbuffer.Push();
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			const engine::GridCoord& rCoord = rReconcileContext.activeCoords[static_cast<size_t>(j)];
			common::gpThreadLocal->mWorkbuffer.PushBack<ActiveFrameRef>({
				.pNext = rReconcileContext.nextFrames.at(rCoord).get(),
				.pCurrent = rReconcileContext.currentFrames.at(rCoord).get(),
				.pFrameInput = &rReconcileContext.frameInputs.at(rCoord),
			});
		}
		std::span<const ActiveFrameRef> activeFrameRefs = common::gpThreadLocal->mWorkbuffer.Span<ActiveFrameRef>();

		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			Frame& rNext = *activeFrameRefs[j].pNext;
			const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
			FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
			FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime);
			rNext.interpolate.iFrame = rReconcileContext.iFrameCounter;
			rNext.interpolate.fCurrentTime = rReconcileContext.fCurrentTime;
			rNext.interpolate.frameFlags.Set(engine::FrameFlags::kRecalculated);
		}

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

		// Diagnostic: validate pPlayers after AllocateAndCopy+Update
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			const engine::GridCoord& rCoord = rReconcileContext.activeCoords[static_cast<size_t>(j)];
			Frame& rNext = *activeFrameRefs[j].pNext;
			if (rNext.postRender.pPlayers != nullptr)
			{
				FILE_LOG(0, "[CatchUp:PostUpdate] coord=({},{}) pPlayers={} pData={} puiIds={} count={}", rCoord.x, rCoord.y, (void*)rNext.postRender.pPlayers.get(), (void*)rNext.postRender.pPlayers->pData.get(), (void*)rNext.postRender.pPlayers->puiIds, rNext.postRender.pPlayers->iCount);
			}
		}

		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			Frame& rNext = *activeFrameRefs[j].pNext;
			const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
			FramePostRender::PreCollision(rNext, rCurrent);
			engine::Collision::Collide(rNext.postRender.alignments, rNext.postRender.vecArea);
			FramePostRender::PostCollision(rNext, rCurrent);
			FramePostRender::AreaDamage(rNext, rCurrent);
		}

		// Diagnostic: validate pPlayers after Collision
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			const engine::GridCoord& rCoord = rReconcileContext.activeCoords[static_cast<size_t>(j)];
			Frame& rNext = *activeFrameRefs[j].pNext;
			if (rNext.postRender.pPlayers != nullptr)
			{
				FILE_LOG(0, "[CatchUp:PostCollision] coord=({},{}) pPlayers={} pData={} puiIds={} count={}", rCoord.x, rCoord.y, (void*)rNext.postRender.pPlayers.get(), (void*)rNext.postRender.pPlayers->pData.get(), (void*)rNext.postRender.pPlayers->puiIds, rNext.postRender.pPlayers->iCount);
			}
		}

		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			FramePostRender::Transfer(*activeFrameRefs[j].pNext);
		}

		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			Frame& rNext = *activeFrameRefs[j].pNext;
			FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
			FramePostRender::Destroy(rNext);
			FramePostRender::Spawn(rNext, rFrameInput);
		}

		// Diagnostic: validate pPlayers after Transfer/Destroy/Spawn
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			const engine::GridCoord& rCoord = rReconcileContext.activeCoords[static_cast<size_t>(j)];
			Frame& rNext = *activeFrameRefs[j].pNext;
			if (rNext.postRender.pPlayers != nullptr)
			{
				FILE_LOG(0, "[CatchUp:PostSpawn] coord=({},{}) pPlayers={} pData={} puiIds={} count={}", rCoord.x, rCoord.y, (void*)rNext.postRender.pPlayers.get(), (void*)rNext.postRender.pPlayers->pData.get(), (void*)rNext.postRender.pPlayers->puiIds, rNext.postRender.pPlayers->iCount);
			}
		}

		common::gpThreadLocal->mWorkbuffer.Pop();

		ReconcileHarvestTransfers(rReconcileContext);

		std::swap(rReconcileContext.currentFrames, rReconcileContext.nextFrames);
		ReconcileEnsureNextFrames(rReconcileContext);

		for (auto& [rCoord, rFrameInput] : rReconcileContext.frameInputs)
		{
			rFrameInput.ClearPressed();
		}

		// Store catch-up snapshot for CRC fast-path
		{
			ExtrapolatedSnapshot& rSnapshot = rReconcileContext.extrapolatedSnapshots[rReconcileContext.iFrameCounter];
			rSnapshot.coordCrcs.clear();
			rSnapshot.serializedFrames.clear();
			rSnapshot.humanGridCoord = rReconcileContext.humanGridCoord;
			rSnapshot.humanPlayerId = rReconcileContext.humanPlayerId;
			rSnapshot.fPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;
			rSnapshot.fCurrentTime = rReconcileContext.fCurrentTime;
			for (const auto& [rCoord, pFrame] : rReconcileContext.currentFrames)
			{
				rSnapshot.coordCrcs[rCoord] = pFrame->ServerCrc();
				std::ostringstream oss;
				oss << *pFrame;
				rSnapshot.serializedFrames[rCoord] = oss.str();
			}
		}
	}

	// ------ 8. FINALIZE ------
	// Update last server player inputs from final frame
	if (rReconcileContext.newLastServerPlayerInputs.empty())
	{
		for (const auto& [rCoord, rFrameInput] : rReconcileContext.frameInputs)
		{
			rReconcileContext.newLastServerPlayerInputs[rCoord] = rFrameInput.playerInputs;
		}
	}

	// Prune stale coords from currentFrames
	ReconcileComputeActiveCoords(rReconcileContext);
	std::erase_if(rReconcileContext.currentFrames, [&rReconcileContext](const auto& rPair)
	{
		return !std::ranges::contains(rReconcileContext.activeCoords, rPair.first);
	});

	if (rReconcileContext.currentFrames.contains(rReconcileContext.humanGridCoord))
	{
		FILE_LOG(0, "[ReconcileImpl] Exit: coord=({},{}) pPlayers={} pVecPos={} count={}", rReconcileContext.humanGridCoord.x, rReconcileContext.humanGridCoord.y, (void*)rReconcileContext.currentFrames.at(rReconcileContext.humanGridCoord)->interpolate.pPlayers.get(), (void*)rReconcileContext.currentFrames.at(rReconcileContext.humanGridCoord)->interpolate.pPlayers->pVecPositions, rReconcileContext.currentFrames.at(rReconcileContext.humanGridCoord)->interpolate.pPlayers->iCount);
	}
}

void Game::ApplyReconcileResult()
{
	// Heap: Frame deserialization, map operations
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	ReconcileContext& rReconcileContext = *mpReconcileContext;

	// Handle desync (network ops must happen on main thread)
	if (rReconcileContext.iDesyncFrame >= 0)
	{
		mpNetworkClient->SendDesyncReport(rReconcileContext.iDesyncFrame, rReconcileContext.desyncCoord, rReconcileContext.desyncServerCrc, rReconcileContext.desyncClientCrc);
		mpNetworkClient->SendDebugFrameRequest(rReconcileContext.iDesyncFrame, rReconcileContext.desyncCoord);

		mDesyncDebugState.iFrame = rReconcileContext.iDesyncFrame;
		mDesyncDebugState.coord = rReconcileContext.desyncCoord;
		mDesyncDebugState.pClientFrame = std::move(rReconcileContext.pDesyncClientFrame);

		mpReconcileContext.reset();
		return;
	}

	// No server data was available — confirmed state unchanged, skip apply
	if (rReconcileContext.bNoChange)
	{
		mpReconcileContext.reset();
		return;
	}

	// Move new confirmed state
	mConfirmedState = std::move(rReconcileContext.newConfirmedState);

	// Update last server player inputs
	mLastServerPlayerInputs = std::move(rReconcileContext.newLastServerPlayerInputs);

	// Update human tracking
	mHumanGridCoord = rReconcileContext.humanGridCoord;
	mHumanPlayerId = rReconcileContext.humanPlayerId;
	mfPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;

	// Clear pending full state only if the worker consumed the same one
	if (rReconcileContext.bPendingConsumed && mPendingFullState.iFrame == rReconcileContext.iPendingConsumedFrame)
	{
		mPendingFullState.iFrame = -1;
		mPendingFullState.serializedFrames.clear();
	}

	// Advance frame ID counter past worker's usage
	muiNextFrameId = std::max(muiNextFrameId, rReconcileContext.uiNextFrameId);

	// Prune processed server entries from buffer
	auto it = mServerUpdateBuffer.begin();
	while (it != mServerUpdateBuffer.end() && it->first <= rReconcileContext.iLastProcessedServerFrame)
	{
		it = mServerUpdateBuffer.erase(it);
	}

	// Prune stale extrapolated snapshots (keep those beyond new confirmed frame)
	std::erase_if(mExtrapolatedSnapshots, [&](const auto& rPair) { return rPair.first <= mConfirmedState.iFrame; });

	if (rReconcileContext.bCrcFastPathHandledAll)
	{
		// Fast-path: main thread's mCurrentFrames already matches the confirmed extrapolation.
		// Only advance mConfirmedState, prune snapshots/buffer — don't touch mCurrentFrames or counters.
		if (mCurrentFrames.contains(mHumanGridCoord))
		{
			FILE_LOG(0, "[ApplyReconcile] CRC fast-path, pPlayers={} pVecPos={} count={}", (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers.get(), (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->pVecPositions, mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->iCount);
		}
	}
	else
	{
		// Preserve catch-up snapshots for CRC fast-path
		for (auto& [iFrame, rSnapshot] : rReconcileContext.extrapolatedSnapshots)
		{
			if (iFrame > mConfirmedState.iFrame)
			{
				mExtrapolatedSnapshots[iFrame] = std::move(rSnapshot);
			}
		}

		// Use caught-up frames directly for rendering (confirmed state is for rollback only)
		mCurrentFrames = std::move(rReconcileContext.currentFrames);

		if (mCurrentFrames.contains(mHumanGridCoord))
		{
			FILE_LOG(0, "[ApplyReconcile] moved frames, coord=({},{}) pPlayers={} pVecPos={} count={}", mHumanGridCoord.x, mHumanGridCoord.y, (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers.get(), (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->pVecPositions, mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->iCount);
		}

		// Restore counters from caught-up state
		miFrameCounter = rReconcileContext.iFrameCounter;
		mfCurrentTime = rReconcileContext.fCurrentTime;

		// Ensure next frames exist
		EnsureNextFrames();

		if (mCurrentFrames.contains(mHumanGridCoord))
		{
			FILE_LOG(0, "[ApplyReconcile] post-EnsureNextFrames pPlayers={} pVecPos={} count={}", (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers.get(), (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->pVecPositions, mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->iCount);
		}
	}

	mpReconcileContext.reset();

	if (mCurrentFrames.contains(mHumanGridCoord))
	{
		FILE_LOG(0, "[ApplyReconcile] post-reset pPlayers={} pVecPos={} count={}", (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers.get(), (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->pVecPositions, mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->iCount);
	}
}

#endif // BT_CLIENT

} // namespace game
