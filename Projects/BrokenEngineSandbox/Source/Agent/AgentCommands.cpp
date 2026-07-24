#include "Agent/AgentCommands.h"

#include "Frame/Collections/Blasters/Blasters.h"
#include "Game.h"

namespace game
{

namespace
{

// ---- collection_layout_capacity_fixture (Fix 2: physical-layout-capacity retention acceptance) ----

// Deterministic per-row shared-member values. The identical formula drives the source-stream writer and the
// post-read verification, so any row SharedCollectionRead fails to preserve is caught. ([[maybe_unused]]: the sole
// callers live in the fixture's kbDebugInput-only branch, discarded on non-debug builds.)
[[maybe_unused]] void FillBlasterSharedRow(BlastersPostRender& rBlasters, int64_t i, int64_t iSeed)
{
	rBlasters.pFlags[i] = ((i + iSeed) & 1) ? BlasterFlags_t {BlasterFlags::kTransfer} : BlasterFlags_t {BlasterFlags::kDestroy};
	rBlasters.pVecVelocities[i] = XMVectorSet(static_cast<float>(i), static_cast<float>(iSeed), static_cast<float>(i + iSeed), 0.0f);
	rBlasters.pfPitches[i] = static_cast<float>(iSeed * 1000 + i);
	rBlasters.pAlignments[i] = engine::alignment_t {static_cast<uint32_t>(iSeed * 100 + i + 1)};
}

[[maybe_unused]] bool BlasterSharedRowMatches(const BlastersPostRender& rBlasters, int64_t i, int64_t iSeed)
{
	BlasterFlags_t expectedFlags = ((i + iSeed) & 1) ? BlasterFlags_t {BlasterFlags::kTransfer} : BlasterFlags_t {BlasterFlags::kDestroy};
	XMVECTOR vecExpected = XMVectorSet(static_cast<float>(i), static_cast<float>(iSeed), static_cast<float>(i + iSeed), 0.0f);
	return rBlasters.pFlags[i] == expectedFlags
		&& XMVector4Equal(rBlasters.pVecVelocities[i], vecExpected)
		&& rBlasters.pfPitches[i] == static_cast<float>(iSeed * 1000 + i)
		&& rBlasters.pAlignments[i] == engine::alignment_t {static_cast<uint32_t>(iSeed * 100 + i + 1)};
}

// Drives the real BlastersPostRender deserialization helpers through logical capacities 100 -> 70 -> 60 -> 150 on one
// reused instance, proving the transient iPhysicalLayoutCapacity holds the true buffer stride across shrink-reuse:
// the two shrinks reuse the 100-wide buffer and (client) zero the full physical layout including rows 70-99, while the
// >100-row read reallocates exactly once and publishes the new capacity only after the allocation succeeds.
void CommandCollectionLayoutCapacityFixture([[maybe_unused]] const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	if constexpr (!kbDebugInput)
	{
		throw std::runtime_error("collection_layout_capacity_fixture requires kbDebugInput build");
	}
	else
	{
#if defined(BT_CLIENT)
		rResult["build"] = "client";
#else
		rResult["build"] = "server";
#endif

		// Writes one shared-wire stream (metadata + SharedMembers) at (iCapacity, iCount) through the production Write path.
		auto BuildStream = [](int64_t iCapacity, int64_t iCount, int64_t iSeed, std::stringstream& rStream)
		{
			BlastersPostRender source;
			engine::GrowCapacityWithCopy(source, iCapacity, 0, source.Members());
			source.iCount = iCount;
			for (int64_t i = 0; i < iCount; ++i)
			{
				FillBlasterSharedRow(source, i, iSeed);
			}
			engine::CollectionWrite(rStream, source, source.SharedMembers());
		};

		BlastersPostRender dest;
		nlohmann::json steps = nlohmann::json::array();

		// Runs one production SharedCollectionRead into dest and records the capacity metadata and buffer-reuse decision.
		auto RunRead = [&](const char* pcLabel, int64_t iCapacity, int64_t iCount, int64_t iSeed)
		{
			std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
			BuildStream(iCapacity, iCount, iSeed, stream);

			const void* pBefore = dest.pData.get();
			engine::SharedCollectionRead(stream, dest);

			nlohmann::json step;
			step["label"] = pcLabel;
			step["logicalCapacity"] = dest.iCapacity;
			step["physicalCapacity"] = dest.iPhysicalLayoutCapacity;
			step["reused"] = (pBefore != nullptr && dest.pData.get() == pBefore);
			steps.push_back(std::move(step));
		};

#if defined(BT_CLIENT)
		// Seeds a nonzero client-only sentinel across the entire physical layout so a later read must clear the full extent.
		auto SeedSounds = [&]()
		{
			for (int64_t i = 0; i < dest.iPhysicalLayoutCapacity; ++i)
			{
				dest.puiSounds[i] = engine::sound_t {engine::uuid_t {0x7fffffffffffffffLL}};
			}
		};
		auto CountNonZeroSounds = [&]() -> int64_t
		{
			int64_t iNonZero = 0;
			for (int64_t i = 0; i < dest.iPhysicalLayoutCapacity; ++i)
			{
				if (dest.puiSounds[i].ToUuid().Value() != 0)
				{
					++iNonZero;
				}
			}
			return iNonZero;
		};
		int64_t iSoundsNonZeroTotal = 0;
#endif

		// 100 -> 70 -> 60: an initial allocation then two shrink-reuses. iPhysicalLayoutCapacity stays 100, so neither
		// shrink reallocates, and each read must zero the full physical layout before reading the smaller live-row count.
		RunRead("read100", 100, 100, 1);
#if defined(BT_CLIENT)
		SeedSounds();
#endif
		RunRead("read70", 70, 70, 2);
#if defined(BT_CLIENT)
		iSoundsNonZeroTotal += CountNonZeroSounds();
		SeedSounds();
#endif
		RunRead("read60", 60, 60, 3);
#if defined(BT_CLIENT)
		iSoundsNonZeroTotal += CountNonZeroSounds();
		rResult["clientSoundsNonZeroCount"] = iSoundsNonZeroTotal;
		rResult["clientSoundsZeroed"] = (iSoundsNonZeroTotal == 0);
#endif

		// Every logical row from the final (seed 3, 60-row) stream must survive exactly.
		int64_t iSharedMismatches = 0;
		for (int64_t i = 0; i < dest.iCount; ++i)
		{
			if (!BlasterSharedRowMatches(dest, i, 3))
			{
				++iSharedMismatches;
			}
		}
		rResult["sharedRowMismatches"] = iSharedMismatches;
		rResult["sharedRowsPreserved"] = (iSharedMismatches == 0);

#if defined(BT_SERVER)
		// The server build serializes Members() directly, so it must equal SharedMembers() (wire/CRC parity).
		rResult["serverMembersEqualShared"] =
			engine::IsMemberTupleSubset(dest.Members(), dest.SharedMembers())
			&& engine::IsMemberTupleSubset(dest.SharedMembers(), dest.Members());
#endif

		// A >100-row read must reallocate exactly once, growing the physical layout and publishing it only after success.
		RunRead("read150", 150, 150, 4);

		bool bReuseOk = steps[1]["reused"].get<bool>()
			&& steps[2]["reused"].get<bool>()
			&& !steps[3]["reused"].get<bool>()
			&& steps[1]["physicalCapacity"].get<int64_t>() == 100
			&& steps[2]["physicalCapacity"].get<int64_t>() == 100
			&& steps[3]["physicalCapacity"].get<int64_t>() == 150;

		bool bPassed = (iSharedMismatches == 0) && bReuseOk;
#if defined(BT_CLIENT)
		bPassed = bPassed && (iSoundsNonZeroTotal == 0);
#endif
#if defined(BT_SERVER)
		bPassed = bPassed && rResult["serverMembersEqualShared"].get<bool>();
#endif
		rResult["passed"] = bPassed;
		rResult["steps"] = std::move(steps);
	}
}

} // namespace

void ExecuteAgentCommand(std::string_view cmd, const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (engine::ExecuteSharedAgentCommand(cmd, rParams, rResult, gpGame != nullptr ? gpGame->TickCounter() : -1))
	{
		return;
	}

	// Game-owned command reachable on both endpoints (internal BT_CLIENT/BT_SERVER split); dispatched here, before the
	// side-specific fallthrough, because AgentCommands.cpp is the only game agent TU compiled into both executables.
	if (cmd == "collection_layout_capacity_fixture")
	{
		CommandCollectionLayoutCapacityFixture(rParams, rResult);
		return;
	}

#if defined(BT_CLIENT)
	if (!ExecuteAgentCommandClient(cmd, rParams, rResult))
	{
		throw std::runtime_error("unknown command");
	}
#elif defined(BT_SERVER)
	if (!ExecuteAgentCommandServer(cmd, rParams, rResult))
	{
		throw std::runtime_error("unknown command");
	}
#else
	throw std::runtime_error("unknown command");
#endif
}

} // namespace game
