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
			rOutput << "       WorktreeCli.exe plan <queue|row> <verb> ...\n";
			rOutput << "       WorktreeCli.exe plan queue lock --repo COMMON-DIR --order PATH --owner TOKEN --session TOKEN\n";
			rOutput << "       WorktreeCli.exe plan queue status --repo COMMON-DIR --order PATH [--owner TOKEN]\n";
			rOutput << "       WorktreeCli.exe plan queue unlock --repo COMMON-DIR --order PATH --owner TOKEN\n";
			rOutput << "       WorktreeCli.exe plan row status --repo COMMON-DIR --order PATH --plan PATH [--owner TOKEN]\n";
			rOutput << "       WorktreeCli.exe plan row unclaim --repo COMMON-DIR --order PATH --plan PATH --owner TOKEN\n";
			rOutput << "       WorktreeCli.exe plan order init --repo COMMON-DIR --worktree CHECKOUT [--force] [--plans-order PATH] [--features-order PATH]\n";
			rOutput << "       WorktreeCli.exe plan order validate --repo COMMON-DIR --worktree CHECKOUT [--plans-order PATH] [--features-order PATH]\n";
			rOutput << "       WorktreeCli.exe plan order add --repo COMMON-DIR --worktree CHECKOUT --owner TOKEN --session TOKEN --request TEMP-REPO-REL [--request-sha256 SHA256] [--plans-order PATH] [--features-order PATH]\n";
			rOutput << "       WorktreeCli.exe plan order update --repo COMMON-DIR --worktree CHECKOUT --owner TOKEN --session TOKEN --request TEMP-REPO-REL [--plans-order PATH] [--features-order PATH]\n";
			rOutput << "       WorktreeCli.exe plan order claim-next --repo COMMON-DIR --primary-worktree CHECKOUT --worktree CHECKOUT --branch TARGET --owner TOKEN --session TOKEN --queue <plans|features> [--plan PATH] [--plans-order PATH] [--features-order PATH]\n";
			rOutput << "       WorktreeCli.exe plan order complete --repo COMMON-DIR --worktree CHECKOUT --owner TOKEN --session TOKEN --plan PATH [--plans-order PATH] [--features-order PATH]\n";
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
