#include "Game.h"

#include "Network/ReconcileReplay.h"

#include "Network/ClientReconciler.h"

namespace game
{

#if defined(BT_CLIENT)

static void LogStatusChangeDetail(const StatusChange& rStatusChange)
{
	Log(kLogNetwork, "Type: {}", StatusChangeTypeName(rStatusChange.eType));
	ScopedLogIndent scopedDetail;

	if (const auto* pSpawn = std::get_if<SpawnPlayerData>(&rStatusChange.data))
	{
		Log(kLogNetwork, "GlobalId: {}", pSpawn->iGlobalId);
	}
	else if (const auto* pDestroy = std::get_if<DestroyPlayerData>(&rStatusChange.data))
	{
		Log(kLogNetwork, "PlayerUuid: {}", pDestroy->iPlayerUuid);
	}
	else if (const auto* pWeapon = std::get_if<WeaponModeChangeData>(&rStatusChange.data))
	{
		Log(kLogNetwork, "PlayerUuid: {}", pWeapon->iPlayerUuid);
	}
	else if (const auto* pTransfer = std::get_if<TransferData>(&rStatusChange.data))
	{
		Log(kLogNetwork, "Position: {} Direction: {} Velocity: {}", pTransfer->vecPosition, pTransfer->vecDirection, pTransfer->vecVelocity);
		char acAlignment[20] {};
		common::ToHex(std::span<char, 20>(acAlignment), pTransfer->alignment.uiValue);
		Log(kLogNetwork, "Alignment: {} Health: {} Shield: {} TypeIndex: {}", acAlignment, pTransfer->fHealth, pTransfer->fShield, pTransfer->uiTypeIndex);
		Log(kLogNetwork, "WindTrailIntensity: {} WindTrailWidth: {} WindTrailLengthMultiplier: {} Acceleration: {}", pTransfer->fWindTrailIntensity, pTransfer->fWindTrailWidth, pTransfer->fWindTrailLengthMultiplier, pTransfer->fAcceleration);
		Log(kLogNetwork, "NextBlasterFireTime: {} NextSecondarySpawnTime: {} ShieldCooldown: {} ShieldDownSoundCooldown: {}", pTransfer->fNextBlasterFireTime, pTransfer->fNextSecondarySpawnTime, pTransfer->fShieldCooldown, pTransfer->fShieldDownSoundCooldown);
		Log(kLogNetwork, "AnimationTime: {} ShieldRotation: {} ShieldShrink: {} PlayerFlags: {}", pTransfer->fAnimationTime, pTransfer->fShieldRotation, pTransfer->fShieldShrink, pTransfer->uiPlayerFlags);
		Log(kLogNetwork, "NextBlasterSpawnTime: {}", pTransfer->fNextBlasterSpawnTime);
		Log(kLogNetwork, "DeltaRotationDelay: {} Time: {} ExhaustDelay: {} NextJitter: {}", pTransfer->fDeltaRotationDelay, pTransfer->fTime, pTransfer->fExhaustDelay, pTransfer->fNextJitter);
	}
}

void LogStatusChangeList(std::string_view label, std::span<const StatusChange> statusChanges)
{
	Log(kLogNetwork, "{} Count: {}", label, statusChanges.size());
	ScopedLogIndent scopedList;
	for (size_t i = 0; const StatusChange& rStatusChange : statusChanges)
	{
		Log(kLogNetwork, "[{}]", i);
		ScopedLogIndent scopedEntry;
		LogStatusChangeDetail(rStatusChange);
		++i;
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
			char acSharedCrc[20] {}, acClientCrc[20] {}, acPrevCrc[20] {};
			common::ToHex(std::span<char, 20>(acSharedCrc), it->second.sharedCrc);
			common::ToHex(std::span<char, 20>(acClientCrc), rWork.snapshots[iPhysical]->postRender.sharedCrc);
			common::ToHex(std::span<char, 20>(acPrevCrc), rWork.snapshots[iPhysical]->postRender.previousCrc);
			Log(kLogNetwork, "CrcValidateLoop Server CRC mismatch Coord: ({},{}) Tick: {} SharedCrc: {} ClientCrc: {} PrevCrc: {}", rWork.coord.x, rWork.coord.y, iExpected, acSharedCrc, acClientCrc, acPrevCrc);
			{
				ScopedLogIndent scopedCrcIndent;
				char acServerInputCrc[20] {}, acClientInputCrc[20] {};
				common::ToHex(std::span<char, 20>(acServerInputCrc), it->second.inputCrc);
				common::ToHex(std::span<char, 20>(acClientInputCrc), rWork.snapshots[iPhysical]->postRender.previousInputCrc);
				Log(kLogNetwork, "ServerInputCrc: {} ClientInputCrc: {}", acServerInputCrc, acClientInputCrc);
				LogStatusChangeList("Server StatusChanges", it->second.statusChanges);
			}
			result.bMatch = false;
			break;
		}
		if (rWork.snapshots[iPhysical]->postRender.previousInputCrc != it->second.inputCrc)
		{
			char acServerInputCrc[20] {}, acClientInputCrc[20] {};
			common::ToHex(std::span<char, 20>(acServerInputCrc), it->second.inputCrc);
			common::ToHex(std::span<char, 20>(acClientInputCrc), rWork.snapshots[iPhysical]->postRender.previousInputCrc);
			Log(kLogNetwork, "CrcValidateLoop Input CRC mismatch Coord: ({},{}) Tick: {} ServerInputCrc: {} ClientInputCrc: {}", rWork.coord.x, rWork.coord.y, iExpected, acServerInputCrc, acClientInputCrc);
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
		Log(kLogNetwork, "CrcFastPathProcessCoord Pending full state forces reconcile Coord: ({},{})", rWork.coord.x, rWork.coord.y);
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
			Log(kLogNetwork, "CrcFastPathProcessCoord CRC mismatch after partial match Coord: ({},{}) LastMatched: {}", rWork.coord.x, rWork.coord.y, validateResult.iLastMatched);
		}
	}
	else if (!validateResult.bMatch)
	{
		result.bHandled = false;
		Log(kLogNetwork, "CrcFastPathProcessCoord CRC mismatch no matches Coord: ({},{})", rWork.coord.x, rWork.coord.y);
	}
	else
	{
		if (rWork.iConfirmedTick + 1 < iTargetTick)
		{
			result.bHandled = false;
			Log(kLogNetwork, "CrcFastPathProcessCoord Snapshot missing Coord: ({},{}) Confirmed: {} Target: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, iTargetTick);
		}
	}

	return result;
}

#endif // BT_CLIENT

} // namespace game
