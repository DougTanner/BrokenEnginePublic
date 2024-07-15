#pragma once

#include "Frame/Pools/PoolConfig.h"

namespace engine
{

struct ExplosionInfo;
struct PointLightInfo;
struct PuffInfo;
struct TrailInfo;

template<typename T, int64_t CONTROLLER_COUNT>
struct ObjectControllerInfo;

inline std::atomic<int64_t> giMultithreading = 0;

// Objects in a pool generally don't destroy themselves and the responsibility is on the owner to Remove() them
template<typename T, typename U, typename V, V POOL_SIZE>
struct alignas(64) ObjectPool
{
	// Needs to be below max() because iterator can point to element beyond the last
	static_assert(POOL_SIZE < std::numeric_limits<V>::max());

	using POOL = ObjectPool<T, U, V, POOL_SIZE>;

	alignas(64) bool pbUsed[POOL_SIZE + 1] {};
	alignas(64) T pObjectInfos[POOL_SIZE + 1] {};
	alignas(64) U pObjects[POOL_SIZE + 1] {};

	V uiMaxIndex = 0;

	static void Copy(POOL& __restrict rCurrent, const POOL& __restrict rPrevious)
	{
		rCurrent.uiMaxIndex = rPrevious.uiMaxIndex;
		memcpy(&rCurrent.pbUsed[0], &rPrevious.pbUsed[0], sizeof(rCurrent.pbUsed));
		memcpy(&rCurrent.pObjectInfos[0], &rPrevious.pObjectInfos[0], (rCurrent.uiMaxIndex + 1) * sizeof(T));
		memcpy(&rCurrent.pObjects[0], &rPrevious.pObjects[0], (rCurrent.uiMaxIndex + 1) * sizeof(U));
	}

	bool operator==(const POOL& rOther) const
	{
		bool bEqual = true;

		bEqual &= uiMaxIndex == rOther.uiMaxIndex;
		common::BreakOnNotEqual(bEqual);

		for (decltype(uiMaxIndex) i = 0; i <= uiMaxIndex; ++i)
		{
			bEqual &= pbUsed[i] == rOther.pbUsed[i];
			common::BreakOnNotEqual(bEqual);

			if (!pbUsed[i])
			{
				continue;
			}

			bEqual &= pObjectInfos[i] == rOther.pObjectInfos[i];
			common::BreakOnNotEqual(bEqual);
			bEqual &= pObjects[i] == rOther.pObjects[i];
			common::BreakOnNotEqual(bEqual);
		}

		return bEqual;
	}

	T& GetInfo(V uiIndex)
	{
		ASSERT(pbUsed[uiIndex]);
		return pObjectInfos[uiIndex];
	}

	const T& GetInfo(V uiIndex) const
	{
		ASSERT(pbUsed[uiIndex]);
		return pObjectInfos[uiIndex];
	}

	U& Get(V uiIndex)
	{
		ASSERT(pbUsed[uiIndex]);
		return pObjects[uiIndex];
	}

	const U& Get(V uiIndex) const
	{
		ASSERT(pbUsed[uiIndex]);
		return pObjects[uiIndex];
	}

	#pragma warning(push, 0)
	#pragma warning(disable : 6294) // https://developercommunity.visualstudio.com/t/Code-analysis-false-positive-warning-C62/759216
	void Add(V& __restrict ruiIndex, const T& __restrict rObjectInfo)
	{
		if (ruiIndex == 0 && uiMaxIndex < POOL_SIZE) [[unlikely]]
		{
			ASSERT(giMultithreading == 0);
			// Doing it this way would mess up the ==, if multithreaded pool access is absolutely necessay
			// need externally build up a list sorted by index of Multithread<> objects
			// auto lock = giMultithreading > 0 ? std::unique_lock<std::mutex>(Mutex()) : std::unique_lock<std::mutex>();

			for (V i = 1; i < POOL_SIZE + 1; ++i)
			{
				if (pbUsed[i] == true)
				{
					continue;
				}

				uiMaxIndex = std::max(uiMaxIndex, i);
				pbUsed[i] = true;
				ruiIndex = i;
				pObjects[ruiIndex] = {};
				break;
			}
		}

		if (ruiIndex == 0) [[unlikely]]
		{
			DEBUG_BREAK();
		}

		pObjectInfos[ruiIndex] = rObjectInfo;

		return;
	}
	#pragma warning(pop)

