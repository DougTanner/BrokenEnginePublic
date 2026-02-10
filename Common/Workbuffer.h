#pragma once

namespace common
{

class Workbuffer
{
public:

	explicit Workbuffer(std::vector<std::byte>& rBuffer)
	: mBuffer(rBuffer)
	{
	}

	// Raw pointer access to the underlying buffer
	template<typename T>
	T GetBuffer(int64_t iSizeInBytes)
	{
		ASSERT(!mbInUse);
		mbInUse = true;
		if (static_cast<int64_t>(mBuffer.size()) < iSizeInBytes) [[unlikely]]
		{
			Grow(iSizeInBytes);
		}
		return reinterpret_cast<T>(mBuffer.data());
	}

	// Tracked-size operations
	void Clear()
	{
		ASSERT(!mbInUse);
		mbInUse = true;
		miSize = 0;
	}

	void Release() { mbInUse = false; }

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
		return {reinterpret_cast<const T*>(mBuffer.data()), static_cast<size_t>(miSize) / sizeof(T)};
	}

private:

	void Grow(int64_t iNeededCapacity);

	std::vector<std::byte>& mBuffer;
	int64_t miSize = 0;
	bool mbInUse = false;
};

} // namespace common
