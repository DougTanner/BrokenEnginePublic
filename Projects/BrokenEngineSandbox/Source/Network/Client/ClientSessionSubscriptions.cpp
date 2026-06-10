#include "Game.h"

#include "Network/Client/ClientSession.h"

namespace game
{

#if defined(BT_CLIENT)

void ClientSession::UpdateDesiredCoords(SubscriptionChangeReason eReason)
{
	// Ensure tick prefix in log output even when called outside ClientUpdate (e.g., from UI)
	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
	{
		optionalTickScope.emplace(gpGame->TickCounter());
	}

	// Heap: mDesiredCoords.assign, mUnwantedTimestamps map ops, and the kNetwork delta log build allocate.
	ScopedSuppressAllocationTracking suppress;

	auto appendCoordSv = [](common::ScopedWorkbufferArena& rArena, engine::GridCoord coord, bool bLeadingComma)
	{
		if (bLeadingComma)
		{
			rArena.Append(std::string_view(","));
		}
		rArena.Append(std::string_view("("));
		rArena.Append(static_cast<int64_t>(coord.x));
		rArena.Append(std::string_view(","));
		rArena.Append(static_cast<int64_t>(coord.y));
		rArena.Append(std::string_view(")"));
	};

	static constexpr int64_t kiMaxDesiredCoords = 9;
	engine::GridCoord desiredCoords[kiMaxDesiredCoords] {};
	int64_t iDesiredCount = 0;

	auto pushCoord = [&](engine::GridCoord coord)
	{
		ASSERT(iDesiredCount < kiMaxDesiredCoords);
		desiredCoords[iDesiredCount++] = coord;
	};

	if (gpGame->ClientPlayerId().IsValid())
	{
		pushCoord(gpGame->mClientGridCoord);

		for (int64_t i = 0; i < gpGame->miVisibleNeighborCount; ++i)
		{
			pushCoord(gpGame->mVisibleNeighbors[i]);
		}
	}
	else
	{
		// No focused player: subscribe to origin for fleet spawn requests
		pushCoord(engine::kOriginCoord);
	}

	std::span<const engine::GridCoord> desiredSpan(desiredCoords, iDesiredCount);

	// Check if desired set changed
	bool bChanged = (iDesiredCount != std::ssize(mDesiredCoords));
	if (!bChanged)
	{
		for (int64_t i = 0; i < iDesiredCount; ++i)
		{
			if (desiredCoords[i] != mDesiredCoords.at(i))
			{
				bChanged = true;
				break;
			}
		}
	}

	// Build removed/added arenas now (independent of bChanged) so the Exit log always has them.
	// Arenas remain on the workbuffer stack until end of function — kept above any nested arenas.
	// CRITICAL: ScopedWorkbufferArena::View() reads the workbuffer's CURRENT top frame, not the arena's
	// own saved frame. Capture the string_view while each arena is still the top frame; the captured
	// pointer+length stay stable because Grow() DEBUG_BREAKs (buffer is pre-sized) and bytes below the
	// current top are preserved across subsequent pushes/appends.
	common::ScopedWorkbufferArena removedArena = common::gpThreadLocal->mWorkbuffer.Push();
	{
		bool bFirst = true;
		for (const engine::GridCoord& rCoord : mDesiredCoords)
		{
			if (!std::ranges::contains(desiredSpan, rCoord))
			{
				appendCoordSv(removedArena, rCoord, !bFirst);
				bFirst = false;
			}
		}
	}
	const std::string_view svRemoved = removedArena.View();

	common::ScopedWorkbufferArena addedArena = common::gpThreadLocal->mWorkbuffer.Push();
	{
		bool bFirst = true;
		for (const engine::GridCoord& rCoord : desiredSpan)
		{
			if (!std::ranges::contains(mDesiredCoords, rCoord))
			{
				appendCoordSv(addedArena, rCoord, !bFirst);
				bFirst = false;
			}
		}
	}
	const std::string_view svAdded = addedArena.View();

	if (bChanged)
	{
		// Track when coords become unwanted for sticky subscriptions; clear sticky timestamps for
		// re-wanted coords. Iteration mirrors the removed/added build above.
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		for (const engine::GridCoord& rCoord : mDesiredCoords)
		{
			if (!std::ranges::contains(desiredSpan, rCoord))
			{
				mUnwantedTimestamps.try_emplace(rCoord, now);
			}
		}
		for (const engine::GridCoord& rCoord : desiredSpan)
		{
			mUnwantedTimestamps.erase(rCoord);
		}

		const bool bAnyDelta = !svRemoved.empty() || !svAdded.empty();
		if (bAnyDelta)
		{
			common::ScopedWorkbufferArena message = common::gpThreadLocal->mWorkbuffer.Push();
			message.Append(std::string_view("Desired subscriptions changed Reason: "));
			message.Append(std::string_view(ToString(eReason)));
			message.Append(std::string_view(" Removed: ["));
			message.Append(svRemoved);
			message.Append(std::string_view("] Added: ["));
			message.Append(svAdded);
			message.Append(std::string_view("] Tick: "));
			message.Append(gpGame->TickCounter());
			LOG(kNetwork, kVerbose, "{}", message);
		}

		mDesiredCoords.assign(desiredSpan.begin(), desiredSpan.end());
	}
}

void ClientSession::UpdateSubscriptions()
{
	if (mpClientNetwork == nullptr)
	{
		return;
	}

	// Heap: vector operations for subscription queue
	ScopedSuppressAllocationTracking suppress;

	// Build effective desired list: fresh desired + unexpired sticky coords
	common::ScopedWorkbufferArena effectiveDesiredArena = common::gpThreadLocal->mWorkbuffer.Push();
	for (const engine::GridCoord& rCoord : mDesiredCoords)
	{
		effectiveDesiredArena.PushBack(rCoord);
	}
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	std::erase_if(mUnwantedTimestamps, [&](const std::pair<const engine::GridCoord, std::chrono::steady_clock::time_point>& rPair)
	{
		if (now - rPair.second >= kStickySubscriptionDuration)
		{
			LOG(kNetwork, kVerbose, "Sticky subscription expired ({},{})", rPair.first.x, rPair.first.y);
			return true;
		}
		effectiveDesiredArena.PushBack(rPair.first);
		return false;
	});

	std::span<const engine::GridCoord> effectiveDesired = effectiveDesiredArena.Span<const engine::GridCoord>();
	UnsubscribeStaleCoords(effectiveDesired);
	BuildSubscriptionQueue(effectiveDesired);
	TrySubscribeNext();
}

#endif // BT_CLIENT

} // namespace game
