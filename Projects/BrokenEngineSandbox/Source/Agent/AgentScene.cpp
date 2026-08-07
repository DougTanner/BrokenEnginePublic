#include "Agent/AgentScene.h"

#if defined(BT_CLIENT)

#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Frame/Collections/Targets/Targets.h"
#include "Game.h"

namespace game
{

namespace
{

// Local JSON helpers (deliberately not shared with the server TU's CoordFromParam/Vec3ToJson — KISS, no new header).
nlohmann::json CoordToJson(engine::GridCoord coord)
{
	return nlohmann::json::array({coord.x, coord.y});
}

nlohmann::json Vec3ToJson(XMVECTOR vec)
{
	return nlohmann::json::array({XMVectorGetX(vec), XMVectorGetY(vec), XMVectorGetZ(vec)});
}

nlohmann::json Vec2ToJson(float fX, float fY)
{
	return nlohmann::json::array({fX, fY});
}

// Boolean PlayerFlags only — the packed nav-direction (bits 8-10) and nav-waypoint (bits 12-13) ranges are
// excluded; iterating them as single bits would emit meaningless names.
nlohmann::json PlayerFlagNames(PlayerFlags_t flags)
{
	nlohmann::json names = nlohmann::json::array();
	if (flags & PlayerFlags::kExploding)
	{
		names.push_back("kExploding");
	}
	if (flags & PlayerFlags::kFireBlaster)
	{
		names.push_back("kFireBlaster");
	}
	if (flags & PlayerFlags::kBlasterSpawnLeft)
	{
		names.push_back("kBlasterSpawnLeft");
	}
	if (flags & PlayerFlags::kFireMissile)
	{
		names.push_back("kFireMissile");
	}
	if (flags & PlayerFlags::kMissileSpawnLeft)
	{
		names.push_back("kMissileSpawnLeft");
	}
	if (flags & PlayerFlags::kTransfer)
	{
		names.push_back("kTransfer");
	}
	if (flags & PlayerFlags::kUseMissiles)
	{
		names.push_back("kUseMissiles");
	}
	if (flags & PlayerFlags::kIsFlagship)
	{
		names.push_back("kIsFlagship");
	}
	if (flags & PlayerFlags::kPendingUseMissiles)
	{
		names.push_back("kPendingUseMissiles");
	}
	return names;
}

nlohmann::json SpaceshipFlagNames(SpaceshipFlags_t flags)
{
	nlohmann::json names = nlohmann::json::array();
	if (flags & SpaceshipFlags::kFleePlayer)
	{
		names.push_back("kFleePlayer");
	}
	if (flags & SpaceshipFlags::kExploding)
	{
		names.push_back("kExploding");
	}
	if (flags & SpaceshipFlags::kReturnToIslandCenter)
	{
		names.push_back("kReturnToIslandCenter");
	}
	if (flags & SpaceshipFlags::kTransfer)
	{
		names.push_back("kTransfer");
	}
	return names;
}

// Focused-fleet members are the only per-fleet member list the client FleetSelection API exposes (FocusedFleet()).
// Non-focused fleets get index + focused flag only.
nlohmann::json BuildFleets()
{
	nlohmann::json fleets = nlohmann::json::array();
	int64_t iFleetCount = gpGame->FleetCount();
	int64_t iFocusedIndex = gpGame->FocusedFleetIndex();
	const Fleet* pFocused = gpGame->FocusedFleet();
	for (int64_t iFleet = 0; iFleet < iFleetCount; ++iFleet)
	{
		bool bFocused = iFleet == iFocusedIndex;
		nlohmann::json fleetJson;
		fleetJson["index"] = iFleet;
		fleetJson["focused"] = bFocused;
		if (bFocused && pFocused != nullptr)
		{
			nlohmann::json members = nlohmann::json::array();
			for (const FleetMember& rMember : pFocused->members)
			{
				members.push_back({{"globalPlayerId", rMember.globalPlayerId.iValue}, {"alive", rMember.bAlive}});
			}
			fleetJson["members"] = std::move(members);
		}
		fleets.push_back(std::move(fleetJson));
	}
	return fleets;
}

} // namespace

const char* UiStateName(UiState eState)
{
	switch (eState)
	{
		case UiState::kNone: return "kNone";
		case UiState::kGameSettings: return "kGameSettings";
		case UiState::kGraphicsSettings: return "kGraphicsSettings";
		case UiState::kModal: return "kModal";
		case UiState::kPause: return "kPause";
		case UiState::kSound: return "kSound";
		case UiState::kTweaks: return "kTweaks";
	}
	return "kNone";
}

nlohmann::json GameFlagNames(engine::GameFlags_t flags)
{
	nlohmann::json names = nlohmann::json::array();
	if (flags & engine::GameFlags::kQuit)
	{
		names.push_back("kQuit");
	}
	if (flags & engine::GameFlags::kSaveReplay)
	{
		names.push_back("kSaveReplay");
	}
	if (flags & engine::GameFlags::kLoadReplay)
	{
		names.push_back("kLoadReplay");
	}
	if (flags & engine::GameFlags::kMainMenu)
	{
		names.push_back("kMainMenu");
	}
	if (flags & engine::GameFlags::kPaused)
	{
		names.push_back("kPaused");
	}
	return names;
}

void CommandDescribeScene(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	// Trust boundary — validate optional params.
	if (rParams.contains("includeUnits") && !rParams.at("includeUnits").is_boolean())
	{
		throw std::runtime_error("describe_scene 'includeUnits' must be a bool");
	}
	if (rParams.contains("maxUnits") && !rParams.at("maxUnits").is_number_integer())
	{
		throw std::runtime_error("describe_scene 'maxUnits' must be an integer");
	}
	bool bIncludeUnits = !rParams.contains("includeUnits") || rParams.at("includeUnits").get<bool>();
	int64_t iMaxUnits = std::max<int64_t>(0, rParams.contains("maxUnits") ? rParams.at("maxUnits").get<int64_t>() : 200);

	// Camera / UI / game state — always present (graceful empty state: no subscribed coords still returns these).
	XMFLOAT4 f4VisibleArea = gpCamera->f4RenderVisibleArea;
	rResult["camera"] =
	{
		{"eye", Vec3ToJson(gpCamera->mVecEyePosition)},
		{"visibleArea", nlohmann::json::array({f4VisibleArea.x, f4VisibleArea.y, f4VisibleArea.z, f4VisibleArea.w})},
		{"lod", gpCamera->miVisibleAreaLod},
	};
	rResult["uiState"] = UiStateName(gpGame->meUiState);
	rResult["gameFlags"] = GameFlagNames(gpGame->mGameFlags);
	rResult["tick"] = gpGame->TickCounter();
	rResult["clientGridCoord"] = CoordToJson(gpGame->mClientGridCoord);
	rResult["fleets"] = BuildFleets();

	nlohmann::json subscribedCoords = nlohmann::json::array();
	nlohmann::json units = nlohmann::json::array();
	nlohmann::json islands = nlohmann::json::array();
	int64_t iPlayerTotal = 0;
	int64_t iSpaceshipTotal = 0;
	int64_t iMissileTotal = 0;
	int64_t iBlasterTotal = 0;
	int64_t iTargetTotal = 0;
	int64_t iUnitCount = 0;
	bool bTruncated = false;

	for (const auto& [rCoord, rFrames] : gpGame->mCoordFrames)
	{
		subscribedCoords.push_back(CoordToJson(rCoord));

		// Island placements come from staticData, available regardless of whether a snapshot has arrived.
		const engine::FrameStaticData& rStaticData = rFrames.staticData;
		bool bHasFootprint = rStaticData.islandRenderQueries.size() == rStaticData.islands.size();
		for (size_t iIsland = 0; iIsland < rStaticData.islands.size(); ++iIsland)
		{
			const engine::IslandPlacement& rPlacement = rStaticData.islands.at(iIsland);
			nlohmann::json islandJson;
			islandJson["coord"] = CoordToJson(rCoord);
			islandJson["center"] = Vec2ToJson(rPlacement.f2WorldPos.x, rPlacement.f2WorldPos.y);
			islandJson["rotation"] = rPlacement.fRotation;
			if (bHasFootprint)
			{
				const engine::IslandRenderQuery& rQuery = rStaticData.islandRenderQueries.at(iIsland);
				islandJson["footprint"] = Vec2ToJson(rQuery.fFootprintX, rQuery.fFootprintY);
			}
			islands.push_back(std::move(islandJson));
		}

		// Units and counts need a rendered snapshot; RenderFrame asserts iSnapshotCount > 0.
		if (rFrames.iSnapshotCount <= 0)
		{
			continue;
		}
		const Frame& rFrame = gpGame->RenderFrame(rCoord);

		iPlayerTotal += rFrame.postRender.pPlayers->iCount;
		iSpaceshipTotal += rFrame.postRender.pSpaceships->iCount;
		iMissileTotal += rFrame.postRender.pMissiles->iCount;
		iBlasterTotal += rFrame.postRender.pBlasters->iCount;
		iTargetTotal += rFrame.postRender.pTargets->iCount;

		if (!bIncludeUnits)
		{
			continue;
		}

		const PlayersInterpolate& rPlayersInterp = *rFrame.interpolate.pPlayers;
		const PlayersPostRender& rPlayersPost = *rFrame.postRender.pPlayers;
		for (int64_t i = 0; i < rPlayersPost.iCount; ++i)
		{
			XMVECTOR vecPosition = rPlayersInterp.pVecPositions[i];
			if (!gpCamera->InVisibleArea(f4VisibleArea, vecPosition))
			{
				continue;
			}
			if (iUnitCount >= iMaxUnits)
			{
				bTruncated = true;
				break;
			}
			XMVECTOR vecScreen = gpCamera->WorldToScreen(vecPosition);
			units.push_back(
			{
				{"type", "player"},
				{"globalId", rPlayersPost.pGlobalPlayerIds[i].iValue},
				{"world", Vec3ToJson(vecPosition)},
				{"screen", Vec2ToJson(XMVectorGetX(vecScreen), XMVectorGetY(vecScreen))},
				{"armor", rPlayersPost.pfArmors[i]},
				{"shield", rPlayersPost.pfShields[i]},
				{"alignment", rPlayersPost.pAlignments[i].Value()},
				{"flags", PlayerFlagNames(rPlayersPost.pFlags[i])},
			});
			++iUnitCount;
		}

		const SpaceshipsInterpolate& rShipsInterp = *rFrame.interpolate.pSpaceships;
		const SpaceshipsPostRender& rShipsPost = *rFrame.postRender.pSpaceships;
		for (int64_t i = 0; i < rShipsPost.iCount; ++i)
		{
			XMVECTOR vecPosition = rShipsInterp.pVecPositions[i];
			if (!gpCamera->InVisibleArea(f4VisibleArea, vecPosition))
			{
				continue;
			}
			if (iUnitCount >= iMaxUnits)
			{
				bTruncated = true;
				break;
			}
			XMVECTOR vecScreen = gpCamera->WorldToScreen(vecPosition);
			units.push_back(
			{
				{"type", "spaceship"},
				{"world", Vec3ToJson(vecPosition)},
				{"screen", Vec2ToJson(XMVectorGetX(vecScreen), XMVectorGetY(vecScreen))},
				{"health", rShipsPost.pfHealths[i]},
				{"alignment", rShipsPost.pAlignments[i].Value()},
				{"flags", SpaceshipFlagNames(rShipsPost.pFlags[i])},
			});
			++iUnitCount;
		}
	}

	rResult["subscribedCoords"] = std::move(subscribedCoords);
	rResult["units"] = std::move(units);
	rResult["counts"] =
	{
		{"players", iPlayerTotal},
		{"spaceships", iSpaceshipTotal},
		{"missiles", iMissileTotal},
		{"blasters", iBlasterTotal},
		{"targets", iTargetTotal},
	};
	rResult["islands"] = std::move(islands);
	rResult["truncated"] = bTruncated;
}

} // namespace game

#endif // defined(BT_CLIENT)
