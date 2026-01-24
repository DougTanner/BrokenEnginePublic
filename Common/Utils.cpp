#include "Utils.h"

namespace common
{

// https://stackoverflow.com/a/50821858
std::wstring GetStringValueFromHKLM(const std::wstring& rRegSubKey, const std::wstring& rRegValue)
{
	size_t uiBufferSize = 0xFFF;
	std::wstring valueBuf;
	valueBuf.resize(uiBufferSize);
	DWORD uiCbData = static_cast<DWORD>(uiBufferSize * sizeof(wchar_t));
	LSTATUS iRc = RegGetValueW(HKEY_LOCAL_MACHINE, rRegSubKey.c_str(), rRegValue.c_str(), RRF_RT_REG_SZ, nullptr, static_cast<void*>(valueBuf.data()), &uiCbData);

	while (iRc == ERROR_MORE_DATA)
	{
		uiCbData /= sizeof(wchar_t);

		if (uiCbData > static_cast<DWORD>(uiBufferSize))
		{
			uiBufferSize = static_cast<size_t>(uiCbData);
		}
		else
		{
			uiBufferSize *= 2;
			uiCbData = static_cast<DWORD>(uiBufferSize * sizeof(wchar_t));
		}

		valueBuf.resize(uiBufferSize);

		iRc = RegGetValueW(HKEY_LOCAL_MACHINE, rRegSubKey.c_str(), rRegValue.c_str(), RRF_RT_REG_SZ, nullptr, static_cast<void*>(valueBuf.data()), &uiCbData);
	}

	if (iRc == ERROR_SUCCESS)
	{
		uiCbData /= sizeof(wchar_t);

		// Remove end null character
		valueBuf.resize(static_cast<size_t>(uiCbData - 1));

		return valueBuf;
	}
	else
	{
		throw std::runtime_error("Windows system error code: " + std::to_string(iRc));
	}
}

} // namespace common
