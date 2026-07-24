#include "PlanScheduler.h"

#include "CoordinationStore.h"
#include "ToolCliCommon.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>

namespace toolcli
{
	namespace
	{
		constexpr std::string_view kMarkerPrefix = "<!-- broken-engine-plan/v1 ";
		constexpr std::string_view kMarkerSuffix = " -->";
		constexpr uint64_t kClaimLifetimeTicks = 48ull * 60ull * 60ull * 10'000'000ull;

		struct Arguments
		{
			std::wstring repository;
			std::wstring worktree;
			std::wstring primaryWorktree;
			std::wstring branch;
			std::wstring owner;
			std::wstring session;
			std::wstring plan;
			std::wstring baseline;
			std::wstring newBaseline;
			std::wstring claimReceipt;
			std::wstring claimReceiptSha256;
			std::wstring terminalReceipt;
			std::wstring terminalReceiptSha256;
			std::wstring writeClaimReceipt;
			std::wstring landedCommit;
			bool bUserAuthorizedRejection = false;
		};

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

		struct Claim
		{
			std::filesystem::path path;
			nlohmann::json json;
			std::string digest;
		};

		std::optional<std::wstring> ResolveGitCommonDirectory(const std::filesystem::path& rWorktree);
		std::optional<std::string> ResolveGitBranch(const std::filesystem::path& rWorktree);
		bool CommitIsAncestorOfWorktreeHead(const std::string& rCommit, const std::filesystem::path& rWorktree);

		void TrimLineEnding(std::string& rValue)
		{
			while (!rValue.empty() && (rValue.back() == '\r' || rValue.back() == '\n')) rValue.pop_back();
		}

		bool Utf8PathLess(const std::wstring& rLeft, const std::wstring& rRight)
		{
			return WideToUtf8(rLeft) < WideToUtf8(rRight);
		}

		bool IsLowerHex(const std::string& rValue, size_t iLength)
		{
			return rValue.size() == iLength && std::all_of(rValue.begin(), rValue.end(), [](char value)
			{
				return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
			});
		}

		bool ParseCanonicalUtcTimestamp(const std::string& rValue, uint64_t& rTicks)
		{
			return coordination::ParseUtcTimestamp(rValue, rTicks) && coordination::FormatUtcTimestamp(rTicks) == rValue;
		}

		bool ReadRequiredString(const nlohmann::json& rValue, const char* pField, std::string& rResult)
		{
			if (!rValue.is_object() || !rValue.contains(pField) || !rValue[pField].is_string())
			{
				return false;
			}
			rResult = rValue[pField].get<std::string>();
			return !rResult.empty();
		}

		std::string JsonText(const nlohmann::json& rValue)
		{
			return rValue.dump();
		}

		void PrintResult(nlohmann::json value)
		{
			value["schemaVersion"] = 1;
			std::cout << value.dump() << '\n';
		}

		int Conflict(const std::wstring& rOperation, const std::string& rCode, const std::string& rMessage, nlohmann::json extra = {})
		{
			extra["operation"] = WideToUtf8(rOperation);
			extra["status"] = "conflict";
			extra["code"] = rCode;
			extra["message"] = rMessage;
			PrintResult(std::move(extra));
			return kiExitStateConflict;
		}

		int Failure(const std::wstring& rOperation, const std::string& rCode, const std::string& rMessage)
		{
			PrintResult({ { "operation", WideToUtf8(rOperation) }, { "status", "error" }, { "code", rCode }, { "message", rMessage } });
			return kiExitFailure;
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

		bool ParseArguments(int iCount, wchar_t* pValues[], int iStart, Arguments& rArguments)
		{
			for (int i = iStart; i < iCount; ++i)
			{
				const std::wstring_view option = pValues[i];
				if (option == L"--user-authorized-rejection")
				{
					rArguments.bUserAuthorizedRejection = true;
					continue;
				}
				std::wstring* destination = nullptr;
				if (option == L"--repo")
				{
					destination = &rArguments.repository;
				}
				else if (option == L"--worktree")
				{
					destination = &rArguments.worktree;
				}
				else if (option == L"--primary-worktree")
				{
					destination = &rArguments.primaryWorktree;
				}
				else if (option == L"--branch")
				{
					destination = &rArguments.branch;
				}
				else if (option == L"--owner")
				{
					destination = &rArguments.owner;
				}
				else if (option == L"--session")
				{
					destination = &rArguments.session;
				}
				else if (option == L"--plan")
				{
					destination = &rArguments.plan;
				}
				else if (option == L"--baseline")
				{
					destination = &rArguments.baseline;
				}
				else if (option == L"--new-baseline")
				{
					destination = &rArguments.newBaseline;
				}
				else if (option == L"--claim-receipt")
				{
					destination = &rArguments.claimReceipt;
				}
				else if (option == L"--claim-receipt-sha256")
				{
					destination = &rArguments.claimReceiptSha256;
				}
				else if (option == L"--terminal-receipt")
				{
					destination = &rArguments.terminalReceipt;
				}
				else if (option == L"--terminal-receipt-sha256")
				{
					destination = &rArguments.terminalReceiptSha256;
				}
				else if (option == L"--write-claim-receipt")
				{
					destination = &rArguments.writeClaimReceipt;
				}
				else if (option == L"--landed-commit")
				{
					destination = &rArguments.landedCommit;
				}
				else
				{
					return false;
				}
				if (++i >= iCount)
				{
					return false;
				}
				*destination = pValues[i];
			}
			return true;
		}

		bool IsPathBelow(const std::filesystem::path& rChild, const std::filesystem::path& rParent)
		{
			std::error_code error;
			const std::filesystem::path child = std::filesystem::weakly_canonical(rChild, error);
			if (error)
			{
				return false;
			}
			const std::filesystem::path parent = std::filesystem::weakly_canonical(rParent, error);
			if (error)
			{
				return false;
			}
			auto childIt = child.begin();
			for (auto parentIt = parent.begin(); parentIt != parent.end(); ++parentIt, ++childIt)
			{
				if (childIt == child.end() || *childIt != *parentIt)
				{
					return false;
				}
			}
			return true;
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

		bool IsCanonicalPositiveDecimal(std::wstring_view rValue)
		{
			return !rValue.empty() && rValue.size() <= 10 && rValue.front() >= L'1' && rValue.front() <= L'9' && std::all_of(rValue.begin() + 1, rValue.end(), [](wchar_t value)
			{
				return value >= L'0' && value <= L'9';
			});
		}

		bool RemovePlanAtomicTemporarySiblings(const std::filesystem::path& rWorktree, const std::wstring& rPlanPath)
		{
			const std::filesystem::path planPath = rWorktree / rPlanPath;
			if (!IsPathBelow(planPath, rWorktree))
			{
				return false;
			}
			const std::filesystem::path parent = planPath.parent_path();
			const std::wstring prefix = planPath.filename().wstring() + L".tmp.";
			std::error_code error;
			for (std::filesystem::directory_iterator it(ExtendedLengthPath(parent), error), end; !error && it != end; it.increment(error))
			{
				const std::wstring filename = it->path().filename().wstring();
				if (!filename.starts_with(prefix))
				{
					continue;
				}
				const std::wstring_view suffix(filename.data() + prefix.size(), filename.size() - prefix.size());
				const size_t uiSeparator = suffix.find(L'.');
				if (uiSeparator == std::wstring_view::npos || suffix.find(L'.', uiSeparator + 1) != std::wstring_view::npos || !IsCanonicalPositiveDecimal(suffix.substr(0, uiSeparator)) || !IsCanonicalPositiveDecimal(suffix.substr(uiSeparator + 1)))
				{
					continue;
				}
				const std::filesystem::path temporaryPath = parent / filename;
				if (!IsPathBelow(temporaryPath, rWorktree))
				{
					continue;
				}
				std::error_code entryError;
				if (!it->is_regular_file(entryError) || entryError)
				{
					continue;
				}
				const DWORD attributes = ::GetFileAttributesW(ExtendedLengthPath(temporaryPath).c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 || (attributes & FILE_ATTRIBUTE_HIDDEN) == 0 || (attributes & FILE_ATTRIBUTE_TEMPORARY) == 0)
				{
					continue;
				}
				if (::DeleteFileW(ExtendedLengthPath(temporaryPath).c_str()) == FALSE)
				{
					return false;
				}
			}
			return !error;
		}

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
				// An ordinary Markdown document is deliberately manual/reference-only.
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
				if (!plan.bValid && plan.diagnostic != "manual" && plan.diagnostic != "missing") rDiagnostics.push_back({ { "plan", WideToUtf8(path) }, { "code", "invalid-metadata" }, { "message", plan.diagnostic } });
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
				if (!plan.bValid && plan.diagnostic != "manual")
				{
					rDiagnostics.push_back({ { "plan", WideToUtf8(path) }, { "code", "invalid-metadata" }, { "message", plan.diagnostic } });
				}
				rPlans.emplace(path, std::move(plan));
			}
			return true;
		}

		std::filesystem::path SchedulerRoot(const std::wstring& rRepository)
		{
			const std::string hash = coordination::HashSha256(WideToUtf8(rRepository)).value_or("invalid");
			return GetLocalApplicationDataPath() / L"BrokenEngineLocks" / L"plan-scheduler" / Utf8ToWide(hash);
		}

		std::filesystem::path ClaimPath(const std::filesystem::path& rRoot, const std::wstring& rPlan)
		{
			return rRoot / L"claims" / Utf8ToWide(coordination::HashSha256(WideToUtf8(rPlan)).value_or("invalid") + ".json");
		}

