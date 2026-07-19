#include "PlanOrderCommands.h"

#include "ToolCliCommon.h"
#include "CoordinationStore.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace toolcli
{
	namespace
	{
		constexpr size_t kuiMaximumOrderBytes = 8u * 1024u * 1024u;
		constexpr size_t kuiMaximumRequestBytes = 8u * 1024u * 1024u;
		constexpr size_t kuiMaximumPlanBytes = 32u * 1024u * 1024u;
		constexpr std::wstring_view kPlansOrderDefault = L"Documents/Plans/Order.md";
		constexpr std::wstring_view kFeaturesOrderDefault = L"Documents/Features/Order.md";
		constexpr std::string_view kTableHeader = "| Plan | Tier | Effort | Impact | Risks | Score | Depends On | Notes |";
		constexpr std::string_view kTableSeparator = "|------|------|--------|--------|-------|-------|------------|-------|";
		constexpr std::wstring_view kPlansStoreFile = L"Plans-Order.md";
		constexpr std::wstring_view kFeaturesStoreFile = L"Features-Order.md";

		struct Arguments
		{
			std::wstring repository;
			std::wstring primaryWorktree;
			std::wstring worktree;
			std::wstring branch;
			std::wstring owner;
			std::wstring session;
			std::wstring queue;
			std::wstring plan;
			std::wstring request;
			std::wstring plansOrder = std::wstring(kPlansOrderDefault);
			std::wstring featuresOrder = std::wstring(kFeaturesOrderDefault);
			bool bForce = false;
		};

		struct Diagnostic
		{
			std::string path;
			int iLine = 0;
			std::string code;
			std::string message;
		};

		struct Row
		{
			std::string plan;
			std::string tier;
			int iEffort = 0;
			int iImpact = 0;
			int iRisks = 0;
			int iScore = 0;
			std::vector<std::string> dependencies;
			std::string notes;
			std::string source;
			std::string rowBytes;
			int iLine = 0;
		};

		struct QueueFile
		{
			std::string path;
			std::string directory;
			std::string bytes;
			std::string newline = "\n";
			size_t uiRowsBegin = std::string::npos;
			size_t uiRowsEnd = std::string::npos;
			std::vector<Row> rows;
			std::set<std::string> references;
		};

		struct QueueState
		{
			QueueFile plans;
			QueueFile features;
			std::unordered_map<std::string, Row*> byPlan;
			std::vector<std::string> sortedPlans;
			std::vector<Diagnostic> diagnostics;
			// Informational findings (orphan-plan) that never affect ok/exit; a session's newly written plan files
			// and a fresh empty store legitimately have unlisted plan documents until rows publish at landing.
			std::vector<Diagnostic> notices;
		};

		struct WorktreeInfo
		{
			std::wstring path;
			std::string head;
			std::string branch;
			bool bBare = false;
			bool bPrunable = false;
		};

		std::string Trim(std::string_view value)
		{
			size_t uiFirst = value.find_first_not_of(" \t\r\n");
			if (uiFirst == std::string_view::npos)
			{
				return {};
			}
			size_t uiLast = value.find_last_not_of(" \t\r\n");
			return std::string(value.substr(uiFirst, uiLast - uiFirst + 1));
		}

		void TrimLineEnd(std::string& rValue)
		{
			while (!rValue.empty() && (rValue.back() == '\r' || rValue.back() == '\n'))
			{
				rValue.pop_back();
			}
		}

		std::optional<int> ParseInteger(std::string_view value)
		{
			const std::string text = Trim(value);
			int iValue = 0;
			const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), iValue);
			return result.ec == std::errc() && result.ptr == text.data() + text.size() ? std::optional<int>(iValue) : std::nullopt;
		}

		bool IsStrictUtf8(std::string_view value)
		{
			if (value.empty())
			{
				return true;
			}
			return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0) > 0;
		}

		bool ReadBoundedFile(const std::filesystem::path& rPath, size_t uiMaximumBytes, std::string& rBytes)
		{
			std::error_code error;
			const uintmax_t uiSize = std::filesystem::file_size(rPath, error);
			if (error || uiSize > uiMaximumBytes)
			{
				return false;
			}
			std::ifstream input(rPath, std::ios::binary);
			if (!input)
			{
				return false;
			}
			rBytes.resize(static_cast<size_t>(uiSize));
			if (uiSize != 0)
			{
				input.read(rBytes.data(), static_cast<std::streamsize>(uiSize));
				if (input.gcount() != static_cast<std::streamsize>(uiSize))
				{
					return false;
				}
			}
			return input.peek() == std::char_traits<char>::eof();
		}

		std::optional<std::string> Sha256(std::string_view value)
		{
			return coordination::HashSha256(value);
		}

		bool ParseArguments(int iArgumentCount, wchar_t* pArgumentValues[], Arguments& rArguments)
		{
			for (int i = 4; i < iArgumentCount; ++i)
			{
				const std::wstring_view argument = pArgumentValues[i];
				if (argument == L"--force")
				{
					rArguments.bForce = true;
					continue;
				}
				std::wstring* pDestination = nullptr;
				if (argument == L"--repo")
				{
					pDestination = &rArguments.repository;
				}
				else if (argument == L"--primary-worktree")
				{
					pDestination = &rArguments.primaryWorktree;
				}
				else if (argument == L"--worktree")
				{
					pDestination = &rArguments.worktree;
				}
				else if (argument == L"--branch")
				{
					pDestination = &rArguments.branch;
				}
				else if (argument == L"--owner")
				{
					pDestination = &rArguments.owner;
				}
				else if (argument == L"--session")
				{
					pDestination = &rArguments.session;
				}
				else if (argument == L"--queue")
				{
					pDestination = &rArguments.queue;
				}
				else if (argument == L"--plan")
				{
					pDestination = &rArguments.plan;
				}
				else if (argument == L"--request")
				{
					pDestination = &rArguments.request;
				}
				else if (argument == L"--plans-order")
				{
					pDestination = &rArguments.plansOrder;
				}
				else if (argument == L"--features-order")
				{
					pDestination = &rArguments.featuresOrder;
				}
				else
				{
					Fail("unknown plan order argument: " + WideToUtf8(argument));
					return false;
				}
				if (++i >= iArgumentCount)
				{
					Fail("plan order option requires a value");
					return false;
				}
				*pDestination = pArgumentValues[i];
			}
			return true;
		}

		std::optional<std::wstring> CanonicalExistingDirectory(const std::wstring& rPath)
		{
			return coordination::CanonicalizeDirectoryPath(rPath);
		}

		bool ParseWorktreeListing(const std::string& rListing, std::vector<WorktreeInfo>& rWorktrees)
		{
			WorktreeInfo current;
			bool bHaveEntry = false;
			for (size_t uiStart = 0; uiStart <= rListing.size();)
			{
				size_t uiEnd = rListing.find('\0', uiStart);
				if (uiEnd == std::string::npos)
				{
					uiEnd = rListing.size();
				}
				const std::string_view field(rListing.data() + uiStart, uiEnd - uiStart);
				if (field.empty())
				{
					if (bHaveEntry)
					{
						if (current.path.empty())
						{
							return false;
						}
						rWorktrees.push_back(std::move(current));
						current = {};
						bHaveEntry = false;
					}
				}
				else
				{
					bHaveEntry = true;
					if (field.starts_with("worktree "))
					{
						current.path = Utf8ToWide(field.substr(9));
					}
					else if (field.starts_with("HEAD "))
					{
						current.head = std::string(field.substr(5));
					}
					else if (field.starts_with("branch "))
					{
						current.branch = std::string(field.substr(7));
					}
					else if (field == "bare")
					{
						current.bBare = true;
					}
					else if (field.starts_with("prunable"))
					{
						current.bPrunable = true;
					}
				}
				if (uiEnd == rListing.size())
				{
					break;
				}
				uiStart = uiEnd + 1;
			}
			if (bHaveEntry)
			{
				if (current.path.empty())
				{
					return false;
				}
				rWorktrees.push_back(std::move(current));
			}
			return !rWorktrees.empty();
		}

		bool ResolveWorktrees(const Arguments& rArguments, std::wstring& rRepository, std::wstring& rWorktree, std::optional<std::wstring>& rPrimary, std::vector<WorktreeInfo>& rListing)
		{
			const std::optional<std::wstring> repository = CanonicalExistingDirectory(rArguments.repository);
			const std::optional<std::wstring> worktree = CanonicalExistingDirectory(rArguments.worktree);
			if (!repository || !worktree)
			{
				Fail("plan order requires valid --repo and --worktree directories");
				return false;
			}
			const std::optional<std::string> commonText = RunGit({ L"-C", *worktree, L"rev-parse", L"--path-format=absolute", L"--git-common-dir" });
			if (!commonText)
			{
				Fail("could not resolve worktree Git common directory");
				return false;
			}
			std::string common = *commonText;
			TrimLineEnd(common);
			const std::optional<std::wstring> canonicalCommon = CanonicalExistingDirectory(Utf8ToWide(common));
			if (!canonicalCommon || *canonicalCommon != *repository)
			{
				Fail("--worktree is not registered for --repo");
				return false;
			}
			const std::optional<std::string> listingText = RunGit({ L"--git-dir", *repository, L"worktree", L"list", L"--porcelain", L"-z" });
			if (!listingText || !ParseWorktreeListing(*listingText, rListing))
			{
				Fail("could not read registered Git worktrees");
				return false;
			}
			bool bFound = false;
			for (WorktreeInfo& rInfo : rListing)
			{
				const std::optional<std::wstring> canonical = CanonicalExistingDirectory(rInfo.path);
				if (!canonical || rInfo.bBare || rInfo.bPrunable)
				{
					continue;
				}
				rInfo.path = *canonical;
				bFound |= rInfo.path == *worktree;
			}
			if (!bFound)
			{
				Fail("--worktree is not an inspectable registered worktree");
				return false;
			}
			if (!rArguments.primaryWorktree.empty())
			{
				const std::optional<std::wstring> primary = CanonicalExistingDirectory(rArguments.primaryWorktree);
				if (!primary || rListing.front().path != *primary)
				{
					Fail("--primary-worktree is not the registered primary checkout");
					return false;
				}
				rPrimary = *primary;
			}
			rRepository = *repository;
			rWorktree = *worktree;
			return true;
		}

		bool IsLexicallyContained(const std::filesystem::path& rRoot, const std::filesystem::path& rTarget)
		{
			const std::filesystem::path relative = rTarget.lexically_relative(rRoot);
			return !relative.empty() && !relative.native().starts_with(L"..");
		}

		std::optional<std::filesystem::path> ResolveContainedPath(const std::wstring& rRoot, const std::wstring& rRelative, bool bRequireExisting)
		{
			if (rRelative.find(L'\\') != std::wstring::npos)
			{
				return std::nullopt;
			}
			const std::optional<std::wstring> normalized = coordination::NormalizeRepositoryRelativeKey(rRelative);
			if (!normalized)
			{
				return std::nullopt;
			}
			std::wstring lexical = std::filesystem::path(rRelative).lexically_normal().generic_wstring();
			if (lexical != rRelative)
			{
				return std::nullopt;
			}
			const std::filesystem::path root(rRoot);
			const std::filesystem::path target = (root / rRelative).lexically_normal();
			if (!IsLexicallyContained(root, target))
			{
				return std::nullopt;
			}
			std::error_code error;
			if (bRequireExisting && !std::filesystem::is_regular_file(target, error))
			{
				return std::nullopt;
			}
			return target;
		}

		std::string ToRepositoryPath(std::wstring value)
		{
			std::replace(value.begin(), value.end(), L'\\', L'/');
			return WideToUtf8(value);
		}

		std::optional<std::string> NormalizeIdentity(std::string_view value)
		{
			if (value.empty() || value.find('\\') != std::string_view::npos)
			{
				return std::nullopt;
			}
			const std::wstring wide = Utf8ToWide(value);
			if (wide.empty())
			{
				return std::nullopt;
			}
			const std::optional<std::wstring> normalized = coordination::NormalizeRepositoryRelativeKey(wide);
			if (!normalized)
			{
				return std::nullopt;
			}
			const std::wstring lexical = std::filesystem::path(wide).lexically_normal().generic_wstring();
			return lexical == wide ? std::optional<std::string>(std::string(value)) : std::nullopt;
		}

		std::optional<std::string> ExtractLink(std::string_view value)
		{
			const size_t uiOpen = value.find("](");
			if (value.empty() || value.front() != '[' || uiOpen == std::string_view::npos || value.back() != ')')
			{
				return std::nullopt;
			}
			return std::string(value.substr(uiOpen + 2, value.size() - uiOpen - 3));
		}

		std::vector<std::string> SplitDependencies(std::string_view value)
		{
			std::vector<std::string> result;
			const std::string text = Trim(value);
			if (text == "-")
			{
				return result;
			}
			for (size_t uiStart = 0; uiStart <= text.size();)
			{
				size_t uiEnd = text.find(';', uiStart);
				if (uiEnd == std::string::npos)
				{
					uiEnd = text.size();
				}
				result.push_back(Trim(std::string_view(text).substr(uiStart, uiEnd - uiStart)));
				if (uiEnd == text.size())
				{
					break;
				}
				uiStart = uiEnd + 1;
			}
			return result;
		}

		void AddDiagnostic(QueueState& rState, std::string path, int iLine, std::string code, std::string message)
		{
			rState.diagnostics.push_back({ std::move(path), iLine, std::move(code), std::move(message) });
		}

		void AddNotice(QueueState& rState, std::string path, int iLine, std::string code, std::string message)
		{
			rState.notices.push_back({ std::move(path), iLine, std::move(code), std::move(message) });
		}

		std::vector<std::string> SplitRowCells(std::string_view line)
		{
			std::vector<std::string> cells;
			if (line.empty() || line.front() != '|')
			{
				return cells;
			}
			size_t uiStart = 1;
			for (int iColumn = 0; iColumn < 7; ++iColumn)
			{
				const size_t uiEnd = line.find('|', uiStart);
				if (uiEnd == std::string_view::npos)
				{
					return {};
				}
				cells.push_back(Trim(line.substr(uiStart, uiEnd - uiStart)));
				uiStart = uiEnd + 1;
			}
			const size_t uiFinal = line.find_last_of('|');
			if (uiFinal < uiStart || !Trim(line.substr(uiFinal + 1)).empty())
			{
				return {};
			}
			cells.push_back(Trim(line.substr(uiStart, uiFinal - uiStart)));
			return cells;
		}

		bool HasPlanExtension(std::string_view path)
		{
			return path.ends_with(".md") || path.ends_with(".txt");
		}

		void ParseReferenceSection(QueueFile& rQueue, QueueState& rState, const std::vector<std::pair<size_t, size_t>>& rLines)
		{
			bool bInReferences = false;
			size_t uiTableRow = 0;
			for (size_t uiIndex = 0; uiIndex < rLines.size(); ++uiIndex)
			{
				const std::string_view line(rQueue.bytes.data() + rLines.at(uiIndex).first, rLines.at(uiIndex).second - rLines.at(uiIndex).first);
				if (line.starts_with("### Reference / Index Documents"))
				{
					bInReferences = true;
					continue;
				}
				if (bInReferences && line.starts_with("## "))
				{
					break;
				}
				if (!bInReferences || line.empty() || line.front() != '|')
				{
					continue;
				}
				++uiTableRow;
				if (uiTableRow <= 2)
				{
					continue;
				}
				const size_t uiOpen = line.find("](");
				const size_t uiClose = uiOpen == std::string_view::npos ? std::string_view::npos : line.find(')', uiOpen + 2);
				if (uiOpen == std::string_view::npos || uiClose == std::string_view::npos)
				{
					AddDiagnostic(rState, rQueue.path, static_cast<int>(uiIndex + 1), "invalid-reference-path", "reference table row does not contain a valid Markdown link");
					continue;
				}
				const std::string relative(line.substr(uiOpen + 2, uiClose - uiOpen - 2));
				const std::optional<std::string> identity = NormalizeIdentity(rQueue.directory + "/" + relative);
				if (!identity || !HasPlanExtension(*identity))
				{
					AddDiagnostic(rState, rQueue.path, static_cast<int>(uiIndex + 1), "invalid-reference-path", "reference document path is not a canonical plan identity");
					continue;
				}
				rQueue.references.insert(*identity);
			}
		}

		bool ParseQueue(QueueFile& rQueue, QueueState& rState)
		{
			if (!IsStrictUtf8(rQueue.bytes))
			{
				AddDiagnostic(rState, rQueue.path, 0, "invalid-utf8", "Order file is not strict UTF-8");
				return false;
			}
			rQueue.newline = rQueue.bytes.find("\r\n") != std::string::npos ? "\r\n" : "\n";
			std::vector<std::pair<size_t, size_t>> lines;
			for (size_t uiStart = 0; uiStart <= rQueue.bytes.size();)
			{
				size_t uiEnd = rQueue.bytes.find('\n', uiStart);
				if (uiEnd == std::string::npos)
				{
					uiEnd = rQueue.bytes.size();
				}
				size_t uiContentEnd = uiEnd;
				if (uiContentEnd > uiStart && rQueue.bytes[uiContentEnd - 1] == '\r')
				{
					--uiContentEnd;
				}
				lines.emplace_back(uiStart, uiContentEnd);
				if (uiEnd == rQueue.bytes.size())
				{
					break;
				}
				uiStart = uiEnd + 1;
			}
			size_t uiHeader = std::string::npos;
			for (size_t uiIndex = 0; uiIndex < lines.size(); ++uiIndex)
			{
				const std::string_view line(rQueue.bytes.data() + lines.at(uiIndex).first, lines.at(uiIndex).second - lines.at(uiIndex).first);
				if (line == kTableHeader)
				{
					if (uiHeader != std::string::npos)
					{
						AddDiagnostic(rState, rQueue.path, static_cast<int>(uiIndex + 1), "duplicate-table", "Order file contains multiple executable tables");
						return false;
					}
					uiHeader = uiIndex;
				}
			}
			if (uiHeader == std::string::npos || uiHeader + 1 >= lines.size())
			{
				AddDiagnostic(rState, rQueue.path, 0, "missing-table", "Order file is missing the mechanically owned executable table");
				return false;
			}
			const std::string separator(rQueue.bytes.data() + lines.at(uiHeader + 1).first, lines.at(uiHeader + 1).second - lines.at(uiHeader + 1).first);
			const std::vector<std::string> separatorCells = SplitRowCells(separator);
			const bool bValidSeparator = separatorCells.size() == 8 && std::ranges::all_of(separatorCells, [](const std::string& rCell)
			{
				return rCell.size() >= 3 && rCell.find_first_not_of('-') == std::string::npos;
			});
			if (!bValidSeparator)
			{
				AddDiagnostic(rState, rQueue.path, static_cast<int>(uiHeader + 2), "malformed-table", "executable table separator is malformed");
				return false;
			}
			rQueue.uiRowsBegin = uiHeader + 2 < lines.size() ? lines.at(uiHeader + 2).first : rQueue.bytes.size();
			rQueue.uiRowsEnd = rQueue.uiRowsBegin;
			for (size_t uiIndex = uiHeader + 2; uiIndex < lines.size(); ++uiIndex)
			{
				const std::string_view line(rQueue.bytes.data() + lines.at(uiIndex).first, lines.at(uiIndex).second - lines.at(uiIndex).first);
				if (line.empty() || line.front() != '|')
				{
					break;
				}
				rQueue.uiRowsEnd = uiIndex + 1 < lines.size() ? lines.at(uiIndex + 1).first : rQueue.bytes.size();
				const std::vector<std::string> cells = SplitRowCells(line);
				if (cells.size() != 8)
				{
					AddDiagnostic(rState, rQueue.path, static_cast<int>(uiIndex + 1), "malformed-row", "executable row does not contain the fixed leading columns and final Notes cell");
					continue;
				}
				const std::optional<std::string> relative = ExtractLink(cells.at(0));
				const std::optional<int> effort = ParseInteger(cells.at(2));
				const std::optional<int> impact = ParseInteger(cells.at(3));
				const std::optional<int> risks = ParseInteger(cells.at(4));
				const std::optional<int> score = ParseInteger(cells.at(5));
				if (!relative || !effort || !impact || !risks || !score)
				{
					AddDiagnostic(rState, rQueue.path, static_cast<int>(uiIndex + 1), "malformed-row", "row link or numeric field is malformed");
					continue;
				}
				const std::optional<std::string> identity = NormalizeIdentity(rQueue.directory + "/" + *relative);
				if (!identity || !HasPlanExtension(*identity))
				{
					AddDiagnostic(rState, rQueue.path, static_cast<int>(uiIndex + 1), "invalid-plan-path", "row plan link is not a canonical queue-relative .md or .txt path");
					continue;
				}
				Row row;
				row.plan = *identity;
				row.tier = cells.at(1);
				row.iEffort = *effort;
				row.iImpact = *impact;
				row.iRisks = *risks;
				row.iScore = *score;
				row.dependencies = SplitDependencies(cells.at(6));
				row.notes = cells.at(7);
				row.source = rQueue.path;
				row.rowBytes = std::string(line);
				row.iLine = static_cast<int>(uiIndex + 1);
				const int64_t iComputedScore = static_cast<int64_t>(row.iEffort) - row.iImpact + row.iRisks;
				if (iComputedScore < INT_MIN || iComputedScore > INT_MAX || row.iScore != iComputedScore)
				{
					AddDiagnostic(rState, rQueue.path, row.iLine, "invalid-score", "Score must equal Effort - Impact + Risks");
				}
				rQueue.rows.push_back(std::move(row));
			}
			ParseReferenceSection(rQueue, rState, lines);
			return true;
		}

		std::string JoinDependencies(const std::vector<std::string>& rDependencies)
		{
			if (rDependencies.empty())
			{
				return "-";
			}
			std::string result;
			for (const std::string& rDependency : rDependencies)
			{
				if (!result.empty())
				{
					result += "; ";
				}
				result += rDependency;
			}
			return result;
		}

		std::string RelativePlanLink(const QueueFile& rQueue, const std::string& rPlan)
		{
			return rPlan.substr(rQueue.directory.size() + 1);
		}

		std::string RenderRow(const QueueFile& rQueue, const Row& rRow)
		{
			const std::string link = RelativePlanLink(rQueue, rRow.plan);
			return "| [" + link + "](" + link + ") | " + rRow.tier + " | " + std::to_string(rRow.iEffort) + " | " + std::to_string(rRow.iImpact) + " | " + std::to_string(rRow.iRisks) + " | " + std::to_string(rRow.iScore) + " | " + JoinDependencies(rRow.dependencies) + " | " + rRow.notes + " |";
		}

		std::string RenderQueue(QueueFile& rQueue)
		{
			std::string rows;
			for (Row& rRow : rQueue.rows)
			{
				if (rRow.rowBytes.empty())
				{
					rRow.rowBytes = RenderRow(rQueue, rRow);
				}
				rows += rRow.rowBytes;
				rows += rQueue.newline;
			}
			return rQueue.bytes.substr(0, rQueue.uiRowsBegin) + rows + rQueue.bytes.substr(rQueue.uiRowsEnd);
		}

		bool IsCanonicalDependency(const std::string& rDependency)
		{
			const std::optional<std::string> normalized = NormalizeIdentity(rDependency);
			return normalized && *normalized == rDependency && HasPlanExtension(rDependency) &&
				(rDependency.starts_with("Documents/Plans/") || rDependency.starts_with("Documents/Features/"));
		}

		void ValidateGraph(QueueState& rState)
		{
			for (QueueFile* pQueue : { &rState.plans, &rState.features })
			{
				for (Row& rRow : pQueue->rows)
				{
					if (rState.byPlan.contains(rRow.plan))
					{
						AddDiagnostic(rState, rRow.source, rRow.iLine, "duplicate-plan", "plan identity appears in more than one executable row");
					}
					else
					{
						rState.byPlan.emplace(rRow.plan, &rRow);
						rState.sortedPlans.push_back(rRow.plan);
					}
				}
			}
			std::sort(rState.sortedPlans.begin(), rState.sortedPlans.end());
			for (const std::string& rPlan : rState.sortedPlans)
			{
				Row* pRow = rState.byPlan.at(rPlan);
				std::set<std::string> unique;
				for (const std::string& rDependency : pRow->dependencies)
				{
					if (!IsCanonicalDependency(rDependency))
					{
						AddDiagnostic(rState, pRow->source, pRow->iLine, "invalid-dependency", "dependency is not a canonical repository-relative plan identity");
					}
					else if (rDependency == rPlan)
					{
						AddDiagnostic(rState, pRow->source, pRow->iLine, "self-dependency", "plan cannot depend on itself");
					}
					else if (!unique.insert(rDependency).second)
					{
						AddDiagnostic(rState, pRow->source, pRow->iLine, "duplicate-dependency", "dependency appears more than once");
					}
					else if (!rState.byPlan.contains(rDependency))
					{
						AddDiagnostic(rState, pRow->source, pRow->iLine, "missing-dependency", "dependency does not identify an executable row");
					}
				}
			}
			std::unordered_map<std::string, int> colors;
			struct VisitFrame
			{
				std::string plan;
				size_t uiNextDependency = 0;
			};
			for (const std::string& rRootPlan : rState.sortedPlans)
			{
				colors.try_emplace(rRootPlan, 0);
				if (colors.at(rRootPlan) != 0)
				{
					continue;
				}
				std::vector<VisitFrame> stack { { rRootPlan, 0 } };
				colors.insert_or_assign(rRootPlan, 1);
				while (!stack.empty())
				{
					VisitFrame& rFrame = stack.back();
					Row* pRow = rState.byPlan.at(rFrame.plan);
					if (rFrame.uiNextDependency >= pRow->dependencies.size())
					{
						colors.insert_or_assign(rFrame.plan, 2);
						stack.pop_back();
						continue;
					}
					const std::string& rDependency = pRow->dependencies.at(rFrame.uiNextDependency++);
					if (!rState.byPlan.contains(rDependency))
					{
						continue;
					}
					std::unordered_map<std::string, int>::iterator it = colors.try_emplace(rDependency, 0).first;
					if (it->second == 0)
					{
						colors.insert_or_assign(rDependency, 1);
						stack.push_back({ rDependency, 0 });
					}
					else if (it->second == 1)
					{
						AddDiagnostic(rState, pRow->source, pRow->iLine, "dependency-cycle", "structured dependency graph contains a cycle");
					}
				}
			}
		}

		void ValidateFiles(const std::wstring& rWorktree, QueueState& rState, const std::set<std::string>& rIgnoredOrphans)
		{
			std::set<std::string> indexed;
			for (const std::string& rPlan : rState.sortedPlans)
			{
				Row* pRow = rState.byPlan.at(rPlan);
				indexed.insert(rPlan);
				const std::filesystem::path path = std::filesystem::path(rWorktree) / Utf8ToWide(rPlan);
				std::error_code error;
				if (!std::filesystem::is_regular_file(path, error) || error || !IsLexicallyContained(rWorktree, path))
				{
					AddDiagnostic(rState, pRow->source, pRow->iLine, "missing-plan-file", "executable row does not resolve to a safe regular plan file");
				}
			}
			std::set<std::string> references = rState.plans.references;
			references.insert(rState.features.references.begin(), rState.features.references.end());
			for (const std::string& rReference : references)
			{
				if (indexed.contains(rReference))
				{
					AddDiagnostic(rState, rReference, 0, "reference-overlap", "document is both executable and reference-only");
				}
				std::error_code error;
				const std::filesystem::path path = std::filesystem::path(rWorktree) / Utf8ToWide(rReference);
				if (!std::filesystem::is_regular_file(path, error) || error || !IsLexicallyContained(rWorktree, path))
				{
					AddDiagnostic(rState, rReference, 0, "missing-reference", "reference document does not resolve to a safe regular file");
				}
			}
			for (const std::string_view root : { std::string_view("Documents/Plans"), std::string_view("Documents/Features") })
			{
				std::error_code error;
				const std::filesystem::path directory = std::filesystem::path(rWorktree) / Utf8ToWide(root);
				for (std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error))
				{
					if (it->is_symlink(error) || error)
					{
						if (!error && it->is_directory(error))
						{
							it.disable_recursion_pending();
						}
						continue;
					}
					if (!it->is_regular_file(error) || error)
					{
						continue;
					}
					const std::wstring extension = ToLowerInvariant(it->path().extension().wstring());
					if (extension != L".md" && extension != L".txt")
					{
						continue;
					}
					std::wstring relative = it->path().lexically_relative(rWorktree).native();
					std::replace(relative.begin(), relative.end(), L'\\', L'/');
					const std::string identity = WideToUtf8(relative);
					if (identity == rState.plans.path || identity == rState.features.path || it->path().filename() == L"AGENTS.md" || it->path().filename() == L"CLAUDE.md" || rIgnoredOrphans.contains(identity))
					{
						continue;
					}
					if (!indexed.contains(identity) && !references.contains(identity))
					{
						AddNotice(rState, identity, 0, "orphan-plan", "plan-like document is neither executable nor reference-only");
					}
				}
				if (error)
				{
					AddDiagnostic(rState, std::string(root), 0, "enumeration-failed", "could not enumerate plan documents");
				}
			}
		}

		std::optional<std::filesystem::path> QueueStoreDirectory(const std::wstring& rRepository)
		{
			const std::optional<std::string> hash = Sha256(WideToUtf8(rRepository));
			std::filesystem::path localApplicationData = GetLocalApplicationDataPath();
			if (!hash || localApplicationData.empty())
			{
				return std::nullopt;
			}
			return localApplicationData / L"BrokenEngineLocks" / L"plan-queue-state" / Utf8ToWide(*hash);
		}

		std::string EmptyQueueTemplate(std::string_view title)
		{
			return "# " + std::string(title) + "\n\n## Plans\n\n" + std::string(kTableHeader) + "\n" + std::string(kTableSeparator) + "\n";
		}

		bool LoadQueueState(const std::wstring& rRepository, const std::wstring& rWorktree, const Arguments& rArguments, const std::optional<std::string>& rPlansBytes, const std::optional<std::string>& rFeaturesBytes, const std::set<std::string>& rIgnoredOrphans, QueueState& rState)
		{
			const std::optional<std::wstring> plansKey = coordination::NormalizeRepositoryRelativeKey(rArguments.plansOrder);
			const std::optional<std::wstring> featuresKey = coordination::NormalizeRepositoryRelativeKey(rArguments.featuresOrder);
			if (!plansKey || !featuresKey)
			{
				Fail("order paths must be canonical repository-relative paths");
				return false;
			}
			rState.plans.path = ToRepositoryPath(rArguments.plansOrder);
			rState.features.path = ToRepositoryPath(rArguments.featuresOrder);
			rState.plans.directory = rState.plans.path.substr(0, rState.plans.path.find_last_of('/'));
			rState.features.directory = rState.features.path.substr(0, rState.features.path.find_last_of('/'));
			const std::optional<std::filesystem::path> storeDirectory = QueueStoreDirectory(rRepository);
			if (!storeDirectory)
			{
				Fail("could not resolve the machine-local plan queue store path");
				return false;
			}
			if (rPlansBytes)
			{
				rState.plans.bytes = *rPlansBytes;
			}
			else if (!ReadBoundedFile(ExtendedLengthPath(*storeDirectory / kPlansStoreFile), kuiMaximumOrderBytes, rState.plans.bytes))
			{
				Fail("could not read the machine-local Plans queue within the size bound (run plan order init)");
				return false;
			}
			if (rFeaturesBytes)
			{
				rState.features.bytes = *rFeaturesBytes;
			}
			else if (!ReadBoundedFile(ExtendedLengthPath(*storeDirectory / kFeaturesStoreFile), kuiMaximumOrderBytes, rState.features.bytes))
			{
				Fail("could not read the machine-local Features queue within the size bound (run plan order init)");
				return false;
			}
			ParseQueue(rState.plans, rState);
			ParseQueue(rState.features, rState);
			ValidateGraph(rState);
			ValidateFiles(rWorktree, rState, rIgnoredOrphans);
			const auto byPathLineCode = [](const Diagnostic& rLeft, const Diagnostic& rRight)
			{
				return std::tie(rLeft.path, rLeft.iLine, rLeft.code, rLeft.message) < std::tie(rRight.path, rRight.iLine, rRight.code, rRight.message);
			};
			std::sort(rState.diagnostics.begin(), rState.diagnostics.end(), byPathLineCode);
			std::sort(rState.notices.begin(), rState.notices.end(), byPathLineCode);
			return true;
		}

		void PrintValidation(const QueueState& rState)
		{
			nlohmann::json diagnostics = nlohmann::json::array();
			for (const Diagnostic& rDiagnostic : rState.diagnostics)
			{
				diagnostics.push_back({ { "path", rDiagnostic.path }, { "line", rDiagnostic.iLine }, { "code", rDiagnostic.code }, { "message", rDiagnostic.message } });
			}
			nlohmann::json notices = nlohmann::json::array();
			for (const Diagnostic& rNotice : rState.notices)
			{
				notices.push_back({ { "path", rNotice.path }, { "line", rNotice.iLine }, { "code", rNotice.code }, { "message", rNotice.message } });
			}
			nlohmann::json rows = nlohmann::json::array();
			for (const std::string& rPlan : rState.sortedPlans)
			{
				Row* pRow = rState.byPlan.at(rPlan);
				rows.push_back({ { "plan", rPlan }, { "queue", pRow->source }, { "rowSha256", Sha256(pRow->rowBytes).value_or("") } });
			}
			coordination::PrintMetadata({ { "ok", rState.diagnostics.empty() }, { "diagnostics", std::move(diagnostics) }, { "notices", std::move(notices) }, { "rows", std::move(rows) } });
		}

		struct QueueLocator
		{
			coordination::Locator locator;
			std::filesystem::path guardPath;
			std::string order;
			std::wstring repository;
			std::wstring normalizedOrder;
			std::wstring owner;
			nlohmann::json metadata;
			bool bAcquired = false;
		};

		std::optional<QueueLocator> MakeQueueLocator(const std::wstring& rRepository, const std::wstring& rOrder)
		{
			const std::optional<std::wstring> normalized = coordination::NormalizeRepositoryRelativeKey(rOrder);
			if (!normalized)
			{
				return std::nullopt;
			}
			const std::wstring key = rRepository + L"\n" + *normalized;
			const std::optional<coordination::Locator> locator = coordination::MakeLocator(L"plan-queue", key);
			if (!locator)
			{
				return std::nullopt;
			}
			return QueueLocator { *locator, locator->path.wstring() + L".guard", ToRepositoryPath(rOrder), rRepository, *normalized };
		}

		bool ValidateQueueMetadata(const nlohmann::json& rMetadata, const QueueLocator& rQueue)
		{
			return coordination::ValidateMetadataEnvelope(rMetadata, rQueue.locator) &&
				rMetadata.contains("repository") && rMetadata["repository"].is_string() && rMetadata["repository"].get<std::string>() == WideToUtf8(rQueue.repository) &&
				rMetadata.contains("order") && rMetadata["order"].is_string() && rMetadata["order"].get<std::string>() == WideToUtf8(rQueue.normalizedOrder);
		}

		void ReportInvalidClaim(const std::filesystem::path& rPath)
		{
			Fail("plan row claim is unreadable or invalid (delete the file to recover): " + WideToUtf8(rPath.wstring()));
		}

		bool ValidateClaimSnapshot(const QueueLocator& rQueue)
		{
			const std::optional<std::string> queueHash = Sha256(WideToUtf8(rQueue.locator.logicalKey));
			if (!queueHash)
			{
				return false;
			}
			const std::filesystem::path rowDirectory = GetLocalApplicationDataPath() / L"BrokenEngineLocks" / L"plan-row" / Utf8ToWide(*queueHash);
			std::error_code error;
			if (!std::filesystem::exists(rowDirectory, error))
			{
				return !error;
			}
			for (std::filesystem::directory_iterator it(rowDirectory, error), end; !error && it != end; it.increment(error))
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
				if (!coordination::ReadMetadata(it->path(), metadata) || !metadata.contains("plan") || !metadata["plan"].is_string())
				{
					ReportInvalidClaim(it->path());
					continue;
				}
				const std::optional<std::wstring> plan = coordination::NormalizeRepositoryRelativeKey(Utf8ToWide(metadata["plan"].get<std::string>()));
				if (!plan)
				{
					ReportInvalidClaim(it->path());
					continue;
				}
				const std::wstring rowKey = rQueue.locator.logicalKey + L"\n" + *plan;
				const std::optional<std::string> expectedHash = Sha256(WideToUtf8(rowKey));
				const coordination::Locator expected { L"plan-row", rowKey, it->path() };
				if (!expectedHash || it->path().filename() != Utf8ToWide(*expectedHash + ".lock") || !coordination::ValidateMetadataEnvelope(metadata, expected) ||
					!metadata.contains("repository") || !metadata["repository"].is_string() || metadata["repository"].get<std::string>() != WideToUtf8(rQueue.repository) ||
					!metadata.contains("order") || !metadata["order"].is_string() || metadata["order"].get<std::string>() != WideToUtf8(rQueue.normalizedOrder) ||
					metadata["plan"].get<std::string>() != WideToUtf8(*plan))
				{
					ReportInvalidClaim(it->path());
					continue;
				}
			}
			if (error)
			{
				Fail("could not enumerate plan row claims");
				return false;
			}
			return true;
		}

		class QueueLocks
		{
		public:
			bool Acquire(const Arguments& rArguments, const std::wstring& rRepository, const std::wstring& rWorktree)
			{
				if (rArguments.owner.empty() || rArguments.session.empty())
				{
					Fail("queue-changing plan order commands require --owner and --session");
					return false;
				}
				for (const std::wstring* pOrder : { &rArguments.plansOrder, &rArguments.featuresOrder })
				{
					std::optional<QueueLocator> locator = MakeQueueLocator(rRepository, *pOrder);
					if (!locator)
					{
						return false;
					}
					mQueues.push_back(std::move(*locator));
				}
				std::sort(mQueues.begin(), mQueues.end(), [](const QueueLocator& rLeft, const QueueLocator& rRight) { return rLeft.locator.logicalKey < rRight.locator.logicalKey; });
				for (QueueLocator& rQueue : mQueues)
				{
					rQueue.owner = rArguments.owner;
					if (!coordination::EnsureParentDirectory(rQueue.locator.path))
					{
						return FailAcquire("could not create plan queue directory");
					}
					coordination::Guard guard(rQueue.guardPath);
					if (!guard.IsValid())
					{
						Fail("could not acquire plan queue guard (" + guard.FailureReason() + ")");
						return FailAcquire(guard.TimedOut() ? "guard-timeout" : "guard-error");
					}
					std::error_code error;
					const bool bExists = std::filesystem::exists(rQueue.locator.path, error);
					if (error)
					{
						Fail("could not inspect plan queue lock");
						return FailAcquire("lock-inspection-failed");
					}
					if (!ValidateClaimSnapshot(rQueue))
					{
						return FailAcquire("invalid-row-claim-snapshot");
					}
					if (bExists)
					{
						nlohmann::json existing;
						if (!coordination::ReadMetadata(rQueue.locator.path, existing) || !ValidateQueueMetadata(existing, rQueue))
						{
							Fail("plan queue metadata is unreadable or invalid (delete the file to recover): " + WideToUtf8(rQueue.locator.path.wstring()));
							return FailAcquire("invalid-existing-lock");
						}
						return FailAcquire("already-locked");
					}
					nlohmann::json metadata = coordination::NewMetadata(rQueue.locator, rArguments.owner, rArguments.session, rWorktree);
					metadata["repository"] = WideToUtf8(rRepository);
					metadata["order"] = WideToUtf8(rQueue.normalizedOrder);
					if (!coordination::WriteMetadataAtomic(rQueue.locator.path, metadata))
					{
						FailWindows("write plan queue lock");
						return FailAcquire("lock-write-failed");
					}
					rQueue.metadata = std::move(metadata);
					rQueue.bAcquired = true;
				}
				return true;
			}

			bool AcquireCommitGuards(std::vector<std::unique_ptr<coordination::Guard>>& rGuards)
			{
				nlohmann::json results = nlohmann::json::array();
				for (QueueLocator& rQueue : mQueues)
				{
					if (!rQueue.bAcquired)
					{
						continue;
					}
					auto guard = std::make_unique<coordination::Guard>(rQueue.guardPath);
					if (!guard->IsValid())
					{
						results.push_back(LockResult(rQueue, false, guard->TimedOut() ? "guard-timeout" : "guard-error"));
						coordination::PrintMetadata({ { "conflict", "queue-ownership-lost" }, { "locks", std::move(results) } });
						return false;
					}
					nlohmann::json metadata;
					if (!ValidateClaimSnapshot(rQueue) || !coordination::ReadMetadata(rQueue.locator.path, metadata) || !ValidateQueueMetadata(metadata, rQueue) || !coordination::HasOwner(metadata, rQueue.owner))
					{
						results.push_back(LockResult(rQueue, false, "metadata-or-owner-mismatch"));
						coordination::PrintMetadata({ { "conflict", "queue-ownership-lost" }, { "locks", std::move(results) } });
						return false;
					}
					results.push_back(LockResult(rQueue, true, "owned"));
					rGuards.push_back(std::move(guard));
				}
				return true;
			}

			bool Release(nlohmann::json& rUnlockResults)
			{
				mbReleaseAttempted = true;
				rUnlockResults = nlohmann::json::array();
				bool bSucceeded = true;
				for (auto it = mQueues.rbegin(); it != mQueues.rend(); ++it)
				{
					QueueLocator& rQueue = *it;
					if (!rQueue.bAcquired)
					{
						continue;
					}
					coordination::Guard guard(rQueue.guardPath);
					bool bReleased = false;
					std::string reason = guard.TimedOut() ? "guard-timeout" : "guard-error";
					if (guard.IsValid())
					{
						nlohmann::json metadata;
						if (!coordination::ReadMetadata(rQueue.locator.path, metadata) || !ValidateQueueMetadata(metadata, rQueue) || !coordination::HasOwner(metadata, rQueue.owner))
						{
							reason = "metadata-or-owner-mismatch";
						}
						else if (::DeleteFileW(ExtendedLengthPath(rQueue.locator.path).c_str()) != FALSE)
						{
							bReleased = true;
							reason = "released";
							rQueue.bAcquired = false;
						}
						else
						{
							reason = "delete-failed";
						}
					}
					rUnlockResults.push_back(LockResult(rQueue, bReleased, reason));
					bSucceeded &= bReleased;
				}
				return bSucceeded;
			}

			~QueueLocks()
			{
				if (!mbReleaseAttempted)
				{
					nlohmann::json ignored;
					Release(ignored);
				}
			}

		private:
			nlohmann::json LockResult(const QueueLocator& rQueue, bool bSucceeded, const std::string& rReason) const
			{
				return { { "order", rQueue.order }, { "lockPath", WideToUtf8(rQueue.locator.path.wstring()) }, { "released", bSucceeded }, { "reason", rReason } };
			}

			bool FailAcquire(const std::string& rReason)
			{
				nlohmann::json unlockResults;
				const bool bReleased = Release(unlockResults);
				coordination::PrintMetadata({ { "conflict", "queue-lock-acquire-failed" }, { "reason", rReason }, { "partialReleaseSucceeded", bReleased }, { "unlockResults", std::move(unlockResults) } });
				return false;
			}

			std::vector<QueueLocator> mQueues;
			bool mbReleaseAttempted = false;
		};

		bool WriteBytesAtomic(const std::filesystem::path& rPath, std::string_view bytes)
		{
			static uint32_t suiSequence = 0;
			std::filesystem::path temporary = rPath;
			temporary += L".plan-order.tmp." + std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(++suiSequence);
			Handle hFile(::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr));
			if (!hFile.IsValid())
			{
				return false;
			}
			DWORD uiWritten = 0;
			const bool bWritten = bytes.size() <= MAXDWORD && ::WriteFile(hFile.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &uiWritten, nullptr) != FALSE && uiWritten == bytes.size() && ::FlushFileBuffers(hFile.Get()) != FALSE;
			hFile.Reset();
			const bool bMoved = bWritten && ::MoveFileExW(temporary.c_str(), rPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
			if (!bMoved)
			{
				::DeleteFileW(temporary.c_str());
			}
			return bMoved;
		}

		struct FileChange
		{
			std::filesystem::path path;
			std::optional<std::string> original;
			std::optional<std::string> replacement;
		};

		bool ApplyFileChanges(std::vector<FileChange>& rChanges)
		{
			size_t uiApplied = 0;
			for (; uiApplied < rChanges.size(); ++uiApplied)
			{
				FileChange& rChange = rChanges.at(uiApplied);
				bool bSucceeded = false;
				if (rChange.replacement)
				{
					bSucceeded = WriteBytesAtomic(rChange.path, *rChange.replacement);
				}
				else
				{
					bSucceeded = ::DeleteFileW(rChange.path.c_str()) != FALSE;
				}
				if (!bSucceeded)
				{
					break;
				}
			}
			if (uiApplied == rChanges.size())
			{
				return true;
			}
			bool bRestored = true;
			while (uiApplied > 0)
			{
				FileChange& rChange = rChanges.at(--uiApplied);
				if (rChange.original)
				{
					bRestored &= WriteBytesAtomic(rChange.path, *rChange.original);
				}
				else
				{
					std::error_code error;
					bRestored &= !std::filesystem::exists(rChange.path, error) || (!error && ::DeleteFileW(rChange.path.c_str()) != FALSE);
				}
			}
			Fail(bRestored ? "plan order transaction failed and original bytes were restored" : "plan order transaction failed and exact rollback also failed");
			return false;
		}

		// Absolute request under the (lowercased) worktree -> worktree-relative remainder, preserving
		// original casing so the Temp/ gate still fires; every other input is returned byte-identical.
		std::wstring NormalizeRequestUnderWorktree(const std::wstring& rRequest, const std::wstring& rWorktree)
		{
			std::wstring request = rRequest;
			std::replace(request.begin(), request.end(), L'/', L'\\');
			if (!std::filesystem::path(request).is_absolute())
			{
				return rRequest;
			}
			std::wstring root = rWorktree;
			while (!root.empty() && root.back() == L'\\')
			{
				root.pop_back();
			}
			const std::wstring requestFolded = ToLowerInvariant(request);
			const std::wstring rootFolded = ToLowerInvariant(root);
			if (requestFolded.size() <= rootFolded.size() || requestFolded.compare(0, rootFolded.size(), rootFolded) != 0 || requestFolded[rootFolded.size()] != L'\\')
			{
				return rRequest;
			}
			std::wstring remainder = request.substr(root.size() + 1);
			std::replace(remainder.begin(), remainder.end(), L'\\', L'/');
			return remainder;
		}

		bool ReadJsonRequest(const Arguments& rArguments, const std::wstring& rWorktree, nlohmann::json& rRequest)
		{
			const std::wstring request = NormalizeRequestUnderWorktree(rArguments.request, rWorktree);
			const std::string requestIdentity = ToRepositoryPath(request);
			if (!requestIdentity.starts_with("Temp/"))
			{
				Fail("--request must be a repository-relative path beneath the session Temp directory");
				return false;
			}
			const std::optional<std::filesystem::path> path = ResolveContainedPath(rWorktree, request, true);
			std::string bytes;
			if (!path || !IsLexicallyContained(std::filesystem::path(rWorktree) / L"Temp", *path) || !ReadBoundedFile(*path, kuiMaximumRequestBytes, bytes) || !IsStrictUtf8(bytes))
			{
				Fail("request is not a safe bounded strict-UTF-8 file beneath Temp");
				return false;
			}
			try
			{
				rRequest = nlohmann::json::parse(bytes);
			}
			catch (const std::exception&)
			{
				Fail("request is not valid JSON");
				return false;
			}
			return rRequest.is_object() && rRequest.contains("schemaVersion") && coordination::JsonIntegerEquals(rRequest["schemaVersion"], 1);
		}

		bool ReadStringField(const nlohmann::json& rObject, const char* pName, std::string& rValue, bool bAllowEmpty = false)
		{
			if (!rObject.contains(pName) || !rObject[pName].is_string())
			{
				return false;
			}
			rValue = rObject[pName].get<std::string>();
			return bAllowEmpty || !rValue.empty();
		}

		bool ReadRowFields(const nlohmann::json& rObject, Row& rRow)
		{
			const std::optional<int64_t> effort = rObject.contains("effort") ? coordination::JsonInt64(rObject["effort"]) : std::nullopt;
			const std::optional<int64_t> impact = rObject.contains("impact") ? coordination::JsonInt64(rObject["impact"]) : std::nullopt;
			const std::optional<int64_t> risks = rObject.contains("risks") ? coordination::JsonInt64(rObject["risks"]) : std::nullopt;
			if (!ReadStringField(rObject, "tier", rRow.tier) || !ReadStringField(rObject, "notes", rRow.notes, true) || !effort || !impact || !risks || *effort < INT_MIN || *effort > INT_MAX || *impact < INT_MIN || *impact > INT_MAX || *risks < INT_MIN || *risks > INT_MAX)
			{
				return false;
			}
			rRow.iEffort = static_cast<int>(*effort);
			rRow.iImpact = static_cast<int>(*impact);
			rRow.iRisks = static_cast<int>(*risks);
			const int64_t iScore = static_cast<int64_t>(rRow.iEffort) - rRow.iImpact + rRow.iRisks;
			if (iScore < INT_MIN || iScore > INT_MAX || rRow.tier.find_first_of("|\r\n") != std::string::npos || rRow.notes.find_first_of("\r\n") != std::string::npos)
			{
				return false;
			}
			rRow.iScore = static_cast<int>(iScore);
			if (rObject.contains("dependsOn"))
			{
				if (!rObject["dependsOn"].is_array())
				{
					return false;
				}
				for (const nlohmann::json& rDependency : rObject["dependsOn"])
				{
					if (!rDependency.is_string())
					{
						return false;
					}
					rRow.dependencies.push_back(rDependency.get<std::string>());
				}
			}
			return true;
		}

		using AddSequence = std::vector<std::pair<std::string, Row>>;

		bool ParseAddRequest(const nlohmann::json& rRequest, std::vector<AddSequence>& rSequences, std::set<std::string>& rPlans)
		{
			if (!rRequest.contains("operation") || rRequest["operation"] != "add" || !rRequest.contains("sequences") || !rRequest["sequences"].is_array())
			{
				return false;
			}
			for (const nlohmann::json& rSequenceJson : rRequest["sequences"])
			{
				if (!rSequenceJson.is_array() || rSequenceJson.empty())
				{
					return false;
				}
				AddSequence sequence;
				for (const nlohmann::json& rEntry : rSequenceJson)
				{
					Row row;
					std::string queueName;
					if (!rEntry.is_object() || !ReadStringField(rEntry, "queue", queueName) || !ReadStringField(rEntry, "plan", row.plan) || !ReadRowFields(rEntry, row) || !IsCanonicalDependency(row.plan) ||
						(queueName != "plans" && queueName != "features") || !rPlans.insert(row.plan).second)
					{
						return false;
					}
					sequence.emplace_back(std::move(queueName), std::move(row));
				}
				rSequences.push_back(std::move(sequence));
			}
			return true;
		}

		QueueFile* QueueForName(QueueState& rState, const std::string& rQueue)
		{
			if (rQueue == "plans")
			{
				return &rState.plans;
			}
			if (rQueue == "features")
			{
				return &rState.features;
			}
			return nullptr;
		}

		QueueFile* QueueForPlan(QueueState& rState, const std::string& rPlan)
		{
			if (rPlan.starts_with(rState.plans.directory + "/"))
			{
				return &rState.plans;
			}
			if (rPlan.starts_with(rState.features.directory + "/"))
			{
				return &rState.features;
			}
			return nullptr;
		}

		std::optional<std::wstring> ClaimPlanKey(const QueueFile& rQueue, const std::string& rPlan)
		{
			if (!rPlan.starts_with(rQueue.directory + "/"))
			{
				return std::nullopt;
			}
			return coordination::NormalizeRepositoryRelativeKey(Utf8ToWide(RelativePlanLink(rQueue, rPlan)));
		}

		std::optional<std::filesystem::path> ClaimPath(const std::wstring& rRepository, const QueueFile& rQueue, const std::string& rPlan)
		{
			const std::wstring order = coordination::NormalizeRepositoryRelativeKey(Utf8ToWide(rQueue.path)).value();
			const std::optional<std::wstring> plan = ClaimPlanKey(rQueue, rPlan);
			if (!plan)
			{
				return std::nullopt;
			}
			const std::wstring queueKey = rRepository + L"\n" + order;
			const std::optional<std::string> queueHash = Sha256(WideToUtf8(queueKey));
			const std::optional<std::string> rowHash = Sha256(WideToUtf8(queueKey + L"\n" + *plan));
			if (!queueHash || !rowHash)
			{
				return std::nullopt;
			}
			return GetLocalApplicationDataPath() / L"BrokenEngineLocks" / L"plan-row" / Utf8ToWide(*queueHash) / Utf8ToWide(*rowHash + ".lock");
		}

		bool ReadClaim(const std::wstring& rRepository, const QueueFile& rQueue, const std::string& rPlan, bool& rbExists, nlohmann::json& rMetadata)
		{
			const std::optional<std::filesystem::path> path = ClaimPath(rRepository, rQueue, rPlan);
			if (!path)
			{
				return false;
			}
			std::error_code error;
			rbExists = std::filesystem::exists(ExtendedLengthPath(*path), error);
			if (error)
			{
				return false;
			}
			if (!rbExists)
			{
				return true;
			}
			const std::wstring order = coordination::NormalizeRepositoryRelativeKey(Utf8ToWide(rQueue.path)).value();
			const std::optional<std::wstring> plan = ClaimPlanKey(rQueue, rPlan);
			if (!plan)
			{
				return false;
			}
			const coordination::Locator locator { L"plan-row", rRepository + L"\n" + order + L"\n" + *plan, *path };
			const bool bValid = coordination::ReadMetadata(*path, rMetadata) &&
				coordination::ValidateMetadataEnvelope(rMetadata, locator) && rMetadata.contains("repository") && rMetadata["repository"].is_string() && rMetadata["repository"].get<std::string>() == WideToUtf8(rRepository) &&
				rMetadata.contains("order") && rMetadata["order"].is_string() && rMetadata["order"].get<std::string>() == WideToUtf8(order) &&
				rMetadata.contains("plan") && rMetadata["plan"].is_string() && rMetadata["plan"].get<std::string>() == WideToUtf8(*plan);
			if (!bValid)
			{
				// An unreadable claim still blocks its own row; only enumeration-wide validation skips it.
				ReportInvalidClaim(*path);
				rMetadata = { { "invalid", true }, { "path", WideToUtf8(path->wstring()) } };
			}
			return true;
		}

		bool IsClaimOwnedByRequester(const nlohmann::json& rClaim, const Arguments& rArguments, const std::wstring& rWorktree)
		{
			return coordination::HasOwner(rClaim, rArguments.owner) &&
				rClaim.contains("session") && rClaim["session"].is_string() && rClaim["session"].get<std::string>() == WideToUtf8(rArguments.session) &&
				rClaim.contains("worktree") && rClaim["worktree"].is_string() && rClaim["worktree"].get<std::string>() == WideToUtf8(rWorktree);
		}

		bool RowValuesEqual(const Row& rLeft, const Row& rRight)
		{
			return rLeft.tier == rRight.tier && rLeft.iEffort == rRight.iEffort && rLeft.iImpact == rRight.iImpact &&
				rLeft.iRisks == rRight.iRisks && rLeft.notes == rRight.notes && rLeft.dependencies == rRight.dependencies;
		}

		bool CollectClaimedPlans(const std::wstring& rRepository, QueueState& rState, std::set<std::string>& rClaimedPlans)
		{
			for (QueueFile* pQueue : { &rState.plans, &rState.features })
			{
				for (const Row& rRow : pQueue->rows)
				{
					bool bClaimExists = false;
					nlohmann::json claim;
					if (!ReadClaim(rRepository, *pQueue, rRow.plan, bClaimExists, claim))
					{
						Fail("could not enumerate plan row claims for the queue");
						return false;
					}
					if (bClaimExists)
					{
						rClaimedPlans.insert(rRow.plan);
					}
				}
			}
			return true;
		}

		const std::string* PlanForRowDiagnostic(const QueueState& rState, const Diagnostic& rDiagnostic)
		{
			for (const QueueFile* pQueue : { &rState.plans, &rState.features })
			{
				for (const Row& rRow : pQueue->rows)
				{
					if (rRow.source == rDiagnostic.path && rRow.iLine == rDiagnostic.iLine)
					{
						return &rRow.plan;
					}
				}
			}
			return nullptr;
		}

		// Rows whose plan file is missing from this worktree but present in the primary checkout: the plan landed on
		// primary after this worktree's baseline, so reconciliation restores it. Empty unless a distinct primary exists;
		// any path-resolution failure leaves the row out so it stays blocking (fail-safe).
		std::set<std::string> CollectStaleBaselinePlans(const QueueState& rState, const std::wstring& rWorktree, const std::optional<std::wstring>& rPrimary)
		{
			std::set<std::string> stalePlans;
			if (!rPrimary || *rPrimary == rWorktree)
			{
				return stalePlans;
			}
			for (const Diagnostic& rDiagnostic : rState.diagnostics)
			{
				if (rDiagnostic.code != "missing-plan-file")
				{
					continue;
				}
				const std::string* pRowPlan = PlanForRowDiagnostic(rState, rDiagnostic);
				if (pRowPlan != nullptr && ResolveContainedPath(*rPrimary, Utf8ToWide(*pRowPlan), true))
				{
					stalePlans.insert(*pRowPlan);
				}
			}
			return stalePlans;
		}

		// orphan-plan is a notice, never a diagnostic, so it never reaches this gate; a missing-plan-file for a currently
		// claimed row is an in-flight completion, and one for a stale-baseline row (plan present on primary, absent here)
		// heals at reconciliation; complete ignores diagnostics attributed to its own target and the dependent-edge tear
		// its rerun heals; add ignores the missing-dependency/duplicate tears its rerun rewrites. Everything else
		// (parse/graph/unrelated file) still blocks.
		bool HasBlockingDiagnostics(const QueueState& rState, const std::set<std::string>& rClaimedPlans, const std::string* pTargetPlan, const std::set<std::string>& rRequestPlans = {}, const std::set<std::string>& rStaleBaselinePlans = {})
		{
			for (const Diagnostic& rDiagnostic : rState.diagnostics)
			{
				const std::string* pRowPlan = PlanForRowDiagnostic(rState, rDiagnostic);
				const std::string* pAttributed = pRowPlan != nullptr ? pRowPlan : (rDiagnostic.iLine == 0 ? &rDiagnostic.path : nullptr);
				if (pTargetPlan != nullptr && pAttributed != nullptr && *pAttributed == *pTargetPlan)
				{
					continue;
				}
				if (rDiagnostic.code == "missing-plan-file" && pRowPlan != nullptr && (rClaimedPlans.contains(*pRowPlan) || rStaleBaselinePlans.contains(*pRowPlan)))
				{
					continue;
				}
				// Torn cross-queue complete: the rerun strips X's dependency edges, healing a dependent row left
				// referencing the just-removed target X by a half-applied write.
				if (rDiagnostic.code == "missing-dependency" && pTargetPlan != nullptr && pRowPlan != nullptr && !rState.byPlan.contains(*pTargetPlan))
				{
					const std::unordered_map<std::string, Row*>::const_iterator it = rState.byPlan.find(*pRowPlan);
					if (it != rState.byPlan.end() && std::find(it->second->dependencies.begin(), it->second->dependencies.end(), *pTargetPlan) != it->second->dependencies.end())
					{
						continue;
					}
				}
				// Torn cross-queue add: the rerun rewrites both queues, so a missing-dependency/duplicate-plan attributed
				// to a plan in the current request is the other half of a half-applied add and heals on retry.
				if ((rDiagnostic.code == "missing-dependency" || rDiagnostic.code == "duplicate-plan") && pAttributed != nullptr && rRequestPlans.contains(*pAttributed))
				{
					continue;
				}
				return true;
			}
			return false;
		}

		std::optional<bool> HasInProgressGitOperation(const std::wstring& rWorktree)
		{
			for (const std::wstring_view name : { L"MERGE_HEAD", L"rebase-merge", L"rebase-apply", L"CHERRY_PICK_HEAD", L"REVERT_HEAD", L"BISECT_LOG", L"sequencer" })
			{
				std::optional<std::string> path = RunGit({ L"-C", rWorktree, L"rev-parse", L"--path-format=absolute", L"--git-path", std::wstring(name) });
				if (!path)
				{
					return std::nullopt;
				}
				TrimLineEnd(*path);
				std::error_code error;
				const bool bExists = std::filesystem::exists(Utf8ToWide(*path), error);
				if (error)
				{
					return std::nullopt;
				}
				if (bExists)
				{
					return true;
				}
			}
			return false;
		}

		int RunValidate(const Arguments& rArguments, const std::wstring& rRepository, const std::wstring& rWorktree, bool bPrimary, const std::optional<std::wstring>& rPrimary)
		{
			QueueState state;
			if (!LoadQueueState(rRepository, rWorktree, rArguments, std::nullopt, std::nullopt, {}, state))
			{
				return kiExitFailure;
			}
			std::set<std::string> claimedPlans;
			if (!CollectClaimedPlans(rRepository, state, claimedPlans))
			{
				return kiExitFailure;
			}
			const std::set<std::string> staleBaselinePlans = CollectStaleBaselinePlans(state, rWorktree, rPrimary);
			// A missing-plan-file for a row held by a live claim is the transient post-advance/pre-phase-2 window (or a
			// crashed session); one for a foreign row whose plan landed on primary after this worktree's baseline is
			// resolved by reconciliation. Either surfaces as a non-blocking notice so it never reds out a later validate.
			std::vector<Diagnostic> blocking;
			for (Diagnostic& rDiagnostic : state.diagnostics)
			{
				const std::string* pRowPlan = PlanForRowDiagnostic(state, rDiagnostic);
				if (rDiagnostic.code == "missing-plan-file" && pRowPlan != nullptr && (claimedPlans.contains(*pRowPlan) || staleBaselinePlans.contains(*pRowPlan)))
				{
					state.notices.push_back(std::move(rDiagnostic));
				}
				else
				{
					blocking.push_back(std::move(rDiagnostic));
				}
			}
			state.diagnostics = std::move(blocking);
			std::sort(state.notices.begin(), state.notices.end(), [](const Diagnostic& rLeft, const Diagnostic& rRight)
			{
				return std::tie(rLeft.path, rLeft.iLine, rLeft.code, rLeft.message) < std::tie(rRight.path, rRight.iLine, rRight.code, rRight.message);
			});
			if (bPrimary)
			{
				const std::optional<std::string> status = RunGit({ L"-C", rWorktree, L"status", L"--porcelain=v1", L"--untracked-files=normal" });
				const std::optional<bool> operation = HasInProgressGitOperation(rWorktree);
				if (!status || !operation)
				{
					AddDiagnostic(state, ".", 0, "git-authority-unverifiable", "primary checkout Git authority could not be verified");
				}
				else if (*operation)
				{
					AddDiagnostic(state, ".", 0, "git-operation-in-progress", "primary checkout has an in-progress Git operation");
				}
				std::sort(state.diagnostics.begin(), state.diagnostics.end(), [](const Diagnostic& rLeft, const Diagnostic& rRight)
				{
					return std::tie(rLeft.path, rLeft.iLine, rLeft.code, rLeft.message) < std::tie(rRight.path, rRight.iLine, rRight.code, rRight.message);
				});
			}
			PrintValidation(state);
			return state.diagnostics.empty() ? kiExitOk : kiExitStateConflict;
		}

		int RunInit(const Arguments& rArguments, const std::wstring& rRepository, const std::wstring& rWorktree)
		{
			const std::optional<std::filesystem::path> storeDirectory = QueueStoreDirectory(rRepository);
			if (!storeDirectory)
			{
				Fail("could not resolve the machine-local plan queue store path");
				return kiExitFailure;
			}
			// Serialize against add/update/complete/claim-next, which guard store writes with the per-queue commit guards
			// (QueueLocks::AcquireCommitGuards) rather than a store-local guard, so init --force cannot interleave with a
			// mutator's store write and drop rows.
			std::vector<QueueLocator> queueGuards;
			for (const std::wstring* pOrder : { &rArguments.plansOrder, &rArguments.featuresOrder })
			{
				std::optional<QueueLocator> locator = MakeQueueLocator(rRepository, *pOrder);
				if (!locator)
				{
					Fail("could not resolve the plan queue guard");
					return kiExitFailure;
				}
				queueGuards.push_back(std::move(*locator));
			}
			std::sort(queueGuards.begin(), queueGuards.end(), [](const QueueLocator& rLeft, const QueueLocator& rRight) { return rLeft.locator.logicalKey < rRight.locator.logicalKey; });
			std::vector<std::unique_ptr<coordination::Guard>> guards;
			for (const QueueLocator& rQueue : queueGuards)
			{
				if (!coordination::EnsureParentDirectory(rQueue.locator.path))
				{
					FailWindows("create plan queue directory");
					return kiExitFailure;
				}
				std::unique_ptr<coordination::Guard> guard = std::make_unique<coordination::Guard>(rQueue.guardPath);
				if (!guard->IsValid())
				{
					Fail("could not acquire plan queue guard (" + guard->FailureReason() + ")");
					return kiExitStateConflict;
				}
				guards.push_back(std::move(guard));
				// A mutator's QueueLocks::Acquire writes the lock metadata file and holds it until Release — spanning its
				// read->commit window that init's momentary guard does not. Reseeding now would be overwritten from the
				// mutator's pre-reseed read, so refuse (both seeded and --force paths) rather than lose rows.
				std::error_code lockError;
				const bool bLocked = std::filesystem::exists(ExtendedLengthPath(rQueue.locator.path), lockError);
				if (lockError)
				{
					Fail("could not inspect the plan queue lock");
					return kiExitFailure;
				}
				if (bLocked)
				{
					Fail("queue mutation in flight; retry after it completes");
					return kiExitStateConflict;
				}
			}
			if (!coordination::EnsureParentDirectory(*storeDirectory / kPlansStoreFile))
			{
				FailWindows("create plan queue store directory");
				return kiExitFailure;
			}
			struct StoreSeed
			{
				std::wstring_view storeName;
				const std::wstring* pWorktreeRelative;
				std::string_view title;
			};
			const StoreSeed seeds[] =
			{
				{ kPlansStoreFile, &rArguments.plansOrder, "Plan Execution Order" },
				{ kFeaturesStoreFile, &rArguments.featuresOrder, "Feature Execution Order" },
			};
			nlohmann::json files = nlohmann::json::array();
			bool bAllPresent = true;
			for (const StoreSeed& rSeed : seeds)
			{
				const std::filesystem::path storePath = *storeDirectory / rSeed.storeName;
				const std::filesystem::path extendedStorePath = ExtendedLengthPath(storePath);
				std::error_code existsError;
				const bool bExists = std::filesystem::exists(extendedStorePath, existsError);
				if (existsError)
				{
					Fail("could not inspect the machine-local queue store file");
					return kiExitFailure;
				}
				if (bExists && !rArguments.bForce)
				{
					// Present store: keep it. An unreadable/oversized existing file is a hard error, never a silent reseed.
					std::string existing;
					if (!ReadBoundedFile(extendedStorePath, kuiMaximumOrderBytes, existing))
					{
						Fail("existing machine-local queue store file is unreadable or oversized (use --force to reseed)");
						return kiExitFailure;
					}
					if (!existing.empty())
					{
						files.push_back({ { "store", WideToUtf8(storePath.wstring()) }, { "action", "kept" }, { "sha256", Sha256(existing).value_or("") } });
						continue;
					}
				}
				// Absent, present-but-empty, or --force: seed.
				bAllPresent = false;
				std::string bytes;
				std::string action;
				const std::optional<std::filesystem::path> source = ResolveContainedPath(rWorktree, *rSeed.pWorktreeRelative, true);
				if (source && ReadBoundedFile(*source, kuiMaximumOrderBytes, bytes) && IsStrictUtf8(bytes))
				{
					action = "seeded-from-worktree";
				}
				else
				{
					bytes = EmptyQueueTemplate(rSeed.title);
					action = "seeded-empty";
				}
				if (!WriteBytesAtomic(extendedStorePath, bytes))
				{
					FailWindows("write machine-local queue store file");
					return kiExitFailure;
				}
				files.push_back({ { "store", WideToUtf8(storePath.wstring()) }, { "action", action }, { "sha256", Sha256(bytes).value_or("") } });
			}
			coordination::PrintMetadata({ { "schemaVersion", 1 }, { "operation", "init" }, { "handled", true }, { "alreadyInitialized", bAllPresent && !rArguments.bForce }, { "force", rArguments.bForce }, { "files", std::move(files) } });
			return kiExitOk;
		}

		int RunAdd(const Arguments& rArguments, const std::wstring& rRepository, const std::wstring& rWorktree, const std::optional<std::wstring>& rPrimary)
		{
			nlohmann::json request;
			std::vector<AddSequence> sequences;
			std::set<std::string> added;
			if (!ReadJsonRequest(rArguments, rWorktree, request) || !ParseAddRequest(request, sequences, added))
			{
				Fail("add request must use schemaVersion 1, operation add, and a sequences array");
				return kiExitFailure;
			}
			QueueLocks locks;
			if (!locks.Acquire(rArguments, rRepository, rWorktree))
			{
				return kiExitStateConflict;
			}
			QueueState state;
			if (!LoadQueueState(rRepository, rWorktree, rArguments, std::nullopt, std::nullopt, added, state))
			{
				return kiExitFailure;
			}
			std::set<std::string> claimedPlans;
			if (!CollectClaimedPlans(rRepository, state, claimedPlans))
			{
				return kiExitFailure;
			}
			const std::set<std::string> staleBaselinePlans = CollectStaleBaselinePlans(state, rWorktree, rPrimary);
			if (HasBlockingDiagnostics(state, claimedPlans, nullptr, added, staleBaselinePlans))
			{
				PrintValidation(state);
				return kiExitStateConflict;
			}
			for (AddSequence& rSequence : sequences)
			{
				std::string predecessor;
				for (auto& [rQueueName, rRow] : rSequence)
				{
					QueueFile* pQueue = QueueForName(state, rQueueName);
					if (pQueue == nullptr || QueueForPlan(state, rRow.plan) != pQueue)
					{
						Fail("add entry queue/path is inconsistent");
						return kiExitStateConflict;
					}
					const std::optional<std::filesystem::path> planPath = ResolveContainedPath(rWorktree, Utf8ToWide(rRow.plan), true);
					std::string planBytes;
					if (!planPath || !ReadBoundedFile(*planPath, kuiMaximumPlanBytes, planBytes) || !IsStrictUtf8(planBytes))
					{
						Fail("add entry plan file is not safe bounded strict UTF-8 input");
						return kiExitFailure;
					}
					for (const std::string& rDependency : rRow.dependencies)
					{
						if (!state.byPlan.contains(rDependency))
						{
							Fail("add entry explicit dependencies must identify already-live executable rows");
							return kiExitStateConflict;
						}
					}
					if (!predecessor.empty())
					{
						rRow.dependencies.push_back(predecessor);
					}
					const std::unordered_map<std::string, Row*>::iterator existing = state.byPlan.find(rRow.plan);
					if (existing != state.byPlan.end())
					{
						// Retry idempotency: an identical row already present is skipped; a differing duplicate is an error.
						if (!RowValuesEqual(*existing->second, rRow))
						{
							coordination::PrintMetadata({ { "conflict", "duplicate-row-mismatch" }, { "plan", rRow.plan } });
							return kiExitStateConflict;
						}
						predecessor = rRow.plan;
						continue;
					}
					rRow.source = pQueue->path;
					pQueue->rows.push_back(std::move(rRow));
					predecessor = pQueue->rows.back().plan;
				}
			}
			std::string plansBytes = RenderQueue(state.plans);
			std::string featuresBytes = RenderQueue(state.features);
			QueueState prospective;
			if (!LoadQueueState(rRepository, rWorktree, rArguments, plansBytes, featuresBytes, added, prospective))
			{
				return kiExitFailure;
			}
			const std::set<std::string> prospectiveStalePlans = CollectStaleBaselinePlans(prospective, rWorktree, rPrimary);
			if (HasBlockingDiagnostics(prospective, claimedPlans, nullptr, added, prospectiveStalePlans))
			{
				PrintValidation(prospective);
				return kiExitStateConflict;
			}
			const std::optional<std::filesystem::path> storeDirectory = QueueStoreDirectory(rRepository);
			if (!storeDirectory)
			{
				Fail("could not resolve the machine-local plan queue store path");
				return kiExitFailure;
			}
			std::vector<FileChange> changes
			{
				{ ExtendedLengthPath(*storeDirectory / kPlansStoreFile), state.plans.bytes, plansBytes },
				{ ExtendedLengthPath(*storeDirectory / kFeaturesStoreFile), state.features.bytes, featuresBytes },
			};
			std::vector<std::unique_ptr<coordination::Guard>> commitGuards;
			if (!locks.AcquireCommitGuards(commitGuards))
			{
				return kiExitStateConflict;
			}
			if (!ApplyFileChanges(changes))
			{
				return kiExitFailure;
			}
			commitGuards.clear();
			nlohmann::json addedPlans = nlohmann::json::array();
			for (const std::string& rPlan : added)
			{
				std::string bytes;
				ReadBoundedFile(std::filesystem::path(rWorktree) / Utf8ToWide(rPlan), kuiMaximumPlanBytes, bytes);
				addedPlans.push_back({ { "plan", rPlan }, { "sha256", Sha256(bytes).value_or("") } });
			}
			nlohmann::json unlockResults;
			const bool bUnlocked = locks.Release(unlockResults);
			nlohmann::json receipt = { { "schemaVersion", 1 }, { "operation", "add" }, { "handled", true }, { "plans", std::move(addedPlans) }, { "plansOrderSha256", Sha256(plansBytes).value_or("") }, { "featuresOrderSha256", Sha256(featuresBytes).value_or("") }, { "unlockResults", std::move(unlockResults) } };
			coordination::PrintMetadata(receipt);
			return bUnlocked ? kiExitOk : kiExitStateConflict;
		}

		int RunUpdate(const Arguments& rArguments, const std::wstring& rRepository, const std::wstring& rWorktree, const std::optional<std::wstring>& rPrimary)
		{
			nlohmann::json request;
			if (!ReadJsonRequest(rArguments, rWorktree, request) || !request.contains("operation") || request["operation"] != "update" || !request.contains("updates") || !request["updates"].is_array() || request["updates"].empty())
			{
				Fail("update request must use schemaVersion 1, operation update, and a non-empty updates array");
				return kiExitFailure;
			}
			QueueLocks locks;
			if (!locks.Acquire(rArguments, rRepository, rWorktree))
			{
				return kiExitStateConflict;
			}
			QueueState state;
			if (!LoadQueueState(rRepository, rWorktree, rArguments, std::nullopt, std::nullopt, {}, state))
			{
				return kiExitFailure;
			}
			std::set<std::string> claimedPlans;
			if (!CollectClaimedPlans(rRepository, state, claimedPlans))
			{
				return kiExitFailure;
			}
			const std::set<std::string> staleBaselinePlans = CollectStaleBaselinePlans(state, rWorktree, rPrimary);
			if (HasBlockingDiagnostics(state, claimedPlans, nullptr, {}, staleBaselinePlans))
			{
				PrintValidation(state);
				return kiExitStateConflict;
			}
			std::vector<FileChange> planChanges;
			nlohmann::json updatedPlans = nlohmann::json::array();
			std::set<std::string> updated;
			for (const nlohmann::json& rUpdate : request["updates"])
			{
				Row replacement;
				std::string expectedPlanHash;
				std::string expectedRowHash;
				std::string stagedContent;
				if (!rUpdate.is_object() || !ReadStringField(rUpdate, "plan", replacement.plan) || !ReadStringField(rUpdate, "expectedPlanSha256", expectedPlanHash) || !ReadStringField(rUpdate, "expectedRowSha256", expectedRowHash) || !ReadStringField(rUpdate, "stagedContent", stagedContent) || !ReadRowFields(rUpdate, replacement) || !IsCanonicalDependency(replacement.plan) || !updated.insert(replacement.plan).second)
				{
					Fail("update entry is malformed or duplicated");
					return kiExitFailure;
				}
				std::unordered_map<std::string, Row*>::iterator it = state.byPlan.find(replacement.plan);
				if (it == state.byPlan.end() || expectedPlanHash.size() != 64 || expectedRowHash.size() != 64)
				{
					Fail("update target or expected hashes are invalid");
					return kiExitStateConflict;
				}
				QueueFile* pQueue = QueueForPlan(state, replacement.plan);
				bool bClaimExists = false;
				nlohmann::json claim;
				if (pQueue == nullptr || !ReadClaim(rRepository, *pQueue, replacement.plan, bClaimExists, claim))
				{
					return kiExitFailure;
				}
				if (bClaimExists && !IsClaimOwnedByRequester(claim, rArguments, rWorktree))
				{
					coordination::PrintMetadata({ { "conflict", "claimed" }, { "plan", replacement.plan }, { "claim", claim } });
					return kiExitStateConflict;
				}
				const std::optional<std::filesystem::path> planPath = ResolveContainedPath(rWorktree, Utf8ToWide(replacement.plan), true);
				const std::string stagedIdentity = ToRepositoryPath(Utf8ToWide(stagedContent));
				const std::optional<std::filesystem::path> stagedPath = ResolveContainedPath(rWorktree, Utf8ToWide(stagedContent), true);
				std::string originalPlan;
				std::string replacementPlan;
				if (!planPath || !stagedPath || !stagedIdentity.starts_with("Temp/") || !IsLexicallyContained(std::filesystem::path(rWorktree) / L"Temp", *stagedPath) ||
					!ReadBoundedFile(*planPath, kuiMaximumPlanBytes, originalPlan) || !ReadBoundedFile(*stagedPath, kuiMaximumPlanBytes, replacementPlan) || !IsStrictUtf8(replacementPlan))
				{
					Fail("update plan or staged content is unsafe, oversized, or invalid UTF-8");
					return kiExitFailure;
				}
				if (Sha256(originalPlan).value_or("") != expectedPlanHash || Sha256(it->second->rowBytes).value_or("") != expectedRowHash)
				{
					coordination::PrintMetadata({ { "conflict", "hash-mismatch" }, { "plan", replacement.plan }, { "actualPlanSha256", Sha256(originalPlan).value_or("") }, { "actualRowSha256", Sha256(it->second->rowBytes).value_or("") } });
					return kiExitStateConflict;
				}
				replacement.source = pQueue->path;
				*it->second = replacement;
				updatedPlans.push_back({ { "plan", replacement.plan }, { "beforeSha256", expectedPlanHash }, { "afterSha256", Sha256(replacementPlan).value_or("") } });
				planChanges.push_back({ *planPath, std::move(originalPlan), std::move(replacementPlan) });
			}
			const std::string plansBytes = RenderQueue(state.plans);
			const std::string featuresBytes = RenderQueue(state.features);
			QueueState prospective;
			if (!LoadQueueState(rRepository, rWorktree, rArguments, plansBytes, featuresBytes, {}, prospective))
			{
				return kiExitFailure;
			}
			const std::set<std::string> prospectiveStalePlans = CollectStaleBaselinePlans(prospective, rWorktree, rPrimary);
			if (HasBlockingDiagnostics(prospective, claimedPlans, nullptr, {}, prospectiveStalePlans))
			{
				PrintValidation(prospective);
				return kiExitStateConflict;
			}
			const std::optional<std::filesystem::path> storeDirectory = QueueStoreDirectory(rRepository);
			if (!storeDirectory)
			{
				Fail("could not resolve the machine-local plan queue store path");
				return kiExitFailure;
			}
			std::vector<FileChange> changes;
			changes.push_back({ ExtendedLengthPath(*storeDirectory / kPlansStoreFile), state.plans.bytes, plansBytes });
			changes.push_back({ ExtendedLengthPath(*storeDirectory / kFeaturesStoreFile), state.features.bytes, featuresBytes });
			changes.insert(changes.end(), std::make_move_iterator(planChanges.begin()), std::make_move_iterator(planChanges.end()));
			std::vector<std::unique_ptr<coordination::Guard>> commitGuards;
			if (!locks.AcquireCommitGuards(commitGuards))
			{
				return kiExitStateConflict;
			}
			for (const std::string& rPlan : updated)
			{
				QueueFile* pQueue = QueueForPlan(state, rPlan);
				bool bClaimExists = false;
				nlohmann::json claim;
				if (pQueue == nullptr || !ReadClaim(rRepository, *pQueue, rPlan, bClaimExists, claim))
				{
					return kiExitFailure;
				}
				if (bClaimExists && !IsClaimOwnedByRequester(claim, rArguments, rWorktree))
				{
					coordination::PrintMetadata({ { "conflict", "claimed-before-commit" }, { "plan", rPlan }, { "claim", claim } });
					return kiExitStateConflict;
				}
			}
			if (!ApplyFileChanges(changes))
			{
				return kiExitFailure;
			}
			commitGuards.clear();
			nlohmann::json unlockResults;
			const bool bUnlocked = locks.Release(unlockResults);
			coordination::PrintMetadata({ { "schemaVersion", 1 }, { "operation", "update" }, { "handled", true }, { "plans", std::move(updatedPlans) }, { "plansOrderSha256", Sha256(plansBytes).value_or("") }, { "featuresOrderSha256", Sha256(featuresBytes).value_or("") }, { "unlockResults", std::move(unlockResults) } });
			return bUnlocked ? kiExitOk : kiExitStateConflict;
		}

		WorktreeInfo* FindWorktree(std::vector<WorktreeInfo>& rListing, const std::wstring& rPath)
		{
			for (WorktreeInfo& rInfo : rListing)
			{
				if (rInfo.path == rPath)
				{
					return &rInfo;
				}
			}
			return nullptr;
		}

		int RunClaimNext(const Arguments& rArguments, const std::wstring& rRepository, const std::wstring& rWorktree, const std::wstring& rPrimary, std::vector<WorktreeInfo>& rListing)
		{
			if (rArguments.owner.empty() || rArguments.session.empty() || rArguments.branch.empty() || (rArguments.queue != L"plans" && rArguments.queue != L"features"))
			{
				Fail("claim-next requires --owner, --session, --branch, and --queue plans|features");
				return kiExitFailure;
			}
			const std::string branch = WideToUtf8(rArguments.branch).starts_with("refs/heads/") ? WideToUtf8(rArguments.branch) : "refs/heads/" + WideToUtf8(rArguments.branch);
			WorktreeInfo* pPrimaryInfo = FindWorktree(rListing, rPrimary);
			WorktreeInfo* pSessionInfo = FindWorktree(rListing, rWorktree);
			std::optional<std::string> targetHead = RunGit({ L"-C", rPrimary, L"rev-parse", L"--verify", Utf8ToWide(branch + "^{commit}") });
			if (!pPrimaryInfo || !pSessionInfo || pPrimaryInfo->branch != branch || !targetHead)
			{
				Fail("primary checkout is not attached to the supplied target branch");
				return kiExitStateConflict;
			}
			TrimLineEnd(*targetHead);
			if (pPrimaryInfo->head != *targetHead || pSessionInfo->head != *targetHead)
			{
				coordination::PrintMetadata({ { "claimed", false }, { "reason", "stale-session" }, { "primaryCommit", *targetHead }, { "sessionCommit", pSessionInfo->head } });
				return kiExitStateConflict;
			}
			QueueLocks locks;
			if (!locks.Acquire(rArguments, rRepository, rWorktree))
			{
				return kiExitStateConflict;
			}
			const std::optional<std::string> lockedPrimaryHead = RunGit({ L"-C", rPrimary, L"rev-parse", L"HEAD" });
			const std::optional<std::string> lockedSessionHead = RunGit({ L"-C", rWorktree, L"rev-parse", L"HEAD" });
			if (!lockedPrimaryHead || !lockedSessionHead || Trim(*lockedPrimaryHead) != *targetHead || Trim(*lockedSessionHead) != *targetHead)
			{
				coordination::PrintMetadata({ { "claimed", false }, { "reason", "stale-session-after-lock" }, { "primaryCommit", *targetHead } });
				return kiExitStateConflict;
			}
			QueueState state;
			if (!LoadQueueState(rRepository, rPrimary, rArguments, std::nullopt, std::nullopt, {}, state))
			{
				return kiExitFailure;
			}
			std::set<std::string> claimedPlans;
			if (!CollectClaimedPlans(rRepository, state, claimedPlans))
			{
				return kiExitFailure;
			}
			if (HasBlockingDiagnostics(state, claimedPlans, nullptr))
			{
				PrintValidation(state);
				return kiExitStateConflict;
			}
			QueueFile* pQueue = rArguments.queue == L"plans" ? &state.plans : &state.features;
			Row* pSelected = nullptr;
			nlohmann::json blockers = nlohmann::json::array();
			const std::string explicitPlan = WideToUtf8(rArguments.plan);
			for (Row& rRow : pQueue->rows)
			{
				if (!explicitPlan.empty() && rRow.plan != explicitPlan)
				{
					continue;
				}
				bool bClaimExists = false;
				nlohmann::json claim;
				if (!ReadClaim(rRepository, *pQueue, rRow.plan, bClaimExists, claim))
				{
					Fail("could not read a plan row claim while selecting an eligible row");
					return kiExitFailure;
				}
				if (bClaimExists || !rRow.dependencies.empty())
				{
					blockers.push_back({ { "plan", rRow.plan }, { "claimed", bClaimExists }, { "dependencies", rRow.dependencies } });
					if (!explicitPlan.empty())
					{
						break;
					}
					continue;
				}
				pSelected = &rRow;
				break;
			}
			if (pSelected == nullptr)
			{
				coordination::PrintMetadata({ { "claimed", false }, { "reason", explicitPlan.empty() ? "no-eligible-row" : "explicit-plan-blocked-or-missing" }, { "blockers", std::move(blockers) } });
				return kiExitStateConflict;
			}
			const std::optional<std::filesystem::path> primaryPlanPath = ResolveContainedPath(rPrimary, Utf8ToWide(pSelected->plan), true);
			const std::optional<std::filesystem::path> sessionPlanPath = ResolveContainedPath(rWorktree, Utf8ToWide(pSelected->plan), true);
			std::string primaryPlan;
			std::string sessionPlan;
			if (!primaryPlanPath || !sessionPlanPath || !ReadBoundedFile(*primaryPlanPath, kuiMaximumPlanBytes, primaryPlan) || !ReadBoundedFile(*sessionPlanPath, kuiMaximumPlanBytes, sessionPlan) || primaryPlan != sessionPlan)
			{
				coordination::PrintMetadata({ { "claimed", false }, { "reason", "plan-byte-mismatch" }, { "plan", pSelected->plan } });
				return kiExitStateConflict;
			}
			const std::optional<std::filesystem::path> claimPath = ClaimPath(rRepository, *pQueue, pSelected->plan);
			if (!claimPath || !coordination::EnsureParentDirectory(*claimPath))
			{
				Fail("could not resolve or create the plan row claim path");
				return kiExitFailure;
			}
			const std::wstring order = coordination::NormalizeRepositoryRelativeKey(Utf8ToWide(pQueue->path)).value();
			const std::optional<std::wstring> plan = ClaimPlanKey(*pQueue, pSelected->plan);
			if (!plan)
			{
				Fail("could not derive the plan row claim key");
				return kiExitFailure;
			}
			const coordination::Locator locator { L"plan-row", rRepository + L"\n" + order + L"\n" + *plan, *claimPath };
			nlohmann::json metadata = coordination::NewMetadata(locator, rArguments.owner, rArguments.session, rWorktree);
			metadata["repository"] = WideToUtf8(rRepository);
			metadata["order"] = WideToUtf8(order);
			metadata["plan"] = WideToUtf8(*plan);
			metadata["primaryCommit"] = *targetHead;
			metadata["orderSha256"] = Sha256(pQueue->bytes).value_or("");
			metadata["planSha256"] = Sha256(primaryPlan).value_or("");
			std::vector<std::unique_ptr<coordination::Guard>> commitGuards;
			if (!locks.AcquireCommitGuards(commitGuards))
			{
				return kiExitStateConflict;
			}
			bool bClaimExists = false;
			nlohmann::json currentClaim;
			if (!ReadClaim(rRepository, *pQueue, pSelected->plan, bClaimExists, currentClaim))
			{
				Fail("could not re-read the plan row claim before commit");
				return kiExitFailure;
			}
			if (bClaimExists)
			{
				coordination::PrintMetadata({ { "claimed", false }, { "reason", "claimed-before-commit" }, { "plan", pSelected->plan }, { "claim", currentClaim } });
				return kiExitStateConflict;
			}
			if (!coordination::WriteMetadataAtomic(*claimPath, metadata))
			{
				FailWindows("write plan row claim");
				return kiExitFailure;
			}
			commitGuards.clear();
			nlohmann::json unlockResults;
			const bool bUnlocked = locks.Release(unlockResults);
			nlohmann::json result = metadata;
			result["plan"] = pSelected->plan;
			result["claimed"] = true;
			result["row"] = { { "tier", pSelected->tier }, { "effort", pSelected->iEffort }, { "impact", pSelected->iImpact }, { "risks", pSelected->iRisks }, { "score", pSelected->iScore }, { "dependsOn", pSelected->dependencies }, { "notes", pSelected->notes } };
			result["dependencyDisposition"] = "eligible";
			result["unlockResults"] = std::move(unlockResults);
			coordination::PrintMetadata(result);
			return bUnlocked ? kiExitOk : kiExitStateConflict;
		}

		int RunComplete(const Arguments& rArguments, const std::wstring& rRepository, const std::wstring& rWorktree, const std::optional<std::wstring>& rPrimary)
		{
			const std::optional<std::string> normalizedPlan = NormalizeIdentity(WideToUtf8(rArguments.plan));
			if (rArguments.owner.empty() || !normalizedPlan || !IsCanonicalDependency(*normalizedPlan))
			{
				Fail("complete requires --owner and a canonical --plan");
				return kiExitFailure;
			}
			QueueLocks locks;
			if (!locks.Acquire(rArguments, rRepository, rWorktree))
			{
				return kiExitStateConflict;
			}
			QueueState state;
			if (!LoadQueueState(rRepository, rWorktree, rArguments, std::nullopt, std::nullopt, {}, state))
			{
				return kiExitFailure;
			}
			QueueFile* pQueue = QueueForPlan(state, *normalizedPlan);
			if (pQueue == nullptr)
			{
				return kiExitFailure;
			}
			bool bClaimExists = false;
			nlohmann::json claim;
			if (!ReadClaim(rRepository, *pQueue, *normalizedPlan, bClaimExists, claim))
			{
				return kiExitFailure;
			}
			if (!bClaimExists || !coordination::HasOwner(claim, rArguments.owner))
			{
				coordination::PrintMetadata({ { "conflict", "owner-mismatch" }, { "plan", *normalizedPlan }, { "claim", bClaimExists ? claim : nlohmann::json { { "held", false } } } });
				return kiExitStateConflict;
			}
			// Tolerate an already-absent target row or plan file (crash-recovery / post-landing completion); block only on
			// unrelated structural or file diagnostics. orphan-plan and claimed-row missing files never block.
			std::set<std::string> claimedPlans;
			if (!CollectClaimedPlans(rRepository, state, claimedPlans))
			{
				return kiExitFailure;
			}
			const std::set<std::string> staleBaselinePlans = CollectStaleBaselinePlans(state, rWorktree, rPrimary);
			if (HasBlockingDiagnostics(state, claimedPlans, &*normalizedPlan, {}, staleBaselinePlans))
			{
				PrintValidation(state);
				return kiExitStateConflict;
			}
			for (QueueFile* pCandidate : { &state.plans, &state.features })
			{
				std::erase_if(pCandidate->rows, [&](const Row& rRow) { return rRow.plan == *normalizedPlan; });
				for (Row& rRow : pCandidate->rows)
				{
					const size_t uiOriginalDependencies = rRow.dependencies.size();
					std::erase(rRow.dependencies, *normalizedPlan);
					if (rRow.dependencies.size() != uiOriginalDependencies)
					{
						rRow.rowBytes.clear();
					}
				}
			}
			const std::string plansBytes = RenderQueue(state.plans);
			const std::string featuresBytes = RenderQueue(state.features);
			QueueState prospective;
			if (!LoadQueueState(rRepository, rWorktree, rArguments, plansBytes, featuresBytes, { *normalizedPlan }, prospective))
			{
				return kiExitFailure;
			}
			const std::set<std::string> prospectiveStalePlans = CollectStaleBaselinePlans(prospective, rWorktree, rPrimary);
			if (HasBlockingDiagnostics(prospective, claimedPlans, &*normalizedPlan, {}, prospectiveStalePlans))
			{
				PrintValidation(prospective);
				return kiExitStateConflict;
			}
			const std::optional<std::filesystem::path> storeDirectory = QueueStoreDirectory(rRepository);
			if (!storeDirectory)
			{
				Fail("could not resolve the machine-local plan queue store path");
				return kiExitFailure;
			}
			std::vector<FileChange> changes
			{
				{ ExtendedLengthPath(*storeDirectory / kPlansStoreFile), state.plans.bytes, plansBytes },
				{ ExtendedLengthPath(*storeDirectory / kFeaturesStoreFile), state.features.bytes, featuresBytes },
			};
			std::vector<std::unique_ptr<coordination::Guard>> commitGuards;
			if (!locks.AcquireCommitGuards(commitGuards))
			{
				return kiExitStateConflict;
			}
			bool bCurrentClaimExists = false;
			nlohmann::json currentClaim;
			if (!ReadClaim(rRepository, *pQueue, *normalizedPlan, bCurrentClaimExists, currentClaim))
			{
				return kiExitFailure;
			}
			if (!bCurrentClaimExists || !coordination::HasOwner(currentClaim, rArguments.owner))
			{
				coordination::PrintMetadata({ { "conflict", "owner-mismatch-before-commit" }, { "plan", *normalizedPlan }, { "claim", bCurrentClaimExists ? currentClaim : nlohmann::json { { "held", false } } } });
				return kiExitStateConflict;
			}
			if (!ApplyFileChanges(changes))
			{
				return kiExitFailure;
			}
			commitGuards.clear();
			nlohmann::json unlockResults;
			const bool bUnlocked = locks.Release(unlockResults);
			coordination::PrintMetadata({
				{ "schemaVersion", 1 }, { "operation", "complete" }, { "handled", true }, { "plan", *normalizedPlan },
				{ "claimOwner", claim["owner"] }, { "claimPrimaryCommit", claim.value("primaryCommit", "") },
				{ "plansOrderSha256", Sha256(plansBytes).value_or("") }, { "featuresOrderSha256", Sha256(featuresBytes).value_or("") }, { "unlockResults", std::move(unlockResults) }
			});
			return bUnlocked ? kiExitOk : kiExitStateConflict;
		}
	}

	int RunPlanOrderCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		if (iArgumentCount < 4)
		{
			Fail("plan order requires init, validate, add, update, claim-next, or complete");
			return kiExitFailure;
		}
		const std::wstring verb = ToLowerInvariant(pArgumentValues[3]);
		if (verb != L"init" && verb != L"validate" && verb != L"add" && verb != L"update" && verb != L"claim-next" && verb != L"complete")
		{
			Fail("unknown plan order verb");
			return kiExitFailure;
		}
		Arguments arguments;
		if (!ParseArguments(iArgumentCount, pArgumentValues, arguments))
		{
			return kiExitFailure;
		}
		std::wstring repository;
		std::wstring worktree;
		std::optional<std::wstring> primary;
		std::vector<WorktreeInfo> listing;
		if (!ResolveWorktrees(arguments, repository, worktree, primary, listing))
		{
			return kiExitFailure;
		}
		// Registered primary checkout (front porcelain entry, canonicalized by ResolveWorktrees); absent when it is bare or
		// prunable, in which case stale-baseline demotion is disabled and missing plan files stay blocking (fail-safe).
		std::optional<std::wstring> primaryPath;
		if (!listing.empty() && !listing.front().bBare && !listing.front().bPrunable)
		{
			primaryPath = listing.front().path;
		}
		if (verb == L"init")
		{
			return RunInit(arguments, repository, worktree);
		}
		if (verb == L"validate")
		{
			return RunValidate(arguments, repository, worktree, !listing.empty() && listing.front().path == worktree, primaryPath);
		}
		if (verb == L"add")
		{
			return RunAdd(arguments, repository, worktree, primaryPath);
		}
		if (verb == L"update")
		{
			return RunUpdate(arguments, repository, worktree, primaryPath);
		}
		if (verb == L"complete")
		{
			return RunComplete(arguments, repository, worktree, primaryPath);
		}
		if (!primary)
		{
			Fail("claim-next requires --primary-worktree");
			return kiExitFailure;
		}
		return RunClaimNext(arguments, repository, worktree, *primary, listing);
	}
}
