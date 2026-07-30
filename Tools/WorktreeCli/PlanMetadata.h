#pragma once

#include "ToolCliCommon.h"

#include <map>

namespace toolcli
{
	constexpr std::string_view kMarkerPrefix = "<!-- broken-engine-plan/v1 ";
	constexpr std::string_view kMarkerSuffix = " -->";

	struct Plan
	{
		std::wstring path;
		std::filesystem::path diskPath;
		std::string bytes;
		std::string digest;
		std::string createdUtc;
		std::vector<std::wstring> dependencies;
		bool bValid = false;
		std::string diagnostic;
	};

	bool Utf8PathLess(const std::wstring& rLeft, const std::wstring& rRight);
	bool ParseCanonicalUtcTimestamp(const std::string& rValue, uint64_t& rTicks);
	bool ReadBytes(const std::filesystem::path& rPath, std::string& rBytes);
	bool NormalizePlanPath(const std::wstring& rValue, std::wstring& rPath);
	bool BuildPlans(const std::filesystem::path& rWorktree, std::map<std::wstring, Plan>& rPlans, nlohmann::json& rDiagnostics);
	bool BuildPlansAtCommit(const std::filesystem::path& rWorktree, const std::wstring& rCommit, std::map<std::wstring, Plan>& rPlans, nlohmann::json& rDiagnostics);
	bool IsBlockedByDependencies(const Plan& rPlan, const std::map<std::wstring, Plan>& rPlans);
	void MarkCycles(std::map<std::wstring, Plan>& rPlans, nlohmann::json& rDiagnostics);
}
