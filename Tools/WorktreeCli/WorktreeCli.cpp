// WorktreeCli — repository build, lock, and plan coordination.

#include "ToolCliCommon.h"
#include "BuildCommand.h"
#include "LandingLockCommands.h"
#include "PlanCommands.h"

#include <iostream>
#include <string_view>

namespace toolcli
{
	namespace
	{
		void PrintUsage(std::ostream& rOutput)
		{
			rOutput << "Usage: WorktreeCli.exe lock <token|claim|status|refresh|recover|release|steal> ...\n";
			rOutput << "       WorktreeCli.exe plan validate --repo COMMON-DIR --worktree CHECKOUT --baseline COMMIT\n";
			rOutput << "       WorktreeCli.exe plan claim-next --repo COMMON-DIR --primary-worktree PRIMARY --worktree SESSION --branch TARGET --owner TOKEN --session TOKEN --write-claim-receipt Temp/RECEIPT [--plan Documents/Plans/...md]\n";
			rOutput << "       WorktreeCli.exe plan claim-status|unclaim --worktree SESSION --claim-receipt Temp/RECEIPT --claim-receipt-sha256 SHA256\n";
			rOutput << "       WorktreeCli.exe plan prepare-completion|prepare-rejection --worktree SESSION --claim-receipt Temp/RECEIPT --claim-receipt-sha256 SHA256\n";
			rOutput << "       WorktreeCli.exe plan release-after-landing --worktree SESSION --claim-receipt Temp/RECEIPT --claim-receipt-sha256 SHA256 --landed-commit COMMIT\n";
			rOutput << "       WorktreeCli.exe plan reparent-claims --repo COMMON-DIR --worktree SESSION --new-baseline COMMIT\n";
			rOutput << "       WorktreeCli.exe build [--files <cpp...> --] <project-or-solution> <MSBuild args...>\n";
			rOutput << "       WorktreeCli.exe --help\n";
		}
	}
}

int wmain(int iArgumentCount, wchar_t* pArgumentValues[])
{
	toolcli::SetToolName("WorktreeCli");
	if (iArgumentCount < 2)
	{
		toolcli::Fail("a command is required");
		return toolcli::kiExitFailure;
	}

	std::wstring_view mode = pArgumentValues[1];
	if (mode == L"--help")
	{
		if (iArgumentCount != 2)
		{
			toolcli::Fail("--help accepts no arguments");
			return toolcli::kiExitFailure;
		}
		toolcli::PrintUsage(std::cout);
		return toolcli::kiExitOk;
	}
	if (mode == L"lock")
	{
		if (iArgumentCount == 3 && toolcli::ToLowerInvariant(pArgumentValues[2]) == L"token")
		{
			return toolcli::PrintOwnerToken();
		}
		return toolcli::RunLandingLockCommand(iArgumentCount, pArgumentValues);
	}
	if (mode == L"plan")
	{
		return toolcli::RunPlanCommand(iArgumentCount, pArgumentValues);
	}
	if (mode == L"build")
	{
		return toolcli::RunBuildCommand(iArgumentCount, pArgumentValues);
	}
	toolcli::Fail("unknown command");
	return toolcli::kiExitFailure;
}
