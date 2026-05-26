#include "Game.h"

#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Network/GamePacketType.h"
#include "Network/Server/ServerBroadcaster.h"
#include "Network/Server/ServerTransferManager.h"
#include "Profile/ProfileManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/Localization.h"
#include "Ui/MiscWrappersBase.h"
#include "Ui/SoundSettingsWrappersBase.h"
#include "Ui/SunMoonWrappersBase.h"
#include "Ui/Screens/TweaksScreen/TweaksScreen.h"

namespace game
{

using enum UiState;

// Camera shake
static constexpr float kfCameraShakeAdd = 0.25f;
static constexpr float kfCameraShakeMax = 1.0f;

// Debug-only main-menu island browser: build the origin cell as a single island centered at (0,0),
// selected by index into the boot-fixed sorted list of all packed islands. clear()+push_back reuses
// the vector's capacity (grown once under ScopedSuppressAllocationTracking in CreateNewFrame), so the
// cycle path never heap-allocates in the main loop.
static void BuildMenuIslandPlacement(int64_t iIndex, std::vector<engine::IslandPlacement>& rOut)
{
	rOut.clear();
	common::crc_t islandCrc = engine::gpIslandTerrain->mIslandCrcsSorted.at(static_cast<size_t>(iIndex));
	rOut.push_back({.islandCrc = islandCrc, .f2WorldPos = {0.0f, 0.0f}, .fRotation = 0.0f});
}

Game::Game()
{
	gpGame = this;

	InitializeLocalization();

	// Set up alignments
	uint32_t uiNextAlignment = 1;
	mPlayerAlignment = engine::alignment_t {uiNextAlignment++};
	mEnemyAlignment = engine::alignment_t {uiNextAlignment++};
	mAlignments.AddAlignment(mPlayerAlignment, mEnemyAlignment, engine::AlignmentFlags::kEnemies);

	// Allocate frames
#if defined(BT_SERVER)
	mpServerSession = std::make_unique<ServerSession>();
	if (!mGameSaveLoad.Autoload())
	{
		CreateNewFrame(GameFlags::kGame);
	}
	meUiState = kNone;
#else
	CreateNewFrame(GameFlags::kMainMenu);
	mGameFlags.Set(engine::GameFlags::kMainMenu);
#endif // BT_SERVER

#if defined(BT_CLIENT)
	mpClientSession = std::make_unique<ClientSession>();
#endif

	// Start music
#if defined(BT_CLIENT)
	StartMenuMusic();
	engine::gpAudioManager->SetNextMusicTrackCallback([this]()
	{
		return GetNextMusicTrack();
	});
#endif // BT_CLIENT

}

engine::global_id_t Game::ClientPlayerId() const
{
#if defined(BT_CLIENT)
	if (miFocusedFleetIndex >= 0 && miFocusedFleetIndex < std::ssize(mClientFleets))
	{
		const Fleet& rFleet = mClientFleets.at(static_cast<size_t>(miFocusedFleetIndex));
		if (miFocusedPlayerInFleetIndex >= 0 && miFocusedPlayerInFleetIndex < std::ssize(rFleet.members))
		{
			const FleetMember& rMember = rFleet.members.at(static_cast<size_t>(miFocusedPlayerInFleetIndex));
			if (rMember.bAlive)
			{
				return rMember.globalPlayerId;
			}
		}
	}
#endif
	return {};
}

bool Game::IsClientPlayer(engine::global_id_t id) const
{
	return id.IsValid() && std::ranges::contains(mClientPlayerIds, id);
}

void Game::AddClientPlayer(engine::global_id_t id, engine::GridCoord coord)
{
	// Heap: mClientPlayerIds / mClientPlayerCoords push_back may grow vectors
	ScopedSuppressAllocationTracking suppress;
	mClientPlayerIds.push_back(id);
	mClientPlayerCoords.push_back(coord);
}

void Game::RemoveClientPlayer(engine::global_id_t id)
{
	for (int64_t i = 0; i < std::ssize(mClientPlayerIds); ++i)
	{
		if (mClientPlayerIds.at(i) == id)
		{
			LOG(kNetwork, kVerbose, "RemoveClientPlayer GlobalPlayerId: {} Index: {} OldPlayerCount: {}", id, i, std::ssize(mClientPlayerIds));
			mClientPlayerIds.erase(mClientPlayerIds.begin() + i);
			mClientPlayerCoords.erase(mClientPlayerCoords.begin() + i);
			return;
		}
	}
}

int64_t Game::PlayerCount() const
{
	return std::ssize(mClientPlayerIds);
}

std::optional<int64_t> Game::ClientPlayerIndex(const PlayersPostRender& rPlayers) const
{
	engine::global_id_t focusedId = ClientPlayerId();
	if (focusedId.IsValid())
	{
		for (int64_t i = 0; i < rPlayers.iCount; ++i)
		{
			if (rPlayers.pGlobalPlayerIds[i] == focusedId)
			{
				return i;
			}
		}
	}

	return std::nullopt;
}

#if defined(BT_CLIENT)

void Game::AutoSelectFirstAliveMember()
{
	miFocusedPlayerInFleetIndex = -1;
	SetClientGridCoord({});
	const Fleet* pFleet = FocusedFleet();
	if (pFleet != nullptr)
	{
		for (int64_t i = 0; i < std::ssize(pFleet->members); ++i)
		{
			if (pFleet->members.at(static_cast<size_t>(i)).bAlive)
			{
				SelectPlayerInFleet(i);
				return;
			}
		}
	}
}

int64_t Game::FleetCount() const
{
	return std::ssize(mClientFleets);
}

int64_t Game::FocusedFleetIndex() const
{
	return miFocusedFleetIndex;
}

void Game::FocusNextFleet()
{
	if (miFocusedFleetIndex < std::ssize(mClientFleets) - 1)
	{
		++miFocusedFleetIndex;
		AutoSelectFirstAliveMember();
		CaptureClientStateAndSaveIfChanged();
	}
}

void Game::FocusPrevFleet()
{
	if (miFocusedFleetIndex > 0)
	{
		--miFocusedFleetIndex;
		AutoSelectFirstAliveMember();
		CaptureClientStateAndSaveIfChanged();
	}
}

bool Game::CanFocusNextFleet() const
{
	return miFocusedFleetIndex < std::ssize(mClientFleets) - 1;
}

bool Game::CanFocusPrevFleet() const
{
	return miFocusedFleetIndex > 0;
}

const Fleet* Game::FocusedFleet() const
{
	if (miFocusedFleetIndex >= 0 && miFocusedFleetIndex < std::ssize(mClientFleets))
	{
		return &mClientFleets.at(static_cast<size_t>(miFocusedFleetIndex));
	}
	return nullptr;
}

void Game::SelectPlayerInFleet(int64_t iPlayerIndex)
{
	const Fleet* pFleet = FocusedFleet();
	if (pFleet == nullptr || iPlayerIndex < 0 || iPlayerIndex >= std::ssize(pFleet->members))
	{
		return;
	}

	miFocusedPlayerInFleetIndex = iPlayerIndex;
	mWeaponModeToggle.Reset();

	// Update mClientGridCoord to match selected player's coord
	const FleetMember& rMember = pFleet->members.at(static_cast<size_t>(iPlayerIndex));
	if (rMember.bAlive)
	{
		for (int64_t i = 0; i < std::ssize(mClientPlayerIds); ++i)
		{
			if (mClientPlayerIds.at(i) == rMember.globalPlayerId)
			{
				SetClientGridCoord(mClientPlayerCoords.at(i));
				break;
			}
		}
	}

	CaptureClientStateAndSaveIfChanged();
}

int64_t Game::FocusedPlayerInFleetIndex() const
{
	return miFocusedPlayerInFleetIndex;
}

void Game::SyncFleets(std::vector<Fleet>&& fleets)
{
	// Heap: mClientFleets rebuild + LOG argument formatting allocations
	ScopedSuppressAllocationTracking suppress;

	LOG(kNetwork, kVerbose, "SyncFleets Fleets: {} Members: {} FocusedFleet: {} FocusedMember: {}", std::ssize(fleets), !fleets.empty() ? std::ssize(fleets.at(0).members) : 0, miFocusedFleetIndex, miFocusedPlayerInFleetIndex);

	int64_t iPrevFleetCount = std::ssize(mClientFleets);
	int64_t iPrevFocusedFleetMemberCount = 0;
	if (miFocusedFleetIndex >= 0 && miFocusedFleetIndex < iPrevFleetCount)
	{
		iPrevFocusedFleetMemberCount = std::ssize(mClientFleets.at(static_cast<size_t>(miFocusedFleetIndex)).members);
	}
	mClientFleets = std::move(fleets);

	// Restore from disk-persisted client state on the first sync after a reconnect-style clear.
	// The remembered FleetGuid identifies which fleet to focus; a missing or destroyed ship falls back to the fleet's current flagship.
	bool bRestoredRemembered = false;
	if (iPrevFleetCount == 0 && mRememberedFleetGuid.IsValid())
	{
		for (int64_t i = 0; i < std::ssize(mClientFleets); ++i)
		{
			const Fleet& rFleet = mClientFleets.at(static_cast<size_t>(i));
			if (rFleet.guid != mRememberedFleetGuid)
			{
				continue;
			}

			miFocusedFleetIndex = i;
			miFocusedPlayerInFleetIndex = -1;
			if (mRememberedFocusedShipId.IsValid())
			{
				for (int64_t j = 0; j < std::ssize(rFleet.members); ++j)
				{
					const FleetMember& rMember = rFleet.members.at(static_cast<size_t>(j));
					if (rMember.globalPlayerId == mRememberedFocusedShipId && rMember.bAlive)
					{
						miFocusedPlayerInFleetIndex = j;
						break;
					}
				}
			}
			if (miFocusedPlayerInFleetIndex < 0
				&& rFleet.iFlagshipIndex >= 0
				&& rFleet.iFlagshipIndex < std::ssize(rFleet.members))
			{
				miFocusedPlayerInFleetIndex = rFleet.iFlagshipIndex;
			}
			// Suppress the auto-newest-fleet / auto-newest-member branches below.
			iPrevFocusedFleetMemberCount = std::ssize(rFleet.members);
			bRestoredRemembered = true;
			break;
		}
	}

	if (!bRestoredRemembered)
	{
		// Clamp fleet index
		if (miFocusedFleetIndex >= std::ssize(mClientFleets))
		{
			miFocusedFleetIndex = std::ssize(mClientFleets) - 1;
		}

		// Auto-activate newly created fleet
		if (iPrevFleetCount < std::ssize(mClientFleets))
		{
			miFocusedFleetIndex = std::ssize(mClientFleets) - 1;
			miFocusedPlayerInFleetIndex = -1;
			iPrevFocusedFleetMemberCount = 0;
		}
	}

	// Clamp or auto-select member index
	const Fleet* pFleet = FocusedFleet();
	if (pFleet != nullptr)
	{
		if (miFocusedPlayerInFleetIndex >= std::ssize(pFleet->members))
		{
			miFocusedPlayerInFleetIndex = std::ssize(pFleet->members) - 1;
		}

		// Auto-focus newly added member (fleet member count grew)
		if (std::ssize(pFleet->members) > iPrevFocusedFleetMemberCount)
		{
			miFocusedPlayerInFleetIndex = std::ssize(pFleet->members) - 1;
		}
		else if (miFocusedPlayerInFleetIndex < 0 && !pFleet->members.empty())
		{
			miFocusedPlayerInFleetIndex = std::ssize(pFleet->members) - 1;
		}

		// If focused member is dead, auto-fallback to first alive member
		if (miFocusedPlayerInFleetIndex >= 0 && !pFleet->members.at(static_cast<size_t>(miFocusedPlayerInFleetIndex)).bAlive)
		{
			miFocusedPlayerInFleetIndex = -1;
			for (int64_t i = 0; i < std::ssize(pFleet->members); ++i)
			{
				if (pFleet->members.at(static_cast<size_t>(i)).bAlive)
				{
					miFocusedPlayerInFleetIndex = i;
					break;
				}
			}
		}
	}
	else
	{
		miFocusedPlayerInFleetIndex = -1;
	}

	// Update mClientGridCoord based on current selection
	engine::global_id_t focusedId = ClientPlayerId();
	bool bGridCoordResolved = false;
	if (focusedId.IsValid())
	{
		for (int64_t i = 0; i < std::ssize(mClientPlayerIds); ++i)
		{
			if (mClientPlayerIds.at(i) == focusedId)
			{
				SetClientGridCoord(mClientPlayerCoords.at(i));
				bGridCoordResolved = true;
				break;
			}
		}
	}

	// No valid selection — camera to origin
	if (!bGridCoordResolved)
	{
		SetClientGridCoord({});
	}

	// Persist whatever final focus state SyncFleets settled on (covers server-driven changes the user didn't trigger directly).
	CaptureClientStateAndSaveIfChanged();
}

#endif // BT_CLIENT

#if defined(BT_CLIENT)
XMVECTOR Game::GetClientPlayerPosition() const
{
	const engine::CoordFrames& rFrames = mCoordFrames.at(mClientGridCoord);
	if (rFrames.iSnapshotCount > 0)
	{
		int64_t iTailPhysical = engine::SnapshotIndex(rFrames.iSnapshotHead, rFrames.iSnapshotCount - 1);
		const std::unique_ptr<Frame>& pTail = rFrames.snapshots[iTailPhysical];
		if (pTail != nullptr)
		{
			std::optional<int64_t> oIdx = ClientPlayerIndex(*pTail->postRender.pPlayers);
			if (oIdx)
			{
				return pTail->interpolate.pPlayers->pVecPositions[*oIdx];
			}
		}
	}
	XMVECTOR vecArea = rFrames.staticData.vecArea;
	return XMVectorSet((XMVectorGetX(vecArea) + XMVectorGetZ(vecArea)) * 0.5f, (XMVectorGetY(vecArea) + XMVectorGetW(vecArea)) * 0.5f, 0.0f, 1.0f);
}
#endif // BT_CLIENT

void Game::ComputeActiveSet()
{
#if defined(BT_SERVER)
	gpServerSession->ComputeActiveSet();
#else
	// Heap: mActiveCoords vector clear/push_back may allocate. Persists as Game member across frame updates
	ScopedSuppressAllocationTracking suppress;

	if (!InMainMenu())
	{
		mActiveCoords.clear();
		for (const auto& [rCoord, rFrames] : mCoordFrames)
		{
			if (rFrames.iSnapshotCount > 0 && (rFrames.iConfirmedTick >= 0 || rCoord == mClientGridCoord))
			{
				mActiveCoords.push_back(rCoord);
			}
		}

		{
			auto it = mCoordFrames.find(mClientGridCoord);
			if (it == mCoordFrames.end() || it->second.iSnapshotCount == 0)
			{
				CreateFrameAtCoord(mClientGridCoord);
			}
			if (!std::ranges::contains(mActiveCoords, mClientGridCoord))
			{
				mActiveCoords.push_back(mClientGridCoord);
			}
		}

		miVisibleNeighborCount = 0;
		if (ClientPlayerId().IsValid())
		{
			// Camera-zoom-dependent VisibleArea: f4LargeVisibleArea packs (minX, maxY, maxX, minY).
			const XMFLOAT4& f4Visible = mCamera.f4LargeVisibleArea;
			XMVECTOR vecArea = mCoordFrames.at(mClientGridCoord).staticData.vecArea;
			float fCellMinX = XMVectorGetX(vecArea);
			float fCellMaxY = XMVectorGetY(vecArea);
			float fCellMaxX = XMVectorGetZ(vecArea);
			float fCellMinY = XMVectorGetW(vecArea);

			auto ensureNeighbor = [&](engine::GridCoord neighbor)
			{
				auto it = mCoordFrames.find(neighbor);
				if (it == mCoordFrames.end() || it->second.iSnapshotCount == 0)
				{
					CreateFrameAtCoord(neighbor);
				}
				if (!std::ranges::contains(mActiveCoords, neighbor))
				{
					mActiveCoords.push_back(neighbor);
				}
			};

			// Adjacent-only clamp: at wide zoom the VisibleArea may extend past the 3x3 ring;
			// only the immediate ring is ever subscribed regardless.
			for (int32_t i = -1; i <= 1; ++i)
			{
				for (int32_t j = -1; j <= 1; ++j)
				{
					if (i == 0 && j == 0)
					{
						continue;
					}
					float fOffsetX = static_cast<float>(i) * Frame::kfCellWidth;
					float fOffsetY = static_cast<float>(j) * Frame::kfCellHeight;
					float fNeighborMinX = fCellMinX + fOffsetX;
					float fNeighborMaxX = fCellMaxX + fOffsetX;
					float fNeighborMinY = fCellMinY + fOffsetY;
					float fNeighborMaxY = fCellMaxY + fOffsetY;
					if (f4Visible.x < fNeighborMaxX && f4Visible.z > fNeighborMinX
					 && f4Visible.w < fNeighborMaxY && f4Visible.y > fNeighborMinY)
					{
						engine::GridCoord neighbor {.x = mClientGridCoord.x + i, .y = mClientGridCoord.y + j};
						mVisibleNeighbors[miVisibleNeighborCount++] = neighbor;
						ensureNeighbor(neighbor);
					}
				}
			}
		}

	}
	else
	{
		mActiveCoords.clear();
		mActiveCoords.push_back(mClientGridCoord);
	}

	// Delete local-only frames outside the active set, preserve network-subscribed frames
	std::erase_if(mCoordFrames, [this](const std::pair<const engine::GridCoord, engine::CoordFrames>& rPair)
	{
		return !std::ranges::contains(mActiveCoords, rPair.first) && rPair.second.iConfirmedTick < 0;
	});

	ASSERT(std::ranges::count_if(mCoordFrames, [](const std::pair<const engine::GridCoord, engine::CoordFrames>& rPair)
	{
		return rPair.second.iConfirmedTick < 0;
	}) <= 9);

	// Update island rendering only for subscribed frames (confirmed server data)
	std::vector<engine::GridCoord> subscribedCoords;
	subscribedCoords.reserve(mActiveCoords.size());
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		auto it = mCoordFrames.find(rCoord);
		if (it != mCoordFrames.end() && (it->second.iConfirmedTick >= 0 || rCoord == mClientGridCoord))
		{
			subscribedCoords.push_back(rCoord);
		}
	}
	engine::gpIslands->UpdateActiveIslands(mCoordFrames, subscribedCoords);
#endif // BT_SERVER
}

