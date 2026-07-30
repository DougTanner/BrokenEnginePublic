#include "PlanMetadata.h"

#include "CoordinationStore.h"

#include <algorithm>
#include <fstream>
#include <functional>

namespace toolcli
{
	bool Utf8PathLess(const std::wstring& rLeft, const std::wstring& rRight)
	{
		return WideToUtf8(rLeft) < WideToUtf8(rRight);
	}

	bool ParseCanonicalUtcTimestamp(const std::string& rValue, uint64_t& rTicks)
	{
		return coordination::ParseUtcTimestamp(rValue, rTicks) && coordination::FormatUtcTimestamp(rTicks) == rValue;
	}

	bool ReadBytes(const std::filesystem::path& rPath, std::string& rBytes)
	{
		std::ifstream input(ExtendedLengthPath(rPath), std::ios::binary);
		if (!input)
		{
			return false;
		}
		input.seekg(0, std::ios::end);
		const std::streamoff iSize = input.tellg();
		if (iSize < 0 || iSize > 4 * 1024 * 1024)
		{
			return false;
		}
		input.seekg(0, std::ios::beg);
		rBytes.assign(static_cast<size_t>(iSize), '\0');
		input.read(rBytes.data(), iSize);
		return input.good() || input.eof();
	}

	bool NormalizePlanPath(const std::wstring& rValue, std::wstring& rPath)
	{
		const std::filesystem::path path(rValue);
		if (rValue.empty() || rValue.find(L'\\') != std::wstring::npos || path.has_root_name() || path.has_root_directory() || rValue.rfind(L"Documents/Plans/", 0) != 0 || rValue.size() <= std::wstring_view(L"Documents/Plans/").size() || !rValue.ends_with(L".md"))
		{
			return false;
		}
		for (const std::filesystem::path& part : path)
		{
			if (part == L"." || part == L"..")
			{
				return false;
			}
		}
		if (path.generic_wstring() != rValue)
		{
			return false;
		}
		rPath = rValue;
		return true;
	}

	namespace
	{
		bool ParsePlanBytes(Plan& rPlan)
		{
			rPlan.digest = coordination::HashSha256(rPlan.bytes).value_or("");
			if (rPlan.bytes.starts_with("\xEF\xBB\xBF"))
			{
				rPlan.diagnostic = "manual";
				return true;
			}
			if (!rPlan.bytes.starts_with(kMarkerPrefix))
			{
				// Classifying rather than failing keeps the stale-baseline erase and dependency blocking working; the
				// reporting sites, not this classification, make a marker-less plan document loud.
				rPlan.diagnostic = "manual";
				return true;
			}
			const size_t uiLineEnd = rPlan.bytes.find('\n');
			size_t uiMarkerEnd = uiLineEnd == std::string::npos ? rPlan.bytes.size() : uiLineEnd;
			if (uiMarkerEnd > 0 && rPlan.bytes[uiMarkerEnd - 1] == '\r')
			{
				--uiMarkerEnd;
			}
			std::string_view marker(rPlan.bytes.data(), uiMarkerEnd);
			if (!marker.ends_with(kMarkerSuffix))
			{
				rPlan.diagnostic = "malformed plan metadata marker";
				return false;
			}
			try
			{
				const size_t uiJsonBegin = kMarkerPrefix.size();
				const size_t uiJsonLength = marker.size() - uiJsonBegin - kMarkerSuffix.size();
				const nlohmann::json metadata = nlohmann::json::parse(std::string(marker.substr(uiJsonBegin, uiJsonLength)));
				if (metadata.size() != 2 || !metadata.contains("createdUtc") || !metadata["createdUtc"].is_string() || !metadata.contains("dependsOn") || !metadata["dependsOn"].is_array())
				{
					rPlan.diagnostic = "metadata requires exactly createdUtc and dependsOn";
					return false;
				}
				uint64_t uiTicks = 0;
				if (!ParseCanonicalUtcTimestamp(metadata["createdUtc"].get<std::string>(), uiTicks))
				{
					rPlan.diagnostic = "createdUtc is invalid";
					return false;
				}
				rPlan.createdUtc = metadata["createdUtc"].get<std::string>();
				for (const nlohmann::json& dependency : metadata["dependsOn"])
				{
					std::wstring path;
					if (!dependency.is_string() || !NormalizePlanPath(Utf8ToWide(dependency.get<std::string>()), path))
					{
						rPlan.diagnostic = "dependency is not a canonical Documents/Plans Markdown path";
						return false;
					}
					rPlan.dependencies.push_back(std::move(path));
				}
				if (!std::is_sorted(rPlan.dependencies.begin(), rPlan.dependencies.end(), Utf8PathLess) || std::adjacent_find(rPlan.dependencies.begin(), rPlan.dependencies.end()) != rPlan.dependencies.end())
				{
					rPlan.diagnostic = "dependencies must be unique ordinal-sorted";
					return false;
				}
				rPlan.bValid = true;
				return true;
			}
			catch (const nlohmann::json::exception&)
			{
				rPlan.diagnostic = "metadata JSON is invalid";
				return false;
			}
		}

