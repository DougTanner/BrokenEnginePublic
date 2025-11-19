#pragma once

#include "Frame/Pools/PoolConfig.h"

namespace engine
{

using id_t = int64_t;

template <typename T>
void AssignAligned(T*& rpElements, int64_t iCapacity, std::byte*& rpCurrent)
{
	rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));
	rpElements = reinterpret_cast<T*>(rpCurrent);
	rpCurrent += iCapacity * sizeof(rpElements[0]);
}

template <typename T>
void AssignAndCopyAligned(T*& rpElements, int64_t iCapacity, int64_t iCount, std::byte*& rpCurrent)
{
	rpCurrent = reinterpret_cast<std::byte*>(common::RoundUp<uintptr_t, 64>(reinterpret_cast<uintptr_t>(rpCurrent)));
	if (rpElements != nullptr)
	{
		memcpy(rpCurrent, rpElements, iCount * sizeof(rpElements[0]));
	}
	rpElements = reinterpret_cast<T*>(rpCurrent);
	rpCurrent += iCapacity * sizeof(rpElements[0]);
}

template <typename TStruct, typename... TMemberPtrRefs>
void AllocateAndAssign(TStruct& rStruct, int64_t iCapacity, TMemberPtrRefs&... memberPtrRefs)
{
	int64_t iBufferSize = 0;
	((iBufferSize += common::RoundUp<int64_t, 64>(iCapacity * sizeof(memberPtrRefs[0]))), ...);

	rStruct.iCapacity = iCapacity;
	rStruct.pData = common::MakeAligned<std::byte>(iBufferSize);

	std::byte* pCurrent = rStruct.pData.get();
	(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);
}

template <typename TStruct, typename... TMemberPtrRefs>
void ResetDataToNull(TStruct& rStruct, TMemberPtrRefs&... memberPtrRefs)
{
	rStruct.pData.reset();
	rStruct.iCapacity = 0;
	((memberPtrRefs = nullptr), ...);
}

template <typename TStruct, typename... TMemberPtrRefs>
bool ReallocateIfCapacityChanged(TStruct& rCurrent, const TStruct& rPrevious, TMemberPtrRefs&... memberPtrRefs)
{
	rCurrent.iCount = rPrevious.iCount;

	if (rPrevious.pData == nullptr)
	{
		ResetDataToNull(rCurrent, memberPtrRefs...);
		return false;
	}

	const int64_t iCapacity = rPrevious.iCapacity;
	if (rCurrent.iCapacity != iCapacity)
	{
		int64_t iBufferSize = 0;
		((iBufferSize += common::RoundUp<int64_t, 64>(iCapacity * sizeof(memberPtrRefs[0]))), ...);

		rCurrent.iCapacity = iCapacity;
		rCurrent.pData = common::MakeAligned<std::byte>(iBufferSize);

		std::byte* pCurrent = rCurrent.pData.get();
		(AssignAligned(memberPtrRefs, iCapacity, pCurrent), ...);

		ASSERT(rCurrent.iCount <= rCurrent.iCapacity);
	}

	return true;
}

template <typename TStruct, typename... TMemberPtrRefs>
void GrowCapacityWithCopy(TStruct& rStruct, int64_t iNewCapacity, int64_t iCurrentCount, TMemberPtrRefs&... memberPtrRefs)
{
	int64_t iBufferSize = 0;
	((iBufferSize += common::RoundUp<int64_t, 64>(iNewCapacity * sizeof(memberPtrRefs[0]))), ...);

	common::AlignedUniquePtr<std::byte> pNewData = common::MakeAligned<std::byte>(iBufferSize);
	std::byte* pCurrent = pNewData.get();
	(AssignAndCopyAligned(memberPtrRefs, iNewCapacity, iCurrentCount, pCurrent), ...);
	rStruct.pData = std::move(pNewData);
	rStruct.iCapacity = iNewCapacity;
}

// Swaps element at index i with the last element (at index rStruct.iCount - 1)
// Caller is responsible for decrementing count and re-checking index if needed
template <typename TStruct, typename... TMemberPtrRefs>
void SwapElement(TStruct& rStruct, int64_t i, TMemberPtrRefs&... memberPtrRefs)
{
	((memberPtrRefs[i] = memberPtrRefs[rStruct.iCount - 1]), ...);
}

template <typename... TMemberPtrRefs>
common::crc_t MultiCrc(int64_t iCount, TMemberPtrRefs... memberPtrRefs)
{
	common::crc_t checksum = 0;
	if (iCount > 0)
	{
		((checksum ^= common::Crc(memberPtrRefs, iCount)), ...);
	}
	return checksum;
}

template <typename... TMemberPtrRefs>
void MultiWrite(std::ostream& rStream, int64_t iCount, TMemberPtrRefs... memberPtrRefs)
{
	((common::Write(rStream, memberPtrRefs, iCount)), ...);
}

template <typename... TMemberPtrRefs>
void MultiRead(std::istream& rStream, int64_t iCount, TMemberPtrRefs... memberPtrRefs)
{
	((common::Read(rStream, memberPtrRefs, iCount)), ...);
}

template <typename TStruct, typename... TMemberPtrRefs>
void AllocateAndRead(TStruct& rStruct, std::istream& rStream, TMemberPtrRefs&... memberPtrRefs)
{
	if (rStruct.iCapacity > 0)
	{
		AllocateAndAssign(rStruct, rStruct.iCapacity, memberPtrRefs...);
	}
	else
	{
		rStruct.pData = nullptr;
	}

	MultiRead(rStream, rStruct.iCount, memberPtrRefs...);
}

} // namespace engine

#if 0

namespace game
{

struct Frame;
struct FrameInput;

} // namespace game

namespace engine
{

// DT: TODO Add 'destroyable'?

template<typename T, int64_t SIZE>
struct Spawnable
{
	// Interpolate
	T pSpawns[SIZE] {};
	int64_t iSpawnCount = 0;

	static void Interpolate(Spawnable& __restrict rCurrent, const Spawnable& __restrict rPrevious)
	{
		rCurrent.iSpawnCount = rPrevious.iSpawnCount;
		memcpy(&rCurrent.pSpawns[0], &rPrevious.pSpawns[0], rCurrent.iSpawnCount * sizeof(T));
	}

	void AddSpawn(const T& rSpawn)
	{
		if (iSpawnCount == SIZE)
		{
			DEBUG_BREAK();
			return;
		}

		pSpawns[iSpawnCount++] = rSpawn;
	}

	bool operator==(const Spawnable& rOther) const
	{
		bool bEqual = common::BreakOnNotEqual(iSpawnCount, rOther.iSpawnCount);

		for (int64_t i = 0; i < iSpawnCount; ++i)
		{
			bEqual &= common::BreakOnNotEqual(pSpawns[i], rOther.pSpawns[i]);
		}

		return bEqual;
	}
};

} // namespace engine

#endif
