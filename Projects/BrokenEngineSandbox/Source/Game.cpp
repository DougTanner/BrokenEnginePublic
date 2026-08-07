#include "Game.h"

#include "Frame/Collections/Players/Players.h"
#include "Network/GamePacketType.h"
#include "Network/Server/ServerBroadcaster.h"
#include "Network/Server/ServerTransferManager.h"
#include "Profile/ProfileManager.h"
#include "SpawnTransfer.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/Localization.h"

namespace game
{

using enum UiState;

// Camera shake
static constexpr float kfCameraShakeAdd = 0.25f;
static constexpr float kfCameraShakeMax = 1.0f;

// Debug-only main-menu island browser: build the origin cell as a single island centered at (0,0),
// selected by index into the boot-fixed area-sorted list of all packed islands (largest footprint
// first). clear()+push_back reuses the vector's capacity (grown once under
// ScopedSuppressAllocationTracking in CreateNewFrame), so the cycle path never heap-allocates in the
// main loop.
static void BuildMenuIslandPlacement(int64_t iIndex, std::vector<engine::IslandPlacement>& rOut)
{
	rOut.clear();
	common::crc_t islandCrc = engine::gpIslandTerrain->mIslandCrcsByArea.at(static_cast<size_t>(iIndex));
	rOut.push_back({.islandCrc = islandCrc, .f2WorldPos = {0.0f, 0.0f}, .fRotation = 0.0f});
}

Game::Game()
#if defined(BT_SERVER)
	: mGameSaveLoad(*this)
#endif // BT_SERVER
{
	ASSERT(gpGame == nullptr);

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
	const Fleet* pFleet = mFleetSelection.FocusedFleet();
	if (pFleet != nullptr)
	{
		int64_t iMember = mFleetSelection.FocusedPlayerInFleetIndex();
		if (iMember >= 0 && iMember < std::ssize(pFleet->members))
		{
			const FleetMember& rMember = pFleet->members.at(static_cast<size_t>(iMember));
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
			const XMFLOAT4& f4Visible = gpCamera->f4LargeVisibleArea;
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

#endif // BT_SERVER
}

#if defined(BT_CLIENT)
void Game::UpdateActiveIslands()
{
	// Update island rendering only for subscribed frames (confirmed server data). Build the filtered
	// coord list in the workbuffer (per-frame, no heap) and pass it as a span; UpdateActiveIslands' own
	// nested PushBuffer is LIFO and pops before this arena does.
	common::ScopedWorkbufferArena subscribedArena = common::gpThreadLocal->mWorkbuffer.Push();
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		auto it = mCoordFrames.find(rCoord);
		if (it != mCoordFrames.end() && (it->second.iConfirmedTick >= 0 || rCoord == mClientGridCoord))
		{
			subscribedArena.PushBack<engine::GridCoord>(rCoord);
		}
	}
	engine::gpIslands->UpdateActiveIslands(mCoordFrames, subscribedArena.Span<const engine::GridCoord>());
}
#endif // BT_CLIENT

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
				gpCamera->mfShake = std::min(gpCamera->mfShake + kfCameraShakeAdd, kfCameraShakeMax);
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
	engine::GenerateIslandChain(coord, rStaticData.islands);
	// navData stays empty; RunFrameTick builds it lazily on the per-coord dispatch thread.
}

void Game::HarvestTransfers()
{
#if defined(BT_SERVER)
	gpServerSession->mpTransferManager->HarvestTransfers();
#endif
}

void Game::CaptureHarvestedTransfers([[maybe_unused]] engine::GridCoord coord, [[maybe_unused]] std::span<const StatusChange> transfers, [[maybe_unused]] const Frame& rPreTransferFrame)
{
#if defined(BT_SERVER)
	mGameSaveLoad.CaptureHarvestedTransfers(coord, transfers, rPreTransferFrame);
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

	if (gpGame == this)
	{
		gpGame = nullptr;
	}
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
	// Reinitialize both references directly to the new session's live zoom on the next camera update; do not carry
	// contraction across sessions.
	game::gpCamera->mfShadowTexelEyeHeight = 0.0f;
	game::gpCamera->mfLightingTexelEyeHeight = 0.0f;
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
	mFleetSelection.Clear();
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
	// release builds keep the procedural island chain. Gameplay cells always use the chain.
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
		engine::GenerateIslandChain(engine::kOriginCoord, rStaticData.islands);
	}
	// navData stays empty; RunFrameTick builds it lazily on the per-coord dispatch thread.

#if defined(BT_SERVER)
	rFrames.pNext = std::make_unique<Frame>();
#endif
}

#if defined(BT_CLIENT)
bool Game::ShouldTrapCursor()
{
	return !InMainMenu();
}
#endif // BT_CLIENT

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
	gpClientSession->mpRuntime->Disconnect();
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

#if defined(BT_CLIENT)
void Game::ProcessMenuInput(const MenuInput& rMenuInput)
{
	// Escape quits only from the basic main menu; with a settings sub-menu open it backs out to the menu below instead
	if (rMenuInput.flags & MenuInputFlags::kQuit || (rMenuInput.flags & MenuInputFlags::kPauseMenu && InMainMenu() && meUiState == UiState::kPause))
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
		if (meUiState == kNone || meUiState == kGameSettings || meUiState == kGraphicsSettings || meUiState == kSound)
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
	if constexpr (kbScreenshots)
	{
		// F9 toggles continuous dev capture: while on, queue a default one-shot request each frame (preserves the
		// prior continuous-capture behavior atop the new request mailbox). Empty request path -> default %TEMP% path.
		static bool sbContinuousScreenshots = false;
		if (rMenuInput.flags & MenuInputFlags::kToggleScreenshots)
		{
			sbContinuousScreenshots = !sbContinuousScreenshots;
		}
		if (sbContinuousScreenshots && !engine::gpGraphics->mScreenshotRequest)
		{
			// Fill only when empty so the per-frame F9 default request can't overwrite an agent-issued request's
			// parameters (path / maxWidth / bPublishResult) before the capture site consumes it.
			engine::gpGraphics->mScreenshotRequest = engine::ScreenshotRequest {};
		}
	}
#endif // BT_CLIENT
}
#endif // BT_CLIENT

