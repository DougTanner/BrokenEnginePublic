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
		return reinterpret_cast<T>(mBuffer.data() + miBase);
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
	std::vector<int64_t> mSavedBase;
};

} // namespace common