#if defined(BT_SERVER)
void Game::EnsureNextFrames()
{
	// Heap: unordered_map insertion + make_unique<Frame>. Frames persist in mNextFrames across game lifetime
	ScopedSuppressAllocationTracking suppress;

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		if (mCoordFrames.try_emplace(rCoord).first->second.pNext == nullptr)
		{
			mCoordFrames.at(rCoord).pNext = std::make_unique<Frame>();
		}
	}
}
#endif // BT_SERVER

void Game::BuildFrameInputs()
{
#if defined(BT_SERVER)
	gpServerSession->mpBroadcaster->BuildFrameInputs();
#else
	// Heap: unordered_map clear/insert for per-coordinate FrameInputs. Map persists as Game member
	ScopedSuppressAllocationTracking suppress;

	mFrameInputs.clear();

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		if (!mCoordFrames.contains(rCoord))
		{
			continue;
		}

		mFrameInputs.try_emplace(rCoord);
	}

	// Camera shake — read most recent ring frame (head + count - 1)
	auto it = mCoordFrames.find(mClientGridCoord);
	const Frame* pTailFrame = nullptr;
	if (it != mCoordFrames.end() && it->second.iSnapshotCount > 0)
	{
		int64_t iTailPhysical = engine::SnapshotIndex(it->second.iSnapshotHead, it->second.iSnapshotCount - 1);
		pTailFrame = it->second.snapshots[iTailPhysical].get();
	}
	if (ClientPlayerId().IsValid() && pTailFrame != nullptr)
	{
		const Frame& rCurrentFrame = *pTailFrame;
		const PlayersPostRender& rPlayersPostRender = *rCurrentFrame.postRender.pPlayers;

		std::optional<int64_t> oIdx = ClientPlayerIndex(rPlayersPostRender);
		if (oIdx)
		{
			// Camera shake: detect armor damage on flagship player
			float fCurrentArmor = rPlayersPostRender.pfArmors[*oIdx];
			if (fCurrentArmor < mfPreviousClientArmor)
			{
				mCamera.mfShake = std::min(mCamera.mfShake + kfCameraShakeAdd, kfCameraShakeMax);
			}
			mfPreviousClientArmor = fCurrentArmor;
		}
	}