#if defined(BT_CLIENT)
void Game::CaptureClientStateAndSaveIfChanged()
{
	// When no fleet is focused (boot before first sync, or post-disconnect cleared fleets), preserve the remembered fleet/ship —
	// don't overwrite the just-loaded saved state with zeros. The next valid focus (user click or post-sync auto-activate) updates it.
	game::FleetGuid newFleetGuid = mRememberedFleetGuid;
	engine::global_id_t newShipId = mRememberedFocusedShipId;
	const Fleet* pFleet = mFleetSelection.FocusedFleet();
	if (pFleet != nullptr)
	{
		newFleetGuid = pFleet->guid;
		newShipId = {};
		int64_t iMember = mFleetSelection.FocusedPlayerInFleetIndex();
		if (iMember >= 0 && iMember < std::ssize(pFleet->members))
		{
			newShipId = pFleet->members.at(static_cast<size_t>(iMember)).globalPlayerId;
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

#if defined(BT_CLIENT)
void Game::ProcessDebugInput(const MenuInput& rMenuInput)
{
	if constexpr (kbDebugInput)
	{
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

		if (rMenuInput.flags & MenuInputFlags::kMenuTweaks)
		{
			mbShowImGui = !mbShowImGui;
		}

		if (rMenuInput.flags & MenuInputFlags::kMenuDebugTexture)
		{
			engine::gDebugTexture.Toggle();
		}
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

		if (rMenuInput.flags & MenuInputFlags::kSlowTime)
		{
			if (engine::gpClient != nullptr)
			{
				engine::gpClient->SendSimplePacket(GamePacketType::kClientTimespeedRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, static_cast<uint8_t>(0));
			}
		}
		else if (rMenuInput.flags & MenuInputFlags::kSpeedUpTime)
		{
			if (engine::gpClient != nullptr)
			{
				engine::gpClient->SendSimplePacket(GamePacketType::kClientTimespeedRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, static_cast<uint8_t>(1));
			}
		}

		if (mTimeStep.mbTimeScaleChanged)
		{
			mTimeStep.mbTimeScaleChanged = false;
			LOG(kNetwork, kWarning, "Timespeed changed Multiply: {} Divide: {}", mTimeStep.miTimeMultiply, mTimeStep.miTimeDivide);

			if (mTimeStep.miTimeMultiply == 1 && mTimeStep.miTimeDivide == 1)
			{
				engine::gpImGuiManager->UpdateTextArea(engine::kTextDebug, "");
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
				engine::gpImGuiManager->UpdateTextArea(engine::kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
			}
		}

		if (rMenuInput.flags & MenuInputFlags::kTogglePauseFrame)
		{
			mGameFlags.Toggle(engine::GameFlags::kPaused);
			if (engine::gpClient != nullptr)
			{
				engine::gpClient->SendSimplePacket(GamePacketType::kClientPauseRequest, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, static_cast<uint8_t>((mGameFlags & engine::GameFlags::kPaused) ? 1 : 0));
			}
			if (mGameFlags & engine::GameFlags::kPaused)
			{
				engine::gpImGuiManager->UpdateTextArea(engine::kTextDebug, "PAUSED");
			}
			else
			{
				engine::gpImGuiManager->UpdateTextArea(engine::kTextDebug, "");
			}
		}

		if (rMenuInput.flags & MenuInputFlags::kConnectLocal && InMainMenu())
		{
			if (gpClientSession->mpRuntime->mStateFlags & engine::ClientSessionStateFlags::kServerDiscovered)
			{
				gpClientSession->mpRuntime->ConnectToDiscoveredServer(engine::kuiDefaultPort, NetworkSessionContract::kiCoordSlots);
			}
			else
			{
				gpClientSession->ConnectToServer("127.0.0.1");
			}
		}

		if (rMenuInput.flags & MenuInputFlags::kCycleMenuIsland && InMainMenu())
		{
			miMenuIslandIndex = (miMenuIslandIndex + 1) % std::ssize(engine::gpIslandTerrain->mIslandCrcsByArea);
			auto it = mCoordFrames.find(engine::kOriginCoord);
			BuildMenuIslandPlacement(miMenuIslandIndex, it->second.staticData.islands);
			// Cycling rewrites the placement list on an existing cell — drop the derived elevation grid
			// and the render-path query cache so RunFrameTick rebuilds both from the new placements
			// next tick.
			it->second.staticData.elevationGrid = {};
			it->second.staticData.islandRenderQueries = {};

			// Pre-mint the texture slot now (mirrors ClientSession::ApplyReceivedStaticData) so the
			// elevation upload and chunk loads are in-flight before UpdateActiveIslands references the
			// slot this same frame. AcquireTextureSlot is idempotent (hot-path early return).
			for (const engine::IslandPlacement& rPlacement : it->second.staticData.islands)
			{
				engine::gpIslandTerrain->AcquireTextureSlot(rPlacement.islandCrc);
			}
		}
	}
}
#endif // BT_CLIENT

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
