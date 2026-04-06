#include "Game.h"

#include "Profile/ProfileManager.h"
#include "Ui/Localization.h"

#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Ui/Screens/TweaksScreen/TweaksScreen.h"

namespace game
{

using enum UiState;

// Camera shake
static constexpr float kfCameraShakeAdd = 0.25f;
static constexpr float kfCameraShakeMax = 1.0f;

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
	if (!mGameSaveLoad.Autoload())
	{
		CreateNewFrame(GameFlags::kGame);
	}
	mpServerSession = std::make_unique<ServerSession>();
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
	engine::gpAudioManager->Set3dSettings(10.0f, 0.0f, 150.0f, 0.05f);
	engine::gpAudioManager->SetNextMusicTrackCallback([this]()
	{
		return GetNextMusicTrack();
	});
#endif // BT_CLIENT

}

engine::global_player_t Game::ClientPlayerId() const
{
	if (miFocusedPlayerIndex >= 0 && miFocusedPlayerIndex < std::ssize(mClientPlayerIds))
	{
		return mClientPlayerIds.at(miFocusedPlayerIndex);
	}
	return {};
}

bool Game::IsClientPlayer(engine::global_player_t id) const
{
	return id.IsValid() && std::ranges::contains(mClientPlayerIds, id);
}

void Game::AddClientPlayer(engine::global_player_t id, engine::GridCoord coord)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	Log(kVerbose, "AddClientPlayer GlobalPlayerId: {} Coord: ({},{}) OldPlayerCount: {}", id.iValue, coord.x, coord.y, std::ssize(mClientPlayerIds)); // DT TEMP
	mClientPlayerIds.push_back(id);
	mClientPlayerCoords.push_back(coord);
	miFocusedPlayerIndex = std::ssize(mClientPlayerIds) - 1;
	mClientGridCoord = coord;
}

void Game::RemoveClientPlayer(engine::global_player_t id)
{
	for (int64_t i = 0; i < std::ssize(mClientPlayerIds); ++i)
	{
		if (mClientPlayerIds.at(i) == id)
		{
			Log(kVerbose, "RemoveClientPlayer GlobalPlayerId: {} Index: {} OldPlayerCount: {}", id.iValue, i, std::ssize(mClientPlayerIds)); // DT TEMP
			mClientPlayerIds.erase(mClientPlayerIds.begin() + i);
			mClientPlayerCoords.erase(mClientPlayerCoords.begin() + i);

			// Adjust focused index: if removed before focus, shift down; if at/past end, clamp
			if (i < miFocusedPlayerIndex)
			{
				--miFocusedPlayerIndex;
			}
			else if (miFocusedPlayerIndex >= std::ssize(mClientPlayerIds))
			{
				miFocusedPlayerIndex = std::ssize(mClientPlayerIds) - 1;
			}

			// Update mClientGridCoord to match new focus (or leave stale if no players)
			if (miFocusedPlayerIndex >= 0)
			{
				mClientGridCoord = mClientPlayerCoords.at(miFocusedPlayerIndex);
			}
			return;
		}
	}
}

int64_t Game::PlayerCount() const
{
	return std::ssize(mClientPlayerIds);
}

int64_t Game::FocusedPlayerIndex() const
{
	return miFocusedPlayerIndex;
}

void Game::FocusNext()
{
	if (miFocusedPlayerIndex < std::ssize(mClientPlayerIds) - 1)
	{
		++miFocusedPlayerIndex;
		mClientGridCoord = mClientPlayerCoords.at(miFocusedPlayerIndex);
	}
}

void Game::FocusPrev()
{
	if (miFocusedPlayerIndex > 0)
	{
		--miFocusedPlayerIndex;
		mClientGridCoord = mClientPlayerCoords.at(miFocusedPlayerIndex);
	}
}

bool Game::CanFocusNext() const
{
	return miFocusedPlayerIndex < std::ssize(mClientPlayerIds) - 1;
}

bool Game::CanFocusPrev() const
{
	return miFocusedPlayerIndex > 0;
}