#endif // BT_SERVER
}

void Game::CreateFrameAtCoord(engine::GridCoord coord)
{
	// Heap: unordered_map insertion + make_unique<Frame>. Frame persists across game lifetime
	ScopedSuppressAllocationTracking suppress;

	engine::CoordFrames& rFrames = mCoordFrames.try_emplace(coord).first->second;
#if defined(BT_CLIENT)
	// Client uses snapshot ring as the source of truth — seed slot 0.
	rFrames.iSnapshotHead = 0;
	rFrames.iSnapshotCount = 1;
	rFrames.snapshots[0] = std::make_unique<Frame>();
	Frame& rFrame = *rFrames.snapshots[0];
#else
	rFrames.pCurrent = std::make_unique<Frame>();
	Frame& rFrame = *rFrames.pCurrent;
#endif
	rFrame.interpolate.iTick = miTickCounter;
	rFrame.interpolate.fCurrentTime = mfCurrentTime;
	rFrame.interpolate.gameFlags.Set(GameFlags::kGame);
	InitFramePostRender(rFrame);

	// Populate static data for this coord
	engine::FrameStaticData& rStaticData = rFrames.staticData;
	XMVECTOR vecBaseArea = XMVectorSet(Frame::kfBaseAreaMinX, Frame::kfBaseAreaMaxY, Frame::kfBaseAreaMaxX, Frame::kfBaseAreaMinY);
	rStaticData.vecArea = ComputeFrameArea(vecBaseArea, coord);
	rStaticData.coord = coord;
	engine::GenerateIslandPlacements(coord, rStaticData.islands);
	// navData stays empty; RunFrameTick builds it lazily on the per-coord dispatch thread.
}

