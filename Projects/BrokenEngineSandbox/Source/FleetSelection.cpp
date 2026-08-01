#include "FleetSelection.h"

#if defined(BT_CLIENT)

#include "Game.h"

namespace game
{

void FleetSelection::AutoSelectFirstAliveMember()
{
	miFocusedPlayerInFleetIndex = -1;
	gpGame->SetClientGridCoord({});
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

int64_t FleetSelection::FleetCount() const
{
	return std::ssize(mClientFleets);
}

int64_t FleetSelection::FocusedFleetIndex() const
{
	return miFocusedFleetIndex;
}

void FleetSelection::FocusNextFleet()
{
	if (miFocusedFleetIndex < std::ssize(mClientFleets) - 1)
	{
		++miFocusedFleetIndex;
		AutoSelectFirstAliveMember();
		gpGame->CaptureClientStateAndSaveIfChanged();
	}
}

void FleetSelection::FocusPrevFleet()
{
	if (miFocusedFleetIndex > 0)
	{
		--miFocusedFleetIndex;
		AutoSelectFirstAliveMember();
		gpGame->CaptureClientStateAndSaveIfChanged();
	}
}

bool FleetSelection::CanFocusNextFleet() const
{
	return miFocusedFleetIndex < std::ssize(mClientFleets) - 1;
}

bool FleetSelection::CanFocusPrevFleet() const
{
	return miFocusedFleetIndex > 0;
}

const Fleet* FleetSelection::FocusedFleet() const
{
	if (miFocusedFleetIndex >= 0 && miFocusedFleetIndex < std::ssize(mClientFleets))
	{
		return &mClientFleets.at(static_cast<size_t>(miFocusedFleetIndex));
	}
	return nullptr;
}

void FleetSelection::SelectPlayerInFleet(int64_t iPlayerIndex)
{
	const Fleet* pFleet = FocusedFleet();
	if (pFleet == nullptr || iPlayerIndex < 0 || iPlayerIndex >= std::ssize(pFleet->members))
	{
		return;
	}

	miFocusedPlayerInFleetIndex = iPlayerIndex;
	gpGame->mWeaponModeToggle.Reset();

	// Update mClientGridCoord to match selected player's coord
	const FleetMember& rMember = pFleet->members.at(static_cast<size_t>(iPlayerIndex));
	if (rMember.bAlive)
	{
		for (int64_t i = 0; i < std::ssize(gpGame->mClientPlayerIds); ++i)
		{
			if (gpGame->mClientPlayerIds.at(i) == rMember.globalPlayerId)
			{
				gpGame->SetClientGridCoord(gpGame->mClientPlayerCoords.at(i));
				break;
			}
		}
	}

	gpGame->CaptureClientStateAndSaveIfChanged();
}

int64_t FleetSelection::FocusedPlayerInFleetIndex() const
{
	return miFocusedPlayerInFleetIndex;
}

void FleetSelection::SyncFleets(std::vector<Fleet>&& fleets)
{
	// Heap: mClientFleets rebuild + LOG argument formatting allocations
	ScopedSuppressAllocationTracking suppress;

	LOG(kNetwork, kVerbose, "SyncFleets Fleets: {} Members: {} FocusedFleet: {} FocusedMember: {}", std::ssize(fleets), !fleets.empty() ? std::ssize(fleets.at(0).members) : 0, miFocusedFleetIndex, miFocusedPlayerInFleetIndex);

	int64_t iPrevFleetCount = std::ssize(mClientFleets);
	int64_t iPrevFocusedFleetMemberCount = 0;
	FleetGuid prevFocusedFleetGuid {};
	if (miFocusedFleetIndex >= 0 && miFocusedFleetIndex < iPrevFleetCount)
	{
		const Fleet& rPrevFocusedFleet = mClientFleets.at(static_cast<size_t>(miFocusedFleetIndex));
		iPrevFocusedFleetMemberCount = std::ssize(rPrevFocusedFleet.members);
		prevFocusedFleetGuid = rPrevFocusedFleet.guid;
	}
	mClientFleets = std::move(fleets);

	auto FindFleetIndexByGuid = [this](const FleetGuid& rGuid) -> int64_t
	{
		if (!rGuid.IsValid())
		{
			return -1;
		}
		for (int64_t i = 0; i < std::ssize(mClientFleets); ++i)
		{
			if (mClientFleets.at(static_cast<size_t>(i)).guid == rGuid)
			{
				return i;
			}
		}
		return -1;
	};

	// Restore from disk-persisted client state on the first sync after a reconnect-style clear.
	// The remembered FleetGuid identifies which fleet to focus; a missing or destroyed ship falls back to the fleet's current flagship.
	bool bRestoredRemembered = false;
	if (iPrevFleetCount == 0)
	{
		int64_t iRememberedFleetIndex = FindFleetIndexByGuid(gpGame->mRememberedFleetGuid);
		if (iRememberedFleetIndex >= 0)
		{
			const Fleet& rFleet = mClientFleets.at(static_cast<size_t>(iRememberedFleetIndex));
			miFocusedFleetIndex = iRememberedFleetIndex;
			miFocusedPlayerInFleetIndex = -1;
			if (gpGame->mRememberedFocusedShipId.IsValid())
			{
				for (int64_t j = 0; j < std::ssize(rFleet.members); ++j)
				{
					const FleetMember& rMember = rFleet.members.at(static_cast<size_t>(j));
					if (rMember.globalPlayerId == gpGame->mRememberedFocusedShipId && rMember.bAlive)
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
		}
	}

	if (!bRestoredRemembered)
	{
		// Re-anchor by identity before clamping: the server can erase a fleet from the middle of the vector,
		// so an index still in range would otherwise silently address a different fleet.
		int64_t iReanchoredFleetIndex = FindFleetIndexByGuid(prevFocusedFleetGuid);
		if (iReanchoredFleetIndex >= 0)
		{
			miFocusedFleetIndex = iReanchoredFleetIndex;
		}

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
	engine::global_id_t focusedId = gpGame->ClientPlayerId();
	bool bGridCoordResolved = false;
	if (focusedId.IsValid())
	{
		for (int64_t i = 0; i < std::ssize(gpGame->mClientPlayerIds); ++i)
		{
			if (gpGame->mClientPlayerIds.at(i) == focusedId)
			{
				gpGame->SetClientGridCoord(gpGame->mClientPlayerCoords.at(i));
				bGridCoordResolved = true;
				break;
			}
		}
	}

	// No valid selection — camera to origin
	if (!bGridCoordResolved)
	{
		gpGame->SetClientGridCoord({});
	}

	// Persist whatever final focus state SyncFleets settled on (covers server-driven changes the user didn't trigger directly).
	gpGame->CaptureClientStateAndSaveIfChanged();
}

void FleetSelection::Clear()
{
	mClientFleets.clear();
	miFocusedFleetIndex = -1;
	miFocusedPlayerInFleetIndex = -1;
}

} // namespace game

#endif // BT_CLIENT
