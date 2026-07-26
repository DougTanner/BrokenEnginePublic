#pragma once

#include <windows.h>

#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// nlohmann::json (vendored under tinygltf). The warning-suppression pragma travels with the header,
// mirroring Common/ExternalHeaders.h for the PCH-backed builds.
#pragma warning(push)
#pragma warning(disable : 5311) // nlohmann::json 3.10.4 uses the pre-C++20 literal-operator-id form 'operator "" _json'
#include "tinygltf/json.hpp"
#pragma warning(pop)

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

	struct ProcessResult
	{
		DWORD uiExitCode = ERROR_GEN_FAILURE;
		std::string output;
	};

	struct RunProcessOptions
	{
		bool bCaptureOutput = true;
		bool bMergeStdError = false;
		bool bKillOnJobClose = false;
		bool bNoWindow = false;
		// When set, each read chunk goes to the sink instead of ProcessResult::output.
		std::function<void(const char*, size_t)> outputSink;
		// When set, receives the composed failure text; null stays silent.
		std::function<void(std::string_view)> failureSink;
	};

	// pExecutable may be null to search PATH using the first argument.
	std::optional<ProcessResult> RunProcess(const std::filesystem::path* pExecutable, const std::vector<std::wstring>& rArguments, const RunProcessOptions& rOptions);
	std::optional<std::string> RunGit(const std::vector<std::wstring>& rArguments);

	void SetToolName(std::string_view name);
	int PrintOwnerToken();
	void Fail(std::string_view message);
	void FailWindows(std::string_view operation);
	std::string WideToUtf8(std::wstring_view value);
	std::wstring Utf8ToWide(std::string_view value);
	std::wstring QuoteCommandLineArgument(std::wstring_view argument);
	std::wstring BuildCommandLine(const std::vector<std::wstring>& rArguments);
	std::filesystem::path GetLocalApplicationDataPath();
	// Prefixes an absolute drive path with the extended-length marker so raw Win32 file APIs and ifstream tolerate
	// paths past MAX_PATH; returns UNC, already-prefixed, and relative paths unchanged.
	std::filesystem::path ExtendedLengthPath(std::filesystem::path path);
	std::wstring ToLowerInvariant(std::wstring value);
	bool EndsWithCaseInsensitive(std::wstring_view value, std::wstring_view suffix);
}