std::optional<int64_t> Game::ClientPlayerIndex(const PlayersPostRender& rPlayers) const
{
	engine::global_player_t focusedId = ClientPlayerId();
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

XMVECTOR Game::GetClientPlayerPosition() const
{
	const Frame& rFrame = CurrentFrame(mClientGridCoord);
	std::optional<int64_t> oIdx = ClientPlayerIndex(*rFrame.postRender.pPlayers);
	if (oIdx)
	{
		return rFrame.interpolate.pPlayers->pVecPositions[*oIdx];
	}
	XMVECTOR vecArea = mCoordFrames.at(mClientGridCoord).staticData.vecArea;
	return XMVectorSet((XMVectorGetX(vecArea) + XMVectorGetZ(vecArea)) * 0.5f, (XMVectorGetY(vecArea) + XMVectorGetW(vecArea)) * 0.5f, 0.0f, 0.0f);
}

void Game::ComputeActiveSet()
{
#if defined(BT_SERVER)
	gpServerSession->ComputeActiveSet();
#else
	// Heap: mActiveCoords vector clear/push_back may allocate. Persists as Game member across frame updates
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	if (!InMainMenu())
	{
		mActiveCoords.clear();
		for (const auto& [rCoord, rFrames] : mCoordFrames)
		{
			if (rFrames.pCurrent != nullptr && (rFrames.iConfirmedTick >= 0 || rCoord == mClientGridCoord))
			{
				mActiveCoords.push_back(rCoord);
			}
		}

		{
			auto it = mCoordFrames.find(mClientGridCoord);
			if (it == mCoordFrames.end() || it->second.pCurrent == nullptr)
			{
				CreateFrameAtCoord(mClientGridCoord);
			}
			if (!std::ranges::contains(mActiveCoords, mClientGridCoord))
			{
				mActiveCoords.push_back(mClientGridCoord);
			}
		}

		if constexpr (kbQuadrantNeighborSubscriptions)
		{
			const Frame& rFrame = RenderFrame(mClientGridCoord);
			std::optional<int64_t> oPlayerIdx = ClientPlayerIndex(*rFrame.postRender.pPlayers);
			if (oPlayerIdx)
			{
				XMVECTOR vecPos = rFrame.interpolate.pPlayers->pVecPositions[*oPlayerIdx];
				XMVECTOR vecArea = mCoordFrames.at(mClientGridCoord).staticData.vecArea;
				float fCenterX = (XMVectorGetX(vecArea) + XMVectorGetZ(vecArea)) * 0.5f;
				float fCenterY = (XMVectorGetY(vecArea) + XMVectorGetW(vecArea)) * 0.5f;
				float fHalfWidth = (XMVectorGetZ(vecArea) - XMVectorGetX(vecArea)) * 0.5f;
				float fHalfHeight = (XMVectorGetY(vecArea) - XMVectorGetW(vecArea)) * 0.5f;

				static constexpr float kfHysteresis = 2.0f;
				float fZoneX = fHalfWidth / 3.0f;
				float fZoneY = fHalfHeight / 3.0f;
				float fDeltaX = XMVectorGetX(vecPos) - fCenterX;
				float fDeltaY = XMVectorGetY(vecPos) - fCenterY;

				// X axis: 3-state transition (edge → center → edge)
				if (miQuadrantDirX == 0)
				{
					if (fDeltaX > fZoneX + kfHysteresis) miQuadrantDirX = 1;
					else if (fDeltaX < -(fZoneX + kfHysteresis)) miQuadrantDirX = -1;
				}
				else if (fDeltaX * static_cast<float>(miQuadrantDirX) < fZoneX - kfHysteresis)
				{
					miQuadrantDirX = 0;
				}

				// Y axis: 3-state transition (edge → center → edge)
				if (miQuadrantDirY == 0)
				{
					if (fDeltaY > fZoneY + kfHysteresis) miQuadrantDirY = 1;
					else if (fDeltaY < -(fZoneY + kfHysteresis)) miQuadrantDirY = -1;
				}
				else if (fDeltaY * static_cast<float>(miQuadrantDirY) < fZoneY - kfHysteresis)
				{
					miQuadrantDirY = 0;
				}
			}

			auto ensureNeighbor = [&](engine::GridCoord neighbor)
			{
				auto it = mCoordFrames.find(neighbor);
				if (it == mCoordFrames.end() || it->second.pCurrent == nullptr)
				{
					CreateFrameAtCoord(neighbor);
				}
				if (!std::ranges::contains(mActiveCoords, neighbor))
				{
					mActiveCoords.push_back(neighbor);
				}
			};

			if (miQuadrantDirX != 0)
				ensureNeighbor({.x = mClientGridCoord.x + miQuadrantDirX, .y = mClientGridCoord.y});
			if (miQuadrantDirY != 0)
				ensureNeighbor({.x = mClientGridCoord.x, .y = mClientGridCoord.y + miQuadrantDirY});
			if (miQuadrantDirX != 0 && miQuadrantDirY != 0)
				ensureNeighbor({.x = mClientGridCoord.x + miQuadrantDirX, .y = mClientGridCoord.y + miQuadrantDirY});
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
	}) <= 4);

	// Update island rendering only for subscribed frames (confirmed server data)
	std::vector<engine::GridCoord> subscribedCoords;
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

void Game::EnsureNextFrames()
{
	// Heap: unordered_map insertion + make_unique<Frame>. Frames persist in mNextFrames across game lifetime
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		if (mCoordFrames.try_emplace(rCoord).first->second.pNext == nullptr)
		{
			mCoordFrames.at(rCoord).pNext = std::make_unique<Frame>();
		}
	}
}