void SpawnTransfer(Frame& rFrame, StatusChangeType eType, const TransferData& rData, engine::alignment_t playerAlignment)
{
	switch (eType)
	{
		case StatusChangeType::kTransferSpaceship:
			SpaceshipsPostRender::Spawn(rFrame, {
				.vecPosition = rData.vecPosition,
				.vecDirection = rData.vecDirection,
				.vecVelocity = rData.vecVelocity,
				.alignment = rData.alignment,
				.fHealth = rData.fHealth,
				.fNextBlasterSpawnTime = rData.fNextBlasterSpawnTime,
				.fArrivalGracePeriod = kfArrivalGracePeriod,
			});
			break;

		case StatusChangeType::kTransferBlaster:
			BlastersPostRender::Spawn(rFrame, {
				.vecPosition = rData.vecPosition,
				.vecVelocity = rData.vecVelocity,
				.uiTypeIndex = rData.uiTypeIndex,
				.alignment = rData.alignment,
				.fWindTrailIntensity = rData.fWindTrailIntensity,
				.fWindTrailWidth = rData.fWindTrailWidth,
				.fWindTrailLengthMultiplier = rData.fWindTrailLengthMultiplier,
			});
			break;

		case StatusChangeType::kTransferMissile:
		{
			MissileFlags_t missileFlags;
			if (rData.alignment == playerAlignment)
			{
				missileFlags.Set(MissileFlags::kTargetEnemy);
			}
			else
			{
				missileFlags.Set(MissileFlags::kTargetPlayer);
			}

			MissilesPostRender::Spawn(rFrame, {
				.vecPosition = rData.vecPosition,
				.vecDirection = rData.vecDirection,
				.vecVelocity = rData.vecVelocity,
				.vecStoredDirection = rData.vecDirection,
				.uiTarget = {},
				.fAcceleration = rData.fAcceleration,
				.flags = missileFlags,
				.alignment = rData.alignment,
				.fDeltaRotationDelay = rData.fDeltaRotationDelay,
				.fTime = rData.fTime,
				.fExhaustDelay = rData.fExhaustDelay,
				.fNextJitter = rData.fNextJitter,
#if defined(BT_CLIENT)
				.smokeTrailId = rData.smokeTrailId,
#endif
			});
			break;
		}

		case StatusChangeType::kTransferPlayer:
			PlayersPostRender::Spawn(rFrame, {
				.vecPosition = rData.vecPosition,
				.vecDirection = rData.vecDirection,
				.vecVelocity = rData.vecVelocity,
				.alignment = rData.alignment,
				.fArmor = rData.fHealth,
				.fShield = rData.fShield,
				.fNextBlasterFireTime = rData.fNextBlasterFireTime,
				.fNextSecondarySpawnTime = rData.fNextSecondarySpawnTime,
				.fShieldCooldown = rData.fShieldCooldown,
				.fShieldDownSoundCooldown = rData.fShieldDownSoundCooldown,
				.fAnimationTime = rData.fAnimationTime,
				.fShieldRotation = rData.fShieldRotation,
				.fShieldShrink = rData.fShieldShrink,
				.flags = PlayerFlags_t {static_cast<PlayerFlags>(rData.uiPlayerFlags)},
				.fTransferLockTimer = 1.0f,
				.fArrivalGracePeriod = kfArrivalGracePeriod,
				.fNavigationDelay = rData.fNavigationDelay,
				.globalPlayerId = rData.globalPlayerId,
				.fleetWantedCoord = rData.fleetWantedCoord,
				.uiPendingFleetWantedCoordTicks = rData.uiPendingFleetWantedCoordTicks,
				.uiPendingWeaponModeTicks = rData.uiPendingWeaponModeTicks,
			});
			break;

		default:
			break;
	}
}

void Game::HarvestTransfers()
{
#if defined(BT_SERVER)
	gpServerSession->mpTransferManager->HarvestTransfers();
#endif
}

