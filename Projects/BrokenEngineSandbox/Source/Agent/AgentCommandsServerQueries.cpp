#include "Agent/AgentCommandsServerQueries.h"

#if defined(BT_SERVER)

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

// ---- Frame query helpers (trust boundary — params are external input) ----

int64_t OptionalCount(const nlohmann::json& rParams, const char* pcKey, int64_t iDefault)
{
	return rParams.contains(pcKey) ? rParams.at(pcKey).get<int64_t>() : iDefault;
}

nlohmann::json Vec3ToJson(XMVECTOR vec)
{
	return nlohmann::json::array({XMVectorGetX(vec), XMVectorGetY(vec), XMVectorGetZ(vec)});
}

// Clamp an offset/limit window against a collection's live count → [iBegin, iEnd).
void ClampWindow(int64_t iTotal, int64_t iOffset, int64_t iLimit, int64_t& riBegin, int64_t& riEnd)
{
	riBegin = std::clamp<int64_t>(iOffset, 0, iTotal);
	riEnd = std::min<int64_t>(iTotal, riBegin + std::max<int64_t>(iLimit, 0));
}

// Minimum+cheap field set per collection: index, pos/dir, health, alignment, id — wherever the member exists.
nlohmann::json ExtractPlayers(const Frame& rFrame, int64_t iOffset, int64_t iLimit)
{
	const PlayersInterpolate& rInterp = *rFrame.interpolate.pPlayers;
	const PlayersPostRender& rPost = *rFrame.postRender.pPlayers;
	int64_t iBegin = 0;
	int64_t iEnd = 0;
	ClampWindow(rPost.iCount, iOffset, iLimit, iBegin, iEnd);
	nlohmann::json items = nlohmann::json::array();
	for (int64_t i = iBegin; i < iEnd; ++i)
	{
		items.push_back(
		{
			{"index", i},
			{"uuid", rPost.puiIds[i].ToUuid().Value()},
			{"globalId", rPost.pGlobalPlayerIds[i].iValue},
			{"pos", Vec3ToJson(rInterp.pVecPositions[i])},
			{"dir", Vec3ToJson(rInterp.pVecDirections[i])},
			{"armor", rPost.pfArmors[i]},
			{"shield", rPost.pfShields[i]},
			{"flags", std::to_underlying(rPost.pFlags[i].meFlags)},
			{"alignment", rPost.pAlignments[i].Value()},
		});
	}
	return items;
}

nlohmann::json ExtractSpaceships(const Frame& rFrame, int64_t iOffset, int64_t iLimit)
{
	const SpaceshipsInterpolate& rInterp = *rFrame.interpolate.pSpaceships;
	const SpaceshipsPostRender& rPost = *rFrame.postRender.pSpaceships;
	int64_t iBegin = 0;
	int64_t iEnd = 0;
	ClampWindow(rPost.iCount, iOffset, iLimit, iBegin, iEnd);
	nlohmann::json items = nlohmann::json::array();
	for (int64_t i = iBegin; i < iEnd; ++i)
	{
		items.push_back(
		{
			{"index", i},
			{"pos", Vec3ToJson(rInterp.pVecPositions[i])},
			{"dir", Vec3ToJson(rInterp.pVecDirections[i])},
			{"health", rPost.pfHealths[i]},
			{"deltaRotation", rInterp.pfDeltaRotations[i]},
			{"alignment", rPost.pAlignments[i].Value()},
		});
	}
	return items;
}

nlohmann::json ExtractMissiles(const Frame& rFrame, int64_t iOffset, int64_t iLimit)
{
	const MissilesInterpolate& rInterp = *rFrame.interpolate.pMissiles;
	const MissilesPostRender& rPost = *rFrame.postRender.pMissiles;
	int64_t iBegin = 0;
	int64_t iEnd = 0;
	ClampWindow(rPost.iCount, iOffset, iLimit, iBegin, iEnd);
	nlohmann::json items = nlohmann::json::array();
	for (int64_t i = iBegin; i < iEnd; ++i)
	{
		items.push_back(
		{
			{"index", i},
			{"pos", Vec3ToJson(rInterp.pVecPositions[i])},
			{"dir", Vec3ToJson(rInterp.pVecDirections[i])},
			{"deltaRotation", rPost.pfDeltaRotations[i]},
			{"deltaRotationDelay", rPost.pfDeltaRotationDelays[i]},
			{"alignment", rPost.pAlignments[i].Value()},
		});
	}
	return items;
}

