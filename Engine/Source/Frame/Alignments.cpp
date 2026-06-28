#include "Alignments.h"

namespace engine
{

static constexpr auto AlignmentKeyLess = [](const AlignmentPair& rPair, uint64_t uiKey) { return rPair.uiKey < uiKey; };

uint64_t Alignments::MakeAlignmentKey(alignment_t idA, alignment_t idB)
{
	uint32_t uiKeyA = idA.Value();
	uint32_t uiKeyB = idB.Value();
	if (uiKeyA > uiKeyB)
	{
		std::swap(uiKeyA, uiKeyB);
	}
	return (static_cast<uint64_t>(uiKeyA) << 32) | uiKeyB;
}

void Alignments::AddAlignment(alignment_t idA, alignment_t idB, uint8_t uiFlags)
{
	uint64_t uiKey = MakeAlignmentKey(idA, idB);
	auto it = std::lower_bound(alignmentPairs.begin(), alignmentPairs.end(), uiKey, AlignmentKeyLess);

	if (it != alignmentPairs.end() && it->uiKey == uiKey)
	{
		it->uiFlags = uiFlags;
	}
	else
	{
		alignmentPairs.insert(it, AlignmentPair {.uiKey = uiKey, .uiFlags = uiFlags,});
	}
}

bool Alignments::CanCollide(alignment_t idA, alignment_t idB) const
{
	uint64_t uiKey = MakeAlignmentKey(idA, idB);
	auto it = std::lower_bound(alignmentPairs.begin(), alignmentPairs.end(), uiKey, AlignmentKeyLess);

	if (it == alignmentPairs.end() || it->uiKey != uiKey)
	{
		return false;
	}
	return (it->uiFlags & AlignmentFlags::kEnemies) != 0;
}

void Alignments::CopyFrom(const Alignments& rOther)
{
	if (alignmentPairs.size() == rOther.alignmentPairs.size())
	{
		std::memcpy(alignmentPairs.data(), rOther.alignmentPairs.data(), alignmentPairs.size() * sizeof(AlignmentPair));
	}
	else
	{
		// Heap: vector copy-assign may realloc when pair count changed (new alliance/faction added or removed).
		//   Can't use workbuffer (persists as member for collision filtering) or pre-alloc (pair count varies at runtime)
		ScopedSuppressAllocationTracking suppress;
		alignmentPairs = rOther.alignmentPairs;
	}
}

bool Alignments::operator==(const Alignments& rOther) const
{
	return alignmentPairs == rOther.alignmentPairs;
}

common::crc_t Alignments::Crc() const
{
	common::crc_t checksum = 0;

	for (const AlignmentPair& rPair : alignmentPairs)
	{
		checksum = (checksum ^ common::Crc(rPair.uiKey)) * common::kCrcMultiplier;
		checksum = (checksum ^ common::Crc(rPair.uiFlags)) * common::kCrcMultiplier;
	}

	return checksum;
}

void Alignments::Write(std::ostream& rStream) const
{
	int64_t iCount = static_cast<int64_t>(alignmentPairs.size());
	common::Write(rStream, iCount);

	for (const AlignmentPair& rPair : alignmentPairs)
	{
		common::Write(rStream, rPair.uiKey);
		common::Write(rStream, rPair.uiFlags);
	}
}

void Alignments::Read(std::istream& rStream)
{
	int64_t iCount = 0;
	common::Read(rStream, iCount);
	// Trust boundary (save / network full-state): bound the count against the stream before resize.
	common::ValidateDeserializedCount(iCount, sizeof(AlignmentPair::uiKey) + sizeof(AlignmentPair::uiFlags), rStream, "Alignments::Read");

	alignmentPairs.resize(iCount);
	for (int64_t i = 0; i < iCount; ++i)
	{
		common::Read(rStream, alignmentPairs.at(i).uiKey);
		common::Read(rStream, alignmentPairs.at(i).uiFlags);
	}
}

} // namespace engine