		bool ValidateManifest(const nlohmann::json& rManifest, const std::wstring& rPlan, const std::string& rPlanDigest)
		{
			if (!rManifest.is_object() || rManifest.size() != 2 || !rManifest.contains("target") || !rManifest["target"].is_object() || rManifest["target"].size() != 3 || !rManifest.contains("children") || !rManifest["children"].is_array())
			{
				return false;
			}
			std::string targetPath;
			std::string beforeDigest;
			std::string afterDigest;
			if (!ReadRequiredString(rManifest["target"], "path", targetPath) || !ReadRequiredString(rManifest["target"], "beforeSha256", beforeDigest) || !ReadRequiredString(rManifest["target"], "afterSha256", afterDigest) || targetPath != WideToUtf8(rPlan) || beforeDigest != rPlanDigest || afterDigest != "absent")
			{
				return false;
			}
			std::set<std::wstring> children;
			for (const nlohmann::json& child : rManifest["children"])
			{
				if (!child.is_object() || child.size() != 3)
				{
					return false;
				}
				std::string childPathText;
				std::string childBefore;
				std::string childAfter;
				std::wstring childPath;
				if (!ReadRequiredString(child, "path", childPathText) || !NormalizePlanPath(Utf8ToWide(childPathText), childPath) || !ReadRequiredString(child, "beforeSha256", childBefore) || !ReadRequiredString(child, "afterSha256", childAfter) || !IsLowerHex(childBefore, 64) || !IsLowerHex(childAfter, 64) || !children.insert(std::move(childPath)).second)
				{
					return false;
				}
			}
			return true;
		}

		bool ValidateClaim(const nlohmann::json& rClaim, const std::wstring& rRepository, const std::wstring& rPlan)
		{
			try
			{
				if (!rClaim.is_object() || !rClaim.contains("schemaVersion") || !rClaim["schemaVersion"].is_number_integer() || rClaim["schemaVersion"].get<int>() != 1)
				{
					return false;
				}
				std::map<std::string, std::string> fields;
				for (const char* field : { "repository", "plan", "owner", "session", "worktree", "branch", "primaryCommit", "planSha256", "claimedAt", "expiresAt", "state" })
				{
					if (!ReadRequiredString(rClaim, field, fields.try_emplace(field).first->second))
					{
						return false;
					}
				}
				if (fields.at("repository") != WideToUtf8(rRepository) || fields.at("plan") != WideToUtf8(rPlan) || !IsLowerHex(fields.at("primaryCommit"), 40) || !IsLowerHex(fields.at("planSha256"), 64))
				{
					return false;
				}
				uint64_t uiClaimedAt = 0;
				uint64_t uiExpiresAt = 0;
				if (!ParseCanonicalUtcTimestamp(fields.at("claimedAt"), uiClaimedAt) || !ParseCanonicalUtcTimestamp(fields.at("expiresAt"), uiExpiresAt) || uiExpiresAt < uiClaimedAt || uiExpiresAt - uiClaimedAt != kClaimLifetimeTicks)
				{
					return false;
				}
				const std::string& state = fields.at("state");
				if (state == "claimed")
				{
					return rClaim.size() == 12;
				}
				if ((state != "preparing" && state != "awaiting-landing") || !rClaim.contains("disposition") || !rClaim["disposition"].is_string() || !rClaim.contains("manifest") || !ValidateManifest(rClaim["manifest"], rPlan, fields.at("planSha256")))
				{
					return false;
				}
				const std::string disposition = rClaim["disposition"].get<std::string>();
				if (disposition != "completed" && disposition != "rejected")
				{
					return false;
				}
				if (state == "preparing")
				{
					return rClaim.size() == 14;
				}
				if (rClaim.size() != 16 || !rClaim.contains("changedPaths") || !rClaim["changedPaths"].is_array() || !rClaim.contains("manifestDigest") || !rClaim["manifestDigest"].is_string() || rClaim["manifestDigest"].get<std::string>() != coordination::HashSha256(JsonText(rClaim["manifest"])).value_or(""))
				{
					return false;
				}
				std::set<std::wstring> changedPaths;
				for (const nlohmann::json& changed : rClaim["changedPaths"])
				{
					std::wstring changedPath;
					if (!changed.is_string() || !NormalizePlanPath(Utf8ToWide(changed.get<std::string>()), changedPath) || !changedPaths.insert(std::move(changedPath)).second)
					{
						return false;
					}
				}
				return true;
			}
			catch (const nlohmann::json::exception&)
			{
				return false;
			}
		}

		bool ReadClaim(const std::filesystem::path& rPath, Claim& rClaim)
		{
			std::string bytes;
			if (!ReadBytes(rPath, bytes))
			{
				return false;
			}
			try
			{
				rClaim.json = nlohmann::json::parse(bytes);
				rClaim.path = rPath;
				rClaim.digest = coordination::HashSha256(bytes).value_or("");
				return true;
			}
			catch (const nlohmann::json::exception&)
			{
				return false;
			}
		}

		std::string ClaimReceiptDigest(const nlohmann::json& rClaim, const std::string& rCurrentDigest)
		{
			if (rClaim["state"].get<std::string>() == "claimed")
			{
				return rCurrentDigest;
			}
			nlohmann::json initialClaim = rClaim;
			for (const char* field : { "disposition", "manifest", "changedPaths", "manifestDigest" }) initialClaim.erase(field);
			initialClaim["state"] = "claimed";
			return coordination::HashSha256(initialClaim.dump(2) + "\n").value_or("");
		}

		void HealClaims(const std::filesystem::path& rRoot, const std::wstring& rRepository, const std::map<std::wstring, Plan>& rPlans, nlohmann::json& rHealed)
		{
			const std::filesystem::path claims = rRoot / L"claims";
			std::error_code error;
			for (std::filesystem::directory_iterator it(ExtendedLengthPath(claims), error), end; !error && it != end; it.increment(error))
			{
				if (!it->is_regular_file(error) || error || it->path().extension() != L".json")
				{
					continue;
				}
				Claim claim;
				bool bRemove = !ReadClaim(it->path(), claim);
				std::wstring path;
				if (!bRemove && (!claim.json.contains("plan") || !claim.json["plan"].is_string() || !NormalizePlanPath(Utf8ToWide(claim.json["plan"].get<std::string>()), path) || !ValidateClaim(claim.json, rRepository, path)))
				{
					bRemove = true;
				}
				uint64_t uiExpiry = 0;
				if (!bRemove && (!coordination::ParseUtcTimestamp(claim.json["expiresAt"].get<std::string>(), uiExpiry) || uiExpiry <= coordination::CurrentUtcTicks()))
				{
					bRemove = true;
				}
				if (!bRemove && it->path().filename() != ClaimPath(rRoot, path).filename())
				{
					bRemove = true;
				}
				if (!bRemove && rPlans.find(path) == rPlans.end() && claim.json.value("state", "") != "preparing" && claim.json.value("state", "") != "awaiting-landing")
				{
					bRemove = true;
				}
				if (!bRemove)
				{
					const std::optional<std::wstring> worktree = coordination::CanonicalizeDirectoryPath(Utf8ToWide(claim.json["worktree"].get<std::string>()));
					std::optional<std::string> branch;
					if (worktree)
					{
						branch = ResolveGitBranch(*worktree);
					}
					if (!worktree || WideToUtf8(*worktree) != claim.json["worktree"].get<std::string>() || ResolveGitCommonDirectory(*worktree) != std::optional<std::wstring>(rRepository) || !branch || *branch != claim.json["branch"].get<std::string>())
					{
						bRemove = true;
					}
					else if (!CommitIsAncestorOfWorktreeHead(claim.json["primaryCommit"].get<std::string>(), *worktree))
					{
						bRemove = true;
					}
				}
				if (bRemove && ::DeleteFileW(ExtendedLengthPath(it->path()).c_str()) != FALSE)
				{
					rHealed.push_back(WideToUtf8(it->path().filename().wstring()));
				}
			}
		}

		bool ReadReceipt(const Arguments& rArguments, nlohmann::json& rReceipt)
		{
			if (rArguments.claimReceipt.empty() || rArguments.claimReceiptSha256.empty())
			{
				return false;
			}
			std::string bytes;
			if (!ReadBytes(std::filesystem::path(rArguments.claimReceipt), bytes) || coordination::HashSha256(bytes) != std::optional<std::string>(WideToUtf8(rArguments.claimReceiptSha256)))
			{
				return false;
			}
			try
			{
				rReceipt = nlohmann::json::parse(bytes);
				if (!rReceipt.is_object() || rReceipt.size() != 11 || !rReceipt.contains("schemaVersion") || !rReceipt["schemaVersion"].is_number_integer() || rReceipt["schemaVersion"].get<int>() != 1)
				{
					return false;
				}
				for (const char* field : { "claimPath", "claimSha256", "repository", "plan", "owner", "session", "worktree", "branch", "primaryCommit", "planSha256" })
				{
					if (!rReceipt.contains(field) || !rReceipt[field].is_string() || rReceipt[field].get<std::string>().empty())
					{
						return false;
					}
				}
				return IsLowerHex(rReceipt["claimSha256"].get<std::string>(), 64) && IsLowerHex(rReceipt["primaryCommit"].get<std::string>(), 40) && IsLowerHex(rReceipt["planSha256"].get<std::string>(), 64);
			}
			catch (const nlohmann::json::exception&)
			{
				return false;
			}
		}

