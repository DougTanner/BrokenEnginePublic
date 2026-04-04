#include "Game.h"

#include "Network/ReconcileReplay.h"

#include "Network/ClientReconciler.h"
#include "Frame/FrameCollections.h"

namespace game
{

#if defined(BT_CLIENT)

static void LogStatusChangeDetail(const StatusChange& rStatusChange)
{
	Log(kLogNetwork, kVerbose, "Type: {}", StatusChangeTypeName(rStatusChange.eType));
	ScopedLogIndent scopedDetail;

	if (const auto* pSpawn = std::get_if<SpawnPlayerData>(&rStatusChange.data))
	{
		Log(kLogNetwork, kVerbose, "GlobalId: {}", pSpawn->iGlobalId);
	}
	else if (const auto* pDestroy = std::get_if<DestroyPlayerData>(&rStatusChange.data))
	{
		Log(kLogNetwork, kVerbose, "PlayerUuid: {}", pDestroy->iPlayerUuid);
	}
	else if (const auto* pWeapon = std::get_if<WeaponModeChangeData>(&rStatusChange.data))
	{
		Log(kLogNetwork, kVerbose, "PlayerUuid: {}", pWeapon->iPlayerUuid);
	}
	else if (const auto* pTransfer = std::get_if<TransferData>(&rStatusChange.data))
	{
		Log(kLogNetwork, kVerbose, "Position: {} Direction: {} Velocity: {}", pTransfer->vecPosition, pTransfer->vecDirection, pTransfer->vecVelocity);
		char acAlignment[20] {};
		common::ToHex(std::span<char, 20>(acAlignment), pTransfer->alignment.uiValue);
		Log(kLogNetwork, kVerbose, "Alignment: {} Health: {} Shield: {} TypeIndex: {}", acAlignment, pTransfer->fHealth, pTransfer->fShield, pTransfer->uiTypeIndex);
		Log(kLogNetwork, kVerbose, "WindTrailIntensity: {} WindTrailWidth: {} WindTrailLengthMultiplier: {} Acceleration: {}", pTransfer->fWindTrailIntensity, pTransfer->fWindTrailWidth, pTransfer->fWindTrailLengthMultiplier, pTransfer->fAcceleration);
		Log(kLogNetwork, kVerbose, "NextBlasterFireTime: {} NextSecondarySpawnTime: {} ShieldCooldown: {} ShieldDownSoundCooldown: {}", pTransfer->fNextBlasterFireTime, pTransfer->fNextSecondarySpawnTime, pTransfer->fShieldCooldown, pTransfer->fShieldDownSoundCooldown);
		Log(kLogNetwork, kVerbose, "AnimationTime: {} ShieldRotation: {} ShieldShrink: {} PlayerFlags: {}", pTransfer->fAnimationTime, pTransfer->fShieldRotation, pTransfer->fShieldShrink, pTransfer->uiPlayerFlags);
		Log(kLogNetwork, kVerbose, "NextBlasterSpawnTime: {}", pTransfer->fNextBlasterSpawnTime);
		Log(kLogNetwork, kVerbose, "DeltaRotationDelay: {} Time: {} ExhaustDelay: {} NextJitter: {}", pTransfer->fDeltaRotationDelay, pTransfer->fTime, pTransfer->fExhaustDelay, pTransfer->fNextJitter);
	}
}

void LogStatusChangeList(std::string_view label, std::span<const StatusChange> statusChanges)
{
	Log(kLogNetwork, kVerbose, "{} Count: {}", label, statusChanges.size());
	ScopedLogIndent scopedList;
	for (size_t i = 0; const StatusChange& rStatusChange : statusChanges)
	{
		Log(kLogNetwork, kVerbose, "[{}]", i);
		ScopedLogIndent scopedEntry;
		LogStatusChangeDetail(rStatusChange);
		++i;
	}
}

// DT TEMP
void LogPerCollectionCrcBreakdown(const Frame& rFrame)
{
	ScopedLogIndent scopedIndent;

	// Entity counts
	Log(kLogNetwork, kVerbose, "EntityCounts Players: {}/{} Blasters: {}/{} Missiles: {}/{} Spaceships: {}/{} Targets: {}/{}",
		rFrame.interpolate.pPlayers->iCount, rFrame.postRender.pPlayers->iCount,
		rFrame.interpolate.pBlasters->iCount, rFrame.postRender.pBlasters->iCount,
		rFrame.interpolate.pMissiles->iCount, rFrame.postRender.pMissiles->iCount,
		rFrame.interpolate.pSpaceships->iCount, rFrame.postRender.pSpaceships->iCount,
		rFrame.interpolate.pTargets->iCount, rFrame.postRender.pTargets->iCount);

	// Per-collection shared CRCs (interpolate)
	{
		common::crc_t baseSharedCrc = static_cast<const engine::FrameInterpolateBase&>(rFrame.interpolate).Crcs();
		common::crc_t playerSharedCrc = engine::CollectionCrc(*rFrame.interpolate.pPlayers, rFrame.interpolate.pPlayers->SharedCrcMembers());
		char acBase[20] {}, acPlayer[20] {};
		common::ToHex(std::span<char, 20>(acBase), baseSharedCrc);
		common::ToHex(std::span<char, 20>(acPlayer), playerSharedCrc);
		Log(kLogNetwork, kVerbose, "InterpSharedCrc Base: {} Players: {}", acBase, acPlayer);

		auto logInterp = [&]<typename T>(const T& col, std::string_view name)
		{
			common::crc_t colCrc = engine::SharedCollectionCrc(col);
			char acCrc[20] {};
			common::ToHex(std::span<char, 20>(acCrc), colCrc);
			Log(kLogNetwork, kVerbose, "InterpSharedCrc {}: {} Count: {}", name, acCrc, col.iCount);
		};
		logInterp(*rFrame.interpolate.pBlasters, "Blasters");
		logInterp(*rFrame.interpolate.pMissiles, "Missiles");
		logInterp(*rFrame.interpolate.pSpaceships, "Spaceships");
		logInterp(*rFrame.interpolate.pTargets, "Targets");
	}

	// Per-collection shared CRCs (post-render)
	{
		common::crc_t baseSharedCrc = static_cast<const engine::FramePostRenderBase&>(rFrame.postRender).Crcs();
		common::crc_t playerSharedCrc = engine::CollectionCrc(*rFrame.postRender.pPlayers, rFrame.postRender.pPlayers->SharedCrcMembers());
		char acBase[20] {}, acPlayer[20] {};
		common::ToHex(std::span<char, 20>(acBase), baseSharedCrc);
		common::ToHex(std::span<char, 20>(acPlayer), playerSharedCrc);
		Log(kLogNetwork, kVerbose, "PostRenderSharedCrc Base: {} Players: {}", acBase, acPlayer);

		auto logPostRender = [&]<typename T>(const T& col, std::string_view name)
		{
			common::crc_t colCrc = engine::SharedCollectionCrc(col);
			char acCrc[20] {};
			common::ToHex(std::span<char, 20>(acCrc), colCrc);
			Log(kLogNetwork, kVerbose, "PostRenderSharedCrc {}: {} Count: {}", name, acCrc, col.iCount);
		};
		logPostRender(*rFrame.postRender.pBlasters, "Blasters");
		logPostRender(*rFrame.postRender.pMissiles, "Missiles");
		logPostRender(*rFrame.postRender.pSpaceships, "Spaceships");
		logPostRender(*rFrame.postRender.pTargets, "Targets");
	}
}

static int64_t FindSnapshotIndex(std::unique_ptr<Frame> (&rSnapshots)[engine::kiNetworkBufferSize], int64_t iHead, int64_t iCount, int64_t iTick)
{
	for (int64_t i = 0; i < iCount; ++i)
	{
		int64_t iPhysical = SnapshotIndex(iHead, i);
		if (rSnapshots[iPhysical] != nullptr && rSnapshots[iPhysical]->interpolate.iTick == iTick)
		{
			return i;
		}
	}
	return -1;
}

struct CrcValidateResult
{
	bool bMatch = true;
	int64_t iLastMatched = -1;
	int64_t iLastMatchedIndex = -1;
};

static CrcValidateResult CrcValidateLoop(CoordReconcileWork& rWork, int64_t iTargetTick)
{
	CrcValidateResult result;
	int64_t iExpected = rWork.iConfirmedTick + 1;
	auto it = rWork.serverUpdates.begin();

	while (it != rWork.serverUpdates.end() && it->first == iExpected && iExpected <= iTargetTick)
	{
		int64_t iIndex = FindSnapshotIndex(rWork.snapshots, rWork.iSnapshotHead, rWork.iSnapshotCount, iExpected);
		if (iIndex < 0)
		{
			break;
		}
		int64_t iPhysical = SnapshotIndex(rWork.iSnapshotHead, iIndex);
		if (rWork.snapshots[iPhysical]->postRender.sharedCrc != it->second.sharedCrc)
		{
			char acSharedCrc[20] {}, acClientCrc[20] {};
			common::ToHex(std::span<char, 20>(acSharedCrc), it->second.sharedCrc);
			common::ToHex(std::span<char, 20>(acClientCrc), rWork.snapshots[iPhysical]->postRender.sharedCrc);
			Log(kLogNetwork, kVerbose, "CrcValidateLoop Server CRC mismatch Coord: ({},{}) Tick: {} SharedCrc: {} ClientCrc: {}", rWork.coord.x, rWork.coord.y, iExpected, acSharedCrc, acClientCrc);
			{
				ScopedLogIndent scopedCrcIndent;
				char acServerInputCrc[20] {}, acClientInputCrc[20] {};
				common::ToHex(std::span<char, 20>(acServerInputCrc), it->second.inputCrc);
				common::ToHex(std::span<char, 20>(acClientInputCrc), rWork.snapshots[iPhysical]->postRender.previousInputCrc);
				Log(kLogNetwork, kVerbose, "ServerInputCrc: {} ClientInputCrc: {}", acServerInputCrc, acClientInputCrc);
				LogStatusChangeList("Server StatusChanges", it->second.statusChanges);
			}
			LogPerCollectionCrcBreakdown(*rWork.snapshots[iPhysical]); // DT TEMP
			result.bMatch = false;
			break;
		}
		if (rWork.snapshots[iPhysical]->postRender.previousInputCrc != it->second.inputCrc)
		{
			char acServerInputCrc[20] {}, acClientInputCrc[20] {};
			common::ToHex(std::span<char, 20>(acServerInputCrc), it->second.inputCrc);
			common::ToHex(std::span<char, 20>(acClientInputCrc), rWork.snapshots[iPhysical]->postRender.previousInputCrc);
			Log(kLogNetwork, kVerbose, "CrcValidateLoop Input CRC mismatch Coord: ({},{}) Tick: {} ServerInputCrc: {} ClientInputCrc: {}", rWork.coord.x, rWork.coord.y, iExpected, acServerInputCrc, acClientInputCrc);
			{
				ScopedLogIndent scopedInputIndent;
				LogStatusChangeList("Server StatusChanges", it->second.statusChanges);
			}
			result.bMatch = false;
			break;
		}
		result.iLastMatched = iExpected;
		result.iLastMatchedIndex = iIndex;
		++iExpected;
		++it;
	}

	return result;
}

static void CrcApplyMatchResult(CoordReconcileWork& rWork, int64_t iLastMatched, int64_t iLastMatchedIndex, ReconcileProfiling& rProfiling)
{
	rWork.bCrcFastPath = true;
	rWork.iNewConfirmedTick = iLastMatched;
	rProfiling.iCrcValidatedFrameTicks += iLastMatched - rWork.iConfirmedTick;

	rWork.iNewConfirmedOffset = SnapshotIndex(rWork.iSnapshotHead, iLastMatchedIndex);
	rWork.iOutputCount = rWork.iSnapshotCount - iLastMatchedIndex;
}

CrcFastPathCoordResult CrcFastPathProcessCoord(CoordReconcileWork& rWork, int64_t iTargetTick, ReconcileProfiling& rProfiling)
{
	CrcFastPathCoordResult result;

	if (rWork.serverUpdates.empty() && !rWork.pendingFullState.has_value())
	{
		return result;
	}

	if (rWork.pendingFullState.has_value())
	{
		result.bHandled = false;
		Log(kLogNetwork, kVerbose, "CrcFastPathProcessCoord Pending full state forces reconcile Coord: ({},{})", rWork.coord.x, rWork.coord.y);
		return result;
	}

	CrcValidateResult validateResult = CrcValidateLoop(rWork, iTargetTick);

	// Gap at confirmed+1 for this coord: first server update is non-consecutive.
	if (validateResult.iLastMatched == -1 && !rWork.serverUpdates.empty() && rWork.serverUpdates.begin()->first != rWork.iConfirmedTick + 1)
	{
		return result;
	}

	// No snapshots to validate: confirmed tick is at or past target tick.
	if (validateResult.iLastMatched == -1 && validateResult.bMatch && rWork.iConfirmedTick >= iTargetTick)
	{
		return result;
	}

	if (validateResult.iLastMatched >= 0)
	{
		CrcApplyMatchResult(rWork, validateResult.iLastMatched, validateResult.iLastMatchedIndex, rProfiling);

		if (!validateResult.bMatch)
		{
			result.bHandled = false;
			Log(kLogNetwork, kVerbose, "CrcFastPathProcessCoord CRC mismatch after partial match Coord: ({},{}) LastMatched: {}", rWork.coord.x, rWork.coord.y, validateResult.iLastMatched);
		}
	}
	else if (!validateResult.bMatch)
	{
		result.bHandled = false;
		Log(kLogNetwork, kVerbose, "CrcFastPathProcessCoord CRC mismatch no matches Coord: ({},{})", rWork.coord.x, rWork.coord.y);
	}
	else
	{
		if (rWork.iConfirmedTick + 1 < iTargetTick)
		{
			result.bHandled = false;
			Log(kLogNetwork, kVerbose, "CrcFastPathProcessCoord Snapshot missing Coord: ({},{}) Confirmed: {} Target: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, iTargetTick);
		}
	}

	return result;
}

#endif // BT_CLIENT

} // namespace game