	void Remove(V& ruiIndex)
	{
		if (ruiIndex == 0)
		{
			return;
		}

		ASSERT(giMultithreading == 0);
		// auto lock = giMultithreading > 0 ? std::unique_lock<std::mutex>(Mutex()) : std::unique_lock<std::mutex>();

		pbUsed[ruiIndex] = false;
		if (ruiIndex == uiMaxIndex)
		{
			uiMaxIndex = 0;
			for (V i = {ruiIndex - 1u}; i > 0; --i)
			{
				if (pbUsed[i])
				{
					uiMaxIndex = i;
					break;
				}
			}
		}

		ruiIndex = 0;
	}

	std::mutex& Mutex()
	{
		if constexpr (std::is_same_v<T, ExplosionInfo>)
		{
			static std::mutex sMutex;
			return sMutex;
		}
		else if constexpr (std::is_same_v<T, PointLightInfo>)
		{
			static std::mutex sMutex;
			return sMutex;
		}
		else if constexpr (std::is_same_v<T, ObjectControllerInfo<PointLightInfo, 2>>)
		{
			static std::mutex sMutex;
			return sMutex;
		}
		else if constexpr (std::is_same_v<T, PuffInfo>)
		{
			static std::mutex sMutex;
			return sMutex;
		}
		else if constexpr (std::is_same_v<T, ObjectControllerInfo<PuffInfo, 2>> || std::is_same_v<T, ObjectControllerInfo<PuffInfo, 3>>)
		{
			static std::mutex sMutex;
			return sMutex;
		}
		else if constexpr (std::is_same_v<T, TrailInfo>)
		{
			static std::mutex sMutex;
			return sMutex;
		}
		else
		{
			DEBUG_BREAK();
			static std::mutex sMutex;
			return sMutex;
		}
	}

	/* Convinient but slow
	template<typename ITERATOR_POOL>
	class Iterator
	{
	public:

		Iterator(V uiIndex, bool bBegin, ITERATOR_POOL pObjectPool)
		: muiIndex(uiIndex)
		, mbBegin(bBegin)
		, mpObjectPool(pObjectPool)
		{
		}

		Iterator operator++()
		{
			do
			{
				++muiIndex;
			}
			while (!mpObjectPool->pbUsed[muiIndex] && muiIndex < mpObjectPool->uiMaxIndex);

			// This can happen if the last element is removed while in a range-based for()
			if (muiIndex > mpObjectPool->uiMaxIndex)
			{
				muiIndex = mpObjectPool->uiMaxIndex + 1;
			}

			return *this;
		}

		V GetIndex() const
		{
			return mbBegin ? muiIndex : mpObjectPool->uiMaxIndex + 1;
		}

		bool operator!=(const Iterator& other) const
		{
			return GetIndex() != other.GetIndex();
		}

		auto operator*()
		{
			return std::make_tuple(muiIndex, std::ref(mpObjectPool->pObjectInfos[muiIndex]), std::ref(mpObjectPool->pObjects[muiIndex]));
		}

		auto operator*() const
		{
			return std::make_tuple(muiIndex, std::cref(mpObjectPool->pObjectInfos[muiIndex]), std::cref(mpObjectPool->pObjects[muiIndex]));
		}

	private:

		V muiIndex = 0;
		bool mbBegin = true;
		ITERATOR_POOL mpObjectPool = nullptr;
	};
	

	V Begin() const
	{
		V uiIndex = 0;

		do
		{
			++uiIndex;
		}
		while (!pbUsed[uiIndex] && uiIndex < uiMaxIndex);

		return uiIndex;
	}

	V End() const
	{
		return uiMaxIndex + 1;
	}

	auto begin()
	{
		return Iterator<ObjectPool*>(Begin(), true, this);
	}

	auto end()
	{
		return Iterator<ObjectPool*>(End(), false, this);
	}

	auto begin() const
	{
		return Iterator<const ObjectPool*>(Begin(), true, this);
	}

	auto end() const
	{
		return Iterator<const ObjectPool*>(End(), false, this);
	}
	*/
};


} // namespace engine
