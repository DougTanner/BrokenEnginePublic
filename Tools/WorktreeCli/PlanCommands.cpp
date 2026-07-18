#include "PlanCommands.h"

#include "ToolCliCommon.h"
#include "CoordinationStore.h"
#include "PlanOrderCommands.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toolcli
{
	namespace
	{
		struct Arguments
		{
			std::wstring repository;
			std::wstring order;
			std::wstring plan;
			std::wstring owner;
			std::wstring expectedOwner;
			std::wstring session;
			std::wstring worktree;
		};

		struct PlanLocator
		{
			coordination::Locator queue;
			coordination::Locator row;
			std::wstring repository;
			std::wstring order;
			std::wstring plan;
			std::filesystem::path rowDirectory;
			std::filesystem::path guardPath;
		};

		bool ParseArguments(int iArgumentCount, wchar_t* pArgumentValues[], int iStartIndex, Arguments& rArguments)
		{
			for (int i = iStartIndex; i < iArgumentCount; ++i)
			{
				std::wstring_view argument = pArgumentValues[i];
				std::wstring* pDestination = nullptr;
				if (argument == L"--repo")
				{
					pDestination = &rArguments.repository;
				}
				else if (argument == L"--order")
				{
					pDestination = &rArguments.order;
				}
				else if (argument == L"--plan")
				{
					pDestination = &rArguments.plan;
				}
				else if (argument == L"--owner")
				{
					pDestination = &rArguments.owner;
				}
				else if (argument == L"--expect")
				{
					pDestination = &rArguments.expectedOwner;
				}
				else if (argument == L"--session")
				{
					pDestination = &rArguments.session;
				}
				else if (argument == L"--worktree")
				{
					pDestination = &rArguments.worktree;
				}
				else
				{
					Fail("unknown plan argument: " + WideToUtf8(argument));
					return false;
				}
				if (++i >= iArgumentCount)
				{
					Fail("plan option requires a value");
					return false;
				}
				*pDestination = pArgumentValues[i];
			}
			return true;
		}

		std::optional<PlanLocator> MakePlanLocator(const Arguments& rArguments, bool bRequirePlan)
		{
			std::optional<std::wstring> repository = coordination::CanonicalizeDirectoryPath(rArguments.repository);
			std::optional<std::wstring> order = coordination::NormalizeRepositoryRelativeKey(rArguments.order);
			std::optional<std::wstring> plan = bRequirePlan ? coordination::NormalizeRepositoryRelativeKey(rArguments.plan) : std::optional<std::wstring>(L"");
			if (!repository || !order || !plan)
			{
				Fail("plan locator requires a valid --repo and --order, plus --plan for row commands");
				return std::nullopt;
			}

			const std::wstring queueKey = *repository + L"\n" + *order;
			std::optional<coordination::Locator> queue = coordination::MakeLocator(L"plan-queue", queueKey);
			std::optional<std::string> queueHash = coordination::HashSha256(WideToUtf8(queueKey));
			if (!queue || !queueHash)
			{
				Fail("could not resolve plan queue storage");
				return std::nullopt;
			}

			PlanLocator locator {};
			locator.queue = *queue;
			locator.repository = *repository;
			locator.order = *order;
			locator.plan = *plan;
			locator.rowDirectory = GetLocalApplicationDataPath() / L"BrokenEngineLocks" / L"plan-row" / Utf8ToWide(*queueHash);
			locator.guardPath = locator.queue.path.wstring() + L".guard";
			if (bRequirePlan)
			{
				const std::wstring rowKey = queueKey + L"\n" + *plan;
				std::optional<std::string> planHash = coordination::HashSha256(WideToUtf8(rowKey));
				if (!planHash)
				{
					Fail("could not resolve plan row storage");
					return std::nullopt;
				}
				locator.row =
				{
					.domain = L"plan-row",
					.logicalKey = rowKey,
					.path = locator.rowDirectory / Utf8ToWide(*planHash + ".lock"),
				};
			}
			return locator;
		}

		bool ValidateMetadata(const nlohmann::json& rMetadata, const coordination::Locator& rLocator, const PlanLocator& rPlanLocator, const std::wstring* pPlan)
		{
			return coordination::ValidateMetadataEnvelope(rMetadata, rLocator) &&
				rMetadata.contains("repository") && rMetadata["repository"].is_string() && rMetadata["repository"].get<std::string>() == WideToUtf8(rPlanLocator.repository) &&
				rMetadata.contains("order") && rMetadata["order"].is_string() && rMetadata["order"].get<std::string>() == WideToUtf8(rPlanLocator.order) &&
				(pPlan == nullptr || (rMetadata.contains("plan") && rMetadata["plan"].is_string() && rMetadata["plan"].get<std::string>() == WideToUtf8(*pPlan)));
		}

		nlohmann::json NewPlanMetadata(const coordination::Locator& rLocator, const PlanLocator& rPlanLocator, const Arguments& rArguments)
		{
			nlohmann::json metadata = coordination::NewMetadata(rLocator, rArguments.owner, rArguments.session, rArguments.worktree);
			metadata["repository"] = WideToUtf8(rPlanLocator.repository);
			metadata["order"] = WideToUtf8(rPlanLocator.order);
			if (!rPlanLocator.plan.empty())
			{
				metadata["plan"] = WideToUtf8(rPlanLocator.plan);
			}
			return metadata;
		}

		bool ReadExisting(const coordination::Locator& rLocator, const PlanLocator& rPlanLocator, const std::wstring* pPlan, bool& rbExists, nlohmann::json& rMetadata)
		{
			std::error_code error;
			rbExists = std::filesystem::exists(ExtendedLengthPath(rLocator.path), error);
			if (error)
			{
				Fail("could not inspect plan coordination record");
				return false;
			}
			if (rbExists && (!coordination::ReadMetadata(rLocator.path, rMetadata) || !ValidateMetadata(rMetadata, rLocator, rPlanLocator, pPlan)))
			{
				Fail("plan coordination metadata is unreadable or invalid (delete the file to recover): " + WideToUtf8(rLocator.path.wstring()));
				return false;
			}
			return true;
		}

		void ReportInvalidClaim(const std::filesystem::path& rPath)
		{
			Fail("plan row claim is unreadable or invalid (delete the file to recover): " + WideToUtf8(rPath.wstring()));
		}

		bool EnumerateClaims(const PlanLocator& rLocator, const std::wstring& rRequester, nlohmann::json& rClaims)
		{
			rClaims = nlohmann::json::array();
			std::error_code error;
			if (!std::filesystem::exists(rLocator.rowDirectory, error))
			{
				return !error;
			}
			std::vector<nlohmann::json> claims;
			for (std::filesystem::directory_iterator it(rLocator.rowDirectory, error), end; !error && it != end; it.increment(error))
			{
				if (!it->is_regular_file(error) || error || it->path().extension() != L".lock")
				{
					if (error)
					{
						break;
					}
					continue;
				}
				nlohmann::json metadata;
				if (!coordination::ReadMetadata(it->path(), metadata) ||
					!metadata.contains("plan") || !metadata["plan"].is_string())
				{
					ReportInvalidClaim(it->path());
					continue;
				}
				std::optional<std::wstring> plan = coordination::NormalizeRepositoryRelativeKey(Utf8ToWide(metadata["plan"].get<std::string>()));
				if (!plan)
				{
					ReportInvalidClaim(it->path());
					continue;
				}
				const std::wstring rowKey = rLocator.queue.logicalKey + L"\n" + *plan;
				coordination::Locator expected
				{
					.domain = L"plan-row",
					.logicalKey = rowKey,
					.path = it->path(),
				};
				std::optional<std::string> expectedHash = coordination::HashSha256(WideToUtf8(rowKey));
				if (!expectedHash || it->path().filename() != Utf8ToWide(*expectedHash + ".lock") || !ValidateMetadata(metadata, expected, rLocator, &*plan))
				{
					ReportInvalidClaim(it->path());
					continue;
				}
				metadata["ownedByRequester"] = !rRequester.empty() && coordination::HasOwner(metadata, rRequester);
				claims.push_back(std::move(metadata));
			}
			if (error)
			{
				Fail("could not enumerate plan row claims");
				return false;
			}
			std::sort(claims.begin(), claims.end(), [](const nlohmann::json& rLeft, const nlohmann::json& rRight)
			{
				return rLeft["plan"].get<std::string>() < rRight["plan"].get<std::string>();
			});
			for (nlohmann::json& rClaim : claims)
			{
				rClaims.push_back(std::move(rClaim));
			}
			return true;
		}

		bool BuildSnapshot(const PlanLocator& rLocator, const std::wstring& rRequester, bool bQueueExists, const nlohmann::json& rQueue, nlohmann::json& rSnapshot)
		{
			nlohmann::json claims;
			if (!EnumerateClaims(rLocator, rRequester, claims))
			{
				return false;
			}
			nlohmann::json queue = bQueueExists ? rQueue : nlohmann::json { { "held", false } };
			if (bQueueExists)
			{
				queue["ownedByRequester"] = !rRequester.empty() && coordination::HasOwner(rQueue, rRequester);
			}
			rSnapshot = { { "queue", std::move(queue) }, { "claims", std::move(claims) } };
			return true;
		}

		bool RequireNewOwner(const Arguments& rArguments)
		{
			if (rArguments.owner.empty() || rArguments.session.empty() || rArguments.worktree.empty())
			{
				Fail("lock, claim, and steal require --owner, --session, and --worktree");
				return false;
			}
			return true;
		}

		int RunQueueCommand(const std::wstring& rVerb, const Arguments& rArguments, const PlanLocator& rLocator)
		{
			if ((rVerb == L"lock" || rVerb == L"steal") && !RequireNewOwner(rArguments))
			{
				return kiExitFailure;
			}
			if (rVerb == L"steal" && rArguments.expectedOwner.empty())
			{
				Fail("queue steal requires --expect");
				return kiExitFailure;
			}
			if (rVerb == L"unlock" && rArguments.owner.empty())
			{
				Fail("queue unlock requires --owner");
				return kiExitFailure;
			}
			if (!coordination::EnsureParentDirectory(rLocator.queue.path))
			{
				Fail("could not create plan queue directory");
				return kiExitFailure;
			}
			coordination::Guard guard(rLocator.guardPath);
			if (!guard.IsValid())
			{
				Fail("could not acquire plan queue guard (" + guard.FailureReason() + ")");
				return kiExitFailure;
			}
			bool bExists = false;
			nlohmann::json metadata;
			if (!ReadExisting(rLocator.queue, rLocator, nullptr, bExists, metadata))
			{
				return kiExitFailure;
			}
			if (rVerb == L"list")
			{
				nlohmann::json snapshot;
				if (!BuildSnapshot(rLocator, rArguments.owner, bExists, metadata, snapshot))
				{
					return kiExitFailure;
				}
				coordination::PrintMetadata(snapshot);
				return kiExitOk;
			}
			if (rVerb == L"status")
			{
				if (!bExists)
				{
					std::cout << "{\"held\":false}\n";
					return kiExitStateConflict;
				}
				metadata["ownedByRequester"] = !rArguments.owner.empty() && coordination::HasOwner(metadata, rArguments.owner);
				coordination::PrintMetadata(metadata);
				return kiExitOk;
			}
			if (rVerb == L"lock")
			{
				if (bExists)
				{
					nlohmann::json snapshot;
					if (!BuildSnapshot(rLocator, rArguments.owner, true, metadata, snapshot))
					{
						return kiExitFailure;
					}
					coordination::PrintMetadata(snapshot);
					return kiExitStateConflict;
				}
				metadata = NewPlanMetadata(rLocator.queue, rLocator, rArguments);
				nlohmann::json snapshot;
				if (!BuildSnapshot(rLocator, rArguments.owner, true, metadata, snapshot))
				{
					return kiExitFailure;
				}
				if (!coordination::WriteMetadataAtomic(rLocator.queue.path, metadata))
				{
					FailWindows("write plan queue metadata");
					return kiExitFailure;
				}
				coordination::PrintMetadata(snapshot);
				return kiExitOk;
			}
			if (!bExists || (rVerb == L"unlock" && !coordination::HasOwner(metadata, rArguments.owner)) ||
				(rVerb == L"steal" && !coordination::HasOwner(metadata, rArguments.expectedOwner)))
			{
				if (bExists)
				{
					coordination::PrintMetadata(metadata);
				}
				else
				{
					std::cout << "{\"held\":false}\n";
				}
				return kiExitStateConflict;
			}
			if (rVerb == L"unlock")
			{
				if (::DeleteFileW(ExtendedLengthPath(rLocator.queue.path).c_str()) == FALSE)
				{
					FailWindows("unlock plan queue");
					return kiExitFailure;
				}
				return kiExitOk;
			}
			metadata = NewPlanMetadata(rLocator.queue, rLocator, rArguments);
			nlohmann::json snapshot;
			if (!BuildSnapshot(rLocator, rArguments.owner, true, metadata, snapshot))
			{
				return kiExitFailure;
			}
			if (!coordination::WriteMetadataAtomic(rLocator.queue.path, metadata))
			{
				FailWindows("replace plan queue metadata");
				return kiExitFailure;
			}
			coordination::PrintMetadata(snapshot);
			return kiExitOk;
		}

		int RunRowCommand(const std::wstring& rVerb, const Arguments& rArguments, const PlanLocator& rLocator)
		{
			if ((rVerb == L"claim" || rVerb == L"steal") && !RequireNewOwner(rArguments))
			{
				return kiExitFailure;
			}
			if (rVerb == L"steal" && rArguments.expectedOwner.empty())
			{
				Fail("row steal requires --expect");
				return kiExitFailure;
			}
			if (rVerb == L"unclaim" && rArguments.owner.empty())
			{
				Fail("row unclaim requires --owner");
				return kiExitFailure;
			}
			if (!coordination::EnsureParentDirectory(rLocator.queue.path) || !coordination::EnsureParentDirectory(rLocator.row.path))
			{
				Fail("could not create plan coordination directory");
				return kiExitFailure;
			}
			coordination::Guard guard(rLocator.guardPath);
			if (!guard.IsValid())
			{
				Fail("could not acquire plan queue guard (" + guard.FailureReason() + ")");
				return kiExitFailure;
			}
			if (rVerb == L"claim" || rVerb == L"steal")
			{
				bool bQueueExists = false;
				nlohmann::json queueMetadata;
				if (!ReadExisting(rLocator.queue, rLocator, nullptr, bQueueExists, queueMetadata))
				{
					return kiExitFailure;
				}
				if (!bQueueExists || !coordination::HasOwner(queueMetadata, rArguments.owner))
				{
					if (bQueueExists)
					{
						coordination::PrintMetadata(queueMetadata);
					}
					else
					{
						std::cout << "{\"held\":false}\n";
					}
					return kiExitStateConflict;
				}
			}
			bool bExists = false;
			nlohmann::json metadata;
			if (!ReadExisting(rLocator.row, rLocator, &rLocator.plan, bExists, metadata))
			{
				return kiExitFailure;
			}
			if (rVerb == L"status")
			{
				if (!bExists)
				{
					std::cout << "{\"held\":false}\n";
					return kiExitStateConflict;
				}
				metadata["ownedByRequester"] = !rArguments.owner.empty() && coordination::HasOwner(metadata, rArguments.owner);
				coordination::PrintMetadata(metadata);
				return kiExitOk;
			}
			if (rVerb == L"claim")
			{
				if (bExists)
				{
					coordination::PrintMetadata(metadata);
					return kiExitStateConflict;
				}
				metadata = NewPlanMetadata(rLocator.row, rLocator, rArguments);
				if (!coordination::WriteMetadataAtomic(rLocator.row.path, metadata))
				{
					FailWindows("write plan row metadata");
					return kiExitFailure;
				}
				coordination::PrintMetadata(metadata);
				return kiExitOk;
			}
			if (!bExists || (rVerb == L"unclaim" && !coordination::HasOwner(metadata, rArguments.owner)) ||
				(rVerb == L"steal" && !coordination::HasOwner(metadata, rArguments.expectedOwner)))
			{
				if (bExists)
				{
					coordination::PrintMetadata(metadata);
				}
				else
				{
					std::cout << "{\"held\":false}\n";
				}
				return kiExitStateConflict;
			}
			if (rVerb == L"unclaim")
			{
				if (::DeleteFileW(ExtendedLengthPath(rLocator.row.path).c_str()) == FALSE)
				{
					FailWindows("unclaim plan row");
					return kiExitFailure;
				}
				return kiExitOk;
			}
			metadata = NewPlanMetadata(rLocator.row, rLocator, rArguments);
			if (!coordination::WriteMetadataAtomic(rLocator.row.path, metadata))
			{
				FailWindows("replace plan row metadata");
				return kiExitFailure;
			}
			coordination::PrintMetadata(metadata);
			return kiExitOk;
		}
	}

	int RunPlanCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		if (iArgumentCount < 4)
		{
			Fail("plan requires queue or row followed by a verb");
			return kiExitFailure;
		}
		const std::wstring target = ToLowerInvariant(pArgumentValues[2]);
		if (target == L"order")
		{
			return RunPlanOrderCommand(iArgumentCount, pArgumentValues);
		}
		const std::wstring verb = ToLowerInvariant(pArgumentValues[3]);
		const bool bQueueVerb = verb == L"lock" || verb == L"list" || verb == L"status" || verb == L"steal" || verb == L"unlock";
		const bool bRowVerb = verb == L"claim" || verb == L"status" || verb == L"steal" || verb == L"unclaim";
		if ((target != L"queue" || !bQueueVerb) && (target != L"row" || !bRowVerb))
		{
			Fail("unknown plan target or verb");
			return kiExitFailure;
		}
		Arguments arguments {};
		if (!ParseArguments(iArgumentCount, pArgumentValues, 4, arguments))
		{
			return kiExitFailure;
		}
		if ((target == L"queue" && !arguments.plan.empty()) || (target == L"row" && arguments.plan.empty()))
		{
			Fail("queue commands do not accept --plan; row commands require it");
			return kiExitFailure;
		}
		std::optional<PlanLocator> locator = MakePlanLocator(arguments, target == L"row");
		if (!locator)
		{
			return kiExitFailure;
		}
		return target == L"queue" ? RunQueueCommand(verb, arguments, *locator) : RunRowCommand(verb, arguments, *locator);
	}
}