void Game::ApplyTransferStatusChanges(Frame& rFrame, FrameInput& rFrameInput)
{
	// Heap: Spawns into frame may grow SOA buffers
	ScopedSuppressAllocationTracking suppress;

	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		if (IsTransferType(rStatusChange.eType))
		{
			SpawnTransfer(rFrame, rStatusChange.eType, std::get<TransferData>(rStatusChange.data), rFrame.postRender.playerAlignment);
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
#if defined(BT_CLIENT)
	mpClientSession.reset();
	engine::gpAudioManager->SetNextMusicTrackCallback(nullptr);
#endif // BT_CLIENT

#if defined(BT_SERVER)
	mpServerSession.reset();
#endif

	if (!(mMenuFlags & engine::MenuFlags::kMouseVisible))
	{
		ShowCursor(TRUE);
	}

	gpGame = nullptr;
}

void Game::Reset()
{
	LOG(kDefault, kDebug, "Game::Reset()");

	miTickCounter = 0;
	mfCurrentTime = 0.0f;

#if defined(BT_SERVER)
	mGameSaveLoad.ResetStreams();
#endif // BT_SERVER

#if defined(BT_CLIENT)
	game::gpCamera->ResetSunAngle();
	game::gpCamera->mVecLastKnownPlayerPosition = {};
	game::gpCamera->mVecLastKnownPlayerVelocity = {};
	game::gpCamera->mfLastKnownPlayerTime = 0.0f;
	game::gpCamera->mVecJumpStartPosition = {};
	game::gpCamera->mVecPreviousTargetPosition = {};
	game::gpCamera->mfJumpStartTime = 0.0f;
	game::gpCamera->mbJumping = false;
	engine::gbSmokeClear = true;
	engine::gpParticleManager->mbReset = true;
	engine::WindTrailsInterpolate::ResetRenderState();
	mVecVisualErrorOffset = {};
	mWeaponModeToggle.Reset();
#endif // BT_CLIENT

	mGameFlags.Clear(engine::GameFlags::kPaused);
	mClientPlayerIds.clear();
	mClientPlayerCoords.clear();
#if defined(BT_CLIENT)
	mClientFleets.clear();
	miFocusedFleetIndex = -1;
	miFocusedPlayerInFleetIndex = -1;
#endif
	mfPreviousClientArmor = 0.0f;
	SetClientGridCoord(engine::kOriginCoord);
	mActiveCoords.clear();
	mActiveCoords.push_back(mClientGridCoord);
}

void Game::CreateNewFrame(GameFlags_t gameFlags)
{
	// Heap: make_unique<Frame> with all its SOA collections. Frame persists across the entire
	// game state lifetime, so workbuffer (lost on Pop) can't hold it.
	ScopedSuppressAllocationTracking suppress;

	mCoordFrames.clear();
	engine::CoordFrames& rFrames = mCoordFrames.try_emplace(engine::kOriginCoord).first->second;
#if defined(BT_CLIENT)
	rFrames.iSnapshotHead = 0;
	rFrames.iSnapshotCount = 1;
	rFrames.snapshots[0] = std::make_unique<Frame>();
	Frame& rFrame = *rFrames.snapshots[0];
#else
	rFrames.pCurrent = std::make_unique<Frame>();
	Frame& rFrame = *rFrames.pCurrent;
#endif
	rFrame.interpolate.gameFlags.Set(gameFlags.meFlags);
	InitFramePostRender(rFrame);

	// Populate static data for origin coord (used as the main-menu cell)
	engine::FrameStaticData& rStaticData = rFrames.staticData;
	rStaticData.vecArea = XMVectorSet(Frame::kfBaseAreaMinX, Frame::kfBaseAreaMaxY, Frame::kfBaseAreaMaxX, Frame::kfBaseAreaMinY);
	rStaticData.coord = engine::kOriginCoord;
	// Debug builds turn the main-menu cell into a single centered island browser ('E' cycles it);
	// release builds keep the procedural 1-4 island scatter. Gameplay cells always use the scatter.
	bool bMenuBrowse = false;
	if constexpr (kbDebugInput)
	{
		bMenuBrowse = static_cast<bool>(gameFlags & GameFlags::kMainMenu);
	}
	if (bMenuBrowse)
	{
		BuildMenuIslandPlacement(miMenuIslandIndex, rStaticData.islands);
	}
	else
	{
		engine::GenerateIslandPlacements(engine::kOriginCoord, rStaticData.islands);
	}
	// navData stays empty; RunFrameTick builds it lazily on the per-coord dispatch thread.

#if defined(BT_SERVER)
	rFrames.pNext = std::make_unique<Frame>();
#endif
}

bool Game::ShouldTrapCursor()
{
	return !InMainMenu();
}

#if defined(BT_CLIENT)
bool Game::ShouldUseCrosshair()
{
	auto it = mCoordFrames.find(mClientGridCoord);
	if (it == mCoordFrames.end() || it->second.iSnapshotCount == 0)
	{
		return false;
	}
	int64_t iTailPhysical = engine::SnapshotIndex(it->second.iSnapshotHead, it->second.iSnapshotCount - 1);
	const std::unique_ptr<Frame>& pTail = it->second.snapshots[iTailPhysical];
	if (pTail == nullptr)
	{
		return false;
	}
	return pTail->interpolate.gameFlags & GameFlags::kGame && meUiState == kNone;
}

bool Game::ShouldShowInGameUi()
{
	return !mbShowImGui;
}
#endif // BT_CLIENT

void Game::ChangeFrame(GameFlags_t gameFlags)
{
#if defined(BT_CLIENT)
	gpClientSession->DisconnectFromServer();
#endif

	if ((gameFlags & GameFlags::kMainMenu && InMainMenu()) ||
	    (gameFlags & GameFlags::kGame && !InMainMenu()))
	{
		DEBUG_BREAK();
		return;
	}

	// Start appropriate music playlist for menu or game mode
#if defined(BT_CLIENT)
	if (gameFlags & GameFlags::kMainMenu)
	{
		StartMenuMusic();
	}
	else
	{
		StartGameMusic();
	}
#endif // BT_CLIENT

	mGameFlags.Set(engine::GameFlags::kMainMenu, gameFlags & GameFlags::kMainMenu);
	CreateNewFrame(gameFlags);
	Reset();
}

void Game::ProcessMenuInput(const MenuInput& rMenuInput)
{
	if (rMenuInput.flags & MenuInputFlags::kQuit || (rMenuInput.flags & MenuInputFlags::kPauseMenu && InMainMenu()))
	{
		mGameFlags.Set(engine::GameFlags::kQuit);
	}

	if (meUiState == UiState::kModal)
	{
		return;
	}

	if (rMenuInput.bGamepad && mMenuFlags & engine::MenuFlags::kMouseVisible)
	{
		ShowCursor(FALSE);
		mMenuFlags.Clear(engine::MenuFlags::kMouseVisible);
	}
	else if (!rMenuInput.bGamepad && !(mMenuFlags & engine::MenuFlags::kMouseVisible))
	{
		ShowCursor(TRUE);
		mMenuFlags.Set(engine::MenuFlags::kMouseVisible);
	}

	if (rMenuInput.flags & MenuInputFlags::kPauseMenu) [[unlikely]]
	{
		if (meUiState == kNone || meUiState == kGraphicsSettings || meUiState == kSound)
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

	if constexpr (kbProfiling)
	{
		if (rMenuInput.flags & MenuInputFlags::kToggleProfileText)
		{
			gpProfileManager->ToggleProfileText();
		}
	}

#if defined(BT_CLIENT)
	if constexpr (kbDebugRender)
	{
		if (rMenuInput.flags & MenuInputFlags::kToggleDebugRender)
		{
			engine::DebugRender::Toggle();
		}
	}
#endif

	ProcessDebugInput(rMenuInput);

#if defined(BT_CLIENT)
	if (rMenuInput.flags & MenuInputFlags::kWeaponModeToggle)
	{
		if (gpClientSession != nullptr && !mWeaponModeToggle.IsPending() && ClientPlayerId().IsValid())
		{
			auto coordIt = mCoordFrames.find(mClientGridCoord);
			if (coordIt != mCoordFrames.end() && coordIt->second.iSnapshotCount > 0)
			{
				int64_t iTailPhysical = engine::SnapshotIndex(coordIt->second.iSnapshotHead, coordIt->second.iSnapshotCount - 1);
				const std::unique_ptr<Frame>& pTail = coordIt->second.snapshots[iTailPhysical];
				if (pTail != nullptr)
				{
					std::optional<int64_t> oIdx = ClientPlayerIndex(*pTail->postRender.pPlayers);
					if (oIdx)
					{
						const PlayersPostRender& rPlayers = *pTail->postRender.pPlayers;
						bool bCurrentMissiles = static_cast<bool>(rPlayers.pFlags[*oIdx] & PlayerFlags::kUseMissiles);
						float fCurrentNavDelay = rPlayers.pfNavigationDelays[*oIdx];
						mWeaponModeToggle.SetPending();
						gpClientSession->SendUpdatePlayerRequest(ClientPlayerId().iValue, !bCurrentMissiles, fCurrentNavDelay);
					}
				}
			}
		}
	}

	if constexpr (kbScreenshots)
	{
		if (rMenuInput.flags & MenuInputFlags::kToggleScreenshots)
		{
			engine::gpCommandBufferManager->mbSaveScreenshot = !engine::gpCommandBufferManager->mbSaveScreenshot;
		}
	}
#endif // BT_CLIENT
}

#if defined(BT_CLIENT)
struct SoundSettings
{
	static constexpr int64_t kiVersion = 2;

	float fMasterVolume = 0.0f;
	float fMusicVolume = 0.0f;
	float fSoundVolume = 0.0f;
};
static constexpr char kpcSoundSettingsPath[] = "SoundSettings.bin";

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

	if constexpr (kbRecording)
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

struct GraphicsSettings
{
	static constexpr int64_t kiVersion = 6;

	bool bFullscreen = false;
	VkPresentModeKHR ePresentMode = VK_PRESENT_MODE_FIFO_KHR;
	bool bMultisampling = false;
	VkSampleCountFlagBits eSampleCount = VK_SAMPLE_COUNT_4_BIT;
	bool bAnisotropy = false;
	float fMaxAnisotropy = 0.0f;
	bool bSampleShading = false;
	float fMinSampleShading = 0.0f;
	float fMipLodBias = 0.0f;
	float fWaterShapeDetail = 0.0f;
	bool bSmoke = false;
	float fSmokeSimulationPixels = 0.0f;
	float fSmokeSimulationArea = 0.0f;
	float fMinimumAmbient = 0.0f;
	bool bWind = false;
	bool bOpaqueUi = false;
	float fUiOpacity = 0.9f;
	float fUiFontScale = 1.0f;
};
static constexpr char kpcGraphicsSettingsPath[] = "GraphicsSettings.bin";

void Game::SaveGraphicsSettings()
{
	// Heap: file I/O allocates
	ScopedSuppressAllocationTracking suppress;

	GraphicsSettings graphicsSettings
	{
		.bFullscreen = engine::gFullscreen.Get<bool>(),
		.ePresentMode = engine::gPresentMode.Get<VkPresentModeKHR>(),
		.bMultisampling = engine::gMultisampling.Get<bool>(),
		.eSampleCount = engine::gSampleCount.Get<VkSampleCountFlagBits>(),
		.bAnisotropy = engine::gAnisotropy.Get<bool>(),
		.fMaxAnisotropy = engine::gMaxAnisotropy.Get(),
		.bSampleShading = engine::gSampleShading.Get<bool>(),
		.fMinSampleShading = engine::gMinSampleShading.Get(),
		.fMipLodBias = engine::gMipLodBias.Get(),
		.fWaterShapeDetail = engine::gWaterShapeDetail.Get(),
		.bSmoke = engine::gSmokeEnabled.Get<bool>(),
		.fSmokeSimulationPixels = engine::gSmokeSimulationPixels.Get(),
		.fSmokeSimulationArea = engine::gSmokeSimulationArea.Get(),
		.fMinimumAmbient = engine::gSunMoonMinimumAmbient.Get(),
		.bWind = engine::gWindEnabled.Get<bool>(),
		.bOpaqueUi = engine::gOpaqueUi.Get<bool>(),
		.fUiOpacity = engine::gUiOpacity.Get(),
		.fUiFontScale = engine::gUiFontScale.Get(),
	};

	engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, kpcGraphicsSettingsPath, graphicsSettings);
}

bool Game::LoadGraphicsSettings()
{
	GraphicsSettings graphicsSettings {};

	if (engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, kpcGraphicsSettingsPath, graphicsSettings))
	{
		engine::gFullscreen.Set(graphicsSettings.bFullscreen);
		engine::gPresentMode.Set<VkPresentModeKHR>(graphicsSettings.ePresentMode);
		engine::gMultisampling.Set(graphicsSettings.bMultisampling);
		engine::gSampleCount.Set<VkSampleCountFlagBits>(graphicsSettings.eSampleCount);
		engine::gAnisotropy.Set(graphicsSettings.bAnisotropy);
		engine::gMaxAnisotropy.Set(graphicsSettings.fMaxAnisotropy);
		engine::gSampleShading.Set(graphicsSettings.bSampleShading);
		engine::gMinSampleShading.Set(graphicsSettings.fMinSampleShading);
		engine::gMipLodBias.Set(graphicsSettings.fMipLodBias);
		engine::gWaterShapeDetail.Set(graphicsSettings.fWaterShapeDetail);
		engine::gSmokeEnabled.Set(graphicsSettings.bSmoke);
		engine::gSmokeSimulationPixels.Set(graphicsSettings.fSmokeSimulationPixels);
		engine::gSmokeSimulationArea.Set(graphicsSettings.fSmokeSimulationArea);
		engine::gSunMoonMinimumAmbient.Set(graphicsSettings.fMinimumAmbient);
		engine::gWindEnabled.Set(graphicsSettings.bWind);
		engine::gOpaqueUi.Set(graphicsSettings.bOpaqueUi);
		engine::gUiOpacity.Set(graphicsSettings.fUiOpacity);
		engine::gUiFontScale.Set(graphicsSettings.fUiFontScale);
		return true;
	}

	return false;
}

