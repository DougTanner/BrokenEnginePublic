#include "PlanScheduler.h"
#include "PlanMetadata.h"

#include "CoordinationStore.h"
#include "ToolCliCommon.h"

#include <algorithm>
#include <iostream>
#include <map>

namespace toolcli
{
	namespace
	{
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
			bool bUserAuthorizedRejection = false;
		};

		struct Claim
		{
			std::filesystem::path path;
			nlohmann::json json;
		};

		void TrimLineEnding(std::string& rValue)
		{
			while (!rValue.empty() && (rValue.back() == '\r' || rValue.back() == '\n')) rValue.pop_back();
		}


		bool IsLowerHex(const std::string& rValue, size_t iLength)
		{
			return rValue.size() == iLength && std::all_of(rValue.begin(), rValue.end(), [](char value)
			{
				return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
			});
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

		void PrintResult(nlohmann::json value, int iSchemaVersion)
		{
			value["schemaVersion"] = iSchemaVersion;
			std::cout << value.dump() << '\n';
		}

		int Failure(const std::string& rCode, int iExitCode = kiExitFailure)
		{
			PrintResult({ { "status", "error" }, { "code", rCode } }, 2);
			return iExitCode;
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
			return !error || error == std::errc::no_such_file_or_directory;
		}


		std::optional<std::filesystem::path> SchedulerRoot(const std::wstring& rRepository)
		{
			const std::filesystem::path localApplicationData = GetLocalApplicationDataPath();
			if (localApplicationData.empty())
			{
				return std::nullopt;
			}
			const std::string hash = coordination::HashSha256(WideToUtf8(rRepository)).value_or("invalid");
			return localApplicationData / L"BrokenEngineLocks" / L"plan-scheduler" / Utf8ToWide(hash);
		}

		std::filesystem::path ClaimPath(const std::filesystem::path& rRoot, const std::wstring& rPlan)
		{
			return rRoot / L"claims" / Utf8ToWide(coordination::HashSha256(WideToUtf8(rPlan)).value_or("invalid") + ".json");
		}

		bool ValidateClaim(const nlohmann::json& rClaim, const std::wstring& rRepository, const std::wstring& rPlan)
		{
			try
			{
				// An exact field count rejects every schema-v1 record, which is how the pre-cutover claims are collected.
				if (!rClaim.is_object() || rClaim.size() != 9 || !rClaim.contains("schemaVersion") || !rClaim["schemaVersion"].is_number_integer() || rClaim["schemaVersion"].get<int>() != 2)
				{
					return false;
				}
				std::map<std::string, std::string> fields;
				for (const char* field : { "repository", "plan", "owner", "session", "worktree", "branch", "claimedAt", "expiresAt" })
				{
					if (!ReadRequiredString(rClaim, field, fields.try_emplace(field).first->second))
					{
						return false;
					}
				}
				if (fields.at("repository") != WideToUtf8(rRepository) || fields.at("plan") != WideToUtf8(rPlan))
				{
					return false;
				}
				uint64_t uiClaimedAt = 0;
				uint64_t uiExpiresAt = 0;
				return ParseCanonicalUtcTimestamp(fields.at("claimedAt"), uiClaimedAt) && ParseCanonicalUtcTimestamp(fields.at("expiresAt"), uiExpiresAt) && uiExpiresAt > uiClaimedAt && uiExpiresAt - uiClaimedAt == kClaimLifetimeTicks;
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
				return true;
			}
			catch (const nlohmann::json::exception&)
			{
				return false;
			}
		}

		bool ClaimIsLive(const Claim& rClaim)
		{
			uint64_t uiExpiry = 0;
			return coordination::ParseUtcTimestamp(rClaim.json["expiresAt"].get<std::string>(), uiExpiry) && uiExpiry > coordination::CurrentUtcTicks();
		}

		void HealClaims(const std::filesystem::path& rRoot, const std::wstring& rRepository, const std::map<std::wstring, Plan>& rPrimaryPlans, nlohmann::json& rHealed)
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
				std::wstring path;
				bool bRemove = !ReadClaim(it->path(), claim) || !claim.json.contains("plan") || !claim.json["plan"].is_string() || !NormalizePlanPath(Utf8ToWide(claim.json["plan"].get<std::string>()), path) || !ValidateClaim(claim.json, rRepository, path);
				if (!bRemove && it->path().filename() != ClaimPath(rRoot, path).filename())
				{
					bRemove = true;
				}
				if (!bRemove && !ClaimIsLive(claim))
				{
					bRemove = true;
				}
				// Absent or demoted at the primary tip means a peer landing already completed or rejected the plan.  This
				// rule is what lets the landing script delete its own claim best-effort: a missed delete heals here.
				if (!bRemove)
				{
					const auto primary = rPrimaryPlans.find(path);
					if (primary == rPrimaryPlans.end() || !primary->second.bValid)
					{
						bRemove = true;
					}
				}
				if (!bRemove && !coordination::CanonicalizeDirectoryPath(Utf8ToWide(claim.json["worktree"].get<std::string>())))
				{
					bRemove = true;
				}
				if (bRemove && ::DeleteFileW(ExtendedLengthPath(it->path()).c_str()) != FALSE)
				{
					rHealed.push_back(WideToUtf8(it->path().filename().wstring()));
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

		// Healing classifies against the primary tip rather than the caller's tree, so a session that has already
		// deleted its own terminal target never heals its own live claim.
		bool BuildPrimaryTipPlans(const std::wstring& rRepository, const std::filesystem::path& rWorktree, std::map<std::wstring, Plan>& rPlans)
		{
			const std::optional<std::string> reference = ResolvePrimaryReference(rRepository);
			const std::optional<std::string> tip = reference ? ResolveRepositoryCommit(rRepository, Utf8ToWide(*reference)) : std::nullopt;
			if (!tip)
			{
				return false;
			}
			nlohmann::json ignored = nlohmann::json::array();
			return BuildPlansAtCommit(rWorktree, Utf8ToWide(*tip), rPlans, ignored);
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

		bool ResolveWorktreeContext(const Arguments& rArguments, std::wstring& rRepo, std::filesystem::path& rWorktree)
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
			std::optional<std::wstring> repo = commonDirectory;
			if (!rArguments.repository.empty())
			{
				repo = coordination::CanonicalizeDirectoryPath(rArguments.repository);
			}
			if (!repo || repo != commonDirectory)
			{
				return false;
			}
			rRepo = *repo;
			rWorktree = *worktree;
			return true;
		}

		// One claim per session, discovered by owner/session identity rather than by plan membership: a session whose
		// tree no longer carries its claimed plan still owns the claim, and gating discovery on a plan map would let it
		// escape and mint a duplicate.  Any scan ambiguity fails closed for the same reason.
		bool FindSessionClaim(const std::filesystem::path& rRoot, const std::wstring& rRepository, const Arguments& rArguments, std::wstring& rPlanPath, Claim& rClaim, bool& rbFound)
		{
			rbFound = false;
			std::error_code error;
			for (std::filesystem::directory_iterator it(ExtendedLengthPath(rRoot / L"claims"), error), end; !error && it != end; it.increment(error))
			{
				if (!it->is_regular_file(error) || error || it->path().extension() != L".json")
				{
					continue;
				}
				Claim claim;
				std::wstring path;
				if (!ReadClaim(it->path(), claim) || !claim.json.contains("plan") || !claim.json["plan"].is_string() || !NormalizePlanPath(Utf8ToWide(claim.json["plan"].get<std::string>()), path) || !ValidateClaim(claim.json, rRepository, path) || !ClaimIsLive(claim))
				{
					continue;
				}
				if (claim.json["owner"].get<std::string>() != WideToUtf8(rArguments.owner) || claim.json["session"].get<std::string>() != WideToUtf8(rArguments.session))
				{
					continue;
				}
				rPlanPath = path;
				rClaim = std::move(claim);
				rbFound = true;
				return true;
			}
			return !error || error == std::errc::no_such_file_or_directory;
		}

		void PrintClaim(const std::string& rCode, const std::wstring& rPlanPath, const nlohmann::json& rClaim)
		{
			PrintResult({ { "status", "ok" }, { "code", rCode }, { "plan", WideToUtf8(rPlanPath) }, { "owner", rClaim["owner"] }, { "session", rClaim["session"] }, { "worktree", rClaim["worktree"] }, { "branch", rClaim["branch"] }, { "claimedAt", rClaim["claimedAt"] }, { "expiresAt", rClaim["expiresAt"] } }, 2);
		}

		int RunValidate(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveContext(rArguments, repo, worktree))
			{
				return Failure("invalid-context");
			}
			std::wstring requestedPlan;
			if (!rArguments.plan.empty() && !NormalizePlanPath(rArguments.plan, requestedPlan))
			{
				return Failure("invalid-plan");
			}
			std::map<std::wstring, Plan> plans; nlohmann::json diagnostics = nlohmann::json::array();
			if (!BuildPlans(worktree, plans, diagnostics))
			{
				return Failure("scan-failed");
			}
			const std::optional<std::filesystem::path> schedulerRoot = SchedulerRoot(repo);
			if (!schedulerRoot)
			{
				return Failure("local-app-data-unavailable");
			}
			const std::filesystem::path guardPath = *schedulerRoot / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure("storage-failed");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Failure("busy", kiExitStateConflict);
			}
			if (!requestedPlan.empty() && plans.find(requestedPlan) == plans.end())
			{
				return Failure("plan-not-found", kiExitStateConflict);
			}
			MarkCycles(plans, diagnostics);
			nlohmann::json healed = nlohmann::json::array();
			std::map<std::wstring, Plan> primaryPlans;
			if (BuildPrimaryTipPlans(repo, worktree, primaryPlans))
			{
				HealClaims(*schedulerRoot, repo, primaryPlans, healed);
			}
			nlohmann::json output = { { "operation", "validate" }, { "status", diagnostics.empty() ? "valid" : "invalid" }, { "code", diagnostics.empty() ? "ok" : "invalid-plans" }, { "message", diagnostics.empty() ? "plan metadata is valid" : "some plans are quarantined" }, { "diagnostics", diagnostics }, { "notices", nlohmann::json::array() }, { "healedClaims", healed }, { "plans", nlohmann::json::array() } };
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
				nlohmann::json dependencies = nlohmann::json::array();
				for (const std::wstring& dependency : pPlan->dependencies)
				{
					dependencies.push_back(WideToUtf8(dependency));
				}
				output["plans"].push_back({ { "path", WideToUtf8(pPlan->path) }, { "createdUtc", pPlan->createdUtc }, { "dependsOn", dependencies } });
			}
			PrintResult(std::move(output), 1);
			return kiExitOk;
		}

		// A read-only preview of what claim-next would decide at this tree state.  It heals nothing, takes no guard, and
		// creates no scheduler storage, so a claim record it cannot use is ignored here rather than deleted.
		int RunList(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveContext(rArguments, repo, worktree))
			{
				return Failure("invalid-context");
			}
			const std::optional<std::string> primaryReference = ResolvePrimaryReference(repo);
			const std::optional<std::string> primaryCommit = primaryReference ? ResolveRepositoryCommit(repo, Utf8ToWide(*primaryReference)) : std::nullopt;
			if (!primaryCommit)
			{
				return Failure("primary-revision-failed");
			}
			const std::optional<std::string> sessionCommit = ResolveCommit(worktree, L"HEAD");
			if (!sessionCommit || !RunGit({ L"--git-dir", repo, L"merge-base", L"--is-ancestor", Utf8ToWide(*sessionCommit), Utf8ToWide(*primaryCommit) }))
			{
				return Failure("git-identity-mismatch");
			}
			// Same two Plan maps claim-next evaluates: the primary tip decides eligibility, the session tree supplies the
			// rows and their marker bytes.  Metadata that never parsed cannot fill a row, so it stays in the diagnostics.
			std::map<std::wstring, Plan> primaryPlans; nlohmann::json primaryDiagnostics = nlohmann::json::array();
			std::map<std::wstring, Plan> sessionPlans; nlohmann::json diagnostics = nlohmann::json::array();
			if (!BuildPlansAtCommit(worktree, Utf8ToWide(*primaryCommit), primaryPlans, primaryDiagnostics) || !BuildPlansAtCommit(worktree, Utf8ToWide(*sessionCommit), sessionPlans, diagnostics))
			{
				return Failure("scan-failed");
			}
			MarkCycles(primaryPlans, diagnostics);
			std::vector<const Plan*> rows;
			for (const auto& [path, plan] : sessionPlans)
			{
				if (plan.bValid)
				{
					rows.push_back(&plan);
				}
			}
			std::sort(rows.begin(), rows.end(), [](const Plan* pLeft, const Plan* pRight) { return pLeft->createdUtc != pRight->createdUtc ? pLeft->createdUtc < pRight->createdUtc : Utf8PathLess(pLeft->path, pRight->path); });
			const std::optional<std::filesystem::path> schedulerRoot = SchedulerRoot(repo);
			if (!schedulerRoot)
			{
				return Failure("local-app-data-unavailable");
			}
			const std::filesystem::path& root = *schedulerRoot;
			nlohmann::json output = { { "operation", "list" }, { "status", "ok" }, { "code", "ok" }, { "diagnostics", diagnostics }, { "plans", nlohmann::json::array() } };
			for (const Plan* pPlan : rows)
			{
				nlohmann::json dependencies = nlohmann::json::array();
				for (const std::wstring& dependency : pPlan->dependencies)
				{
					dependencies.push_back(WideToUtf8(dependency));
				}
				nlohmann::json row = { { "path", WideToUtf8(pPlan->path) }, { "createdUtc", pPlan->createdUtc }, { "dependsOn", dependencies } };
				Claim claim;
				const bool bClaimed = ReadClaim(ClaimPath(root, pPlan->path), claim) && ValidateClaim(claim.json, repo, pPlan->path) && ClaimIsLive(claim) && coordination::CanonicalizeDirectoryPath(Utf8ToWide(claim.json["worktree"].get<std::string>())).has_value();
				if (bClaimed)
				{
					row["claim"] = { { "session", claim.json["session"] }, { "worktree", claim.json["worktree"] }, { "expiresAt", claim.json["expiresAt"] } };
				}
				const auto primary = primaryPlans.find(pPlan->path);
				if (primary == primaryPlans.end() || !primary->second.bValid)
				{
					// Excluded from selection for a reason that is not a dependency edge: a peer landing already removed
					// the plan, or the primary tip quarantines it.
					row["state"] = "quarantined";
					row["diagnostic"] = primary == primaryPlans.end() ? "absent at the primary tip" : primary->second.diagnostic;
				}
				else if (IsBlockedByDependencies(*pPlan, sessionPlans) || IsBlockedByDependencies(primary->second, primaryPlans))
				{
					std::vector<std::wstring> blocking;
					for (const std::wstring& dependency : pPlan->dependencies)
					{
						if (sessionPlans.find(dependency) != sessionPlans.end())
						{
							blocking.push_back(dependency);
						}
					}
					for (const std::wstring& dependency : primary->second.dependencies)
					{
						if (primaryPlans.find(dependency) != primaryPlans.end() && std::find(blocking.begin(), blocking.end(), dependency) == blocking.end())
						{
							blocking.push_back(dependency);
						}
					}
					std::sort(blocking.begin(), blocking.end(), Utf8PathLess);
					row["state"] = "blocked";
					row["blockedBy"] = nlohmann::json::array();
					for (const std::wstring& dependency : blocking)
					{
						row["blockedBy"].push_back(WideToUtf8(dependency));
					}
				}
				else
				{
					row["state"] = bClaimed ? "claimed" : "eligible";
				}
				output["plans"].push_back(std::move(row));
			}
			PrintResult(std::move(output), 1);
			return kiExitOk;
		}

		int RunClaimNext(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveContext(rArguments, repo, worktree) || rArguments.owner.empty() || rArguments.session.empty() || rArguments.branch.empty() || rArguments.primaryWorktree.empty())
			{
				return Failure("invalid-context");
			}
			const std::optional<std::filesystem::path> schedulerRoot = SchedulerRoot(repo);
			if (!schedulerRoot)
			{
				return Failure("local-app-data-unavailable");
			}
			const std::filesystem::path& root = *schedulerRoot;
			const std::filesystem::path guardPath = root / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure("storage-failed");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Failure("busy", kiExitStateConflict);
			}
			const std::optional<std::wstring> primaryWorktree = coordination::CanonicalizeDirectoryPath(rArguments.primaryWorktree);
			const std::optional<std::string> worktreeBranch = ResolveGitBranch(worktree);
			const std::optional<std::string> primaryBranch = ResolvePrimaryBranch(repo);
			if (!primaryWorktree || ResolveGitCommonDirectory(*primaryWorktree) != std::optional<std::wstring>(repo) || !worktreeBranch || *worktreeBranch != WideToUtf8(rArguments.branch) || !primaryBranch || ResolveGitBranch(*primaryWorktree) != primaryBranch)
			{
				return Failure("git-identity-mismatch");
			}
			std::wstring requestedPlan;
			if (!rArguments.plan.empty() && !NormalizePlanPath(rArguments.plan, requestedPlan))
			{
				return Failure("invalid-plan");
			}
			const std::optional<std::string> primaryCommit = ResolveCommit(*primaryWorktree, L"HEAD");
			if (!primaryCommit)
			{
				return Failure("primary-revision-failed");
			}
			const std::optional<std::string> sessionCommit = ResolveCommit(worktree, L"HEAD");
			if (!sessionCommit || !RunGit({ L"--git-dir", repo, L"merge-base", L"--is-ancestor", Utf8ToWide(*sessionCommit), Utf8ToWide(*primaryCommit) }))
			{
				return Failure("git-identity-mismatch");
			}
			// Two Plan maps: the primary tip drives healing and eligibility - including dependency blocking, so a behind
			// session neither heals a peer's claim on a plan present only at primary nor claims past a dependency edge
			// landed after its baseline; the session tree drives selection bytes and its own dependency evaluation.
			std::map<std::wstring, Plan> plans; nlohmann::json diagnostics = nlohmann::json::array();
			if (!BuildPlansAtCommit(*primaryWorktree, Utf8ToWide(*primaryCommit), plans, diagnostics))
			{
				return Failure("scan-failed");
			}
			MarkCycles(plans, diagnostics);
			nlohmann::json healed = nlohmann::json::array();
			HealClaims(root, repo, plans, healed);
			std::map<std::wstring, Plan> sessionPlans; nlohmann::json sessionDiagnostics = nlohmann::json::array();
			if (!BuildPlansAtCommit(worktree, Utf8ToWide(*sessionCommit), sessionPlans, sessionDiagnostics))
			{
				return Failure("scan-failed");
			}
			std::wstring existingPlanPath; Claim existing; bool bFound = false;
			if (!FindSessionClaim(root, repo, rArguments, existingPlanPath, existing, bFound))
			{
				return Failure("claim-scan-failed");
			}
			if (bFound)
			{
				PrintClaim("existing", existingPlanPath, existing.json);
				return kiExitOk;
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
				if (IsBlockedByDependencies(primary->second, plans))
				{
					continue; // a prerequisite landed at the primary tip after this session's baseline
				}
				candidates.push_back(&plan);
			}
			std::sort(candidates.begin(), candidates.end(), [](const Plan* pLeft, const Plan* pRight) { return pLeft->createdUtc != pRight->createdUtc ? pLeft->createdUtc < pRight->createdUtc : Utf8PathLess(pLeft->path, pRight->path); });
			for (const Plan* pPlan : candidates)
			{
				const std::filesystem::path claimPath = ClaimPath(root, pPlan->path);
				Claim occupied;
				if (ReadClaim(claimPath, occupied))
				{
					continue; // claimed by another session, or an unhealable record
				}
				const uint64_t uiClaimedAt = coordination::CurrentUtcTicks();
				nlohmann::json claim = { { "schemaVersion", 2 }, { "repository", WideToUtf8(repo) }, { "plan", WideToUtf8(pPlan->path) }, { "owner", WideToUtf8(rArguments.owner) }, { "session", WideToUtf8(rArguments.session) }, { "worktree", WideToUtf8(worktree.wstring()) }, { "branch", WideToUtf8(rArguments.branch) }, { "claimedAt", coordination::FormatUtcTimestamp(uiClaimedAt) }, { "expiresAt", coordination::FormatUtcTimestamp(uiClaimedAt + kClaimLifetimeTicks) } };
				if (!coordination::EnsureParentDirectory(claimPath) || !coordination::WriteMetadataAtomic(claimPath, claim))
				{
					return Failure("claim-write-failed");
				}
				PrintClaim("claimed", pPlan->path, claim);
				return kiExitOk;
			}
			PrintResult({ { "status", "ok" }, { "code", "none" } }, 2);
			return kiExitOk;
		}

		int RunClaimStatus(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveWorktreeContext(rArguments, repo, worktree) || rArguments.owner.empty() || rArguments.session.empty())
			{
				return Failure("invalid-context");
			}
			const std::optional<std::filesystem::path> schedulerRoot = SchedulerRoot(repo);
			if (!schedulerRoot)
			{
				return Failure("local-app-data-unavailable");
			}
			const std::filesystem::path& root = *schedulerRoot;
			const std::filesystem::path guardPath = root / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure("storage-failed");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Failure("busy", kiExitStateConflict);
			}
			std::wstring plan; Claim claim; bool bFound = false;
			if (!FindSessionClaim(root, repo, rArguments, plan, claim, bFound))
			{
				return Failure("claim-scan-failed");
			}
			if (!bFound)
			{
				PrintResult({ { "status", "ok" }, { "code", "none" } }, 2);
				return kiExitOk;
			}
			PrintClaim("claimed", plan, claim.json);
			return kiExitOk;
		}

		int RunUnclaim(const Arguments& rArguments)
		{
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveWorktreeContext(rArguments, repo, worktree) || rArguments.owner.empty() || rArguments.session.empty())
			{
				return Failure("invalid-context");
			}
			const std::optional<std::filesystem::path> schedulerRoot = SchedulerRoot(repo);
			if (!schedulerRoot)
			{
				return Failure("local-app-data-unavailable");
			}
			const std::filesystem::path& root = *schedulerRoot;
			const std::filesystem::path guardPath = root / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure("storage-failed");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Failure("busy", kiExitStateConflict);
			}
			std::wstring plan; Claim claim; bool bFound = false;
			if (!FindSessionClaim(root, repo, rArguments, plan, claim, bFound))
			{
				return Failure("claim-scan-failed");
			}
			if (!bFound)
			{
				PrintResult({ { "status", "ok" }, { "code", "already-absent" } }, 2);
				return kiExitOk;
			}
			if (::DeleteFileW(ExtendedLengthPath(claim.path).c_str()) == FALSE)
			{
				return Failure("delete-failed");
			}
			PrintResult({ { "status", "ok" }, { "code", "released" } }, 2);
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

		// Terminal preparation owns exactly two edits: drop the target from every direct dependency child's byte-zero
		// marker, and delete the target Plan file.  Both are idempotent by construction — a child whose marker no longer
		// lists the target is not selected, and an absent target is skipped — so a rerun reports only what it changed.
		int RunTerminal(const std::wstring& rOperation, const Arguments& rArguments)
		{
			const bool bReject = rOperation == L"reject";
			if (bReject && !rArguments.bUserAuthorizedRejection)
			{
				return Failure("authorization-required");
			}
			std::wstring repo; std::filesystem::path worktree;
			if (!ResolveWorktreeContext(rArguments, repo, worktree) || rArguments.owner.empty() || rArguments.session.empty())
			{
				return Failure("invalid-context");
			}
			const std::optional<std::filesystem::path> schedulerRoot = SchedulerRoot(repo);
			if (!schedulerRoot)
			{
				return Failure("local-app-data-unavailable");
			}
			const std::filesystem::path& root = *schedulerRoot;
			const std::filesystem::path guardPath = root / L"scheduler.guard";
			if (!coordination::EnsureParentDirectory(guardPath))
			{
				return Failure("storage-failed");
			}
			coordination::Guard guard(guardPath);
			if (!guard.IsValid())
			{
				return Failure("busy", kiExitStateConflict);
			}
			std::wstring target; Claim claim; bool bFound = false;
			if (!FindSessionClaim(root, repo, rArguments, target, claim, bFound))
			{
				return Failure("claim-scan-failed");
			}
			if (!bFound)
			{
				return Failure("claim-missing", kiExitStateConflict);
			}
			// The claim is found by owner/session identity, so a session invoked from the wrong checkout would otherwise
			// rewrite child markers and delete the Plan in that unrelated tree.  Both sides are canonical final paths.
			if (WideToUtf8(worktree.wstring()) != claim.json["worktree"].get<std::string>())
			{
				return Failure("claim-worktree-mismatch", kiExitStateConflict);
			}
			std::map<std::wstring, Plan> plans; nlohmann::json diagnostics = nlohmann::json::array();
			if (!BuildPlans(worktree, plans, diagnostics))
			{
				return Failure("scan-failed");
			}
			if (!RemovePlanAtomicTemporarySiblings(worktree, target))
			{
				return Failure("orphan-cleanup-failed");
			}
			nlohmann::json changedChildren = nlohmann::json::array();
			for (const auto& [path, plan] : plans)
			{
				if (path == target || std::find(plan.dependencies.begin(), plan.dependencies.end(), target) == plan.dependencies.end())
				{
					continue;
				}
				if (!plan.bValid)
				{
					return Failure("child-invalid", kiExitStateConflict);
				}
				std::string after;
				if (!RenderDependencies(plan, target, after))
				{
					return Failure("child-invalid", kiExitStateConflict);
				}
				if (!RemovePlanAtomicTemporarySiblings(worktree, path))
				{
					return Failure("orphan-cleanup-failed");
				}
				if (!coordination::WriteBytesAtomic(worktree / path, after))
				{
					return Failure("rewrite-failed");
				}
				changedChildren.push_back(WideToUtf8(path));
			}
			nlohmann::json changed = nlohmann::json::array();
			const std::filesystem::path targetDiskPath = worktree / target;
			std::error_code targetError;
			if (std::filesystem::exists(targetDiskPath, targetError) && !targetError)
			{
				if (plans.find(target) == plans.end())
				{
					return Failure("plan-untracked", kiExitStateConflict);
				}
				if (::DeleteFileW(ExtendedLengthPath(targetDiskPath).c_str()) == FALSE)
				{
					return Failure("delete-failed");
				}
				changed.push_back(WideToUtf8(target));
			}
			else if (targetError)
			{
				return Failure("target-read-failed");
			}
			changed.insert(changed.end(), changedChildren.begin(), changedChildren.end());
			PrintResult({ { "status", "ok" }, { "code", bReject ? "rejected" : "completed" }, { "plan", WideToUtf8(target) }, { "changedPaths", changed } }, 2);
			return kiExitOk;
		}
	}

	int RunPlanSchedulerCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		if (iArgumentCount < 3)
		{
			return Failure("usage");
		}
		const std::wstring operation = ToLowerInvariant(pArgumentValues[2]);
		Arguments arguments {};
		if (!ParseArguments(iArgumentCount, pArgumentValues, 3, arguments))
		{
			return Failure("usage");
		}
		if (operation == L"validate")
		{
			return RunValidate(arguments);
		}
		if (operation == L"list")
		{
			return RunList(arguments);
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
		if (operation == L"complete" || operation == L"reject")
		{
			return RunTerminal(operation, arguments);
		}
		return Failure("usage");
	}
}
