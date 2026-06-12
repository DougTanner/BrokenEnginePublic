#pragma once

namespace common
{

class Workbuffer;
class ScopedWorkbufferArena;
template<typename T> class ScopedWorkbufferAllocation;

class Workbuffer
{
public:

	explicit Workbuffer(std::vector<std::byte>& rBuffer)
	: mBuffer(rBuffer)
	{
		mSavedBase.resize(64); // Pre-sized for the deepest arena nesting source structure ever reaches (see RawPush).
	}

	// Non-copyable/non-movable: mBuffer is a reference member, so a copy would bind into the source's storage.
	Workbuffer(const Workbuffer&) = delete;
	Workbuffer& operator=(const Workbuffer&) = delete;
	Workbuffer(Workbuffer&&) = delete;
	Workbuffer& operator=(Workbuffer&&) = delete;

	// Scope marker. Returns an RAII handle that pops the frame on scope exit.
	[[nodiscard]] ScopedWorkbufferArena Push();

	// Typed reservation. Opens a frame and reserves iSizeInBytes; returns an RAII handle
	// carrying the typed pointer (implicit-convertible to T) that pops on scope exit.
	template<typename T>
	[[nodiscard]] ScopedWorkbufferAllocation<T> PushBuffer(int64_t iSizeInBytes);

	// String building. Append/View/Span are valid only within an open frame (Push()/PushBuffer()) — a read at
	// depth 0 (no open frame) is asserted, since miBase/miSize are frame-relative.
	void Append(std::string_view text);
	void Append(std::wstring_view text);
	void Append(int64_t iValue);
	void AppendFloat(float fValue, int iPrecision);
	std::string_view View() const;

	// Typed element append
	template<typename T>
	void PushBack(const T& rValue)
	{
		int64_t iNeeded = miSize + static_cast<int64_t>(sizeof(T));
		if (iNeeded > static_cast<int64_t>(mBuffer.size())) [[unlikely]]
		{
			Grow(iNeeded);
		}
		memcpy(mBuffer.data() + miSize, &rValue, sizeof(T));
		miSize += static_cast<int64_t>(sizeof(T));
	}

	// Shrinks the most recent PushBuffer reservation to iActualSize bytes.
	// Operates on the last PushBuffer call only — Push frames don't update the tracked size.
	void ShrinkLastPushBuffer(int64_t iActualSize)
	{
		// The tracked PushBuffer size must belong to the current top frame; a bare Push() opening a deeper frame
		// after the PushBuffer would otherwise corrupt the live frame's size accounting.
		ASSERT(miDepth == miLastPushBufferDepth);
		miSize -= (miLastPushBufferSize - iActualSize);
		miLastPushBufferSize = iActualSize;
	}

	template<typename T>
	std::span<const T> Span() const
	{
		ASSERT(miDepth > 0);
		return {reinterpret_cast<const T*>(mBuffer.data() + miBase), static_cast<size_t>(miSize - miBase) / sizeof(T)};
	}

	template<typename T>
	std::span<T> Span()
	{
		ASSERT(miDepth > 0);
		return {reinterpret_cast<T*>(mBuffer.data() + miBase), static_cast<size_t>(miSize - miBase) / sizeof(T)};
	}

private:

	void RawPush()
	{
		if (miDepth == static_cast<int64_t>(mSavedBase.size())) [[unlikely]]
		{
			// mSavedBase is pre-sized for the deepest arena nesting source structure ever reaches; exceeding it is
			// an under-sizing bug (DEBUG_BREAK alerts in debug). Growth still proceeds so gameplay never fails.
			DEBUG_BREAK();
			// Heap: under-sizing recovery growth, flagged by the DEBUG_BREAK above
			ScopedSuppressAllocationTracking suppress;
			mSavedBase.resize(mSavedBase.size() * 2);
		}
		mSavedBase[miDepth] = miBase;
		++miDepth;
		miBase = miSize;
	}

	template<typename T>
	T RawPushBuffer(int64_t iSizeInBytes)
	{
		if (miDepth == static_cast<int64_t>(mSavedBase.size())) [[unlikely]]
		{
			// See RawPush: pre-sized for max nesting; DEBUG_BREAK flags under-sizing, growth still succeeds.
			DEBUG_BREAK();
			// Heap: under-sizing recovery growth, flagged by the DEBUG_BREAK above
			ScopedSuppressAllocationTracking suppress;
			mSavedBase.resize(mSavedBase.size() * 2);
		}
		mSavedBase[miDepth] = miBase;
		++miDepth;
		miBase = miSize;
		int64_t iNeeded = miBase + iSizeInBytes;
		if (iNeeded > static_cast<int64_t>(mBuffer.size())) [[unlikely]]
		{
			Grow(iNeeded);
		}
		miSize = iNeeded;
		miLastPushBufferSize = iSizeInBytes;
		miLastPushBufferDepth = miDepth;
		void* pData = mBuffer.data() + miBase;
		return static_cast<T>(pData);
	}

	void Pop()
	{
		ASSERT(miDepth > 0);
		--miDepth;
		miSize = miBase;
		miBase = mSavedBase[miDepth];
	}

	void Grow(int64_t iNeededCapacity);

	std::vector<std::byte>& mBuffer;
	int64_t miSize = 0;
	int64_t miBase = 0;
	int64_t miDepth = 0;
	int64_t miLastPushBufferSize = 0;
	int64_t miLastPushBufferDepth = 0;
	std::vector<int64_t> mSavedBase;