void Game::ResetGraphicsSettings()
{
	engine::gFullscreen.ResetToDefault();
	engine::gPresentMode.ResetToDefault();
	engine::gMultisampling.ResetToDefault();
	engine::gSampleCount.ResetToDefault();
	engine::gAnisotropy.ResetToDefault();
	engine::gMaxAnisotropy.ResetToDefault();
	engine::gSampleShading.ResetToDefault();
	engine::gMinSampleShading.ResetToDefault();
	engine::gMipLodBias.ResetToDefault();
	engine::gWaterShapeDetail.ResetToDefault();
	engine::gSmokeEnabled.ResetToDefault();
	engine::gSmokeSimulationPixels.ResetToDefault();
	engine::gSmokeSimulationArea.ResetToDefault();
	engine::gSunMoonMinimumAmbient.ResetToDefault();
	engine::gWindEnabled.ResetToDefault();
	engine::gOpaqueUi.ResetToDefault();
	engine::gUiOpacity.ResetToDefault();
	engine::gUiFontScale.ResetToDefault();

	SaveGraphicsSettings();
}

struct TweaksSettings
{
	static constexpr int64_t kiVersion = 12;

	bool bShowImGui = false;
	bool bSectionVisible[static_cast<size_t>(engine::TweakSection::kCount)] {};
	float fWindowPositionX[static_cast<size_t>(engine::TweakSection::kCount)] {};
	float fWindowPositionY[static_cast<size_t>(engine::TweakSection::kCount)] {};
	int8_t iActiveSubtab[static_cast<size_t>(engine::TweakSection::kCount)] {};
	float fSunAngle = 1.15f;
	bool bSectionCollapsed[static_cast<size_t>(engine::TweakSection::kCount)] {};
};
static constexpr char kpcTweaksSettingsPath[] = "TweaksSettings.bin";

