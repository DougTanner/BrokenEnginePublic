#pragma once

namespace common
{

class Workbuffer
{
public:

	explicit Workbuffer(std::vector<std::byte>& rBuffer)
	: mBuffer(rBuffer)
	{
		mSavedBase.resize(8);
	}

	// Raw pointer access to the underlying buffer
	template<typename T>
	T PushBuffer(int64_t iSizeInBytes)
	{
		if (miDepth == static_cast<int64_t>(mSavedBase.size())) [[unlikely]]
		{
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
		void* pData = mBuffer.data() + miBase;
		return static_cast<T>(pData);
	}

	// Tracked-size operations
	void Push()
	{
		if (miDepth == static_cast<int64_t>(mSavedBase.size())) [[unlikely]]
		{
			mSavedBase.resize(mSavedBase.size() * 2);
		}
		mSavedBase[miDepth] = miBase;
		++miDepth;
		miBase = miSize;
	}

	void Pop()
	{
		ASSERT(miDepth > 0);
		--miDepth;
		miSize = miBase;
		miBase = mSavedBase[miDepth];
	}

	// String building
	void Append(std::string_view text);
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

	void ShrinkLastPushBuffer(int64_t iActualSize)
	{
		miSize -= (miLastPushBufferSize - iActualSize);
		miLastPushBufferSize = iActualSize;
	}

	template<typename T>
	std::span<const T> Span() const
	{
		return {reinterpret_cast<const T*>(mBuffer.data() + miBase), static_cast<size_t>(miSize - miBase) / sizeof(T)};
	}

private:

	void Grow(int64_t iNeededCapacity);

	std::vector<std::byte>& mBuffer;
	int64_t miSize = 0;
	int64_t miBase = 0;
	int64_t miDepth = 0;
	int64_t miLastPushBufferSize = 0;
	std::vector<int64_t> mSavedBase;
};

class ScopedWorkbufferPop
{
public:

	ScopedWorkbufferPop(Workbuffer& rWorkbuffer, const char* pcData)
	: mWorkbuffer(rWorkbuffer)
	, mpcData(pcData)
	{
	}

	~ScopedWorkbufferPop() { mWorkbuffer.Pop(); }

	ScopedWorkbufferPop(const ScopedWorkbufferPop&) = delete;
	ScopedWorkbufferPop& operator=(const ScopedWorkbufferPop&) = delete;

	operator const char*() const { return mpcData; }

private:

	Workbuffer& mWorkbuffer;
	const char* mpcData;
};

class ScopedWorkbufferBuilder
{
public:

	explicit ScopedWorkbufferBuilder(Workbuffer& rWorkbuffer)
	: mWorkbuffer(rWorkbuffer)
	{
		mWorkbuffer.Push();
	}

	~ScopedWorkbufferBuilder() { mWorkbuffer.Pop(); }

	ScopedWorkbufferBuilder(const ScopedWorkbufferBuilder&) = delete;
	ScopedWorkbufferBuilder& operator=(const ScopedWorkbufferBuilder&) = delete;

	void Append(std::string_view text) { mWorkbuffer.Append(text); }
	void Append(int64_t iValue) { mWorkbuffer.Append(iValue); }
	void AppendFloat(float fValue, int iPrecision) { mWorkbuffer.AppendFloat(fValue, iPrecision); }

	std::string_view View() const { return mWorkbuffer.View(); }

private:

	Workbuffer& mWorkbuffer;
};

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

} // namespace common

template <>
struct std::formatter<common::ScopedWorkbufferPop> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::ScopedWorkbufferPop& rValue, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(static_cast<const char*>(rValue), rContext);
	}
};

template <>
struct std::formatter<common::ScopedWorkbufferBuilder> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::ScopedWorkbufferBuilder& rValue, CONTEXT& rContext) const
	{
		return std::formatter<std::string_view>::format(rValue.View(), rContext);
	}
};

template <>
struct std::formatter<common::Wb> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::Wb& rValue, CONTEXT& rContext) const
	{
		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		rWorkbuffer.Push();
		rWorkbuffer.AppendFloat(rValue.fValue, rValue.iPrecision);
		auto result = std::formatter<std::string_view>::format(rWorkbuffer.View(), rContext);
		rWorkbuffer.Pop();
		return result;
	}
};

template <>
struct std::formatter<common::WbV2> : std::formatter<std::string_view>
{
	template <typename CONTEXT>
	auto format(const common::WbV2& rValue, CONTEXT& rContext) const
	{
		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		rWorkbuffer.Push();
		rWorkbuffer.Append(std::string_view("("));
		rWorkbuffer.AppendFloat(DirectX::XMVectorGetX(rValue.vec), rValue.iPrecision);
		rWorkbuffer.Append(std::string_view(","));
		rWorkbuffer.AppendFloat(DirectX::XMVectorGetY(rValue.vec), rValue.iPrecision);
		rWorkbuffer.Append(std::string_view(")"));
		auto result = std::formatter<std::string_view>::format(rWorkbuffer.View(), rContext);
		rWorkbuffer.Pop();
		return result;
	}
};