	friend class ScopedWorkbufferArena;
	template<typename> friend class ScopedWorkbufferAllocation;
};

class [[nodiscard]] ScopedWorkbufferArena
{
public:

	explicit ScopedWorkbufferArena(Workbuffer& rBuffer)
	: mBuffer(rBuffer)
	{
		mBuffer.RawPush();
	}

	~ScopedWorkbufferArena() { mBuffer.Pop(); }

	ScopedWorkbufferArena(const ScopedWorkbufferArena&) = delete;
	ScopedWorkbufferArena& operator=(const ScopedWorkbufferArena&) = delete;

	void Append(std::string_view text)              { mBuffer.Append(text); }
	void Append(std::wstring_view text)             { mBuffer.Append(text); }
	void Append(int64_t iValue)                     { mBuffer.Append(iValue); }
	void AppendFloat(float fValue, int iPrecision)  { mBuffer.AppendFloat(fValue, iPrecision); }
	template<typename T> void PushBack(const T& rValue) { mBuffer.PushBack(rValue); }
	std::string_view View() const                   { return mBuffer.View(); }
	template<typename T> std::span<const T> Span() const { return mBuffer.Span<T>(); }
	template<typename T> std::span<T> Span()             { return mBuffer.Span<T>(); }
	void ShrinkLastPushBuffer(int64_t iActualSize)  { mBuffer.ShrinkLastPushBuffer(iActualSize); }

private:

	Workbuffer& mBuffer;
};

template<typename T>
class [[nodiscard]] ScopedWorkbufferAllocation
{
public:

	~ScopedWorkbufferAllocation()
	{
		if (mpBuffer != nullptr) [[likely]]
		{
			mpBuffer->Pop();
		}
	}

	ScopedWorkbufferAllocation(const ScopedWorkbufferAllocation&) = delete;
	ScopedWorkbufferAllocation& operator=(const ScopedWorkbufferAllocation&) = delete;
	ScopedWorkbufferAllocation& operator=(ScopedWorkbufferAllocation&&) = delete;

	// Move ctor: transfer frame ownership; source becomes inert.
	ScopedWorkbufferAllocation(ScopedWorkbufferAllocation&& rOther) noexcept
	: mpBuffer(rOther.mpBuffer)
	, mpData(rOther.mpData)
	{
		rOther.mpBuffer = nullptr;
	}

	// Reinterpret the pointer type while transferring frame ownership. Lets a function that
	// allocated a typed buffer return an allocation typed against a different pointer (e.g.,
	// `EnumToString::Convert` allocates a char* scratch but returns a const char* map pointer).
	template<typename U>
	[[nodiscard]] ScopedWorkbufferAllocation<U> Adopt(U pData) && noexcept
	{
		Workbuffer& rBuffer = *mpBuffer;
		mpBuffer = nullptr;
		return ScopedWorkbufferAllocation<U>(rBuffer, pData);
	}

	operator T() const { return mpData; }
	T operator->() const { return mpData; }

private:

	ScopedWorkbufferAllocation(Workbuffer& rBuffer, T pData)
	: mpBuffer(&rBuffer)
	, mpData(pData)
	{
	}

	Workbuffer* mpBuffer;
	T mpData;

	friend class Workbuffer;
	template<typename> friend class ScopedWorkbufferAllocation;
};

inline ScopedWorkbufferArena Workbuffer::Push()
{
	return ScopedWorkbufferArena(*this);
}

template<typename T>
ScopedWorkbufferAllocation<T> Workbuffer::PushBuffer(int64_t iSizeInBytes)
{
	return ScopedWorkbufferAllocation<T>(*this, RawPushBuffer<T>(iSizeInBytes));
}

// Per-argument workbuffer-aware float wrapper for use inside LOG(...).
// Each {} placeholder Push/Pops a workbuffer frame inside its formatter,
// so multiple Wb/WbV2 wrappers in one LOG call are safe.
struct Wb
{
	Wb(float fValue, int iPrecision)
	: fValue(fValue), iPrecision(iPrecision) {}

	float fValue;
	int   iPrecision;
};

// 2D XMVECTOR formatted as "(x,y)" with shared precision.
struct WbV2
{
	WbV2(DirectX::XMVECTOR vec, int iPrecision)
	: vec(vec), iPrecision(iPrecision) {}

	DirectX::XMVECTOR vec;
	int               iPrecision;
};

// 3D XMVECTOR formatted as "(x,y,z)" with shared precision.
struct WbV3
{
	WbV3(DirectX::XMVECTOR vec, int iPrecision)
	: vec(vec), iPrecision(iPrecision) {}

	DirectX::XMVECTOR vec;
	int               iPrecision;
};

// 4D XMVECTOR formatted as "(x,y,z,w)" with shared precision.
struct WbV4
{
	WbV4(DirectX::XMVECTOR vec, int iPrecision)
	: vec(vec), iPrecision(iPrecision) {}

	DirectX::XMVECTOR vec;
	int               iPrecision;
};

} // namespace common

template <>
struct std::formatter<common::ScopedWorkbufferArena> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::ScopedWorkbufferArena& rValue, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(rValue.View(), rContext);
	}
};

template <>
struct std::formatter<common::ScopedWorkbufferAllocation<const char*>> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::ScopedWorkbufferAllocation<const char*>& rValue, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(static_cast<const char*>(rValue), rContext);
	}
};