void Game::BuildFrameInputs()
{
#if defined(BT_SERVER)
	gpServerSession->BuildFrameInputs();
#else
	// Heap: unordered_map clear/insert for per-coordinate FrameInputs. Map persists as Game member
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mFrameInputs.clear();

	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		if (!mCoordFrames.contains(rCoord))
		{
			continue;
		}

		mFrameInputs.try_emplace(rCoord);
	}

	// Camera shake
	auto it = mCoordFrames.find(mClientGridCoord);
	if (ClientPlayerId().IsValid() && it != mCoordFrames.end() && it->second.pCurrent != nullptr)
	{
		const Frame& rCurrentFrame = CurrentFrame(mClientGridCoord);
		const PlayersPostRender& rPlayersPostRender = *rCurrentFrame.postRender.pPlayers;

		std::optional<int64_t> oIdx = ClientPlayerIndex(rPlayersPostRender);
		if (oIdx)
		{
			// Camera shake: detect armor damage on human player
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
	// Heap: unordered_map insertion + make_unique<Frame>. Frame persists in mCurrentFrames across game lifetime
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::unique_ptr<Frame>& pFrame = mCoordFrames.try_emplace(coord).first->second.pCurrent;
	pFrame = std::make_unique<Frame>();
	pFrame->interpolate.iTick = miTickCounter;
	pFrame->interpolate.fCurrentTime = mfCurrentTime;
	pFrame->interpolate.gameFlags.Set(GameFlags::kGame);
	InitFramePostRender(*pFrame);

	// Populate static data for this coord
	engine::FrameStaticData& rStaticData = mCoordFrames.at(coord).staticData;
	XMVECTOR vecBaseArea = XMVectorSet(Frame::kfBaseAreaMinX, Frame::kfBaseAreaMaxY, Frame::kfBaseAreaMaxX, Frame::kfBaseAreaMinY);
	rStaticData.vecArea = ComputeFrameArea(vecBaseArea, coord);
	bool bFlipX = (std::abs(coord.x) % 2) == 1;
	bool bFlipY = (std::abs(coord.y) % 2) == 1;
	rStaticData.eIslandsFlip = static_cast<engine::IslandsFlip>((bFlipX ? engine::kFlipX : 0) | (bFlipY ? engine::kFlipY : 0));
	rStaticData.f2IslandOffset = ComputeIslandOffset(coord);
	engine::BuildCellNavData(rStaticData.navData, engine::gpIslandTerrain->mNavContour, rStaticData.vecArea, rStaticData.eIslandsFlip, rStaticData.f2IslandOffset, Frame::kfIslandWidth, Frame::kfIslandHeight);

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
			});
			break;

		default:
			break;
	}
}

void Game::HarvestTransfers()
{
#if defined(BT_SERVER)
	gpServerSession->HarvestTransfers();
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
		ShowCursor(true);
	}

	gpGame = nullptr;
}

void Game::Reset()
{
	Log("Game::Reset()");

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
	engine::gSunAngleOverride.Reset(game::gpCamera->RawSunAngle());
	engine::gbSmokeClear = true;
	engine::gpParticleManager->mbReset = true;
	engine::WindTrailsInterpolate::ResetRenderState();
	mVecVisualErrorOffset = {};
	mWeaponModeToggle.Reset();
#endif // BT_CLIENT

	mGameFlags.Clear(engine::GameFlags::kDeathScreen);
	mGameFlags.Clear(engine::GameFlags::kPaused);
	mClientPlayerIds.clear();
	mClientPlayerCoords.clear();
	miFocusedPlayerIndex = -1;
	mfPreviousClientArmor = 0.0f;
	mClientGridCoord = engine::kOriginCoord;
	miQuadrantDirX = 0;
	miQuadrantDirY = 0;
	mActiveCoords.clear();
	mActiveCoords.push_back(mClientGridCoord);
}