nlohmann::json ExtractBlasters(const Frame& rFrame, int64_t iOffset, int64_t iLimit)
{
	const BlastersInterpolate& rInterp = *rFrame.interpolate.pBlasters;
	const BlastersPostRender& rPost = *rFrame.postRender.pBlasters;
	int64_t iBegin = 0;
	int64_t iEnd = 0;
	ClampWindow(rPost.iCount, iOffset, iLimit, iBegin, iEnd);
	nlohmann::json items = nlohmann::json::array();
	for (int64_t i = iBegin; i < iEnd; ++i)
	{
		items.push_back(
		{
			{"index", i},
			{"pos", Vec3ToJson(rInterp.pVecPositions[i])},
			{"dir", Vec3ToJson(rInterp.pVecDirections[i])},
			{"alignment", rPost.pAlignments[i].Value()},
		});
	}
	return items;
}

nlohmann::json ExtractTargets(const Frame& rFrame, int64_t iOffset, int64_t iLimit)
{
	const TargetsInterpolate& rInterp = *rFrame.interpolate.pTargets;
	const TargetsPostRender& rPost = *rFrame.postRender.pTargets;
	int64_t iBegin = 0;
	int64_t iEnd = 0;
	ClampWindow(rPost.iCount, iOffset, iLimit, iBegin, iEnd);
	nlohmann::json items = nlohmann::json::array();
	for (int64_t i = iBegin; i < iEnd; ++i)
	{
		items.push_back(
		{
			{"index", i},
			{"uuid", rPost.puiIds[i].ToUuid().Value()},
			{"pos", Vec3ToJson(rInterp.pVecPositions[i])},
			{"flags", std::to_underlying(rPost.pFlags[i].meFlags)},
			{"alignment", rPost.pAlignments[i].Value()},
		});
	}
	return items;
}

const Frame& QueryFrame(const nlohmann::json& rParams)
{
	engine::GridCoord coord = CoordFromParam(rParams);
	auto framesIt = gpGame->mCoordFrames.find(coord);
	if (framesIt == gpGame->mCoordFrames.end())
	{
		throw std::runtime_error("coord has no loaded frame");
	}
	// pCurrent is null for a just-activated coord until the first post-tick SwapFrames (never happens while paused);
	// CurrentFrame would deref it. Coord is external input — trust-boundary check, not a useless ASSERT.
	if (framesIt->second.pCurrent == nullptr)
	{
		throw std::runtime_error("coord frame not ready");
	}
	return gpGame->CurrentFrame(coord);
}

} // namespace

void CommandQueryFrame(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	const Frame& rFrame = QueryFrame(rParams);
	rResult["players"] = {{"count", rFrame.postRender.pPlayers->iCount}};
	rResult["spaceships"] = {{"count", rFrame.postRender.pSpaceships->iCount}};
	rResult["missiles"] = {{"count", rFrame.postRender.pMissiles->iCount}};
	rResult["blasters"] = {{"count", rFrame.postRender.pBlasters->iCount}};
	rResult["targets"] = {{"count", rFrame.postRender.pTargets->iCount}};
}

void CommandQueryPlayers(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	const Frame& rFrame = QueryFrame(rParams);
	int64_t iOffset = OptionalCount(rParams, "offset", 0);
	int64_t iLimit = OptionalCount(rParams, "limit", 256);
	rResult["total"] = rFrame.postRender.pPlayers->iCount;
	rResult["players"] = ExtractPlayers(rFrame, iOffset, iLimit);
}

void CommandQueryCollection(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	const Frame& rFrame = QueryFrame(rParams);
	if (!rParams.contains("collection") || !rParams.at("collection").is_string())
	{
		throw std::runtime_error("query_collection requires string 'collection'");
	}
	std::string collection = rParams.at("collection").get<std::string>();
	int64_t iOffset = OptionalCount(rParams, "offset", 0);
	int64_t iLimit = OptionalCount(rParams, "limit", 256);

	int64_t iTotal = 0;
	nlohmann::json items;
	if (collection == "spaceships")
	{
		iTotal = rFrame.postRender.pSpaceships->iCount;
		items = ExtractSpaceships(rFrame, iOffset, iLimit);
	}
	else if (collection == "missiles")
	{
		iTotal = rFrame.postRender.pMissiles->iCount;
		items = ExtractMissiles(rFrame, iOffset, iLimit);
	}
	else if (collection == "blasters")
	{
		iTotal = rFrame.postRender.pBlasters->iCount;
		items = ExtractBlasters(rFrame, iOffset, iLimit);
	}
	else if (collection == "targets")
	{
		iTotal = rFrame.postRender.pTargets->iCount;
		items = ExtractTargets(rFrame, iOffset, iLimit);
	}
	else
	{
		throw std::runtime_error("'collection' must be spaceships|missiles|blasters|targets");
	}
	rResult["total"] = iTotal;
	rResult["items"] = std::move(items);
}

} // namespace game

#endif // defined(BT_SERVER)
