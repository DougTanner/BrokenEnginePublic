#include "Workbuffer.h"

#include "ErrorUtils.h"

namespace common
{

void Workbuffer::Append(std::string_view text)
{
	int64_t iNeeded = miSize + static_cast<int64_t>(text.size());
	if (iNeeded > static_cast<int64_t>(mBuffer.size()))
	{
		Grow(iNeeded);
	}

	memcpy(reinterpret_cast<char*>(mBuffer.data()) + miSize, text.data(), text.size());
	miSize += static_cast<int64_t>(text.size());
}

void Workbuffer::Append(int64_t iValue)
{
	char* pStart = reinterpret_cast<char*>(mBuffer.data()) + miSize;
	char* pEnd = reinterpret_cast<char*>(mBuffer.data()) + mBuffer.size();
	std::to_chars_result result = std::to_chars(pStart, pEnd, iValue);
	if (result.ec == std::errc::value_too_large)
	{
		Grow(miSize + 32);
		pStart = reinterpret_cast<char*>(mBuffer.data()) + miSize;
		pEnd = reinterpret_cast<char*>(mBuffer.data()) + mBuffer.size();
		result = std::to_chars(pStart, pEnd, iValue);
	}

	miSize = result.ptr - reinterpret_cast<char*>(mBuffer.data());
}

void Workbuffer::AppendFloat(float fValue, int iPrecision)
{
	char* pStart = reinterpret_cast<char*>(mBuffer.data()) + miSize;
	char* pEnd = reinterpret_cast<char*>(mBuffer.data()) + mBuffer.size();
	std::to_chars_result result = std::to_chars(pStart, pEnd, fValue, std::chars_format::fixed, iPrecision);
	if (result.ec == std::errc::value_too_large)
	{
		Grow(miSize + 64);
		pStart = reinterpret_cast<char*>(mBuffer.data()) + miSize;
		pEnd = reinterpret_cast<char*>(mBuffer.data()) + mBuffer.size();
		result = std::to_chars(pStart, pEnd, fValue, std::chars_format::fixed, iPrecision);
	}

	miSize = result.ptr - reinterpret_cast<char*>(mBuffer.data());
}

std::string_view Workbuffer::View() const
{
	return std::string_view(reinterpret_cast<const char*>(mBuffer.data()), miSize);
}

void Workbuffer::Grow(int64_t iNeededCapacity)
{
	common::DebugBreak();
	mBuffer.resize(iNeededCapacity * 2);
}

} // namespace common
