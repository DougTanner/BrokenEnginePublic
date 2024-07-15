#pragma once

#include "Profile/ProfileManager.h"
#include "ObjectPool.h"

namespace engine
{

template<typename T, int64_t CONTROLLER_LERP_SIZE>
struct ObjectControllerInfo
{
	bool bDestroysSelf = false;
	float pfTimes[CONTROLLER_LERP_SIZE];
	T pObjectInfos[CONTROLLER_LERP_SIZE] {};

	bool operator==(const ObjectControllerInfo<T, CONTROLLER_LERP_SIZE>& rOther) const = default;
};

template<typename V>
struct ObjectController
{
	float fStartTime = 0.0f;
	V objectIndex {};

	bool operator==(const ObjectController<V>& rOther) const = default;
};

template <typename T>
concept Lerpable = requires(T t, T u, float f) { { T::Lerp(t, u, f) } -> std::convertible_to<T>; };

template<Lerpable POOL_T, typename POOL_U, typename POOL_V, POOL_V POOL_SIZE,
         uint64_t CONTROLLER_LERP_SIZE, typename CONTROLLER_V, CONTROLLER_V CONTROLLER_SIZE>
struct ObjectControllerPool : public ObjectPool<ObjectControllerInfo<POOL_T, CONTROLLER_LERP_SIZE>, ObjectController<POOL_V>, CONTROLLER_V, CONTROLLER_SIZE>
{
	static_assert(CONTROLLER_SIZE <= std::numeric_limits<CONTROLLER_V>::max());

	using CONTROLLER_T = ObjectControllerInfo<POOL_T, CONTROLLER_LERP_SIZE>;
	using CONTROLLER_U = ObjectController<POOL_V>;
	using CONTROLLER_POOL = ObjectPool<CONTROLLER_T, CONTROLLER_U, CONTROLLER_V, CONTROLLER_SIZE>;

	using OBJECT_POOL = ObjectPool<POOL_T, POOL_U, POOL_V, POOL_SIZE>;

	void Add(OBJECT_POOL& __restrict rPool, CONTROLLER_V& __restrict ruiControllerIndex, float fCurrentTime, const CONTROLLER_T& __restrict rControllerInfo)
	{
		bool bNew = ruiControllerIndex == 0;

		CONTROLLER_POOL::Add(ruiControllerIndex, rControllerInfo);

		if (bNew && ruiControllerIndex != 0)
		{
			CONTROLLER_U& rController = CONTROLLER_POOL::Get(ruiControllerIndex);
			rController.fStartTime = fCurrentTime;
			rPool.Add(rController.objectIndex, rControllerInfo.pObjectInfos[0]);
		}
	}

	void Add(OBJECT_POOL& __restrict rPool, float fCurrentTime, const CONTROLLER_T& __restrict rControllerInfo)
	{
		ASSERT(rControllerInfo.bDestroysSelf);
		CONTROLLER_V uiControllerIndex = 0;
		Add(rPool, uiControllerIndex, fCurrentTime, rControllerInfo);
	}

	void Remove(OBJECT_POOL& __restrict rPool, CONTROLLER_V& __restrict ruiController)
	{
		if (ruiController == 0)
		{
			return;
		}

		CONTROLLER_U& rController = CONTROLLER_POOL::Get(ruiController);
		rPool.Remove(rController.objectIndex);
		CONTROLLER_POOL::Remove(ruiController);
	}

	static float EndTime(CONTROLLER_POOL& __restrict rCurrent, CONTROLLER_V uiController)
	{
		const CONTROLLER_T& rControllerInfo = rCurrent.GetInfo(uiController);
		const CONTROLLER_U& rController = rCurrent.Get(uiController);
		return rController.fStartTime + rControllerInfo.pfTimes[CONTROLLER_LERP_SIZE - 1];
	}

	static void UpdateMain(CONTROLLER_POOL& __restrict rCurrent, const CONTROLLER_POOL& __restrict rPrevious, OBJECT_POOL& __restrict rPool, float fCurrentTime)
	{
		SCOPED_CPU_PROFILE(kCpuTimerControllers);

		CONTROLLER_POOL::Copy(rCurrent, rPrevious);

		int64_t iCount = 0;
		for (CONTROLLER_V i = 0; i <= rCurrent.uiMaxIndex; ++i)
		{
			if (!rCurrent.pbUsed[i])
			{
				continue;
			}

			CONTROLLER_T& rControllerInfo = rCurrent.pObjectInfos[i];
			CONTROLLER_U& rController = rCurrent.pObjects[i];
			
			++iCount;

			float fEndTime = EndTime(rCurrent, i);
			if (rControllerInfo.bDestroysSelf && fCurrentTime > fEndTime)
			{
				--iCount;

				rPool.Remove(rController.objectIndex);

				CONTROLLER_V uiController = i;
				rCurrent.Remove(uiController);

				continue;
			}

			if (rController.objectIndex == 0)
			{
				continue;
			}

			POOL_T& rObjectInfo = rPool.GetInfo(rController.objectIndex);
			for (int64_t j = 1; j < CONTROLLER_LERP_SIZE; ++j)
			{
				float fTime = rController.fStartTime + rControllerInfo.pfTimes[j];
				if (fCurrentTime < fTime)
				{
					float fPreviousTime = rController.fStartTime + rControllerInfo.pfTimes[j - 1];
					float fPercent = std::clamp((fCurrentTime - fPreviousTime) / (fTime - fPreviousTime), 0.0f, 1.0f);
					rObjectInfo = POOL_T::Lerp(rControllerInfo.pObjectInfos[j - 1], rControllerInfo.pObjectInfos[j], fPercent);
					break;
				}
			}
		}
		PROFILE_SET_COUNT(kCpuCounterControllers, iCount);
	}
};

} // namespace engine
