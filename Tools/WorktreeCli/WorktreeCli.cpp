// WorktreeCli — repository build, lock, and plan coordination.

#include "ToolCliCommon.h"
#include "BuildCommand.h"
#include "LandingLockCommands.h"
#include "PlanScheduler.h"

#include <iostream>
#include <string_view>

namespace toolcli
{
	namespace
	{
		void PrintUsage(std::ostream& rOutput)
		{
			rOutput << "Usage: WorktreeCli.exe lock <token|claim|status|refresh|recover|release|steal> ...\n";
			rOutput << "       WorktreeCli.exe plan validate --repo COMMON-DIR --worktree CHECKOUT [--plan Documents/Plans/...md]\n";
			rOutput << "       WorktreeCli.exe plan list --repo COMMON-DIR --worktree CHECKOUT\n";
			rOutput << "       WorktreeCli.exe plan claim-next --repo COMMON-DIR --primary-worktree PRIMARY --worktree SESSION --branch TARGET --owner TOKEN --session TOKEN [--plan Documents/Plans/...md]\n";
			rOutput << "       WorktreeCli.exe plan claim-status|unclaim --repo COMMON-DIR --worktree SESSION --owner TOKEN --session TOKEN\n";
			rOutput << "       WorktreeCli.exe plan complete --repo COMMON-DIR --worktree SESSION --owner TOKEN --session TOKEN\n";
			rOutput << "       WorktreeCli.exe plan reject --repo COMMON-DIR --worktree SESSION --owner TOKEN --session TOKEN --user-authorized-rejection\n";
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
		return toolcli::RunPlanSchedulerCommand(iArgumentCount, pArgumentValues);
	}
	if (mode == L"build")
	{
		return toolcli::RunBuildCommand(iArgumentCount, pArgumentValues);
	}
	toolcli::Fail("unknown command");
	return toolcli::kiExitFailure;
}
