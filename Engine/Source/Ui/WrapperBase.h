#pragma once

#if defined(BT_CLIENT)
#include "CurveData.h"
#endif

namespace engine
{

class Wrapper
{
public:

	Wrapper() = delete;

	explicit Wrapper(float fValue, float fMin, float fMax, float fStep = 0.0f)
	: mfStep(fStep)
	, mfDefault(Snap(fValue, fStep))
	, mfMin(Snap(fMin, fStep))
	, mfMax(Snap(fMax, fStep))
	, mfCurrent(mfDefault)
	, mfPrevious(mfCurrent)
	{
		ASSERT(fStep >= 0.0f);
		ASSERT(mfMin != mfMax);
		ASSERT(mfDefault >= mfMin && mfDefault <= mfMax);
	}

	explicit Wrapper(bool bValue)
	: mfDefault(bValue ? 1.0f : 0.0f)
	, mfMin(0.0f)
	, mfMax(1.0f)
	, mfCurrent(mfDefault)
	, mfPrevious(mfCurrent)
	{
	}

	template <typename T>
	Wrapper(T value, const std::vector<T>& rAllowedValues)
	: mfDefault(static_cast<float>(value))
	, mfMin(0.0f)
	, mfMax(1.0f)
	, mfCurrent(mfDefault)
	, mfPrevious(mfCurrent)
	{
		mAllowed.reserve(rAllowedValues.size());
		for (const T& rValue : rAllowedValues)
		{
			mAllowed.push_back(static_cast<float>(rValue));
			mfMax = std::max(static_cast<float>(rValue), mfMax);
		}

		ASSERT(mfMin != mfMax);
	}

	~Wrapper() = default;

	template <typename T>
	std::tuple<T, T, bool> Changed()
	{
		std::tuple<T, T, bool> values = std::make_tuple(static_cast<T>(mfCurrent), static_cast<T>(mfPrevious), mfPrevious != mfCurrent);
		mfPrevious = mfCurrent;
		return values;
	}

	void Toggle()
	{
		mfCurrent = mfCurrent == 0.0f ? 1.0f : 0.0f;
	}

	float Get() const
	{
		return mfCurrent;
	}

	float GetMin() const
	{
		return mfMin;
	}

	float GetMax() const
	{
		return mfMax;
	}

	float GetDefault() const
	{
		return mfDefault;
	}

	template <typename T>
	T Get() const
	{
		static_assert(!std::is_same_v<T, float>);

		if constexpr (std::is_same_v<T, bool>)
		{
			return mfCurrent == 1.0f;
		}
		else
		{
			return static_cast<T>(mfCurrent);
		}
	}

	template <typename T>
	T GetDefault() const
	{
		static_assert(!std::is_same_v<T, float>);

		if constexpr (std::is_same_v<T, bool>)
		{
			return mfDefault == 1.0f;
		}
		else
		{
			return static_cast<T>(mfDefault);
		}
	}

	void Set(float fValue)
	{
		mfCurrent = std::clamp(Snap(fValue, mfStep), mfMin, mfMax);
	}

	void Set(bool bValue)
	{
		mfCurrent = bValue ? 1.0f : 0.0f;
	}

	template <typename T>
	void Set(T value)
	{
		static_assert(!std::is_same_v<T, float> && !std::is_same_v<T, bool>);

		mfCurrent = static_cast<float>(value);
		GetIndex();
	}

	template <typename T>
	void operator=(T value) = delete;

	void Reset(float fValue)
	{
		mfCurrent = mfPrevious = Snap(fValue, mfStep);
	}

	template <typename T>
	void Reset(T value)
	{
		static_assert(!std::is_same_v<T, float> && !std::is_same_v<T, bool>);

		mfCurrent = mfPrevious = static_cast<float>(value);
		GetIndex();
	}

	void ResetToDefault()
	{
		mfCurrent = mfDefault;
	}

	float Percent() const
	{
		return (Get() - mfMin) / (mfMax - mfMin);
	}

	void SetPercent(float fPercent)
	{
		Set(mfMin + fPercent * (mfMax - mfMin));
	}

	int64_t GetIndex() const
	{
		int64_t iIndex = 0;
		for (const float& rfValue : mAllowed)
		{
			if (Get() == rfValue)
			{
				return iIndex;
			}

			++iIndex;
		}

		DEBUG_BREAK();
		return 0;
	}

	void SetIndex(int64_t iIndex)
	{
		Set(mAllowed.at(iIndex));
	}

private:

	static float Snap(float fValue, float fStep)
	{
		return fStep > 0.0f ? std::round(fValue / fStep) * fStep : fValue;
	}

	float mfStep = 0.0f;
	float mfDefault = 0.0f;
	float mfMin = 0.0f;
	float mfMax = 1.0f;

	float mfCurrent = 0.0f;
	float mfPrevious = 0.0f;
	std::vector<float> mAllowed;
};

// Internal-only wrappers (not bound to any UI: not Tweaks, not GraphicsMenuScreen, not SoundMenuScreen).
extern Wrapper gFov;
extern Wrapper gWireframe;
extern Wrapper gBaseHeight;

// Islands & terrain
extern Wrapper gVisibleAreaExtraTop;
extern Wrapper gVisibleAreaExtraBottom;
extern Wrapper gTerrainEarlyOut;
extern Wrapper gTerrainElevationTextureMultiplier;

// Smoke
extern Wrapper gSmokeTrailPower;
extern Wrapper gSmokeTrailAlpha;

// Particles

// Debug
extern Wrapper gDebugTexture;
extern Wrapper gDebugTextureIndex;

} // namespace engine
