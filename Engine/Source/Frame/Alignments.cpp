#include "Pch.h"

#include "Alignments.h"

namespace engine
{

uint64_t Alignments::MakeAlignmentKey(alignment_t idA, alignment_t idB)
{
	uint32_t keyA = idA.Value();
	uint32_t keyB = idB.Value();
	if (keyA > keyB)
	{
		std::swap(keyA, keyB);
	}
	return (static_cast<uint64_t>(keyA) << 32) | keyB;
}

void Alignments::AddAlignment(alignment_t idA, alignment_t idB, uint8_t flags)
{
	alignmentPairs[MakeAlignmentKey(idA, idB)] = flags;
}

void Alignments::RemoveAlignment(alignment_t idA, alignment_t idB)
{
	alignmentPairs.erase(MakeAlignmentKey(idA, idB));
}

bool Alignments::CanCollide(alignment_t idA, alignment_t idB) const
{
	auto it = alignmentPairs.find(MakeAlignmentKey(idA, idB));
	if (it == alignmentPairs.end())
	{
		return false;
	}
	return (it->second & AlignmentFlags::kEnemies) != 0;
}

bool Alignments::operator==(const Alignments& rOther) const
{
	return common::BreakOnNotEqual(alignmentPairs, rOther.alignmentPairs);
}

common::crc_t Alignments::Crc() const
{
	common::crc_t checksum = 0;

	// CRC alignmentPairs in deterministic order
	std::vector<uint64_t> keys;
	keys.reserve(alignmentPairs.size());
	for (const std::pair<const uint64_t, uint8_t>& rPair : alignmentPairs)
	{
		keys.push_back(rPair.first);
	}
	std::sort(keys.begin(), keys.end());

	for (uint64_t key : keys)
	{
		checksum ^= common::Crc(key);
		checksum ^= common::Crc(alignmentPairs.at(key));
	}

	return checksum;
}

void Alignments::Write(std::ostream& rStream) const
{
	int64_t iCount = static_cast<int64_t>(alignmentPairs.size());
	common::Write(rStream, iCount);

	// Write in sorted order for determinism
	std::vector<uint64_t> keys;
	keys.reserve(alignmentPairs.size());
	for (const std::pair<const uint64_t, uint8_t>& rPair : alignmentPairs)
	{
		keys.push_back(rPair.first);
	}
	std::sort(keys.begin(), keys.end());

	for (uint64_t key : keys)
	{
		common::Write(rStream, key);
		common::Write(rStream, alignmentPairs.at(key));
	}
}

void Alignments::Read(std::istream& rStream)
{
	int64_t iCount = 0;
	common::Read(rStream, iCount);

	alignmentPairs.clear();
	for (int64_t i = 0; i < iCount; ++i)
	{
		uint64_t key = 0;
		uint8_t flags = 0;
		common::Read(rStream, key);
		common::Read(rStream, flags);
		alignmentPairs[key] = flags;
	}
}

} // namespace engine
