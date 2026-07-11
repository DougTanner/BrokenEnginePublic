#include "AgentCliCommon.h"

#include <iostream>
#include <utility>

namespace agentcli
{
	Handle::Handle(HANDLE hHandle) :
		mhHandle(hHandle)
	{
	}

	Handle::~Handle()
	{
		Reset();
	}

	Handle::Handle(Handle&& rOther) noexcept :
		mhHandle(rOther.Release())
	{
	}

	Handle& Handle::operator=(Handle&& rOther) noexcept
	{
		if (this != &rOther)
		{
			Reset(rOther.Release());
		}
		return *this;
	}

	HANDLE Handle::Get() const
	{
		return mhHandle;
	}

	bool Handle::IsValid() const
	{
		return mhHandle != nullptr && mhHandle != INVALID_HANDLE_VALUE;
	}

	HANDLE Handle::Release()
	{
		HANDLE hHandle = mhHandle;
		mhHandle = INVALID_HANDLE_VALUE;
		return hHandle;
	}

	void Handle::Reset(HANDLE hHandle)
	{
		if (IsValid())
		{
			::CloseHandle(mhHandle);
		}
		mhHandle = hHandle;
	}

	void Fail(std::string_view message)
	{
		std::cerr << "AgentCli: " << message << '\n';
	}

	void FailWindows(std::string_view operation)
	{
		Fail(std::string(operation) + " failed (Windows error " + std::to_string(::GetLastError()) + ")");
	}

	std::string WideToUtf8(std::wstring_view value)
	{
		if (value.empty())
		{
			return {};
		}

		int iLength = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		if (iLength <= 0)
		{
			return {};
		}

		std::string result(static_cast<size_t>(iLength), '\0');
		if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), iLength, nullptr, nullptr) != iLength)
		{
			return {};
		}
		return result;
	}

	std::wstring Utf8ToWide(std::string_view value)
	{
		if (value.empty())
		{
			return {};
		}

		int iLength = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (iLength <= 0)
		{
			return {};
		}

		std::wstring result(static_cast<size_t>(iLength), L'\0');
		if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), iLength) != iLength)
		{
			return {};
		}
		return result;
	}

	std::wstring QuoteCommandLineArgument(std::wstring_view argument)
	{
		if (argument.empty())
		{
			return L"\"\"";
		}
		if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
		{
			return std::wstring(argument);
		}

		std::wstring result = L"\"";
		size_t uiBackslashes = 0;
		for (wchar_t cCharacter : argument)
		{
			if (cCharacter == L'\\')
			{
				++uiBackslashes;
				continue;
			}

			if (cCharacter == L'\"')
			{
				result.append(uiBackslashes * 2 + 1, L'\\');
				result.push_back(cCharacter);
				uiBackslashes = 0;
				continue;
			}

			result.append(uiBackslashes, L'\\');
			uiBackslashes = 0;
			result.push_back(cCharacter);
		}
		result.append(uiBackslashes * 2, L'\\');
		result.push_back(L'\"');
		return result;
	}

	std::wstring BuildCommandLine(const std::vector<std::wstring>& rArguments)
	{
		std::wstring commandLine;
		for (const std::wstring& rArgument : rArguments)
		{
			if (!commandLine.empty())
			{
				commandLine.push_back(L' ');
			}
			commandLine += QuoteCommandLineArgument(rArgument);
		}
		return commandLine;
	}

	std::filesystem::path GetLocalApplicationDataPath()
	{
		DWORD uiRequired = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
		if (uiRequired == 0)
		{
			return {};
		}

		std::wstring value(uiRequired, L'\0');
		DWORD uiWritten = ::GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), uiRequired);
		if (uiWritten == 0 || uiWritten >= uiRequired)
		{
			return {};
		}
		value.resize(uiWritten);
		return std::filesystem::path(value);
	}

	std::filesystem::path GetCurrentExecutablePath()
	{
		std::wstring value(32768, L'\0');
		DWORD uiWritten = ::GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
		if (uiWritten == 0 || uiWritten >= value.size())
		{
			return {};
		}
		value.resize(uiWritten);
		return std::filesystem::path(value);
	}

	std::wstring ToLowerInvariant(std::wstring value)
	{
		if (!value.empty())
		{
			int iResult = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(), static_cast<int>(value.size()), value.data(), static_cast<int>(value.size()), nullptr, nullptr, 0);
			if (iResult == 0)
			{
				return {};
			}
		}
		return value;
	}

	bool EndsWithCaseInsensitive(std::wstring_view value, std::wstring_view suffix)
	{
		if (value.size() < suffix.size())
		{
			return false;
		}
		std::wstring valueEnd(value.substr(value.size() - suffix.size()));
		return ToLowerInvariant(std::move(valueEnd)) == ToLowerInvariant(std::wstring(suffix));
	}
}
