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
		iHumanIndex = (humanFlags & HumanFlags::kAlive) ? HumanPlayerIndex(rPlayers).value_or(-1) : -1;

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

	if (!mCurrentFrames.contains(mHumanGridCoord))
	{
		return;
	}

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

#ifdef BT_CLIENT
static void CopyPlayerInputs(FrameInput& rFrameInput, const std::vector<PlayerInput>& rSource)
{
	int64_t iCopyCount = std::min(static_cast<int64_t>(rSource.size()), static_cast<int64_t>(rFrameInput.playerInputs.size()));
	for (int64_t i = 0; i < iCopyCount; ++i)
	{
		rFrameInput.playerInputs.at(i) = rSource.at(i);
	}
}
#endif

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
			if (!mCurrentFrames.contains(rCoord))
			{
				continue;
			}

			const Frame& rCurrentFrame = CurrentFrame(rCoord);
			int64_t iPlayerCount = rCurrentFrame.interpolate.pPlayers->iCount;

			FrameInput& rFrameInput = mFrameInputs[rCoord];
			rFrameInput.playerInputs.resize(iPlayerCount);

			// Apply last server-confirmed inputs from per-coord reconcile state
			auto stateIt = mCoordReconcileStates.find(rCoord);
			if (stateIt != mCoordReconcileStates.end() && !stateIt->second.lastServerPlayerInputs.empty())
			{
				CopyPlayerInputs(rFrameInput, stateIt->second.lastServerPlayerInputs);
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

#ifdef BT_CLIENT
static void ApplyServerTransfers(
	std::unordered_map<engine::GridCoord, std::unique_ptr<Frame>>& rNextFrames,
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>>& rServerTransfers,
	const std::vector<engine::GridCoord>& rActiveCoords,
	engine::alignment_t playerAlignment,
	player_t& rHumanPlayerId,
	engine::GridCoord& rHumanGridCoord,
	float& rfPreviousHumanArmor)
{
	// Sort by server sequence to restore original spawn order
	for (auto& [rCoord, rTransfers] : rServerTransfers)
	{
		std::ranges::sort(rTransfers, {}, &StatusChange::uiSequence);
	}

	// Save pre-spawn player counts per destination for human ID matching
	std::unordered_map<engine::GridCoord, int64_t> preSpawnPlayerCounts;
	for (const auto& [rCoord, rTransfers] : rServerTransfers)
	{
		auto it = rNextFrames.find(rCoord);
		if (it != rNextFrames.end() && it->second != nullptr)
		{
			preSpawnPlayerCounts[rCoord] = it->second->postRender.pPlayers->iCount;
		}
	}

	// Apply ALL transfers from server
	for (const auto& [rCoord, rTransfers] : rServerTransfers)
	{
		auto it = rNextFrames.find(rCoord);
		if (it == rNextFrames.end() || it->second == nullptr)
		{
			continue;
		}

		Frame& rDestFrame = *it->second;
		for (const StatusChange& rChange : rTransfers)
		{
			SpawnTransfer(rDestFrame, rChange.eType, rChange.data, playerAlignment);
		}
	}
	rServerTransfers.clear();

	// Track human player transfer from local transferRequests (no spawning)
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto nextIt = rNextFrames.find(rCoord);
		if (nextIt == rNextFrames.end() || nextIt->second == nullptr)
		{
			continue;
		}
		Frame& rNextFrame = *nextIt->second;
		for (const TransferRequest& rRequest : rNextFrame.postRender.transferRequests)
		{
			if (rRequest.eType == StatusChangeType::kTransferPlayer &&
				rHumanPlayerId.IsValid() && rRequest.iEntityId == rHumanPlayerId.ToUuid().Value())
			{
				engine::GridCoord dest {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};
				rHumanGridCoord = dest;
				auto destIt = rNextFrames.find(dest);
				ASSERT(destIt != rNextFrames.end());
				Frame& rDestFrame = *destIt->second;

				// Find the human's new ID by matching position among recently-spawned players
				int64_t iPreCount = preSpawnPlayerCounts.contains(dest) ? preSpawnPlayerCounts[dest] : 0;
				bool bFound = false;
				for (int64_t i = iPreCount; i < rDestFrame.postRender.pPlayers->iCount; ++i)
				{
					if (XMVector4Equal(rDestFrame.interpolate.pPlayers->pVecPositions[i], rRequest.data.vecPosition))
					{
						rHumanPlayerId = rDestFrame.postRender.pPlayers->puiIds[i];
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					common::Log("ApplyServerTransfers: No position match for human transfer to ({},{}), falling back to last player", dest.x, dest.y);
					rHumanPlayerId = rDestFrame.postRender.pPlayers->puiIds[rDestFrame.postRender.pPlayers->iCount - 1];
				}
				rfPreviousHumanArmor = rRequest.data.fHealth;

				FILE_LOG(0, "[ApplyServerTransfers] Human transfer: entityId={} newId={} to=({},{})", rRequest.iEntityId, rHumanPlayerId.ToUuid().Value(), dest.x, dest.y);
			}
		}
	}
}
#endif

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
		ApplyServerTransfers(mNextFrames, mServerTransferStatusChanges, mActiveCoords, mPlayerAlignment, mHumanPlayerId, mHumanGridCoord, mfPreviousHumanArmor);
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
	mActiveCoords.clear();
	mActiveCoords.push_back(mHumanGridCoord);
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
	if (!mCurrentFrames.contains(mHumanGridCoord))
	{
		return false;
	}
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
	if (meUiState == UiState::kModal)
	{
		return;
	}

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
	mModalMessage[0] = '\0';
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

void Game::StoreExtrapolatedSnapshot(int64_t iFrame)
{
	// Store per-coord extrapolated snapshots for CRC fast-path
	for (const auto& [rCoord, pFrame] : mCurrentFrames)
	{
		auto stateIt = mCoordReconcileStates.find(rCoord);
		if (stateIt == mCoordReconcileStates.end())
		{
			continue;
		}

		CoordExtrapolatedSnapshot& rSnapshot = stateIt->second.extrapolatedSnapshots[iFrame];
		rSnapshot.crc = pFrame->ServerCrc();
		std::ostringstream oss;
		oss << *pFrame;
		rSnapshot.serializedFrame = oss.str();
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
	mCoordReconcileStates.clear();
	mConfirmedHumanState = {};
	mDesyncDebugState = {};
	mServerTransferStatusChanges.clear();
	mSubscriptionQueue.clear();
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
			char pcAddress[16] {};
			snprintf(pcAddress, sizeof(pcAddress), "%s", mpDiscoveryScanner->GetFoundAddress());
			FILE_LOG(0, "Discovery: found server at {}", pcAddress);
			mpDiscoveryScanner.reset();
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

	// Wait for server connection response before entering game
	if (!mpNetworkClient->IsConnectionAccepted())
	{
		const char* pRejection = mpNetworkClient->GetRejectionReason();
		if (pRejection != nullptr)
		{
			snprintf(mModalMessage, sizeof(mModalMessage), "%s", pRejection);
			DisconnectFromServer();
			meUiState = UiState::kModal;
			return;
		}

		if (mpNetworkClient->WasDisconnected())
		{
			snprintf(mModalMessage, sizeof(mModalMessage), "Connection failed");
			DisconnectFromServer();
			meUiState = UiState::kModal;
			return;
		}

		return;
	}

	// Connection accepted - transition to game mode (runs once)
	if (CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kMainMenu)
	{
		miGameMusicIndex = 0;
		engine::gpAudioManager->PlayMusic(mGameMusicPlaylist.at(0));
		CreateNewFrame(GameFlags::kGame);
		Reset();
		meUiState = UiState::kNone;
	}

	// Check for debug frame response
	std::unique_ptr<engine::ReceivedDebugFrame> pDebugFrame = mpNetworkClient->DrainReceivedDebugFrame();
	if (pDebugFrame != nullptr && mDesyncDebugState.pClientFrame != nullptr)
	{
		FILE_LOG(0, "[PollNetworkClient] Debug frame received for frame={} coord=({},{}), running CompareWithServerFrame", mDesyncDebugState.iFrame, mDesyncDebugState.coord.x, mDesyncDebugState.coord.y);
		CompareWithServerFrame(*mDesyncDebugState.pClientFrame, *pDebugFrame->pFrame, mDesyncDebugState.iFrame, mDesyncDebugState.coord);
		mDesyncDebugState = {};
		DEBUG_BREAK();
		snprintf(mModalMessage, sizeof(mModalMessage), "Desynced from server");
		mpNetworkClient->Disconnect();
		return;
	}

	if (mpNetworkClient->WasDisconnected())
	{
		FILE_LOG(0, "[PollNetworkClient] Disconnected while waiting for debug frame: desyncFrame={}", mDesyncDebugState.iFrame);
		ChangeFrame(GameFlags::kMainMenu);
		meUiState = mModalMessage[0] != '\0' ? UiState::kModal : UiState::kPause;
		return;
	}

	// Waiting for debug frame response — skip normal processing
	if (mDesyncDebugState.iFrame >= 0)
	{
		return;
	}

	// Check for player assignments
	for (const engine::ReceivedAssignment& rAssignment : mpNetworkClient->DrainReceivedAssignments())
	{
		if (rAssignment.playerId != mHumanPlayerId)
		{
			player_t oldHumanPlayerId = mHumanPlayerId; // DT: TEMP
			mHumanPlayerId = rAssignment.playerId;
			mHumanGridCoord = rAssignment.coord;
			mSpawnFlags = {};

			// DT: TEMP
			FILE_LOG(0, "[PollNetworkClient] Assignment: old={} new={} grid=({},{}) frame={}", oldHumanPlayerId.ToUuid().Value(), mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, miFrameCounter);

			// Trigger subscription updates for the new grid position
			UpdateSubscriptions();
		}
	}

	ApplyReceivedFullStates();
	UpdateSubscriptions();
	ApplyReceivedUpdates();
}

void Game::ApplyReceivedFullStates()
{
	// Heap: Moving unique_ptr<Frame> into mCurrentFrames, stringstream serialization
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::ReceivedCoordFullState>& rFullStates = mpNetworkClient->DrainReceivedFullStates();
	if (rFullStates.empty())
	{
		return;
	}

	for (engine::ReceivedCoordFullState& rFullState : rFullStates)
	{
		engine::GridCoord coord = rFullState.coord;
		int64_t iFrame = rFullState.iFrame;

		// Hydrate client-only objects
		Frame& rFrame = *rFullState.pFrame;
		BlastersInterpolate::HydrateClientObjects(rFrame);
		MissilesInterpolate::HydrateClientObjects(rFrame);
		SpaceshipsInterpolate::HydrateClientObjects(rFrame);

		bool bNewEntry = !mCoordReconcileStates.contains(coord);
		CoordReconcileState& rState = mCoordReconcileStates[coord];
		if (bNewEntry)
		{
			rState.uiGeneration = muiNextReconcileGeneration++;
		}

		if (rState.iConfirmedFrame < 0)
		{
			// First full state for this coord: inject directly into mCurrentFrames
			mCurrentFrames[coord] = std::move(rFullState.pFrame);

			if (!mNextFrames.contains(coord))
			{
				mNextFrames[coord] = std::make_unique<Frame>();
			}

			// Establish confirmed state for this coord
			rState.iConfirmedFrame = iFrame;
			std::ostringstream oss;
			oss << *mCurrentFrames[coord];
			rState.confirmedSerializedFrame = oss.str();

			// Set frame counter from first received full state
			if (miFrameCounter < iFrame)
			{
				miFrameCounter = iFrame;
				mfCurrentTime = mCurrentFrames[coord]->interpolate.fCurrentTime;
			}

			mConfirmedHumanState.humanGridCoord = mHumanGridCoord;
			mConfirmedHumanState.humanPlayerId = mHumanPlayerId;
			mConfirmedHumanState.fPreviousHumanArmor = mfPreviousHumanArmor;
			mConfirmedHumanState.fCurrentTime = mfCurrentTime;

			FILE_LOG(0, "[ApplyReceivedFullStates] Initial coord=({},{}) frame={}", coord.x, coord.y, iFrame);
		}
		else
		{
			// Coord already has confirmed state: store as pending for reconcile injection
			std::ostringstream oss;
			oss << rFrame;
			rState.pendingFullState = {iFrame, oss.str()};

			FILE_LOG(0, "[ApplyReceivedFullStates] Deferred coord=({},{}) frame={}", coord.x, coord.y, iFrame);
		}
	}

	// Try subscribing to the next coord in the queue
	TrySubscribeNext();
}

void Game::ApplyReceivedUpdates()
{
	// Heap: map insertion for per-frame server updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const auto& rCoordSlots = mpNetworkClient->GetCoordSlots();
	auto& rAllUpdates = mpNetworkClient->DrainReceivedCoordUpdates();

	for (int64_t iSlot = 0; iSlot < engine::NetworkManager::kiMaxCoordSlots; ++iSlot)
	{
		std::vector<engine::ReceivedCoordUpdate>& rSlotUpdates = rAllUpdates[iSlot];
		if (rSlotUpdates.empty())
		{
			continue;
		}

		const engine::ClientCoordSlot& rSlot = rCoordSlots[iSlot];
		if (rSlot.eState != engine::CoordSubscriptionState::kActive)
		{
			rSlotUpdates.clear();
			continue;
		}

		engine::GridCoord coord = rSlot.coord;
		CoordReconcileState& rState = mCoordReconcileStates[coord];

		for (engine::ReceivedCoordUpdate& rUpdate : rSlotUpdates)
		{
			// Skip frames at or before confirmed frame for this coord
			if (rUpdate.iFrame <= rState.iConfirmedFrame)
			{
				continue;
			}

			if (static_cast<int64_t>(rState.serverUpdates.size()) >= engine::kiMaxBufferedFrames)
			{
				continue;
			}

			miLatestServerFrame = std::max(miLatestServerFrame, rUpdate.iFrame);

			if (!rState.serverUpdates.contains(rUpdate.iFrame))
			{
				rState.serverUpdates[rUpdate.iFrame] = {
					.serverCrc = rUpdate.serverCrc,
					.statusChanges = std::move(rUpdate.statusChanges),
					.playerInputs = std::move(rUpdate.playerInputs),
				};
			}
		}

		rSlotUpdates.clear();
	}
}

int64_t Game::GetConfirmedFrame() const
{
	int64_t iMin = -1;
	for (const auto& [rCoord, rState] : mCoordReconcileStates)
	{
		if (rState.iConfirmedFrame >= 0 && (iMin < 0 || rState.iConfirmedFrame < iMin))
		{
			iMin = rState.iConfirmedFrame;
		}
	}
	return iMin;
}

int64_t Game::GetServerUpdateBufferSize() const
{
	int64_t iTotal = 0;
	for (const auto& [rCoord, rState] : mCoordReconcileStates)
	{
		iTotal += static_cast<int64_t>(rState.serverUpdates.size());
	}
	return iTotal;
}

void Game::UpdateSubscriptions()
{
	if (mpNetworkClient == nullptr)
	{
		return;
	}

	// Heap: vector operations for subscription queue
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Compute desired coords (priority-ordered)
	std::vector<engine::GridCoord> desiredCoords;

	// Priority 0: Human's current coord
	desiredCoords.push_back(mHumanGridCoord);

	// Priority 1: Direct neighbors
	for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
	{
		engine::GridCoord neighbor {mHumanGridCoord.x + rOffset.x, mHumanGridCoord.y + rOffset.y};
		desiredCoords.push_back(neighbor);
	}

	// Priority 2: Origin if not already included
	if (!std::ranges::contains(desiredCoords, engine::kOriginCoord))
	{
		desiredCoords.push_back(engine::kOriginCoord);
	}

	auto& rSlots = mpNetworkClient->GetCoordSlots();

	// Unsubscribe from coords no longer desired
	for (int64_t i = 0; i < engine::NetworkManager::kiMaxCoordSlots; ++i)
	{
		if (rSlots[i].eState == engine::CoordSubscriptionState::kUnsubscribed ||
		    rSlots[i].eState == engine::CoordSubscriptionState::kUnsubscribing)
		{
			continue;
		}

		if (!std::ranges::contains(desiredCoords, rSlots[i].coord))
		{
			engine::GridCoord unsubCoord = rSlots[i].coord;
			if (rSlots[i].eState == engine::CoordSubscriptionState::kSubscribing)
			{
				rSlots[i] = {};
			}
			else
			{
				mpNetworkClient->SendUnsubscribe(i);
			}
			mCoordReconcileStates.erase(unsubCoord);
			FILE_LOG(0, "[UpdateSubscriptions] Unsubscribe slot={} coord=({},{})", i, unsubCoord.x, unsubCoord.y);
		}
	}

	// Build subscription queue: desired coords not yet subscribed (in priority order)
	mSubscriptionQueue.clear();
	for (const engine::GridCoord& rCoord : desiredCoords)
	{
		bool bAlreadySubscribed = false;
		for (int64_t i = 0; i < engine::NetworkManager::kiMaxCoordSlots; ++i)
		{
			if (rSlots[i].coord == rCoord &&
			    rSlots[i].eState != engine::CoordSubscriptionState::kUnsubscribed &&
			    rSlots[i].eState != engine::CoordSubscriptionState::kUnsubscribing)
			{
				bAlreadySubscribed = true;
				break;
			}
		}

		if (!bAlreadySubscribed)
		{
			mSubscriptionQueue.push_back(rCoord);
		}
	}

	// Start subscribing
	TrySubscribeNext();
}

void Game::TrySubscribeNext()
{
	if (mpNetworkClient == nullptr || mSubscriptionQueue.empty())
	{
		return;
	}

	// Check if any slot is currently subscribing or waiting for full state
	const auto& rSlots = mpNetworkClient->GetCoordSlots();
	for (int64_t i = 0; i < engine::NetworkManager::kiMaxCoordSlots; ++i)
	{
		if (rSlots[i].eState == engine::CoordSubscriptionState::kSubscribing ||
		    rSlots[i].eState == engine::CoordSubscriptionState::kWaitingFullState)
		{
			return; // Wait for current subscription to complete
		}
	}

	engine::GridCoord coord = mSubscriptionQueue.front();
	mSubscriptionQueue.erase(mSubscriptionQueue.begin());

	mpNetworkClient->SendSubscribe(coord);
	FILE_LOG(0, "[TrySubscribeNext] Subscribe coord=({},{}) remaining={}", coord.x, coord.y, mSubscriptionQueue.size());
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
		tempInput.playerInputs.resize(iHumanIndex + 1);
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

	mpNetworkClient->SendInput(mLocalPlayerInput);
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
}

void Game::TryKickReconcile()
{
	if (mpNetworkClient == nullptr || GetConfirmedFrame() < 0)
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
	// DT: TEMP - Log entity counts before deep comparison fires BreakOnNotEqual
	FILE_LOG(0, "[CompareWithServerFrame] frame={} coord=({},{})", iFrame, coord.x, coord.y);
	FILE_LOG(0, "[CompareWithServerFrame] CLIENT: pushers={}/{} spaceships={} missiles={} blasters={} players={} explosions={}", rClientFrame.interpolate.pushers.iCount, rClientFrame.interpolate.pushers.idToIndexMap.size(), rClientFrame.interpolate.pSpaceships->iCount, rClientFrame.interpolate.pMissiles->iCount, rClientFrame.interpolate.pBlasters->iCount, rClientFrame.interpolate.pPlayers->iCount, rClientFrame.interpolate.explosions.iCount);
	FILE_LOG(0, "[CompareWithServerFrame] SERVER: pushers={}/{} spaceships={} missiles={} blasters={} players={} explosions={}", rServerFrame.interpolate.pushers.iCount, rServerFrame.interpolate.pushers.idToIndexMap.size(), rServerFrame.interpolate.pSpaceships->iCount, rServerFrame.interpolate.pMissiles->iCount, rServerFrame.interpolate.pBlasters->iCount, rServerFrame.interpolate.pPlayers->iCount, rServerFrame.interpolate.explosions.iCount);

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

	// Union all clients' subscribed coords
	const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (const engine::ClientConnection& rClient : rClients)
	{
		for (int64_t i = 0; i < engine::NetworkManager::kiMaxCoordSlots; ++i)
		{
			if (rClient.coordSubscriptions[i].bActive)
			{
				engine::GridCoord coord = rClient.coordSubscriptions[i].coord;
				if (!std::ranges::contains(mActiveCoords, coord))
				{
					mActiveCoords.push_back(coord);
				}
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
		const engine::ClientConnection* pClient = engine::gpNetworkServer->FindClient(rInput.iClientId);
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

		{
			uint32_t uiExisting = 0;
			std::memcpy(&uiExisting, &rFrameInput.pressedFlags, sizeof(uint32_t));
			uint32_t uiNew = 0;
			std::memcpy(&uiNew, &rInput.pressedFlags, sizeof(uint32_t));
			uiExisting |= uiNew;
			std::memcpy(&rFrameInput.pressedFlags, &uiExisting, sizeof(uint32_t));
		}
	}

	// Add spawn StatusChanges for clients waiting for initial spawn
	for (const ClientSpawnInfo& rInfo : mClientsWaitingForSpawn)
	{
		mFrameInputs[rInfo.spawnCoord].statusChanges.push_back({.eType = StatusChangeType::kSpawnPlayer});
	}

	// Add destroy StatusChanges for disconnected players
	for (const PendingPlayerDestroy& rDestroy : mPendingPlayerDestroys)
	{
		auto frameInputIt = mFrameInputs.find(rDestroy.coord);
		if (frameInputIt == mFrameInputs.end())
		{
			continue;
		}

		StatusChange destroyChange {.eType = StatusChangeType::kDestroyPlayer};
		int64_t iPlayerUuid = rDestroy.playerId.ToUuid().Value();
		XMFLOAT4A f4 {};
		std::memcpy(&f4, &iPlayerUuid, sizeof(int64_t));
		destroyChange.data.vecPosition = XMLoadFloat4A(&f4);
		frameInputIt->second.statusChanges.push_back(destroyChange);
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
	if (!mClientsWaitingForSpawn.empty() && mCurrentFrames.contains(engine::kOriginCoord))
	{
		RefreshPreSpawnSnapshot();
	}
	else
	{
		mPreSpawnPlayerIds.clear();
	}
}

void Game::ProcessSpawnRequestsServer()
{
	// Heap: vector push_back for spawn StatusChanges
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::PendingSpawnRequest& rRequest : engine::gpNetworkServer->DrainPendingSpawnRequests())
	{
		const engine::ClientConnection* pClient = engine::gpNetworkServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		if (rRequest.flags & engine::ClientRequestFlags::kRespawnRequested ||
		    rRequest.flags & engine::ClientRequestFlags::kSpawnRequested)
		{
			mClientsWaitingForSpawn.push_back({rRequest.iClientId, engine::kOriginCoord});
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

	FILE_LOG(0, "[BroadcastServer] frame={} activeCoords={}", iFrame, mActiveCoords.size());

	// Build unfiltered data: spawns + all transfers
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

	// Buffer per-coord frame data into ring buffers
	std::vector<std::pair<engine::GridCoord, engine::GridUpdateData>> allGridUpdates;
	allGridUpdates.reserve(mActiveCoords.size());
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		engine::GridUpdateData updateData {};
		updateData.serverCrc = CurrentFrame(rCoord).ServerCrc();

		auto it = allChanges.find(rCoord);
		if (it != allChanges.end())
		{
			updateData.statusChanges = std::span<const StatusChange>(it->second);
		}

		auto frameInputIt = mFrameInputs.find(rCoord);
		if (frameInputIt != mFrameInputs.end())
		{
			updateData.playerInputs = std::span<const PlayerInput>(frameInputIt->second.playerInputs);
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

	// Send per-client updates (server iterates each client's subscribed slots internally)
	std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		engine::gpNetworkServer->SendUpdate(rClient, iFrame);
		engine::gpNetworkServer->SendResends(rClient, iFrame);
	}
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

		if (std::ranges::contains(mClientsWaitingForSpawn, rClient.iClientId, &ClientSpawnInfo::iClientId))
		{
			continue;
		}

		mClientsWaitingForSpawn.push_back({rClient.iClientId, engine::kOriginCoord});
	}
}

void Game::RefreshPreSpawnSnapshot()
{
	mPreSpawnPlayerIds.clear();
	const PlayersPostRender& rPlayers = *CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
	for (int64_t i = 0; i < rPlayers.iCount; ++i)
	{
		mPreSpawnPlayerIds.push_back(rPlayers.puiIds[i]);
	}
}

void Game::FinalizeNewClientsServer([[maybe_unused]] int64_t iFrame)
{
	if (mClientsWaitingForSpawn.empty())
	{
		return;
	}

	// Heap: vector operations
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
	// Client handles subscriptions — no full state sent here
	size_t iAssignCount = std::min(mClientsWaitingForSpawn.size(), newPlayerIds.size());
	for (size_t i = 0; i < iAssignCount; ++i)
	{
		int64_t iClientId = mClientsWaitingForSpawn.at(i).iClientId;
		player_t playerId = newPlayerIds.at(i);

		engine::gpNetworkServer->SendAssignPlayer(iClientId, playerId, engine::kOriginCoord);
	}

	mClientsWaitingForSpawn.erase(mClientsWaitingForSpawn.begin(), mClientsWaitingForSpawn.begin() + static_cast<int64_t>(iAssignCount));

	// Refresh snapshot for subsequent physics frames in this tick
	RefreshPreSpawnSnapshot();
}

void Game::HandleSubscriptionUpdatesServer([[maybe_unused]] int64_t iFrame)
{
	// Send full state for newly subscribed coords (validate slot is still active)
	std::vector<engine::PendingNewSubscription>& rNewSubs = engine::gpNetworkServer->DrainPendingNewSubscriptions();
	for (const engine::PendingNewSubscription& rSub : rNewSubs)
	{
		const engine::ClientConnection* pClient = engine::gpNetworkServer->FindClient(rSub.iClientId);
		bool bSlotStillValid = (pClient != nullptr
			&& rSub.iSlot < engine::NetworkManager::kiMaxCoordSlots
			&& pClient->coordSubscriptions[rSub.iSlot].bActive
			&& pClient->coordSubscriptions[rSub.iSlot].coord == rSub.coord);
		if (!bSlotStillValid)
		{
			continue;
		}

		auto frameIt = mCurrentFrames.find(rSub.coord);
		if (frameIt != mCurrentFrames.end())
		{
			engine::gpNetworkServer->SendCoordFullState(rSub.iClientId, rSub.iSlot, miFrameCounter, rSub.coord, frameIt->second.get());
		}
	}

	if (mPendingSubscriptionUpdates.empty())
	{
		return;
	}

	// Client handles subscriptions — server just sends player assignment
	for (const SubscriptionUpdate& rUpdate : mPendingSubscriptionUpdates)
	{
		engine::gpNetworkServer->SendAssignPlayer(rUpdate.iClientId, rUpdate.newPlayerId, rUpdate.newCoord);

		FILE_LOG(0, "[HandleSubscriptionUpdatesServer] client={} newId={} newCoord=({},{})", rUpdate.iClientId, rUpdate.newPlayerId.ToUuid().Value(), rUpdate.newCoord.x, rUpdate.newCoord.y);
	}

	mPendingSubscriptionUpdates.clear();
}

#endif // BT_SERVER

#ifdef BT_CLIENT

void Game::KickReconcile()
{
	mpReconcileContext = std::make_unique<ReconcileContext>();
	ReconcileContext& rReconcileContext = *mpReconcileContext;

	// Populate per-coord work items
	for (auto& [rCoord, rState] : mCoordReconcileStates)
	{
		if (rState.iConfirmedFrame < 0)
		{
			continue;
		}

		CoordReconcileWork work;
		work.coord = rCoord;
		work.uiGeneration = rState.uiGeneration;
		work.iConfirmedFrame = rState.iConfirmedFrame;
		work.confirmedSerializedFrame = rState.confirmedSerializedFrame;
		work.serverUpdates = std::move(rState.serverUpdates);
		work.extrapolatedSnapshots = std::move(rState.extrapolatedSnapshots);
		work.lastServerPlayerInputs = rState.lastServerPlayerInputs;
		work.pendingFullState = std::move(rState.pendingFullState);

		rState.serverUpdates.clear();
		rState.extrapolatedSnapshots.clear();
		rState.pendingFullState.reset();

		rReconcileContext.coordWork.push_back(std::move(work));
	}

	// Global input
	rReconcileContext.confirmedHumanState = mConfirmedHumanState;
	rReconcileContext.uiNextFrameId = muiNextFrameId;
	rReconcileContext.iTargetFrame = miFrameCounter;
	rReconcileContext.playerAlignment = mPlayerAlignment;

	FILE_LOG(0, "[KickReconcile] coords={} target={}", rReconcileContext.coordWork.size(), rReconcileContext.iTargetFrame);

	// Dispatch to worker
	mpReconcileWorker->Wake([this]()
	{
		Reconcile(*mpReconcileContext, mAlignments);
	});
}

void Game::ReconcileRunPhysics(ReconcileContext& rReconcileContext)
{
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

void Game::ReconcileBuildFrameInput(ReconcileContext& rReconcileContext, int64_t iServerFrame, const std::unordered_map<engine::GridCoord, CoordReconcileState::CoordServerUpdate>& rCoordUpdates)
{
	rReconcileContext.frameInputs.clear();
	rReconcileContext.serverTransferStatusChanges.clear();

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
	for (const auto& [rCoord, rUpdate] : rCoordUpdates)
	{
		auto frameInputIt = rReconcileContext.frameInputs.find(rCoord);
		if (frameInputIt == rReconcileContext.frameInputs.end())
		{
			continue;
		}

		FrameInput& rFrameInput = frameInputIt->second;

		for (const StatusChange& rChange : rUpdate.statusChanges)
		{
			if (IsTransferType(rChange.eType))
			{
				FILE_LOG(0, "[ReconcileBuildFrameInput] ServerTransfer: type={} at ({},{}) frame={}", static_cast<int>(rChange.eType), rCoord.x, rCoord.y, iServerFrame);
				rReconcileContext.serverTransferStatusChanges[rCoord].push_back(rChange);
			}
			else
			{
				rFrameInput.statusChanges.push_back(rChange);
			}
		}

		CopyPlayerInputs(rFrameInput, rUpdate.playerInputs);
	}
}

void Game::ReconcileHarvestTransfers(ReconcileContext& rReconcileContext)
{
	bool bHasServerTransfers = !rReconcileContext.serverTransferStatusChanges.empty();

	if (bHasServerTransfers)
	{
		ApplyServerTransfers(rReconcileContext.nextFrames, rReconcileContext.serverTransferStatusChanges, rReconcileContext.activeCoords, rReconcileContext.playerAlignment, rReconcileContext.humanPlayerId, rReconcileContext.humanGridCoord, rReconcileContext.fPreviousHumanArmor);
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

			FILE_LOG(0, "[ReconcileHarvestTransfers] Local coord=({},{}) transferCount={}", rCoord.x, rCoord.y, rNextFrame.postRender.transferRequests.size());
			for (const TransferRequest& rRequest : rNextFrame.postRender.transferRequests)
			{
				FILE_LOG(0, "[ReconcileHarvestTransfers] Local detail: type={} dest=({},{}) entityId={}", static_cast<int>(rRequest.eType), rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY, rRequest.iEntityId);
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

void Game::ReconcileInjectPendingFullState(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork)
{
	rReconcileContext.currentFrames[rWork.coord] = std::make_unique<Frame>();
	std::istringstream iss(rWork.pendingFullState->second);
	iss >> *rReconcileContext.currentFrames[rWork.coord];

	rWork.pendingFullState.reset();
}

void Game::ReconcilePruneInactiveFrames(ReconcileContext& rReconcileContext)
{
	ReconcileComputeActiveCoords(rReconcileContext);
	std::erase_if(rReconcileContext.currentFrames, [&rReconcileContext](const auto& rPair)
	{
		return !std::ranges::contains(rReconcileContext.activeCoords, rPair.first);
	});
}

void Game::Reconcile(ReconcileContext& rReconcileContext, [[maybe_unused]] const engine::Alignments& rAlignments)
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	FILE_LOG(0, "[Reconcile] Entry: coords={} target={}", rReconcileContext.coordWork.size(), rReconcileContext.iTargetFrame);

	// ------ 1. PER-COORD CRC FAST-PATH ------
	bool bAllHandled = true;
	int64_t iMinConfirmedFrame = INT64_MAX;

	int64_t iOldMinConfirmed = INT64_MAX;
	for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iOldMinConfirmed)
		{
			iOldMinConfirmed = rWork.iConfirmedFrame;
		}
	}

	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.serverUpdates.empty() && !rWork.pendingFullState.has_value())
		{
			if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iMinConfirmedFrame)
			{
				iMinConfirmedFrame = rWork.iConfirmedFrame;
			}
			continue;
		}

		if (rWork.pendingFullState.has_value())
		{
			bAllHandled = false;
			if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iMinConfirmedFrame)
			{
				iMinConfirmedFrame = rWork.iConfirmedFrame;
			}
			continue;
		}

		// Check CRC fast-path for this coord
		int64_t iExpected = rWork.iConfirmedFrame + 1;
		bool bMatch = true;
		int64_t iLastMatched = -1;
		auto it = rWork.serverUpdates.begin();

		while (it != rWork.serverUpdates.end() && it->first == iExpected)
		{
			auto snapIt = rWork.extrapolatedSnapshots.find(iExpected);
			if (snapIt == rWork.extrapolatedSnapshots.end() || snapIt->second.crc != it->second.serverCrc)
			{
				bMatch = false;
				break;
			}
			iLastMatched = iExpected;
			++iExpected;
			++it;
		}

		if (bMatch && iLastMatched >= 0)
		{
			rWork.bCrcFastPath = true;
			rWork.iNewConfirmedFrame = iLastMatched;
			rWork.newConfirmedSerializedFrame = std::move(rWork.extrapolatedSnapshots.at(iLastMatched).serializedFrame);

			auto lastIt = rWork.serverUpdates.find(iLastMatched);
			rWork.newLastServerPlayerInputs = lastIt->second.playerInputs;

			// Keep snapshots beyond matched frame
			for (auto snapIt = rWork.extrapolatedSnapshots.begin(); snapIt != rWork.extrapolatedSnapshots.end(); )
			{
				if (snapIt->first <= iLastMatched)
				{
					snapIt = rWork.extrapolatedSnapshots.erase(snapIt);
				}
				else
				{
					++snapIt;
				}
			}
			rWork.newExtrapolatedSnapshots = std::move(rWork.extrapolatedSnapshots);

			if (rWork.iNewConfirmedFrame < iMinConfirmedFrame)
			{
				iMinConfirmedFrame = rWork.iNewConfirmedFrame;
			}
		}
		else
		{
			bAllHandled = false;
			if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iMinConfirmedFrame)
			{
				iMinConfirmedFrame = rWork.iConfirmedFrame;
			}
		}
	}

	if (bAllHandled)
	{
		rReconcileContext.bCrcFastPathHandledAll = true;
		rReconcileContext.newConfirmedHumanState = rReconcileContext.confirmedHumanState;
		rReconcileContext.humanGridCoord = rReconcileContext.confirmedHumanState.humanGridCoord;
		rReconcileContext.humanPlayerId = rReconcileContext.confirmedHumanState.humanPlayerId;
		rReconcileContext.fPreviousHumanArmor = rReconcileContext.confirmedHumanState.fPreviousHumanArmor;
		if (iOldMinConfirmed != INT64_MAX && iMinConfirmedFrame > iOldMinConfirmed)
		{
			for (int64_t i = 0; i < iMinConfirmedFrame - iOldMinConfirmed; ++i)
			{
				rReconcileContext.newConfirmedHumanState.fCurrentTime += kfDeltaTime;
			}
		}
		FILE_LOG(0, "[Reconcile] All coords fast-pathed or no-change");
		return;
	}

	// Build coord-to-index map for O(1) lookups
	std::unordered_map<engine::GridCoord, size_t> coordWorkIndex;
	for (size_t i = 0; i < rReconcileContext.coordWork.size(); ++i)
	{
		coordWorkIndex[rReconcileContext.coordWork[i].coord] = i;
	}

	// ------ 2. UNIFIED ROLLBACK ------
	rReconcileContext.currentFrames.clear();
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		rReconcileContext.currentFrames[rWork.coord] = std::make_unique<Frame>();
		std::istringstream iss(rWork.confirmedSerializedFrame);
		iss >> *rReconcileContext.currentFrames[rWork.coord];
	}

	rReconcileContext.humanGridCoord = rReconcileContext.confirmedHumanState.humanGridCoord;
	rReconcileContext.humanPlayerId = rReconcileContext.confirmedHumanState.humanPlayerId;
	rReconcileContext.fPreviousHumanArmor = rReconcileContext.confirmedHumanState.fPreviousHumanArmor;
	rReconcileContext.iFrameCounter = iMinConfirmedFrame;
	rReconcileContext.fCurrentTime = rReconcileContext.confirmedHumanState.fCurrentTime;

	FILE_LOG(0, "[Reconcile] Restore: frame={} humanId={} grid=({},{})", rReconcileContext.iFrameCounter, rReconcileContext.humanPlayerId.ToUuid().Value(), rReconcileContext.humanGridCoord.x, rReconcileContext.humanGridCoord.y);

	// Inject pending full states at or before rollback frame
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.pendingFullState.has_value() && rWork.pendingFullState->first <= iMinConfirmedFrame)
		{
			ReconcileInjectPendingFullState(rReconcileContext, rWork);
		}
	}

	// Ensure next frames exist
	for (const auto& [rCoord, pFrame] : rReconcileContext.currentFrames)
	{
		if (!rReconcileContext.nextFrames.contains(rCoord))
		{
			rReconcileContext.nextFrames[rCoord] = std::make_unique<Frame>();
		}
	}

	// ------ 3. FIND REPLAY RANGE ------
	int64_t iReplayStart = iMinConfirmedFrame + 1;
	int64_t iMaxConsecutive = iReplayStart - 1;
	for (int64_t iFrame = iReplayStart; ; ++iFrame)
	{
		bool bAnyCoordHasData = false;
		for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.serverUpdates.contains(iFrame))
			{
				bAnyCoordHasData = true;
				break;
			}
		}
		if (!bAnyCoordHasData)
		{
			break;
		}
		iMaxConsecutive = iFrame;
	}

	// ------ 4. MAIN REPLAY ------
	const int64_t iMaxReplay = (iMaxConsecutive - iMinConfirmedFrame + 1) / 2;
	int64_t iReplayCount = 0;

	for (int64_t iFrame = iReplayStart; iFrame <= iMaxConsecutive; ++iFrame)
	{
		if (iReplayCount >= iMaxReplay || rReconcileContext.iFrameCounter >= rReconcileContext.iTargetFrame)
		{
			break;
		}

		ReconcileComputeActiveCoords(rReconcileContext);
		ReconcileEnsureNextFrames(rReconcileContext);

		++rReconcileContext.iFrameCounter;
		rReconcileContext.fCurrentTime += kfDeltaTime;

		// Gather per-coord server updates for this frame
		std::unordered_map<engine::GridCoord, CoordReconcileState::CoordServerUpdate> frameCoordUpdates;
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto updateIt = rWork.serverUpdates.find(iFrame);
			if (updateIt != rWork.serverUpdates.end())
			{
				frameCoordUpdates[rWork.coord] = std::move(updateIt->second);
			}
		}

		ReconcileBuildFrameInput(rReconcileContext, iFrame, frameCoordUpdates);

		// Update lastServerPlayerInputs for coords that received server data this frame
		for (const auto& [rCoord, rUpdate] : frameCoordUpdates)
		{
			auto indexIt = coordWorkIndex.find(rCoord);
			if (indexIt != coordWorkIndex.end())
			{
				auto inputIt = rReconcileContext.frameInputs.find(rCoord);
				if (inputIt != rReconcileContext.frameInputs.end())
				{
					rReconcileContext.coordWork[indexIt->second].lastServerPlayerInputs = inputIt->second.playerInputs;
				}
			}
		}

		// For coords without server data, use last server inputs (extrapolated)
		for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
		{
			if (frameCoordUpdates.contains(rCoord))
			{
				continue;
			}
			auto frameIt = rReconcileContext.currentFrames.find(rCoord);
			if (frameIt == rReconcileContext.currentFrames.end())
			{
				continue;
			}
			int64_t iPlayerCount = frameIt->second->interpolate.pPlayers->iCount;
			FrameInput& rFrameInput = rReconcileContext.frameInputs[rCoord];
			if (rFrameInput.playerInputs.empty())
			{
				rFrameInput.playerInputs.resize(iPlayerCount);
			}
			auto indexIt = coordWorkIndex.find(rCoord);
			if (indexIt != coordWorkIndex.end())
			{
				CopyPlayerInputs(rFrameInput, rReconcileContext.coordWork[indexIt->second].lastServerPlayerInputs);
			}
		}

		ReconcileRunPhysics(rReconcileContext);

		// Inject pending full states at the matching transfer frame
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.pendingFullState.has_value() && rWork.pendingFullState->first == iFrame)
			{
				ReconcileInjectPendingFullState(rReconcileContext, rWork);
				FILE_LOG(0, "[Reconcile] Injected coord=({},{}) frame={}", rWork.coord.x, rWork.coord.y, iFrame);
			}
		}

		// CRC validation per coord that had server data
		for (const auto& [rCoord, rUpdate] : frameCoordUpdates)
		{
			if (!rReconcileContext.currentFrames.contains(rCoord))
			{
				continue;
			}

			rReconcileContext.currentFrames.at(rCoord)->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
			common::crc_t clientCrc = rReconcileContext.currentFrames.at(rCoord)->ServerCrc();
			if (clientCrc != rUpdate.serverCrc)
			{
				common::Log("Reconcile desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.serverCrc, clientCrc, iFrame);
				FILE_LOG(0, "[Reconcile] Desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.serverCrc, clientCrc, iFrame);

				rReconcileContext.iDesyncFrame = iFrame;
				rReconcileContext.desyncCoord = rCoord;
				rReconcileContext.desyncServerCrc = rUpdate.serverCrc;
				rReconcileContext.desyncClientCrc = clientCrc;

				std::ostringstream oss(std::ios::binary);
				oss << *rReconcileContext.currentFrames.at(rCoord);
				std::istringstream iss(oss.str(), std::ios::binary);
				rReconcileContext.pDesyncClientFrame = std::make_unique<Frame>();
				iss >> *rReconcileContext.pDesyncClientFrame;
				return;
			}
		}

		++iReplayCount;
	}

	FILE_LOG(0, "[Reconcile] Replay: replayed={} frameCounter={} target={}", iReplayCount, rReconcileContext.iFrameCounter, rReconcileContext.iTargetFrame);

	// Prune stale coords before saving confirmed state
	ReconcilePruneInactiveFrames(rReconcileContext);

	// ------ 5. SAVE CONFIRMED STATE ------
	rReconcileContext.newConfirmedHumanState.humanGridCoord = rReconcileContext.humanGridCoord;
	rReconcileContext.newConfirmedHumanState.humanPlayerId = rReconcileContext.humanPlayerId;
	rReconcileContext.newConfirmedHumanState.fPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;
	rReconcileContext.newConfirmedHumanState.fCurrentTime = rReconcileContext.fCurrentTime;

	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.bCrcFastPath)
		{
			continue;
		}

		auto frameIt = rReconcileContext.currentFrames.find(rWork.coord);
		if (frameIt != rReconcileContext.currentFrames.end())
		{
			rWork.iNewConfirmedFrame = rReconcileContext.iFrameCounter;
			std::ostringstream oss;
			oss << *frameIt->second;
			rWork.newConfirmedSerializedFrame = oss.str();
		}

		auto frameInputIt = rReconcileContext.frameInputs.find(rWork.coord);
		if (frameInputIt != rReconcileContext.frameInputs.end())
		{
			rWork.newLastServerPlayerInputs = frameInputIt->second.playerInputs;
		}
	}

	// ------ 6. PREDICTIVE CATCH-UP ------
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

			auto indexIt = coordWorkIndex.find(rCoord);
			if (indexIt != coordWorkIndex.end())
			{
				CoordReconcileWork& rWork = rReconcileContext.coordWork[indexIt->second];
				const std::vector<PlayerInput>& rInputs = rWork.newLastServerPlayerInputs.empty() ? rWork.lastServerPlayerInputs : rWork.newLastServerPlayerInputs;
				CopyPlayerInputs(rFrameInput, rInputs);
			}
		}

		ReconcileRunPhysics(rReconcileContext);

		// Store per-coord catch-up snapshots for CRC fast-path
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto frameIt = rReconcileContext.currentFrames.find(rWork.coord);
			if (frameIt == rReconcileContext.currentFrames.end())
			{
				continue;
			}

			frameIt->second->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
			CoordExtrapolatedSnapshot& rSnapshot = rWork.newExtrapolatedSnapshots[rReconcileContext.iFrameCounter];
			rSnapshot.crc = frameIt->second->ServerCrc();
			std::ostringstream oss;
			oss << *frameIt->second;
			rSnapshot.serializedFrame = oss.str();
		}
	}

	// Final prune
	ReconcilePruneInactiveFrames(rReconcileContext);

	FILE_LOG(0, "[Reconcile] Exit: frameCounter={} target={}", rReconcileContext.iFrameCounter, rReconcileContext.iTargetFrame);
}

