#pragma once

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInputHeld;

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
		bool bEqual = true;

		bEqual &= iSpawnCount == rOther.iSpawnCount;

		common::BreakOnNotEqual(bEqual);

		for (int64_t i = 0; i < iSpawnCount; ++i)
		{
			bEqual &= pSpawns[i] == rOther.pSpawns[i];

			common::BreakOnNotEqual(bEqual);
		}

		return bEqual;
	}
};

} // namespace engine