void Game::SaveTweaksSettings()
{
	if constexpr (!kbDebugInput)
	{
		return;
	}

	bool bSectionVisible[static_cast<size_t>(engine::TweakSection::kCount)] {};
	ImVec2 f2WindowPositions[static_cast<size_t>(engine::TweakSection::kCount)] {};
	int8_t iActiveSubtab[static_cast<size_t>(engine::TweakSection::kCount)] {};
	bool bSectionCollapsed[static_cast<size_t>(engine::TweakSection::kCount)] {};
	engine::gpImGuiManager->mpTweaksScreen->SaveState(bSectionVisible, f2WindowPositions, iActiveSubtab, bSectionCollapsed);

	TweaksSettings settings {};
	settings.bShowImGui = gpGame->mbShowImGui;
	for (size_t i = 0; i < static_cast<size_t>(engine::TweakSection::kCount); ++i)
	{
		settings.bSectionVisible[i] = bSectionVisible[i];
		settings.fWindowPositionX[i] = f2WindowPositions[i].x;
		settings.fWindowPositionY[i] = f2WindowPositions[i].y;
		settings.iActiveSubtab[i] = iActiveSubtab[i];
		settings.bSectionCollapsed[i] = bSectionCollapsed[i];
	}
	settings.fSunAngle = engine::gSunAngleOverride.Get();

	engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, kpcTweaksSettingsPath, settings);
}

void Game::LoadTweaksSettings()
{
	if constexpr (!kbDebugInput)
	{
		return;
	}

	TweaksSettings settings {};
	if (engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, kpcTweaksSettingsPath, settings))
	{
		gpGame->mbShowImGui = settings.bShowImGui;

		ImVec2 f2WindowPositions[static_cast<size_t>(engine::TweakSection::kCount)] {};
		for (size_t i = 0; i < static_cast<size_t>(engine::TweakSection::kCount); ++i)
		{
			f2WindowPositions[i] = {settings.fWindowPositionX[i], settings.fWindowPositionY[i]};
		}

		engine::gpImGuiManager->mpTweaksScreen->LoadState(settings.bSectionVisible, f2WindowPositions, settings.iActiveSubtab, settings.bSectionCollapsed);

		engine::gSunAngleOverride.Set(settings.fSunAngle);
	}
	else
	{
		LOG(kDefault, kWarning, "LoadTweaks FAILED to read file");
	}
}

struct ClientStateSettings
{
	static constexpr int64_t kiVersion = 3;

	game::FleetGuid fleetGuid {};
	int64_t iFocusedShipId = 0;
	float fCameraEyeHeightTarget = 198.0f; // matches Camera::kfCameraEyeHeightInitial
};
static constexpr char kpcClientStatePath[] = "ClientState.bin";

void Game::SaveClientState()
{
	// Heap: engine::WriteVersionedFile file I/O
	ScopedSuppressAllocationTracking suppress;

	ClientStateSettings settings
	{
		.fleetGuid              = gpGame->mRememberedFleetGuid,
		.iFocusedShipId         = gpGame->mRememberedFocusedShipId.iValue,
		.fCameraEyeHeightTarget = gpGame->mfRememberedCameraEyeHeightTarget,
	};
	engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, kpcClientStatePath, settings);
}

void Game::LoadClientState()
{
	// Heap: engine::ReadVersionedFile file I/O
	ScopedSuppressAllocationTracking suppress;

	ClientStateSettings settings {};
	if (!engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, kpcClientStatePath, settings))
	{
		return;
	}

	gpGame->mRememberedFleetGuid = settings.fleetGuid;
	gpGame->mRememberedFocusedShipId = engine::global_id_t {settings.iFocusedShipId};
	gpGame->mfRememberedCameraEyeHeightTarget = settings.fCameraEyeHeightTarget;

	// Apply zoom directly so the camera starts AT the saved zoom rather than easing from the default.
	gpCamera->mfCameraEyeHeight = settings.fCameraEyeHeightTarget;
	gpCamera->mfCameraEyeHeightTarget = settings.fCameraEyeHeightTarget;
}

void Game::CaptureClientStateAndSaveIfChanged()
{
	// When no fleet is focused (boot before first sync, or post-disconnect cleared fleets), preserve the remembered fleet/ship —
	// don't overwrite the just-loaded saved state with zeros. The next valid focus (user click or post-sync auto-activate) updates it.
	game::FleetGuid newFleetGuid = mRememberedFleetGuid;
	engine::global_id_t newShipId = mRememberedFocusedShipId;
	if (miFocusedFleetIndex >= 0 && miFocusedFleetIndex < std::ssize(mClientFleets))
	{
		const Fleet& rFleet = mClientFleets.at(static_cast<size_t>(miFocusedFleetIndex));
		newFleetGuid = rFleet.guid;
		newShipId = {};
		if (miFocusedPlayerInFleetIndex >= 0 && miFocusedPlayerInFleetIndex < std::ssize(rFleet.members))
		{
			newShipId = rFleet.members.at(static_cast<size_t>(miFocusedPlayerInFleetIndex)).globalPlayerId;
		}
	}

	const float fNewCameraEyeHeightTarget = gpCamera->mfCameraEyeHeightTarget;

	if (newFleetGuid == mRememberedFleetGuid
		&& newShipId == mRememberedFocusedShipId
		&& fNewCameraEyeHeightTarget == mfRememberedCameraEyeHeightTarget)
	{
		return;
	}

	mRememberedFleetGuid              = newFleetGuid;
	mRememberedFocusedShipId          = newShipId;
	mfRememberedCameraEyeHeightTarget = fNewCameraEyeHeightTarget;
	SaveClientState();
}
#endif // BT_CLIENT

#if defined(BT_CLIENT)
common::crc_t Game::GetNextMusicTrack()
{
	if (InMainMenu())
	{
		miMenuMusicIndex = (miMenuMusicIndex + 1) % static_cast<int64_t>(std::size(mMenuMusicPlaylist));
		return mMenuMusicPlaylist[miMenuMusicIndex];
	}
	else
	{
		miGameMusicIndex = (miGameMusicIndex + 1) % static_cast<int64_t>(std::size(mGameMusicPlaylist));
		return mGameMusicPlaylist[miGameMusicIndex];
	}
}
#endif // BT_CLIENT

void Game::InitFramePostRender(Frame& rFrame)
{
	rFrame.postRender.uiFrameId = GenerateFrameId();
	rFrame.postRender.randomEngine.TimeSeed();
	rFrame.postRender.playerAlignment = mPlayerAlignment;
	rFrame.postRender.enemyAlignment = mEnemyAlignment;
	rFrame.postRender.alignments = mAlignments;
}