void Game::ApplyReconcileResult()
{
	// Heap: Frame deserialization, map operations
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	ReconcileContext& rReconcileContext = *mpReconcileContext;

	// Handle desync (network ops must happen on main thread)
	if (rReconcileContext.iDesyncFrame >= 0)
	{
		FILE_LOG(0, "[ApplyReconcileResult] Desync detected: frame={} coord=({},{}) sending debug frame request", rReconcileContext.iDesyncFrame, rReconcileContext.desyncCoord.x, rReconcileContext.desyncCoord.y);
		mpNetworkClient->SendDesyncReport(rReconcileContext.iDesyncFrame, rReconcileContext.desyncCoord, rReconcileContext.desyncServerCrc, rReconcileContext.desyncClientCrc);
		mpNetworkClient->SendDebugFrameRequest(rReconcileContext.iDesyncFrame, rReconcileContext.desyncCoord);
		mpNetworkClient->SetDesyncDebugMode(true);

		mDesyncDebugState.iFrame = rReconcileContext.iDesyncFrame;
		mDesyncDebugState.coord = rReconcileContext.desyncCoord;
		mDesyncDebugState.pClientFrame = std::move(rReconcileContext.pDesyncClientFrame);

		// Restore per-coord state (moved to context at kick time)
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto stateIt = mCoordReconcileStates.find(rWork.coord);
			if (stateIt == mCoordReconcileStates.end() || stateIt->second.uiGeneration != rWork.uiGeneration)
			{
				continue;
			}
			CoordReconcileState& rState = stateIt->second;

			for (auto& [iFrame, rUpdate] : rWork.serverUpdates)
			{
				if (iFrame > rState.iConfirmedFrame)
				{
					rState.serverUpdates[iFrame] = std::move(rUpdate);
				}
			}
			for (auto& [iFrame, rSnapshot] : rWork.extrapolatedSnapshots)
			{
				rState.extrapolatedSnapshots[iFrame] = std::move(rSnapshot);
			}
		}

		mpReconcileContext.reset();
		return;
	}

	// Write back per-coord results
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		auto stateIt = mCoordReconcileStates.find(rWork.coord);
		if (stateIt == mCoordReconcileStates.end() || stateIt->second.uiGeneration != rWork.uiGeneration)
		{
			continue;
		}
		CoordReconcileState& rState = stateIt->second;

		// Advance confirmed state
		rState.iConfirmedFrame = rWork.iNewConfirmedFrame;
		rState.confirmedSerializedFrame = std::move(rWork.newConfirmedSerializedFrame);
		rState.lastServerPlayerInputs = std::move(rWork.newLastServerPlayerInputs);

		// Prune stale extrapolated snapshots
		std::erase_if(rState.extrapolatedSnapshots, [&](const auto& rPair) { return rPair.first <= rState.iConfirmedFrame; });

		// Merge new extrapolated snapshots from catch-up
		for (auto& [iFrame, rSnapshot] : rWork.newExtrapolatedSnapshots)
		{
			if (iFrame > rState.iConfirmedFrame)
			{
				rState.extrapolatedSnapshots[iFrame] = std::move(rSnapshot);
			}
		}

		// Restore unconsumed server updates that were moved to context
		for (auto& [iFrame, rUpdate] : rWork.serverUpdates)
		{
			if (iFrame > rState.iConfirmedFrame)
			{
				rState.serverUpdates[iFrame] = std::move(rUpdate);
			}
		}
	}

	// Update human tracking from reconciled state
	mConfirmedHumanState = rReconcileContext.newConfirmedHumanState;
	mHumanGridCoord = rReconcileContext.humanGridCoord;
	mHumanPlayerId = rReconcileContext.humanPlayerId;
	mfPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;

	// Advance frame ID counter past worker's usage
	muiNextFrameId = std::max(muiNextFrameId, rReconcileContext.uiNextFrameId);

	if (rReconcileContext.bCrcFastPathHandledAll)
	{
		if (mCurrentFrames.contains(mHumanGridCoord))
		{
			FILE_LOG(0, "[ApplyReconcile] CRC fast-path, pPlayers={} pVecPos={} count={}", (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers.get(), (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->pVecPositions, mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->iCount);
		}
	}
	else
	{
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

	FILE_LOG(0, "[ApplyReconcileResult] path={}", rReconcileContext.bCrcFastPathHandledAll ? "crc" : "replay");

	mpReconcileContext.reset();

	if (mCurrentFrames.contains(mHumanGridCoord))
	{
		FILE_LOG(0, "[ApplyReconcile] post-reset pPlayers={} pVecPos={} count={}", (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers.get(), (void*)mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->pVecPositions, mCurrentFrames.at(mHumanGridCoord)->interpolate.pPlayers->iCount);
	}
}

#endif // BT_CLIENT

} // namespace game