void Game::CreateNewFrame(GameFlags_t gameFlags)
{
	// Heap: make_unique<Frame> with all its SOA collections. The frame must persist in mCurrentFrames
	// across the entire game state lifetime, so workbuffer (lost on Pop) can't hold it.
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mCoordFrames.clear();
	std::unique_ptr<Frame>& pFrame = mCoordFrames.try_emplace(engine::kOriginCoord).first->second.pCurrent;
	pFrame = std::make_unique<Frame>();
	pFrame->interpolate.gameFlags.Set(gameFlags.meFlags);
	InitFramePostRender(*pFrame);
	// Populate static data for origin coord (main menu: centered island)
	engine::FrameStaticData& rStaticData = mCoordFrames.at(engine::kOriginCoord).staticData;
	rStaticData.vecArea = XMVectorSet(Frame::kfBaseAreaMinX, Frame::kfBaseAreaMaxY, Frame::kfBaseAreaMaxX, Frame::kfBaseAreaMinY);
	rStaticData.eIslandsFlip = engine::kFlipNone;
	rStaticData.f2IslandOffset = ComputeIslandOffset(engine::kOriginCoord);
	engine::BuildCellNavData(rStaticData.navData, engine::gpIslandTerrain->mNavContour, rStaticData.vecArea, rStaticData.eIslandsFlip, rStaticData.f2IslandOffset, Frame::kfIslandWidth, Frame::kfIslandHeight);

	mCoordFrames.at(engine::kOriginCoord).pNext = std::make_unique<Frame>();
}

bool Game::ShouldTrapCursor()
{
	return !InMainMenu();
}

