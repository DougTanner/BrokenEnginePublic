#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace toolcli
{
	constexpr int kiExitOk = 0;
	constexpr int kiExitStateConflict = 2;
	constexpr int kiExitFailure = 1;

	class Handle
	{
	public:
		Handle() = default;
		explicit Handle(HANDLE hHandle);
		~Handle();

		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;
		Handle(Handle&& rOther) noexcept;
		Handle& operator=(Handle&& rOther) noexcept;

		[[nodiscard]] HANDLE Get() const;
		[[nodiscard]] bool IsValid() const;
		HANDLE Release();
		void Reset(HANDLE hHandle = INVALID_HANDLE_VALUE);

	private:
		HANDLE mhHandle = INVALID_HANDLE_VALUE;
	};

	void SetToolName(std::string_view name);
	int PrintOwnerToken();
	void Fail(std::string_view message);
	void FailWindows(std::string_view operation);
	std::string WideToUtf8(std::wstring_view value);
	std::wstring Utf8ToWide(std::string_view value);
	std::wstring QuoteCommandLineArgument(std::wstring_view argument);
	std::wstring BuildCommandLine(const std::vector<std::wstring>& rArguments);
	std::filesystem::path GetLocalApplicationDataPath();
	std::wstring ToLowerInvariant(std::wstring value);
	bool EndsWithCaseInsensitive(std::wstring_view value, std::wstring_view suffix);
}
