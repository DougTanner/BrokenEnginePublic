#include "Pch.h"

#include "Alignment.h"

#include "Frame/FrameBase.h"

namespace engine
{

alignment_t Alignment::Add(FramePostRenderBase& rFrame)
{
	// Generate unique ID
	alignment_t newId = alignment_t::Generate(rFrame);

	int64_t iNewIndex = iCount;
	int64_t iNewCount = iCount + 1;

	// Create new matrix with expanded size
	std::vector<uint8_t> newMatrix(static_cast<size_t>(iNewCount * iNewCount), 1);

	// Copy old data with offset adjustment
	for (int64_t row = 0; row < iCount; ++row)
	{
		for (int64_t col = 0; col < iCount; ++col)
		{
			newMatrix[static_cast<size_t>(row * iNewCount + col)] = matrixData[static_cast<size_t>(row * iCount + col)];
		}
	}

	// New row/column defaults to true (collides with all)
	// Already set by initialization above

	matrixData = std::move(newMatrix);
	idToIndexMap[newId] = iNewIndex;
	indexToIdMap.push_back(newId);
	iCount = iNewCount;

	return newId;
}

void Alignment::Remove(alignment_t id)
{
	auto it = idToIndexMap.find(id);
	if (it == idToIndexMap.end())
	{
		return;
	}

	int64_t iRemovedIndex = it->second;
	idToIndexMap.erase(it);

	Compact(iRemovedIndex);
}

void Alignment::Compact(int64_t iRemovedIndex)
{
	int64_t iNewCount = iCount - 1;

	if (iNewCount == 0)
	{
		matrixData.clear();
		indexToIdMap.clear();
		iCount = 0;
		return;
	}

	// Create new matrix without removed row/column
	std::vector<uint8_t> newMatrix(static_cast<size_t>(iNewCount * iNewCount));

	int64_t iNewRow = 0;
	for (int64_t row = 0; row < iCount; ++row)
	{
		if (row == iRemovedIndex)
		{
			continue;
		}

		int64_t iNewCol = 0;
		for (int64_t col = 0; col < iCount; ++col)
		{
			if (col == iRemovedIndex)
			{
				continue;
			}

			newMatrix[static_cast<size_t>(iNewRow * iNewCount + iNewCol)] = matrixData[static_cast<size_t>(row * iCount + col)];
			++iNewCol;
		}
		++iNewRow;
	}

	matrixData = std::move(newMatrix);

	// Update indexToIdMap - remove the entry and shift indices
	indexToIdMap.erase(indexToIdMap.begin() + iRemovedIndex);

	// Update idToIndexMap - decrement indices greater than removed
	for (auto& [rGroupId, rIndex] : idToIndexMap)
	{
		if (rIndex > iRemovedIndex)
		{
			--rIndex;
		}
	}

	iCount = iNewCount;
}

void Alignment::SetCanCollide(alignment_t idA, alignment_t idB, bool bCanCollide)
{
	auto itA = idToIndexMap.find(idA);
	auto itB = idToIndexMap.find(idB);

	if (itA == idToIndexMap.end() || itB == idToIndexMap.end())
	{
		return;
	}

	int64_t iIndexA = itA->second;
	int64_t iIndexB = itB->second;

	// Matrix is symmetric
	matrixData[static_cast<size_t>(iIndexA * iCount + iIndexB)] = bCanCollide;
	matrixData[static_cast<size_t>(iIndexB * iCount + iIndexA)] = bCanCollide;
}

bool Alignment::CanCollide(alignment_t idA, alignment_t idB) const
{
	// Invalid IDs collide with everything
	if (!idA.IsValid() || !idB.IsValid())
	{
		return true;
	}

	auto itA = idToIndexMap.find(idA);
	auto itB = idToIndexMap.find(idB);

	// Unknown IDs collide with everything
	if (itA == idToIndexMap.end() || itB == idToIndexMap.end())
	{
		return true;
	}

	int64_t iIndexA = itA->second;
	int64_t iIndexB = itB->second;

	return matrixData[static_cast<size_t>(iIndexA * iCount + iIndexB)];
}

bool Alignment::operator==(const Alignment& rOther) const
{
	bool bEqual = true;

	bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);
	bEqual &= common::BreakOnNotEqual(matrixData, rOther.matrixData);

	// Compare ID mappings
	if (idToIndexMap.size() != rOther.idToIndexMap.size())
	{
		bEqual = false;
	}
	else
	{
		for (const auto& [rId, rIndex] : idToIndexMap)
		{
			auto it = rOther.idToIndexMap.find(rId);
			if (it == rOther.idToIndexMap.end() || it->second != rIndex)
			{
				bEqual = false;
				break;
			}
		}
	}

	return bEqual;
}

common::crc_t Alignment::Crc() const
{
	common::crc_t checksum = 0;

	checksum ^= common::Crc(iCount);

	// Batch CRC the matrix data
	checksum ^= common::Crc(matrixData.data(), static_cast<int64_t>(matrixData.size()));

	// CRC the ID mappings (in index order for determinism)
	for (const alignment_t& id : indexToIdMap)
	{
		checksum ^= common::Crc(id);
	}

	return checksum;
}

void Alignment::Write(std::ostream& rStream) const
{
	common::Write(rStream, iCount);

	// Batch write matrix data
	rStream.write(reinterpret_cast<const char*>(matrixData.data()), static_cast<std::streamsize>(matrixData.size()));

	// Write ID mappings (in index order)
	for (const alignment_t& id : indexToIdMap)
	{
		id.Write(rStream);
	}
}

void Alignment::Read(std::istream& rStream)
{
	common::Read(rStream, iCount);

	// Batch read matrix data
	int64_t iMatrixSize = iCount * iCount;
	matrixData.resize(static_cast<size_t>(iMatrixSize));
	rStream.read(reinterpret_cast<char*>(matrixData.data()), static_cast<std::streamsize>(iMatrixSize));

	// Read ID mappings
	indexToIdMap.resize(static_cast<size_t>(iCount));
	idToIndexMap.clear();
	for (int64_t i = 0; i < iCount; ++i)
	{
		alignment_t id;
		id.Read(rStream);
		indexToIdMap[static_cast<size_t>(i)] = id;
		idToIndexMap[id] = i;
	}
}

} // namespace engine