		bool ParsePlan(Plan& rPlan)
		{
			if (!ReadBytes(rPlan.diskPath, rPlan.bytes))
			{
				std::error_code error;
				rPlan.diagnostic = !std::filesystem::exists(rPlan.diskPath, error) && !error ? "missing" : "could not read plan bytes";
				return false;
			}
			return ParsePlanBytes(rPlan);
		}

		bool IsDirectoryGuidance(const std::wstring& rPath)
		{
			const std::wstring filename = std::filesystem::path(rPath).filename().wstring();
			return filename == L"AGENTS.md" || filename == L"CLAUDE.md";
		}

		// Guidance metadata is inert in both directions: never executable, and never another Plan's dependency child, so
		// dropping its outgoing edges keeps it out of terminal preparation's child scans.  The entry itself stays in the Plan
		// map because inbound edges block on membership alone, which is what stops a dependent plan going stale.  A tracked
		// file absent from the worktree keeps its "missing" classification, which the baseline comparison needs to erase it.
		void ClassifyDirectoryGuidance(Plan& rPlan)
		{
			if (!IsDirectoryGuidance(rPlan.path) || rPlan.diagnostic == "missing") return;
			rPlan.bValid = false;
			rPlan.diagnostic = "manual";
			rPlan.dependencies.clear();
		}

		// Every plan document carries byte-zero metadata, so a marker-less one is a defect rather than a reference file.
		// Guidance is the one document that is never a plan, at any depth, so it stays silent instead of being reported.
		void ReportInvalidMetadata(const Plan& rPlan, nlohmann::json& rDiagnostics)
		{
			if (IsDirectoryGuidance(rPlan.path)) return;
			const std::string message = rPlan.diagnostic == "manual" ? "plan document requires byte-zero broken-engine-plan/v1 metadata" : rPlan.diagnostic;
			rDiagnostics.push_back({ { "plan", WideToUtf8(rPlan.path) }, { "code", "invalid-metadata" }, { "message", message } });
		}
	}

	bool BuildPlans(const std::filesystem::path& rWorktree, std::map<std::wstring, Plan>& rPlans, nlohmann::json& rDiagnostics)
	{
		const std::optional<std::string> listing = RunGit({ L"-C", rWorktree.wstring(), L"ls-files", L"-z", L"--", L"Documents/Plans" });
		if (!listing)
		{
			return false;
		}
		size_t uiOffset = 0;
		while (uiOffset < listing->size())
		{
			const size_t uiEnd = listing->find('\0', uiOffset);
			if (uiEnd == std::string::npos)
			{
				return false;
			}
			std::wstring path;
			if (!NormalizePlanPath(Utf8ToWide(std::string_view(listing->data() + uiOffset, uiEnd - uiOffset)), path))
			{
				uiOffset = uiEnd + 1;
				continue;
			}
			uiOffset = uiEnd + 1;
			Plan plan {};
			plan.path = path;
			plan.diskPath = rWorktree / path;
			ParsePlan(plan);
			ClassifyDirectoryGuidance(plan);
			if (!plan.bValid && plan.diagnostic != "missing") ReportInvalidMetadata(plan, rDiagnostics);
			rPlans.emplace(path, std::move(plan));
		}
		return true;
	}

