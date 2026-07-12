#pragma once

namespace engine
{

// Float-backed container for a UI-bound setting (float / bool / discrete-enum flavors).
// Thread contract: no atomics. All writes (Set/Reset/ResetToDefault/Toggle/SetPercent/SetIndex) are main-thread
// operations. The writer sites span several subsystems — menu/Tweaks screens (ImGuiManager::Prepare), input
// handling, render-target maintenance, swapchain creation, texture-capability clamps, and settings load, among
// others — so this is not a single-writer contract. mfCurrent is unsynchronized, so the invariant that keeps reads
// safe is not "single writer" but "no write overlaps an active gpMultithreading->Dispatch() window": the main thread
// is blocked inside Dispatch for the tick, so a wrapper read from a worker thread there (e.g. gBaseHeight in
// NavQuery.cpp:465/580/600) cannot race a concurrent writer.
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

	// Bool wrappers only: writes mfCurrent raw, bypassing Snap(), Set(float)'s clamp, and the discrete-enum
	// allowed-set check (GetIndex()). Set(bool) below shares the same raw-write nature. Callers: gFullscreen,
	// gDebugTexture (both bool).
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

		// Intentional soft-fall: an off-grid value is legitimately reachable — a corrupt/hand-edited persisted
		// setting (ClientSettings load) or a device-capability clamp (gSampleCount/gPresentMode) — so this never
		// throws. Returns index 0; the sole return-consuming caller clamps it (TweaksScreenBase.cpp), and the
		// graphics path re-clamps sample-count/present-mode against device support. DEBUG_BREAK is a debug-only
		// hint for genuine internal misuse (no-op in release).
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

	// Every flavor (bool, discrete-enum, int64 index) round-trips through these floats. Exactness ceiling:
	// integer-backed values must stay below 2^24 or Get<T>() loses precision. Live caution: gPresentMode's allowed
	// set holds VK_PRESENT_MODE_FIFO_LATEST_READY_KHR (1000361000 -> 1000361024.0f) — safe only because
	// SwapchainManager.cpp:160-175 normalizes to FIFO/Mailbox/Immediate and Resets before any Get<VkPresentModeKHR>()
	// consumption (:245), and no UI nor the ClientSettings.cpp:125 load path selects it today.
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
extern Wrapper gTerrainElevationTextureMultiplier;

// Smoke
extern Wrapper gSmokeTrailPower;
extern Wrapper gSmokeTrailAlpha;

// Particles

// Debug
extern Wrapper gDebugTexture;
extern Wrapper gDebugTextureIndex;

} // namespace engine