		bool IsSafeReceiptDestination(const std::filesystem::path& rWorktree, const std::filesystem::path& rTarget)
		{
			const std::filesystem::path temp = rWorktree / L"Temp";
			std::error_code error;
			const DWORD tempAttributes = ::GetFileAttributesW(ExtendedLengthPath(temp).c_str());
			if (tempAttributes == INVALID_FILE_ATTRIBUTES || (tempAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (tempAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				return false;
			}
			const std::filesystem::path canonicalTemp = std::filesystem::canonical(temp, error);
			if (error || !IsPathBelow(rTarget, canonicalTemp) || rTarget == canonicalTemp || std::filesystem::exists(rTarget, error) || error)
			{
				return false;
			}
			std::filesystem::path current = canonicalTemp;
			const std::filesystem::path relative = std::filesystem::relative(rTarget, canonicalTemp, error);
			if (error || relative.empty() || relative.has_parent_path() && relative.begin()->wstring() == L"..")
			{
				return false;
			}
			for (const std::filesystem::path& part : relative.parent_path())
			{
				current /= part;
				const DWORD attributes = ::GetFileAttributesW(ExtendedLengthPath(current).c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				{
					return false;
				}
			}
			return true;
		}

		bool WriteClaimReceipt(const Arguments& rArguments, const std::filesystem::path& rClaimPath, const Plan& rPlan, nlohmann::json& rReceipt)
		{
			const std::filesystem::path target(rArguments.writeClaimReceipt);
			if (target.empty() || !IsSafeReceiptDestination(std::filesystem::path(rArguments.worktree), target))
			{
				return false;
			}
			std::string claimBytes;
			if (!ReadBytes(rClaimPath, claimBytes))
			{
				return false;
			}
			const nlohmann::json claim = nlohmann::json::parse(claimBytes);
			rReceipt = { { "schemaVersion", 1 }, { "repository", claim["repository"] }, { "plan", WideToUtf8(rPlan.path) }, { "owner", claim["owner"] }, { "session", claim["session"] }, { "worktree", claim["worktree"] }, { "branch", claim["branch"] }, { "primaryCommit", claim["primaryCommit"] }, { "planSha256", rPlan.digest }, { "claimPath", WideToUtf8(rClaimPath.wstring()) }, { "claimSha256", ClaimReceiptDigest(claim, coordination::HashSha256(claimBytes).value_or("")) } };
			const std::string bytes = rReceipt.dump();
			Handle receipt(::CreateFileW(ExtendedLengthPath(target).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!receipt.IsValid())
			{
				return false;
			}
			DWORD written = 0;
			const bool bOk = ::WriteFile(receipt.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE && written == bytes.size() && ::FlushFileBuffers(receipt.Get()) != FALSE;
			receipt.Reset();
			if (!bOk)
			{
				::DeleteFileW(ExtendedLengthPath(target).c_str());
				return false;
			}
			rReceipt["path"] = WideToUtf8(target.wstring());
			rReceipt["sha256"] = coordination::HashSha256(bytes).value_or("");
			rReceipt["size"] = bytes.size();
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

		std::optional<std::wstring> ResolveGitCommonDirectory(const std::filesystem::path& rWorktree)
		{
			std::optional<std::string> commonDirectory = RunGit({ L"-C", rWorktree.wstring(), L"rev-parse", L"--path-format=absolute", L"--git-common-dir" });
			if (!commonDirectory)
			{
				return std::nullopt;
			}
			TrimLineEnding(*commonDirectory);
			return coordination::CanonicalizeDirectoryPath(Utf8ToWide(*commonDirectory));
		}

		std::optional<std::string> ResolveGitBranch(const std::filesystem::path& rWorktree)
		{
			std::optional<std::string> branch = RunGit({ L"-C", rWorktree.wstring(), L"symbolic-ref", L"--quiet", L"--short", L"HEAD" });
			if (!branch)
			{
				return std::nullopt;
			}
			TrimLineEnding(*branch);
			if (branch->empty())
			{
				return std::nullopt;
			}
			return branch;
		}

		std::optional<std::string> ResolvePrimaryBranch(const std::wstring& rRepository)
		{
			std::optional<std::string> branch = RunGit({ L"--git-dir", rRepository, L"symbolic-ref", L"--quiet", L"--short", L"HEAD" });
			if (!branch)
			{
				return std::nullopt;
			}
			TrimLineEnding(*branch);
			if (branch->empty())
			{
				return std::nullopt;
			}
			return branch;
		}

		std::optional<std::string> ResolvePrimaryReference(const std::wstring& rRepository)
		{
			std::optional<std::string> reference = RunGit({ L"--git-dir", rRepository, L"symbolic-ref", L"--quiet", L"HEAD" });
			if (!reference)
			{
				return std::nullopt;
			}
			TrimLineEnding(*reference);
			if (!reference->starts_with("refs/heads/") || reference->size() == std::string_view("refs/heads/").size())
			{
				return std::nullopt;
			}
			return reference;
		}

		std::optional<std::string> ResolveCommit(const std::filesystem::path& rWorktree, const std::wstring& rRevision)
		{
			std::optional<std::string> commit = RunGit({ L"-C", rWorktree.wstring(), L"rev-parse", L"--verify", rRevision + L"^{commit}" });
			if (!commit)
			{
				return std::nullopt;
			}
			TrimLineEnding(*commit);
			return IsLowerHex(*commit, 40) ? commit : std::nullopt;
		}

		std::optional<std::string> ResolveRepositoryCommit(const std::wstring& rRepository, const std::wstring& rRevision)
		{
			std::optional<std::string> commit = RunGit({ L"--git-dir", rRepository, L"rev-parse", L"--verify", rRevision + L"^{commit}" });
			if (!commit)
			{
				return std::nullopt;
			}
			TrimLineEnding(*commit);
			return IsLowerHex(*commit, 40) ? commit : std::nullopt;
		}

		bool ReceiptMatchesClaim(const nlohmann::json& rReceipt, const Claim& rClaim)
		{
			for (const char* field : { "repository", "plan", "owner", "session", "worktree", "branch", "primaryCommit", "planSha256" })
			{
				if (rClaim.json[field].get<std::string>() != rReceipt[field].get<std::string>())
				{
					return false;
				}
			}
			return ClaimReceiptDigest(rClaim.json, rClaim.digest) == rReceipt["claimSha256"].get<std::string>();
		}

		bool ClaimIsLive(const Claim& rClaim)
		{
			uint64_t uiExpiry = 0;
			return coordination::ParseUtcTimestamp(rClaim.json["expiresAt"].get<std::string>(), uiExpiry) && uiExpiry > coordination::CurrentUtcTicks();
		}

		bool CommitIsAncestorOfWorktreeHead(const std::string& rCommit, const std::filesystem::path& rWorktree)
		{
			const std::optional<std::string> head = ResolveCommit(rWorktree, L"HEAD");
			return head && RunGit({ L"-C", rWorktree.wstring(), L"merge-base", L"--is-ancestor", Utf8ToWide(rCommit), Utf8ToWide(*head) });
		}

		bool ClaimBaselineIsAncestorOfWorktreeHead(const Claim& rClaim, const std::filesystem::path& rWorktree)
		{
			return CommitIsAncestorOfWorktreeHead(rClaim.json["primaryCommit"].get<std::string>(), rWorktree);
		}

		bool ClaimMatchesSession(const Claim& rClaim, const Arguments& rArguments, const std::filesystem::path& rWorktree)
		{
			return rClaim.json["owner"].get<std::string>() == WideToUtf8(rArguments.owner) && rClaim.json["session"].get<std::string>() == WideToUtf8(rArguments.session) && rClaim.json["worktree"].get<std::string>() == WideToUtf8(rWorktree.wstring()) && rClaim.json["branch"].get<std::string>() == WideToUtf8(rArguments.branch) && ClaimBaselineIsAncestorOfWorktreeHead(rClaim, rWorktree);
		}

		bool HasTerminalReceiptFor(const Arguments& rArguments, const std::wstring& rRepository, const std::filesystem::path& rWorktree, const std::wstring& rPath, const std::string& rDigest)
		{
			if (rArguments.terminalReceipt.empty() || rArguments.terminalReceiptSha256.empty())
			{
				return false;
			}
			Arguments receiptArguments = rArguments;
			receiptArguments.claimReceipt = rArguments.terminalReceipt;
			receiptArguments.claimReceiptSha256 = rArguments.terminalReceiptSha256;
			nlohmann::json receipt;
			if (!IsPathBelow(std::filesystem::path(rArguments.terminalReceipt), rWorktree / L"Temp") || !ReadReceipt(receiptArguments, receipt))
			{
				return false;
			}
			try
			{
				const std::optional<std::string> branch = ResolveGitBranch(rWorktree);
				if (receipt["repository"].get<std::string>() != WideToUtf8(rRepository) || receipt["worktree"].get<std::string>() != WideToUtf8(rWorktree.wstring()) || !branch || receipt["branch"].get<std::string>() != *branch || receipt["plan"].get<std::string>() != WideToUtf8(rPath) || receipt["planSha256"].get<std::string>() != rDigest)
				{
					return false;
				}
				const std::filesystem::path expectedClaimPath = ClaimPath(SchedulerRoot(rRepository), rPath);
				if (std::filesystem::path(Utf8ToWide(receipt["claimPath"].get<std::string>())) != expectedClaimPath)
				{
					return false;
				}
				Claim claim;
				if (!ReadClaim(expectedClaimPath, claim) || !ValidateClaim(claim.json, rRepository, rPath) || !ReceiptMatchesClaim(receipt, claim))
				{
					return false;
				}
				const std::string state = claim.json["state"].get<std::string>();
				return (state == "preparing" || state == "awaiting-landing") && ClaimIsLive(claim) && ClaimBaselineIsAncestorOfWorktreeHead(claim, rWorktree);
			}
			catch (const nlohmann::json::exception&)
			{
				return false;
			}
		}

		void ValidateBaselineMetadata(const Arguments& rArguments, const std::wstring& rRepository, const std::filesystem::path& rWorktree, const std::wstring& rBaseline, std::map<std::wstring, Plan>& rPlans, nlohmann::json& rDiagnostics, nlohmann::json& rNotices)
		{
			std::map<std::wstring, Plan> baselinePlans;
			nlohmann::json ignored = nlohmann::json::array();
			if (!BuildPlansAtCommit(rWorktree, rBaseline, baselinePlans, ignored))
			{
				rDiagnostics.push_back({ { "code", "baseline-read-failed" }, { "message", "could not enumerate baseline Plans" } });
				return;
			}
			std::map<std::wstring, Plan> primaryPlans;
			bool bPrimaryAdvanceInWorktree = false;
			const std::optional<std::string> primaryReference = ResolvePrimaryReference(rRepository);
			const std::optional<std::string> primaryTip = primaryReference ? ResolveRepositoryCommit(rRepository, Utf8ToWide(*primaryReference)) : std::nullopt;
			if (primaryTip && *primaryTip != WideToUtf8(rBaseline) && RunGit({ L"--git-dir", rRepository, L"merge-base", L"--is-ancestor", rBaseline, Utf8ToWide(*primaryTip) }) && CommitIsAncestorOfWorktreeHead(*primaryTip, rWorktree))
			{
				bPrimaryAdvanceInWorktree = BuildPlansAtCommit(rWorktree, Utf8ToWide(*primaryTip), primaryPlans, ignored);
			}
			std::set<std::wstring> classifiedMissing;
			for (const auto& [path, baselinePlan] : baselinePlans)
			{
				auto current = rPlans.find(path);
				const bool bCurrentAbsent = current == rPlans.end();
				const bool bCurrentMissing = bCurrentAbsent || current->second.diagnostic == "missing";
				if (bCurrentMissing && baselinePlan.diagnostic == "manual")
				{
					if (current != rPlans.end()) rPlans.erase(current);
					continue;
				}
				if (!baselinePlan.bValid)
				{
					continue;
				}
				if (bCurrentMissing || !current->second.bValid)
				{
					if (bCurrentMissing) classifiedMissing.insert(path);
					if (!HasTerminalReceiptFor(rArguments, rRepository, rWorktree, path, baselinePlan.digest))
					{
						const auto primary = primaryPlans.find(path);
						const bool bIndexMatchesHead = RunGit({ L"-C", rWorktree.wstring(), L"diff", L"--cached", L"--quiet", L"HEAD", L"--", path }).has_value();
						const bool bWorktreeMatchesIndex = RunGit({ L"-C", rWorktree.wstring(), L"diff", L"--quiet", L"--", path }).has_value();
						const bool bMatchesPrimaryAdvance = bPrimaryAdvanceInWorktree && bIndexMatchesHead && bWorktreeMatchesIndex && ((bCurrentAbsent && primary == primaryPlans.end()) || (!bCurrentMissing && primary != primaryPlans.end() && !primary->second.bValid && primary->second.digest == current->second.digest));
						if (bMatchesPrimaryAdvance)
						{
							rNotices.push_back({ { "plan", WideToUtf8(path) }, { "code", "missing-plan-file" }, { "message", "baseline executable plan was removed or demoted by an incorporated primary advance" } });
						}
						else
						{
							rDiagnostics.push_back({ { "plan", WideToUtf8(path) }, { "code", "baseline-plan-missing-or-demoted" }, { "message", "baseline executable plan was removed or demoted without its terminal receipt" } });
						}
					}
					continue;
				}
				if (current->second.createdUtc != baselinePlan.createdUtc)
				{
					current->second.bValid = false;
					current->second.diagnostic = "createdUtc metadata changed after the baseline";
					rDiagnostics.push_back({ { "plan", WideToUtf8(path) }, { "code", "immutable-created-utc" }, { "message", current->second.diagnostic } });
				}
			}
			for (const auto& [path, plan] : rPlans)
			{
				if (plan.diagnostic == "missing" && !classifiedMissing.contains(path))
				{
					rDiagnostics.push_back({ { "plan", WideToUtf8(path) }, { "code", "invalid-metadata" }, { "message", "tracked plan is missing from worktree" } });
				}
			}
		}

		bool ResolveContext(const Arguments& rArguments, std::wstring& rRepo, std::filesystem::path& rWorktree)
		{
			std::optional<std::wstring> repo = coordination::CanonicalizeDirectoryPath(rArguments.repository);
			std::optional<std::wstring> worktree = coordination::CanonicalizeDirectoryPath(rArguments.worktree);
			if (!repo || !worktree || ResolveGitCommonDirectory(*worktree) != repo)
			{
				return false;
			}
			rRepo = *repo; rWorktree = *worktree;
			return true;
		}

		bool ResolveReceiptContext(const Arguments& rArguments, std::wstring& rRepo, std::filesystem::path& rWorktree)
		{
			std::optional<std::wstring> worktree = coordination::CanonicalizeDirectoryPath(rArguments.worktree);
			if (!worktree)
			{
				return false;
			}
			const std::optional<std::wstring> commonDirectory = ResolveGitCommonDirectory(*worktree);
			if (!commonDirectory)
			{
				return false;
			}
			std::optional<std::wstring> repo;
			if (!rArguments.repository.empty())
			{
				repo = coordination::CanonicalizeDirectoryPath(rArguments.repository);
			}
			else
			{
				repo = commonDirectory;
			}
			if (!repo || repo != commonDirectory)
			{
				return false;
			}
			rRepo = *repo;
			rWorktree = *worktree;
			return true;
		}

		int RunValidate(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveContext(rArguments, repo, worktree))
			{
				return Failure(L"validate", "invalid-context", "validate requires a Git common directory --repo shared by --worktree");
			}
			if (rArguments.baseline.empty())
			{
				return Failure(L"validate", "missing-baseline", "validate requires --baseline");
			}
			const std::optional<std::string> baseline = ResolveCommit(worktree, rArguments.baseline);
			if (!baseline)
			{
				return Failure(L"validate", "baseline-revision-failed", "could not resolve baseline commit");
			}
			std::wstring requestedPlan;
			if (!rArguments.plan.empty() && !NormalizePlanPath(rArguments.plan, requestedPlan))
			{
				return Failure(L"validate", "invalid-plan", "--plan must be a canonical case-sensitive Documents/Plans Markdown path");
			}
			std::map<std::wstring, Plan> plans; nlohmann::json diagnostics = nlohmann::json::array(); nlohmann::json notices = nlohmann::json::array();
			if (!BuildPlans(worktree, plans, diagnostics))
			{
				return Failure(L"validate", "scan-failed", "could not scan Documents/Plans");
			}
			nlohmann::json healed = nlohmann::json::array();
			const std::filesystem::path schedulerRoot = SchedulerRoot(repo);
			const std::filesystem::path guardPath = schedulerRoot / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure(L"validate", "storage-failed", "could not create scheduler storage");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Conflict(L"validate", "busy", "scheduler guard is held");
			}
			ValidateBaselineMetadata(rArguments, repo, worktree, Utf8ToWide(*baseline), plans, diagnostics, notices);
			if (!requestedPlan.empty() && plans.find(requestedPlan) == plans.end())
			{
				std::map<std::wstring, Plan> baselinePlans;
				nlohmann::json ignored = nlohmann::json::array();
				if (!BuildPlansAtCommit(worktree, Utf8ToWide(*baseline), baselinePlans, ignored))
				{
					return Failure(L"validate", "baseline-read-failed", "could not enumerate baseline Plans");
				}
				auto baselinePlan = baselinePlans.find(requestedPlan);
				if (baselinePlan == baselinePlans.end() || !baselinePlan->second.bValid || !HasTerminalReceiptFor(rArguments, repo, worktree, requestedPlan, baselinePlan->second.digest))
				{
					return Conflict(L"validate", "plan-not-found", "--plan is not a current executable Plan or proven terminal baseline plan");
				}
			}
			MarkCycles(plans, diagnostics);
			HealClaims(schedulerRoot, repo, plans, healed);
			nlohmann::json output = { { "operation", "validate" }, { "status", diagnostics.empty() ? "valid" : "invalid" }, { "code", diagnostics.empty() ? "ok" : "invalid-plans" }, { "message", diagnostics.empty() ? "plan metadata is valid" : "some plans are quarantined" }, { "diagnostics", diagnostics }, { "notices", notices }, { "healedClaims", healed }, { "plans", nlohmann::json::array() } };
			for (const auto& [path, plan] : plans)
			{
				if (!plan.bValid || (!requestedPlan.empty() && path != requestedPlan))
				{
					continue;
				}
				for (const std::wstring& dependency : plan.dependencies)
				{
					if (plans.find(dependency) == plans.end())
					{
						output["notices"].push_back({ { "plan", WideToUtf8(path) }, { "code", "stale-dependency" }, { "dependency", WideToUtf8(dependency) } });
					}
				}
			}
			std::vector<const Plan*> outputPlans;
			for (const auto& [path, plan] : plans)
			{
				if (plan.bValid && (requestedPlan.empty() || path == requestedPlan))
				{
					outputPlans.push_back(&plan);
				}
			}
			std::sort(outputPlans.begin(), outputPlans.end(), [](const Plan* pLeft, const Plan* pRight) { return pLeft->createdUtc != pRight->createdUtc ? pLeft->createdUtc < pRight->createdUtc : Utf8PathLess(pLeft->path, pRight->path); });
			for (const Plan* pPlan : outputPlans)
			{
				const std::wstring& path = pPlan->path;
				const Plan& plan = *pPlan;
				nlohmann::json dependencies = nlohmann::json::array();
				for (const std::wstring& dependency : plan.dependencies)
				{
					dependencies.push_back(WideToUtf8(dependency));
				}
				output["plans"].push_back({ { "path", WideToUtf8(path) }, { "createdUtc", plan.createdUtc }, { "dependsOn", dependencies } });
			}
			PrintResult(std::move(output));
			return kiExitOk;
		}

		int RunClaimNext(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveContext(rArguments, repo, worktree) || rArguments.owner.empty() || rArguments.session.empty() || rArguments.branch.empty() || rArguments.primaryWorktree.empty())
			{
				return Failure(L"claim-next", "invalid-context", "claim-next requires repo, primary worktree, worktree, branch, owner, and session");
			}
			const std::filesystem::path root = SchedulerRoot(repo);
			const std::filesystem::path guardPath = root / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure(L"claim-next", "storage-failed", "could not create scheduler storage");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Conflict(L"claim-next", "busy", "scheduler guard is held");
			}
			const std::optional<std::wstring> primaryWorktree = coordination::CanonicalizeDirectoryPath(rArguments.primaryWorktree);
			const std::optional<std::string> worktreeBranch = ResolveGitBranch(worktree);
			const std::optional<std::string> primaryBranch = ResolvePrimaryBranch(repo);
			if (!primaryWorktree || ResolveGitCommonDirectory(*primaryWorktree) != std::optional<std::wstring>(repo) || !worktreeBranch || *worktreeBranch != WideToUtf8(rArguments.branch) || !primaryBranch || ResolveGitBranch(*primaryWorktree) != primaryBranch)
			{
				return Failure(L"claim-next", "git-identity-mismatch", "repo, primary worktree, session worktree, and branch identities do not match live Git state");
			}
			std::wstring requestedPlan;
			if (!rArguments.plan.empty() && !NormalizePlanPath(rArguments.plan, requestedPlan))
			{
				return Failure(L"claim-next", "invalid-plan", "--plan must be a canonical case-sensitive Documents/Plans Markdown path");
			}
			std::optional<std::string> primaryCommit = ResolveCommit(*primaryWorktree, L"HEAD");
			if (!primaryCommit)
			{
				return Failure(L"claim-next", "primary-revision-failed", "could not resolve primary commit");
			}
			const std::optional<std::string> sessionCommit = ResolveCommit(worktree, L"HEAD");
			if (!sessionCommit || !RunGit({ L"--git-dir", repo, L"merge-base", L"--is-ancestor", Utf8ToWide(*sessionCommit), Utf8ToWide(*primaryCommit) }))
			{
				return Failure(L"claim-next", "git-identity-mismatch", "session worktree HEAD is not an ancestor of the primary tip");
			}
			// Two Plan maps: the primary tip drives healing and terminal recovery so a behind session never heals a peer's
			// claim on a plan present only at primary; the session tree drives selection bytes and dependency evaluation.
			std::map<std::wstring, Plan> plans; nlohmann::json diagnostics = nlohmann::json::array();
			if (!BuildPlansAtCommit(*primaryWorktree, Utf8ToWide(*primaryCommit), plans, diagnostics))
			{
				return Failure(L"claim-next", "scan-failed", "could not load Plans from resolved primary commit");
			}
			MarkCycles(plans, diagnostics);
			nlohmann::json healed = nlohmann::json::array(); HealClaims(root, repo, plans, healed);
			std::map<std::wstring, Plan> sessionPlans; nlohmann::json sessionDiagnostics = nlohmann::json::array();
			if (!BuildPlansAtCommit(worktree, Utf8ToWide(*sessionCommit), sessionPlans, sessionDiagnostics))
			{
				return Failure(L"claim-next", "scan-failed", "could not load Plans from session worktree HEAD");
			}
			// One claim per session, discovered by scanning the claims directory independently of either Plan map.  A
			// behind session may own a claim whose plan is present at the primary tip yet absent from its own HEAD tree
			// (or the reverse); gating discovery on map membership would let such a claim escape both classifications and
			// mint a duplicate.  A terminal transaction that removed its target from a tree keeps a retryable receipt
			// regardless of membership, so terminal state alone (not primary-map absence) qualifies for the receipt return.
			std::error_code claimScanError;
			for (std::filesystem::directory_iterator it(ExtendedLengthPath(root / L"claims"), claimScanError), end; !claimScanError && it != end; it.increment(claimScanError))
			{
				if (!it->is_regular_file(claimScanError) || claimScanError || it->path().extension() != L".json")
				{
					continue;
				}
				Claim existing;
				std::wstring existingPlanPath;
				if (!ReadClaim(it->path(), existing) || !existing.json.contains("plan") || !existing.json["plan"].is_string() || !NormalizePlanPath(Utf8ToWide(existing.json["plan"].get<std::string>()), existingPlanPath) || !ValidateClaim(existing.json, repo, existingPlanPath))
				{
					continue;
				}
				if (!ClaimMatchesSession(existing, rArguments, worktree))
				{
					continue;
				}
				Plan ownedPlan {};
				ownedPlan.path = existingPlanPath;
				ownedPlan.digest = existing.json.value("planSha256", "");
				const std::filesystem::path existingPath = ClaimPath(root, existingPlanPath);
				const std::string state = existing.json.value("state", "");
				if (state == "preparing" || state == "awaiting-landing")
				{
					nlohmann::json receipt;
					if (!WriteClaimReceipt(rArguments, existingPath, ownedPlan, receipt))
					{
						return Failure(L"claim-next", "receipt-failed", "existing terminal claim retained because receipt creation failed");
					}
					PrintResult({ { "operation", "claim-next" }, { "status", "ok" }, { "code", "claimed" }, { "message", "existing terminal session claim returned" }, { "claimed", true }, { "plan", WideToUtf8(existingPlanPath) }, { "digest", ownedPlan.digest }, { "baseline", existing.json.value("primaryCommit", "") }, { "claimedAt", existing.json.value("claimedAt", "") }, { "expiresAt", existing.json.value("expiresAt", "") }, { "receipt", receipt }, { "healedClaims", healed } });
					return kiExitOk;
				}
				// Live claim returned idempotently.  Reconcile against session bytes only when the plan is still present in
				// the session tree; a plan the session HEAD no longer carries has no session digest to compare against.
				const auto sessionPlan = sessionPlans.find(existingPlanPath);
				if (sessionPlan != sessionPlans.end() && ownedPlan.digest != sessionPlan->second.digest)
				{
					return Conflict(L"claim-next", "digest-mismatch", "existing owned claim does not match session plan bytes");
				}
				nlohmann::json receipt;
				if (!WriteClaimReceipt(rArguments, existingPath, ownedPlan, receipt))
				{
					return Failure(L"claim-next", "receipt-failed", "existing claim retained because receipt creation failed");
				}
				PrintResult({ { "operation", "claim-next" }, { "status", "ok" }, { "code", "claimed" }, { "message", "existing session claim returned" }, { "claimed", true }, { "plan", WideToUtf8(existingPlanPath) }, { "digest", ownedPlan.digest }, { "baseline", existing.json.value("primaryCommit", "") }, { "claimedAt", existing.json.value("claimedAt", "") }, { "expiresAt", existing.json.value("expiresAt", "") }, { "receipt", receipt }, { "healedClaims", healed } });
				return kiExitOk;
			}
			// Fail closed on a real enumeration error.  A missing claims directory (fresh repo) sets no_such_file_or_directory
			// and is benign — no existing claims, proceed to selection; any other error means the scan may have skipped this
			// session's owned claim, and falling through would mint a duplicate, so refuse rather than violate one-claim-per-session.
			if (claimScanError && claimScanError != std::errc::no_such_file_or_directory)
			{
				return Failure(L"claim-next", "claim-scan-failed", "could not enumerate existing claims");
			}
			std::vector<Plan*> candidates;
			for (auto& [path, plan] : sessionPlans)
			{
				if (!plan.bValid || IsBlockedByDependencies(plan, sessionPlans))
				{
					continue;
				}
				if (!requestedPlan.empty() && path != requestedPlan)
				{
					continue;
				}
				const auto primary = plans.find(path);
				if (primary == plans.end() || !primary->second.bValid)
				{
					continue; // absent or demoted at the primary tip: a peer landing already completed or rejected it
				}
				candidates.push_back(&plan);
			}
			std::sort(candidates.begin(), candidates.end(), [](const Plan* left, const Plan* right) { return left->createdUtc != right->createdUtc ? left->createdUtc < right->createdUtc : Utf8PathLess(left->path, right->path); });
			for (Plan* plan : candidates)
			{
				const std::filesystem::path claimPath = ClaimPath(root, plan->path);
				Claim existing;
				if (ReadClaim(claimPath, existing))
				{
					continue; // claim exists and was not session-owned per the plans-map loop — another session's claim or an unhealable record
				}
				const uint64_t uiClaimedAt = coordination::CurrentUtcTicks();
				nlohmann::json claim = { { "schemaVersion", 1 }, { "repository", WideToUtf8(repo) }, { "plan", WideToUtf8(plan->path) }, { "owner", WideToUtf8(rArguments.owner) }, { "session", WideToUtf8(rArguments.session) }, { "worktree", WideToUtf8(worktree.wstring()) }, { "branch", WideToUtf8(rArguments.branch) }, { "primaryCommit", *sessionCommit }, { "planSha256", plan->digest }, { "claimedAt", coordination::FormatUtcTimestamp(uiClaimedAt) }, { "expiresAt", coordination::FormatUtcTimestamp(uiClaimedAt + kClaimLifetimeTicks) }, { "state", "claimed" } };
				if (!coordination::EnsureParentDirectory(claimPath) || !coordination::WriteMetadataAtomic(claimPath, claim))
				{
					return Failure(L"claim-next", "claim-write-failed", "could not write claim record");
				}
				nlohmann::json receipt;
				if (!WriteClaimReceipt(rArguments, claimPath, *plan, receipt))
				{
					::DeleteFileW(ExtendedLengthPath(claimPath).c_str());
					return Failure(L"claim-next", "receipt-failed", "claim rolled back because receipt creation failed");
				}
				PrintResult({ { "operation", "claim-next" }, { "status", "ok" }, { "code", "claimed" }, { "message", "plan claimed" }, { "claimed", true }, { "plan", WideToUtf8(plan->path) }, { "digest", plan->digest }, { "baseline", *sessionCommit }, { "claimedAt", claim["claimedAt"] }, { "expiresAt", claim["expiresAt"] }, { "receipt", receipt }, { "healedClaims", healed } }); return kiExitOk;
			}
			// Surface both Plan maps' diagnostics: primary-tip (and its cycle marks) plus session-tree scan, so none is dropped.
			nlohmann::json blockers = diagnostics;
			blockers.insert(blockers.end(), sessionDiagnostics.begin(), sessionDiagnostics.end());
			PrintResult({ { "operation", "claim-next" }, { "status", "ok" }, { "code", "none-available" }, { "message", "no eligible Plans plan is available" }, { "claimed", false }, { "blockers", blockers }, { "healedClaims", healed } });
			return kiExitOk;
		}

		bool ReadReceiptIdentity(const Arguments& rArguments, const std::wstring& rRepository, const std::filesystem::path& rWorktree, nlohmann::json& rReceipt, std::wstring& rPlanPath, std::filesystem::path& rClaimPath)
		{
			if (!IsPathBelow(std::filesystem::path(rArguments.claimReceipt), rWorktree / L"Temp") || !ReadReceipt(rArguments, rReceipt) || !NormalizePlanPath(Utf8ToWide(rReceipt["plan"].get<std::string>()), rPlanPath))
			{
				return false;
			}
			const std::optional<std::string> branch = ResolveGitBranch(rWorktree);
			if (!branch || rReceipt["repository"].get<std::string>() != WideToUtf8(rRepository) || rReceipt["worktree"].get<std::string>() != WideToUtf8(rWorktree.wstring()) || rReceipt["branch"].get<std::string>() != *branch || !CommitIsAncestorOfWorktreeHead(rReceipt["primaryCommit"].get<std::string>(), rWorktree))
			{
				return false;
			}
			rClaimPath = Utf8ToWide(rReceipt["claimPath"].get<std::string>());
			return rClaimPath == ClaimPath(SchedulerRoot(rRepository), rPlanPath);
		}

		bool LocateReceiptClaim(const Arguments& rArguments, const std::wstring& rRepository, const std::filesystem::path& rWorktree, std::wstring& rPlanPath, Claim& rClaim)
		{
			nlohmann::json receipt;
			std::filesystem::path claimPath;
			if (!ReadReceiptIdentity(rArguments, rRepository, rWorktree, receipt, rPlanPath, claimPath) || !ReadClaim(claimPath, rClaim) || !ValidateClaim(rClaim.json, rRepository, rPlanPath) || !ReceiptMatchesClaim(receipt, rClaim))
			{
				return false;
			}
			return ClaimIsLive(rClaim) && ClaimBaselineIsAncestorOfWorktreeHead(rClaim, rWorktree);
		}

		int RunClaimStatus(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveReceiptContext(rArguments, repo, worktree))
			{
				return Failure(L"claim-status", "invalid-context", "claim-status requires a receipt-bound Git worktree and repository");
			}
			const std::filesystem::path guardPath = SchedulerRoot(repo) / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure(L"claim-status", "storage-failed", "could not create scheduler storage");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Conflict(L"claim-status", "busy", "scheduler guard is held");
			}
			std::wstring plan; Claim claim;
			if (!LocateReceiptClaim(rArguments, repo, worktree, plan, claim))
			{
				return Conflict(L"claim-status", "receipt-invalid", "claim receipt is invalid or no longer identifies a claim");
			}
			PrintResult({ { "operation", "claim-status" }, { "status", "ok" }, { "code", "claimed" }, { "message", "claim state returned" }, { "plan", WideToUtf8(plan) }, { "claimState", claim.json.value("state", "claimed") }, { "disposition", claim.json.value("disposition", "") }, { "expiresAt", claim.json.value("expiresAt", "") }, { "ownedByReceipt", true } }); return kiExitOk;
		}