	bool BuildPlansAtCommit(const std::filesystem::path& rWorktree, const std::wstring& rCommit, std::map<std::wstring, Plan>& rPlans, nlohmann::json& rDiagnostics)
	{
		// -z keeps paths unambiguous.  Every path still passes the scheduler's stricter canonical check.
		const std::optional<std::string> listing = RunGit({ L"-C", rWorktree.wstring(), L"ls-tree", L"-rz", L"--full-tree", rCommit, L"--", L"Documents/Plans" });
		if (!listing)
		{
			return false;
		}
		size_t uiOffset = 0;
		while (uiOffset < listing->size())
		{
			const size_t uiEnd = listing->find('\0', uiOffset);
			if (uiEnd == std::string::npos)
			{
				return false;
			}
			const std::string_view entry(listing->data() + uiOffset, uiEnd - uiOffset);
			uiOffset = uiEnd + 1;
			const size_t uiTab = entry.find('\t');
			if (uiTab == std::string::npos || !entry.starts_with("100644 blob "))
			{
				continue;
			}
			std::wstring path;
			if (!NormalizePlanPath(Utf8ToWide(entry.substr(uiTab + 1)), path))
			{
				continue;
			}
			const std::optional<std::string> bytes = RunGit({ L"-C", rWorktree.wstring(), L"show", rCommit + L":" + path });
			if (!bytes)
			{
				return false;
			}
			Plan plan {};
			plan.path = path;
			plan.bytes = *bytes;
			ParsePlanBytes(plan);
			ClassifyDirectoryGuidance(plan);
			if (!plan.bValid)
			{
				ReportInvalidMetadata(plan, rDiagnostics);
			}
			rPlans.emplace(path, std::move(plan));
		}
		return true;
	}

	bool IsBlockedByDependencies(const Plan& rPlan, const std::map<std::wstring, Plan>& rPlans)
	{
		for (const std::wstring& dependency : rPlan.dependencies)
		{
			auto found = rPlans.find(dependency);
			if (found != rPlans.end())
			{
				return true;
			}
		}
		return false;
	}

	void MarkCycles(std::map<std::wstring, Plan>& rPlans, nlohmann::json& rDiagnostics)
	{
		std::map<std::wstring, int> colors;
		std::vector<std::wstring> stack;
		std::function<void(const std::wstring&)> visit = [&](const std::wstring& path)
		{
			colors.insert_or_assign(path, 1); stack.push_back(path);
			for (const std::wstring& dependency : rPlans.at(path).dependencies)
			{
				auto found = rPlans.find(dependency);
				if (found == rPlans.end() || !found->second.bValid)
				{
					continue;
				}
				const auto color = colors.find(dependency);
				const int iDependencyColor = color == colors.end() ? 0 : color->second;
				if (iDependencyColor == 0) visit(dependency);
				else if (iDependencyColor == 1)
				{
					for (auto it = std::find(stack.begin(), stack.end(), dependency); it != stack.end(); ++it)
					{
						rPlans.at(*it).bValid = false;
						rPlans.at(*it).diagnostic = "dependency cycle";
						rDiagnostics.push_back({ { "plan", WideToUtf8(*it) }, { "code", "dependency-cycle" }, { "message", "plan belongs to a dependency cycle" } });
					}
				}
			}
			stack.pop_back(); colors.insert_or_assign(path, 2);
		};
		for (const auto& [path, plan] : rPlans)
		{
			const auto color = colors.find(path);
			if (plan.bValid && (color == colors.end() || color->second == 0)) visit(path);
		}
		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;
			for (auto& [path, plan] : rPlans) if (plan.bValid)
			{
				for (const std::wstring& dependency : plan.dependencies)
				{
					auto found = rPlans.find(dependency);
					if (found != rPlans.end() && !found->second.bValid)
					{
						plan.bValid = false;
						plan.diagnostic = "dependency is quarantined";
						rDiagnostics.push_back({ { "plan", WideToUtf8(path) }, { "code", "dependency-quarantined" }, { "message", plan.diagnostic } });
						bChanged = true;
						break;
					}
				}
			}
		}
	}
}