void Game::ProcessDebugInput(const MenuInput& rMenuInput)
{
	if constexpr (kbDebugInput)
	{
#if defined(BT_CLIENT)
		if (engine::gpClient != nullptr)
		{
			if (rMenuInput.flags & MenuInputFlags::kQuicksave)
			{
				engine::gpClient->SendSimplePacket(GamePacketType::kClientSaveRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE);
			}
			if (rMenuInput.flags & MenuInputFlags::kQuickload)
			{
				engine::gpClient->SendSimplePacket(GamePacketType::kClientLoadRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE);
			}
			if (rMenuInput.flags & MenuInputFlags::kSaveReplay)
			{
				engine::gpClient->SendSimplePacket(GamePacketType::kClientReplayRecordRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE);
			}
			if (rMenuInput.flags & MenuInputFlags::kLoadReplay)
			{
				engine::gpClient->SendSimplePacket(GamePacketType::kClientReplayPlaybackRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE);
			}
			if (rMenuInput.flags & MenuInputFlags::kResetFrame)
			{
				engine::gpClient->SendSimplePacket(GamePacketType::kClientResetRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE);
			}
		}
#endif // defined(BT_CLIENT)

		if (rMenuInput.flags & MenuInputFlags::kMenuTweaks)
		{
			mbShowImGui = !mbShowImGui;
		}

		if (rMenuInput.flags & MenuInputFlags::kMenuDebugTexture)
		{
			engine::gDebugTexture.Toggle();
		}
#if defined(BT_CLIENT)
		if (rMenuInput.flags & MenuInputFlags::kDebugTextureNext)
		{
			float fNext = engine::gDebugTextureIndex.Get() + 1.0f;
			if (fNext >= static_cast<float>(engine::gpTextureManager->mRenderTargetTextures.miDebugTextureCount))
			{
				fNext = 0.0f;
			}
			engine::gDebugTextureIndex.Set(fNext);
		}
		if (rMenuInput.flags & MenuInputFlags::kDebugTexturePrev)
		{
			float fPrev = engine::gDebugTextureIndex.Get() - 1.0f;
			if (fPrev < 0.0f)
			{
				fPrev = static_cast<float>(engine::gpTextureManager->mRenderTargetTextures.miDebugTextureCount - 1);
			}
			engine::gDebugTextureIndex.Set(fPrev);
		}
#endif

		if (rMenuInput.flags & MenuInputFlags::kSlowTime)
		{
#if defined(BT_CLIENT)
			if (engine::gpClient != nullptr)
			{
				engine::gpClient->SendSimplePacket(engine::PacketType::kClientTimespeedRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, static_cast<uint8_t>(0));
			}
#else
			mTimeStep.DecreaseTimeScale();
#endif
		}
		else if (rMenuInput.flags & MenuInputFlags::kSpeedUpTime)
		{
#if defined(BT_CLIENT)
			if (engine::gpClient != nullptr)
			{
				engine::gpClient->SendSimplePacket(engine::PacketType::kClientTimespeedRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, static_cast<uint8_t>(1));
			}
#else
			mTimeStep.IncreaseTimeScale();
#endif
		}

		if (mTimeStep.mbTimeScaleChanged)
		{
			mTimeStep.mbTimeScaleChanged = false;
			LOG(kNetwork, kWarning, "Timespeed changed Multiply: {} Divide: {}", mTimeStep.miTimeMultiply, mTimeStep.miTimeDivide);

			if (mTimeStep.miTimeMultiply == 1 && mTimeStep.miTimeDivide == 1)
			{
#if defined(BT_CLIENT)
				engine::gpTextManager->UpdateTextArea(engine::kTextDebug, "");
#endif
			}
			else
			{
				common::ScopedWorkbufferArena scopedWorkbufferArena = common::gpThreadLocal->mWorkbuffer.Push();
				if (mTimeStep.miTimeMultiply > 1)
				{
					common::gpThreadLocal->mWorkbuffer.Append("Time ratio: ");
					common::gpThreadLocal->mWorkbuffer.Append(mTimeStep.miTimeMultiply);
					common::gpThreadLocal->mWorkbuffer.Append("x");
				}
				else
				{
					common::gpThreadLocal->mWorkbuffer.Append("Time ratio: 1/");
					common::gpThreadLocal->mWorkbuffer.Append(mTimeStep.miTimeDivide);
					common::gpThreadLocal->mWorkbuffer.Append("x");
				}
#if defined(BT_CLIENT)
				engine::gpTextManager->UpdateTextArea(engine::kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
#endif
			}
		}

		if (rMenuInput.flags & MenuInputFlags::kTogglePauseFrame)
		{
			mGameFlags.Toggle(engine::GameFlags::kPaused);
#if defined(BT_CLIENT)
			if (engine::gpClient != nullptr)
			{
				engine::gpClient->SendSimplePacket(GamePacketType::kClientPauseRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, static_cast<uint8_t>((mGameFlags & engine::GameFlags::kPaused) ? 1 : 0));
			}
			if (mGameFlags & engine::GameFlags::kPaused)
			{
				engine::gpTextManager->UpdateTextArea(engine::kTextDebug, "PAUSED");
			}
			else
			{
				engine::gpTextManager->UpdateTextArea(engine::kTextDebug, "");
			}
#else
			LOG(kDefault, kDebug, "Server paused: {}", static_cast<bool>(mGameFlags & engine::GameFlags::kPaused));
#endif
		}

#if defined(BT_CLIENT)
		if (rMenuInput.flags & MenuInputFlags::kConnectLocal && InMainMenu())
		{
			if (gpClientSession->mbServerDiscovered)
			{
				gpClientSession->ConnectToDiscoveredServer();
			}
			else
			{
				gpClientSession->ConnectToServer("127.0.0.1");
			}
		}

		if (rMenuInput.flags & MenuInputFlags::kCycleMenuIsland && InMainMenu())
		{
			miMenuIslandIndex = (miMenuIslandIndex + 1) % std::ssize(engine::gpIslandTerrain->mIslandCrcsSorted);
			auto it = mCoordFrames.find(engine::kOriginCoord);
			BuildMenuIslandPlacement(miMenuIslandIndex, it->second.staticData.islands);

			// Pre-mint the texture slot now (mirrors ClientDataReceiver::ApplyReceivedStaticData) so the
			// elevation upload and chunk loads are in-flight before UpdateActiveIslands references the
			// slot this same frame. AcquireTextureSlot is idempotent (hot-path early return).
			for (const engine::IslandPlacement& rPlacement : it->second.staticData.islands)
			{
				engine::gpIslandTerrain->AcquireTextureSlot(rPlacement.islandCrc);
			}
		}
#endif
	}
}

void Game::RestoreReplayMeta(const ReplayMeta& rMeta)
{
	SetClientGridCoord(rMeta.clientGridCoord);
	if (rMeta.iClientPlayerIdValue != 0)
	{
		engine::global_id_t globalId {rMeta.iClientPlayerIdValue};
		AddClientPlayer(globalId, rMeta.clientGridCoord);
	}
	mfPreviousClientArmor = rMeta.fPreviousClientArmor;
}

} // namespace game