		int RunUnclaim(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveReceiptContext(rArguments, repo, worktree))
			{
				return Failure(L"unclaim", "invalid-context", "unclaim requires a receipt-bound Git worktree and repository");
			}
			const std::filesystem::path guardPath = SchedulerRoot(repo) / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure(L"unclaim", "storage-failed", "could not create scheduler storage");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Conflict(L"unclaim", "busy", "scheduler guard is held");
			}
			nlohmann::json receipt;
			std::wstring plan;
			std::filesystem::path claimPath;
			if (!ReadReceiptIdentity(rArguments, repo, worktree, receipt, plan, claimPath))
			{
				return Conflict(L"unclaim", "receipt-invalid", "claim receipt is invalid or does not prove an owned claim");
			}
			Claim claim;
			if (!ReadClaim(claimPath, claim))
			{
				PrintResult({ { "operation", "unclaim" }, { "status", "ok" }, { "code", "already-absent" }, { "message", "claim was already absent" }, { "released", false }, { "plan", WideToUtf8(plan) } });
				return kiExitOk;
			}
			if (!ValidateClaim(claim.json, repo, plan) || !ReceiptMatchesClaim(receipt, claim) || !ClaimIsLive(claim) || !ClaimBaselineIsAncestorOfWorktreeHead(claim, worktree))
			{
				return Conflict(L"unclaim", "receipt-invalid", "claim receipt no longer proves the guarded claim");
			}
			if (::DeleteFileW(ExtendedLengthPath(claim.path).c_str()) == FALSE)
			{
				return Failure(L"unclaim", "delete-failed", "could not remove owned claim");
			}
			PrintResult({ { "operation", "unclaim" }, { "status", "ok" }, { "code", "released" }, { "message", "claim released; plan is immediately eligible" }, { "released", true }, { "plan", WideToUtf8(plan) } });
			return kiExitOk;
		}

		bool RenderDependencies(const Plan& rPlan, const std::wstring& rRemoved, std::string& rBytes)
		{
			std::vector<std::wstring> dependencies = rPlan.dependencies;
			auto found = std::find(dependencies.begin(), dependencies.end(), rRemoved);
			if (found == dependencies.end())
			{
				return false;
			}
			dependencies.erase(found);
			const size_t uiLineEnd = rPlan.bytes.find('\n');
			const size_t uiSuffixStart = uiLineEnd == std::string::npos ? rPlan.bytes.size() : (uiLineEnd > 0 && rPlan.bytes[uiLineEnd - 1] == '\r' ? uiLineEnd - 1 : uiLineEnd);
			nlohmann::json metadata = { { "createdUtc", rPlan.createdUtc }, { "dependsOn", nlohmann::json::array() } };
			for (const std::wstring& dependency : dependencies)
			{
				metadata["dependsOn"].push_back(WideToUtf8(dependency));
			}
			rBytes = std::string(kMarkerPrefix) + metadata.dump() + std::string(kMarkerSuffix) + rPlan.bytes.substr(uiSuffixStart);
			return true;
		}

		int RunPrepare(const std::wstring& rOperation, const Arguments& rArguments)
		{
			if (rOperation == L"prepare-rejection" && !rArguments.bUserAuthorizedRejection)
			{
				return Failure(rOperation, "authorization-required", "prepare-rejection requires --user-authorized-rejection");
			}
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveReceiptContext(rArguments, repo, worktree))
			{
				return Failure(rOperation, "invalid-context", "prepare requires a Git worktree and an optional canonical repo");
			}
			const std::filesystem::path guardPath = SchedulerRoot(repo) / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure(rOperation, "storage-failed", "could not create scheduler storage");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Conflict(rOperation, "busy", "scheduler guard is held");
			}
			std::wstring target; Claim claim;
			if (!LocateReceiptClaim(rArguments, repo, worktree, target, claim))
			{
				return Conflict(rOperation, "receipt-invalid", "claim receipt is invalid");
			}
			std::map<std::wstring, Plan> plans; nlohmann::json diagnostics = nlohmann::json::array();
			if (!BuildPlans(worktree, plans, diagnostics))
			{
				return Failure(rOperation, "scan-failed", "could not scan Plans while preparing terminal state");
			}
			const std::string entryState = claim.json.value("state", "");
			const bool bRecoveringPreparation = entryState == "preparing";
			const bool bRecovering = bRecoveringPreparation || entryState == "awaiting-landing";
			if (!bRecovering)
			{
				auto foundTarget = plans.find(target);
				if (foundTarget == plans.end() || !foundTarget->second.bValid)
				{
					return Conflict(rOperation, "plan-missing", "claimed executable plan is absent or invalid in worktree");
				}
				if (foundTarget->second.digest != claim.json.value("planSha256", ""))
				{
					return Conflict(rOperation, "digest-mismatch", "claimed plan bytes changed");
				}
				claim.json["state"] = "preparing";
				claim.json["disposition"] = rOperation == L"prepare-rejection" ? "rejected" : "completed";
				claim.json["manifest"] = { { "target", { { "path", WideToUtf8(target) }, { "beforeSha256", foundTarget->second.digest }, { "afterSha256", "absent" } } }, { "children", nlohmann::json::array() } };
				for (const auto& [path, plan] : plans)
				{
					if (path == target || std::find(plan.dependencies.begin(), plan.dependencies.end(), target) == plan.dependencies.end())
					{
						continue;
					}
					if (!plan.bValid)
					{
						return Conflict(rOperation, "child-invalid", "direct dependency child is invalid or manual");
					}
					std::string after;
					if (!RenderDependencies(plan, target, after))
					{
						return Conflict(rOperation, "child-invalid", "direct dependency child cannot be rewritten");
					}
					claim.json["manifest"]["children"].push_back({ { "path", WideToUtf8(path) }, { "beforeSha256", plan.digest }, { "afterSha256", coordination::HashSha256(after).value_or("") } });
				}
				if (!coordination::WriteMetadataAtomic(claim.path, claim.json))
				{
					return Failure(rOperation, "manifest-write-failed", "could not persist terminal preparation manifest");
				}
			}
			if (!claim.json.contains("manifest") || !claim.json["manifest"].contains("target") || !claim.json["manifest"].contains("children"))
			{
				return Conflict(rOperation, "manifest-invalid", "terminal claim lacks a recoverable manifest");
			}
			if (!RemovePlanAtomicTemporarySiblings(worktree, target))
			{
				return Failure(rOperation, "orphan-cleanup-failed", "could not clean scheduler atomic temporary files for terminal target");
			}
			for (const nlohmann::json& child : claim.json["manifest"]["children"])
			{
				std::wstring childPath;
				if (!child.contains("path") || !child["path"].is_string() || !NormalizePlanPath(Utf8ToWide(child["path"].get<std::string>()), childPath))
				{
					return Conflict(rOperation, "manifest-invalid", "child manifest is invalid");
				}
				if (!RemovePlanAtomicTemporarySiblings(worktree, childPath))
				{
					return Failure(rOperation, "orphan-cleanup-failed", "could not clean scheduler atomic temporary files for terminal child");
				}
			}
			if (bRecovering)
			{
				std::set<std::wstring> manifestChildren;
				for (const nlohmann::json& child : claim.json["manifest"]["children"])
				{
					if (child.contains("path") && child["path"].is_string())
					{
						manifestChildren.insert(Utf8ToWide(child["path"].get<std::string>()));
					}
				}
				bool bExpandedManifest = false;
				for (const auto& [path, plan] : plans)
				{
					if (path == target || std::find(plan.dependencies.begin(), plan.dependencies.end(), target) == plan.dependencies.end() || manifestChildren.contains(path))
					{
						continue;
					}
					if (!plan.bValid)
					{
						return Conflict(rOperation, "child-invalid", "new direct dependency child is invalid or manual");
					}
					std::string after;
					if (!RenderDependencies(plan, target, after))
					{
						return Conflict(rOperation, "child-invalid", "new direct dependency child cannot be rewritten");
					}
					claim.json["manifest"]["children"].push_back({ { "path", WideToUtf8(path) }, { "beforeSha256", plan.digest }, { "afterSha256", coordination::HashSha256(after).value_or("") } });
					bExpandedManifest = true;
				}
				if (bExpandedManifest)
				{
					claim.json["state"] = "preparing";
					claim.json.erase("changedPaths");
					claim.json.erase("manifestDigest");
					if (!coordination::WriteMetadataAtomic(claim.path, claim.json))
					{
						return Failure(rOperation, "manifest-write-failed", "could not persist reconciled child manifest");
					}
				}
			}
			nlohmann::json changed = nlohmann::json::array();
			for (const nlohmann::json& child : claim.json["manifest"]["children"])
			{
				std::wstring childPath;
				if (!child.contains("path") || !child["path"].is_string() || !NormalizePlanPath(Utf8ToWide(child["path"].get<std::string>()), childPath))
				{
					return Conflict(rOperation, "manifest-invalid", "child manifest is invalid");
				}
				auto found = plans.find(childPath);
				if (found == plans.end() || !found->second.bValid)
				{
					return Conflict(rOperation, "child-invalid", "child plan is not a tracked executable Plan during terminal preparation");
				}
				std::string bytes;
				const std::filesystem::path diskPath = worktree / childPath;
				if (!ReadBytes(diskPath, bytes))
				{
					return Conflict(rOperation, "recovery-conflict", "child plan is absent during terminal recovery");
				}
				const std::string digest = coordination::HashSha256(bytes).value_or("");
				if (digest == child.value("afterSha256", ""))
				{
					if (bRecoveringPreparation) changed.push_back(WideToUtf8(childPath));
					continue;
				}
				if (digest != child.value("beforeSha256", ""))
				{
					return Conflict(rOperation, "recovery-conflict", "child plan has third-party bytes");
				}
				std::string after;
				if (!RenderDependencies(found->second, target, after) || coordination::HashSha256(after).value_or("") != child.value("afterSha256", ""))
				{
					return Conflict(rOperation, "recovery-conflict", "child rewrite no longer matches manifest");
				}
				if (!coordination::WriteBytesAtomic(diskPath, after))
				{
					return Failure(rOperation, "rewrite-failed", "could not rewrite direct child metadata");
				}
				changed.push_back(WideToUtf8(childPath));
			}
			const nlohmann::json& manifestTarget = claim.json["manifest"]["target"];
			const std::filesystem::path targetDiskPath = worktree / target;
			std::string targetBytes;
			if (ReadBytes(targetDiskPath, targetBytes))
			{
				if (plans.find(target) == plans.end())
				{
					return Conflict(rOperation, "plan-untracked", "terminal target is not tracked in the worktree index");
				}
				if (coordination::HashSha256(targetBytes).value_or("") != manifestTarget.value("beforeSha256", ""))
				{
					return Conflict(rOperation, "recovery-conflict", "target plan has third-party bytes");
				}
				if (::DeleteFileW(ExtendedLengthPath(targetDiskPath).c_str()) == FALSE)
				{
					return Failure(rOperation, "delete-failed", "could not delete terminal plan file");
				}
				changed.push_back(WideToUtf8(target));
			}
			else if (std::filesystem::exists(targetDiskPath))
			{
				return Failure(rOperation, "target-read-failed", "could not inspect terminal plan file");
			}
			else if (bRecoveringPreparation)
			{
				changed.push_back(WideToUtf8(target));
			}
			claim.json["state"] = "awaiting-landing";
			claim.json["changedPaths"] = changed;
			claim.json["manifestDigest"] = coordination::HashSha256(JsonText(claim.json["manifest"])).value_or("");
			if (!coordination::WriteMetadataAtomic(claim.path, claim.json))
			{
				return Failure(rOperation, "claim-write-failed", "terminal plan bytes changed but claim state could not be persisted");
			}
			PrintResult({ { "operation", WideToUtf8(rOperation) }, { "status", "ok" }, { "code", bRecovering ? "recovered" : "prepared" }, { "message", "terminal plan deletion prepared" }, { "prepared", true }, { "recovered", bRecovering }, { "disposition", claim.json.value("disposition", "") }, { "changedPaths", changed }, { "claimState", "awaiting-landing" }, { "manifestDigest", claim.json["manifestDigest"] } });
			return kiExitOk;
		}

		int RunReleaseAfterLanding(const Arguments& rArguments)
		{
			if (rArguments.landedCommit.empty())
			{
				return Failure(L"release-after-landing", "missing-landed-commit", "release-after-landing requires --landed-commit");
			}
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveReceiptContext(rArguments, repo, worktree))
			{
				return Failure(L"release-after-landing", "invalid-context", "release requires a Git worktree and an optional canonical repo");
			}
			const std::filesystem::path guardPath = SchedulerRoot(repo) / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure(L"release-after-landing", "storage-failed", "could not create scheduler storage");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Conflict(L"release-after-landing", "busy", "scheduler guard is held");
			}
			nlohmann::json receipt;
			std::wstring target;
			std::filesystem::path claimPath;
			if (!ReadReceiptIdentity(rArguments, repo, worktree, receipt, target, claimPath))
			{
				return Conflict(L"release-after-landing", "receipt-invalid", "claim receipt is invalid");
			}
			const std::optional<std::string> landed = ResolveRepositoryCommit(repo, rArguments.landedCommit);
			if (!landed)
			{
				return Conflict(L"release-after-landing", "landed-commit-invalid", "landed commit cannot be resolved");
			}
			const std::string landedCommit = *landed;
			const std::optional<std::string> primaryReference = ResolvePrimaryReference(repo);
			std::optional<std::string> primaryTip;
			if (primaryReference)
			{
				primaryTip = ResolveRepositoryCommit(repo, Utf8ToWide(*primaryReference));
			}
			if (!primaryTip)
			{
				return Conflict(L"release-after-landing", "primary-tip-invalid", "actual primary branch tip cannot be resolved");
			}
			if (!RunGit({ L"--git-dir", repo, L"merge-base", L"--is-ancestor", Utf8ToWide(receipt["primaryCommit"].get<std::string>()), Utf8ToWide(landedCommit) }))
			{
				return Conflict(L"release-after-landing", "landed-history-invalid", "landed commit does not contain claim primary baseline");
			}
			if (!RunGit({ L"--git-dir", repo, L"merge-base", L"--is-ancestor", Utf8ToWide(landedCommit), Utf8ToWide(*primaryTip) }))
			{
				return Conflict(L"release-after-landing", "landed-not-on-primary", "landed commit is not incorporated into the actual primary branch tip");
			}
			std::map<std::wstring, Plan> primaryPlans;
			nlohmann::json diagnostics = nlohmann::json::array();
			if (!BuildPlansAtCommit(worktree, Utf8ToWide(*primaryTip), primaryPlans, diagnostics))
			{
				return Failure(L"release-after-landing", "primary-read-failed", "could not inspect actual primary Plans tree");
			}
			bool bTerminalStateVerified = primaryPlans.find(target) == primaryPlans.end();
			for (const auto& [path, plan] : primaryPlans)
			{
				if (plan.bValid && std::find(plan.dependencies.begin(), plan.dependencies.end(), target) != plan.dependencies.end())
				{
					bTerminalStateVerified = false;
				}
				if (!plan.bValid && plan.bytes.starts_with(kMarkerPrefix))
				{
					bTerminalStateVerified = false;
				}
			}
			if (!bTerminalStateVerified)
			{
				return Conflict(L"release-after-landing", "terminal-state-not-proven", "actual primary Plans tree still contains target or a direct dependency edge");
			}
			Claim claim;
			if (!ReadClaim(claimPath, claim))
			{
				PrintResult({ { "operation", "release-after-landing" }, { "status", "ok" }, { "code", "already-released" }, { "message", "terminal claim was already released" }, { "released", false }, { "alreadyReleased", true }, { "terminalStateVerified", true }, { "landedCommit", landedCommit } });
				return kiExitOk;
			}
			if (!ValidateClaim(claim.json, repo, target) || !ReceiptMatchesClaim(receipt, claim) || !ClaimIsLive(claim) || !ClaimBaselineIsAncestorOfWorktreeHead(claim, worktree))
			{
				return Conflict(L"release-after-landing", "receipt-invalid", "claim receipt no longer proves the guarded claim");
			}
			if (claim.json["state"].get<std::string>() != "awaiting-landing")
			{
				return Conflict(L"release-after-landing", "not-terminal", "claim was not prepared for landing");
			}
			if (::DeleteFileW(ExtendedLengthPath(claim.path).c_str()) == FALSE)
			{
				return Failure(L"release-after-landing", "delete-failed", "could not remove terminal claim");
			}
			PrintResult({ { "operation", "release-after-landing" }, { "status", "ok" }, { "code", "released" }, { "message", "terminal claim released" }, { "released", true }, { "alreadyReleased", false }, { "terminalStateVerified", true }, { "landedCommit", landedCommit } });
			return kiExitOk;
		}

		std::optional<std::string> ResolveRebaseHeadBranch(const std::filesystem::path& rWorktree)
		{
			// A conflicted `rebase --onto` leaves HEAD detached; the interrupted rebase records the original
			// branch in the per-worktree git dir, so reparent can still match claims mid-conflict.
			std::optional<std::string> gitDirectory = RunGit({ L"-C", rWorktree.wstring(), L"rev-parse", L"--path-format=absolute", L"--git-dir" });
			if (!gitDirectory)
			{
				return std::nullopt;
			}
			TrimLineEnding(*gitDirectory);
			std::string headName;
			if (!ReadBytes(std::filesystem::path(Utf8ToWide(*gitDirectory)) / L"rebase-merge" / L"head-name", headName))
			{
				return std::nullopt;
			}
			TrimLineEnding(headName);
			static constexpr std::string_view kBranchPrefix = "refs/heads/";
			if (!headName.starts_with(kBranchPrefix) || headName.size() == kBranchPrefix.size())
			{
				return std::nullopt;
			}
			return headName.substr(kBranchPrefix.size());
		}

		bool ReplaceReceiptBytes(const std::filesystem::path& rReceipt, const std::string& rBytes)
		{
			// Rewrite an existing receipt in place while preserving WriteClaimReceipt's normal-file shape:
			// write a sibling temporary, then atomically replace.
			static uint32_t suiSequence = 0;
			const std::filesystem::path target = ExtendedLengthPath(rReceipt);
			std::filesystem::path temporary = target;
			temporary += L".tmp." + std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(++suiSequence);
			Handle receipt(::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!receipt.IsValid())
			{
				return false;
			}
			DWORD written = 0;
			const bool bOk = ::WriteFile(receipt.Get(), rBytes.data(), static_cast<DWORD>(rBytes.size()), &written, nullptr) != FALSE && written == rBytes.size() && ::FlushFileBuffers(receipt.Get()) != FALSE;
			receipt.Reset();
			if (bOk && ::MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
			{
				return true;
			}
			::DeleteFileW(temporary.c_str());
			return false;
		}

		// The wrapper re-parents its worktree with `git rebase --onto <newBaseline> <oldBaseline> <branch>`
		// outside the scheduler guard, then calls this op to move its live claim (and matching Temp receipts)
		// onto the squashed tip.  Two invariants the caller must honor:
		//   * Accepted residual: a sub-second window exists between the wrapper's rebase and this op in which a
		//     concurrent machine-local `plan validate`/`claim-next` could HealClaims-delete the not-yet-reparented
		//     claim, because its old primaryCommit is no longer an ancestor of the rebased worktree HEAD.  The
		//     window is wrapper-internal, so the rebase is not moved inside the guarded CLI.
		//   * Mid-conflict prohibition: while a conflicted rebase is unresolved HEAD is detached, so any
		//     `plan validate`/`claim-next` heal-deletes the freshly reparented claim — ResolveGitBranch returns
		//     nullopt and HealClaims' branch check fails before the ancestry check is reached.  Callers must run
		//     no scheduler op until `git rebase --continue` reattaches HEAD.
		int RunReparentClaims(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveContext(rArguments, repo, worktree))
			{
				return Failure(L"reparent-claims", "invalid-context", "reparent-claims requires a Git common directory --repo shared by --worktree");
			}
			const std::string newBaseline = WideToUtf8(rArguments.newBaseline);
			if (!IsLowerHex(newBaseline, 40))
			{
				return Failure(L"reparent-claims", "invalid-baseline", "reparent-claims requires a 40-hex --new-baseline");
			}
			const std::filesystem::path root = SchedulerRoot(repo);
			const std::filesystem::path guardPath = root / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure(L"reparent-claims", "storage-failed", "could not create scheduler storage");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Conflict(L"reparent-claims", "busy", "scheduler guard is held");
			}
			if (!CommitIsAncestorOfWorktreeHead(newBaseline, worktree))
			{
				return Conflict(L"reparent-claims", "worktree-not-reparented", "new baseline is not an ancestor of the worktree HEAD");
			}
			std::optional<std::string> branch = ResolveGitBranch(worktree);
			if (!branch)
			{
				branch = ResolveRebaseHeadBranch(worktree);
			}
			if (!branch)
			{
				return Conflict(L"reparent-claims", "branch-unresolved", "worktree HEAD is detached without a resolvable rebase branch");
			}
			nlohmann::json reparentedClaims = nlohmann::json::array();
			nlohmann::json updatedReceipts = nlohmann::json::array();
			const std::filesystem::path claims = root / L"claims";
			std::error_code error;
			for (std::filesystem::directory_iterator it(ExtendedLengthPath(claims), error), end; !error && it != end; it.increment(error))
			{
				if (!it->is_regular_file(error) || error || it->path().extension() != L".json")
				{
					continue;
				}
				Claim claim;
				std::wstring planPath;
				if (!ReadClaim(it->path(), claim) || !claim.json.contains("plan") || !claim.json["plan"].is_string()
					|| !NormalizePlanPath(Utf8ToWide(claim.json["plan"].get<std::string>()), planPath)
					|| !ValidateClaim(claim.json, repo, planPath) || !ClaimIsLive(claim))
				{
					continue;
				}
				// Select exactly the claims HealClaims would delete: bound to this worktree and branch, not
				// already on the new tip, and no longer an ancestor of the worktree HEAD.  Claim-next stamps
				// primaryCommit from the session HEAD (a primary-tip ancestor, possibly newer than the wrapper
				// receipt baseline), so keying on a fixed old baseline would miss such a claim and leave it to be
				// heal-deleted.
				const std::string oldPrimary = claim.json["primaryCommit"].get<std::string>();
				if (claim.json["worktree"].get<std::string>() != WideToUtf8(worktree.wstring())
					|| claim.json["branch"].get<std::string>() != *branch
					|| oldPrimary == newBaseline
					|| CommitIsAncestorOfWorktreeHead(oldPrimary, worktree))
				{
					continue;
				}
				const std::string oldInitialDigest = ClaimReceiptDigest(claim.json, claim.digest);
				claim.json["primaryCommit"] = newBaseline;
				if (!coordination::WriteMetadataAtomic(claim.path, claim.json))
				{
					return Failure(L"reparent-claims", "claim-write-failed", "could not persist reparented claim");
				}
				Claim reparented;
				if (!ReadClaim(claim.path, reparented))
				{
					// Best-effort restore the claim's own old primaryCommit so a rerun re-selects it (again orphaned
					// from the worktree HEAD) and retries; otherwise the claim carries newBaseline while receipts still
					// chain to the old digest.  A crash between the two WriteMetadataAtomic writes leaves that same
					// divergence and is the plan's accepted granularity.
					claim.json["primaryCommit"] = oldPrimary;
					coordination::WriteMetadataAtomic(claim.path, claim.json);
					return Failure(L"reparent-claims", "claim-read-failed", "could not reread reparented claim");
				}
				const std::string newInitialDigest = ClaimReceiptDigest(reparented.json, reparented.digest);
				reparentedClaims.push_back(WideToUtf8(planPath));

				// Rewrite this claim's chained Temp receipts.  Match on the ClaimPath value a receipt records
				// (mirroring ReadReceiptIdentity), not the enumerated extended-length claim path.
				const std::string claimPathText = WideToUtf8(ClaimPath(root, planPath).wstring());
				const std::filesystem::path temp = worktree / L"Temp";
				const DWORD tempAttributes = ::GetFileAttributesW(ExtendedLengthPath(temp).c_str());
				if (tempAttributes == INVALID_FILE_ATTRIBUTES || (tempAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (tempAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
				{
					continue;
				}
				std::error_code receiptError;
				for (std::filesystem::directory_iterator receiptIt(ExtendedLengthPath(temp), receiptError), receiptEnd; !receiptError && receiptIt != receiptEnd; receiptIt.increment(receiptError))
				{
					if (receiptIt->path().extension() != L".json")
					{
						continue;
					}
					const std::filesystem::path receiptPath = temp / receiptIt->path().filename();
					const DWORD attributes = ::GetFileAttributesW(ExtendedLengthPath(receiptPath).c_str());
					if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
					{
						continue;
					}
					std::string receiptBytes;
					if (!ReadBytes(receiptPath, receiptBytes))
					{
						continue;
					}
					nlohmann::json receiptJson;
					try
					{
						receiptJson = nlohmann::json::parse(receiptBytes);
					}
					catch (const nlohmann::json::exception&)
					{
						continue;
					}
					if (!receiptJson.is_object() || receiptJson.size() != 11 || !receiptJson.contains("schemaVersion") || !receiptJson["schemaVersion"].is_number_integer() || receiptJson["schemaVersion"].get<int>() != 1)
					{
						continue;
					}
					bool bWellFormed = true;
					for (const char* field : { "claimPath", "claimSha256", "repository", "plan", "owner", "session", "worktree", "branch", "primaryCommit", "planSha256" })
					{
						if (!receiptJson.contains(field) || !receiptJson[field].is_string() || receiptJson[field].get<std::string>().empty())
						{
							bWellFormed = false;
							break;
						}
					}
					if (!bWellFormed || !IsLowerHex(receiptJson["claimSha256"].get<std::string>(), 64) || !IsLowerHex(receiptJson["primaryCommit"].get<std::string>(), 40) || !IsLowerHex(receiptJson["planSha256"].get<std::string>(), 64))
					{
						continue;
					}
					if (receiptJson["claimPath"].get<std::string>() != claimPathText || receiptJson["claimSha256"].get<std::string>() != oldInitialDigest || receiptJson["primaryCommit"].get<std::string>() != oldPrimary)
					{
						continue;
					}
					receiptJson["primaryCommit"] = newBaseline;
					receiptJson["claimSha256"] = newInitialDigest;
					const std::string rewritten = receiptJson.dump();
					if (!ReplaceReceiptBytes(receiptPath, rewritten))
					{
						// Best-effort restore the claim's own old primaryCommit so a rerun re-selects it and rewrites the
						// remaining receipts; already-rewritten receipts reproduce identical digests on retry.  A crash
						// mid-restore leaves the claim/receipt divergence and is the plan's accepted fail-closed granularity.
						claim.json["primaryCommit"] = oldPrimary;
						coordination::WriteMetadataAtomic(claim.path, claim.json);
						return Failure(L"reparent-claims", "receipt-write-failed", "could not rewrite claim receipt");
					}
					updatedReceipts.push_back({ { "path", WideToUtf8(receiptPath.wstring()) }, { "sha256", coordination::HashSha256(rewritten).value_or("") } });
				}
			}
			PrintResult({ { "operation", "reparent-claims" }, { "status", "ok" }, { "code", reparentedClaims.empty() ? "no-claims" : "reparented" }, { "message", reparentedClaims.empty() ? "no live claim matched the old baseline" : "claim baseline reparented onto the new tip" }, { "reparentedClaims", reparentedClaims }, { "updatedReceipts", updatedReceipts } });
			return kiExitOk;
		}
	}

	int RunPlanSchedulerCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		if (iArgumentCount < 3)
		{
			return Failure(L"plan", "usage", "plan requires validate, claim-next, claim-status, unclaim, prepare-completion, prepare-rejection, reparent-claims, or release-after-landing");
		}
		const std::wstring operation = ToLowerInvariant(pArgumentValues[2]);
		Arguments arguments {};
		if (!ParseArguments(iArgumentCount, pArgumentValues, 3, arguments))
		{
			return Failure(operation, "usage", "unknown or incomplete plan option");
		}
		if (operation == L"validate")
		{
			return RunValidate(arguments);
		}
		if (operation == L"claim-next")
		{
			return RunClaimNext(arguments);
		}
		if (operation == L"claim-status")
		{
			return RunClaimStatus(arguments);
		}
		if (operation == L"unclaim")
		{
			return RunUnclaim(arguments);
		}
		if (operation == L"prepare-completion" || operation == L"prepare-rejection")
		{
			return RunPrepare(operation, arguments);
		}
		if (operation == L"release-after-landing")
		{
			return RunReleaseAfterLanding(arguments);
		}
		if (operation == L"reparent-claims")
		{
			return RunReparentClaims(arguments);
		}
		return Failure(operation, "usage", "unknown plan operation");
	}
}