#if defined(BT_CLIENT)
bool Game::ShouldUseCrosshair()
{
	auto it = mCoordFrames.find(mClientGridCoord);
	if (it == mCoordFrames.end() || it->second.pCurrent == nullptr)
	{
		return false;
	}
	return CurrentFrame(mClientGridCoord).interpolate.gameFlags & GameFlags::kGame && meUiState == kNone;
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

	if constexpr (kbProfiling)
	{
		if (rMenuInput.flags & MenuInputFlags::kToggleProfileText)
		{
			gpProfileManager->ToggleProfileText();
		}
	}

	ProcessDebugInput(rMenuInput);

#if defined(BT_CLIENT)
	if (rMenuInput.flags & MenuInputFlags::kWeaponModeToggle)
	{
		if (gpClientSession != nullptr && !mWeaponModeToggle.IsPending() && ClientPlayerId().IsValid())
		{
			auto coordIt = mCoordFrames.find(mClientGridCoord);
			if (coordIt != mCoordFrames.end() && coordIt->second.pCurrent != nullptr)
			{
				std::optional<int64_t> oIdx = ClientPlayerIndex(*coordIt->second.pCurrent->postRender.pPlayers);
				if (oIdx)
				{
					const PlayersPostRender& rPlayers = *coordIt->second.pCurrent->postRender.pPlayers;
					bool bCurrentMissiles = static_cast<bool>(rPlayers.pFlags[*oIdx] & PlayerFlags::kUseMissiles);
					float fCurrentNavDelay = rPlayers.pfNavigationDelays[*oIdx];
					mWeaponModeToggle.SetPending();
					engine::gpClient->SendUpdatePlayerRequest(ClientPlayerId().iValue, !bCurrentMissiles, fCurrentNavDelay);
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
	static constexpr int64_t kiVersion = 1;

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

struct TweaksSettings
{
	static constexpr int64_t kiVersion = 2;

	bool bShowImGui = false;
	bool bSectionVisible[static_cast<size_t>(engine::TweakSection::kCount)] {};
	float fWindowPositionX[static_cast<size_t>(engine::TweakSection::kCount)] {};
	float fWindowPositionY[static_cast<size_t>(engine::TweakSection::kCount)] {};
	int8_t iActiveSubtab[static_cast<size_t>(engine::TweakSection::kCount)] {};
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
	engine::gpImGuiManager->mpTweaksScreen->SaveState(bSectionVisible, f2WindowPositions, iActiveSubtab);

	for (size_t i = 0; i < static_cast<size_t>(engine::TweakSection::kCount); ++i)
	{
		Log(kVerbose, "SaveTweaks [{}] visible:{} pos:({:.0f},{:.0f}) subtab:{}", i, bSectionVisible[i], f2WindowPositions[i].x, f2WindowPositions[i].y, iActiveSubtab[i]); // DT TEMP
	}

	TweaksSettings settings {};
	settings.bShowImGui = gpGame->mbShowImGui;
	for (size_t i = 0; i < static_cast<size_t>(engine::TweakSection::kCount); ++i)
	{
		settings.bSectionVisible[i] = bSectionVisible[i];
		settings.fWindowPositionX[i] = f2WindowPositions[i].x;
		settings.fWindowPositionY[i] = f2WindowPositions[i].y;
		settings.iActiveSubtab[i] = iActiveSubtab[i];
	}

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

		for (size_t i = 0; i < static_cast<size_t>(engine::TweakSection::kCount); ++i)
		{
			Log(kVerbose, "LoadTweaks [{}] visible:{} pos:({:.0f},{:.0f}) subtab:{}", i, settings.bSectionVisible[i], settings.fWindowPositionX[i], settings.fWindowPositionY[i], settings.iActiveSubtab[i]); // DT TEMP
		}

		ImVec2 f2WindowPositions[static_cast<size_t>(engine::TweakSection::kCount)] {};
		for (size_t i = 0; i < static_cast<size_t>(engine::TweakSection::kCount); ++i)
		{
			f2WindowPositions[i] = {settings.fWindowPositionX[i], settings.fWindowPositionY[i]};
		}

		engine::gpImGuiManager->mpTweaksScreen->LoadState(settings.bSectionVisible, f2WindowPositions, settings.iActiveSubtab);
	}
	else
	{
		Log(kWarning, "LoadTweaks FAILED to read file"); // DT TEMP
	}
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
				engine::gpClient->SendSaveRequest();
			}
			if (rMenuInput.flags & MenuInputFlags::kQuickload)
			{
				engine::gpClient->SendLoadRequest();
			}
			if (rMenuInput.flags & MenuInputFlags::kSaveReplay)
			{
				engine::gpClient->SendReplayRecordRequest();
			}
			if (rMenuInput.flags & MenuInputFlags::kLoadReplay)
			{
				engine::gpClient->SendReplayPlaybackRequest();
			}
			if (rMenuInput.flags & MenuInputFlags::kResetFrame)
			{
				engine::gpClient->SendResetRequest();
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
				engine::gpClient->SendTimespeedRequest(0);
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
				engine::gpClient->SendTimespeedRequest(1);
			}
#else
			mTimeStep.IncreaseTimeScale();
#endif
		}

		if (mTimeStep.mbTimeScaleChanged)
		{
			mTimeStep.mbTimeScaleChanged = false;
			Log(kLogNetwork, kWarning, "Timespeed changed Multiply: {} Divide: {}", mTimeStep.miTimeMultiply, mTimeStep.miTimeDivide);

			if (mTimeStep.miTimeMultiply == 1 && mTimeStep.miTimeDivide == 1)
			{
#if defined(BT_CLIENT)
				engine::gpTextManager->UpdateTextArea(engine::kTextDebug, "");
#endif
			}
			else
			{
				common::gpThreadLocal->mWorkbuffer.Push();
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
				common::gpThreadLocal->mWorkbuffer.Pop();
			}
		}

		if (rMenuInput.flags & MenuInputFlags::kTogglePauseFrame)
		{
			mGameFlags.Toggle(engine::GameFlags::kPaused);
#if defined(BT_CLIENT)
			if (engine::gpClient != nullptr)
			{
				engine::gpClient->SendPauseRequest(static_cast<bool>(mGameFlags & engine::GameFlags::kPaused));
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
			Log("Server paused: {}", static_cast<bool>(mGameFlags & engine::GameFlags::kPaused));
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
#endif
	}
}

void Game::RestoreReplayMeta(const ReplayMeta& rMeta)
{
	mClientGridCoord = rMeta.clientGridCoord;
	if (rMeta.iClientPlayerIdValue != 0)
	{
		engine::global_player_t globalId {rMeta.iClientPlayerIdValue};
		AddClientPlayer(globalId, rMeta.clientGridCoord);
	}
	mfPreviousClientArmor = rMeta.fPreviousClientArmor;
}

} // namespace game
