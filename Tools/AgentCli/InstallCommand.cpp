#include "InstallCommand.h"

#include "AgentCliCommon.h"

#include <filesystem>
#include <iostream>

namespace agentcli
{
	int RunInstallCommand(int iArgumentCount)
	{
		if (iArgumentCount != 2)
		{
			Fail("install accepts no arguments");
			return kiExitFailure;
		}

		std::filesystem::path sourcePath = GetCurrentExecutablePath();
		std::filesystem::path localApplicationData = GetLocalApplicationDataPath();
		if (sourcePath.empty() || localApplicationData.empty())
		{
			Fail("could not resolve install paths");
			return kiExitFailure;
		}

		std::filesystem::path destinationPath = localApplicationData / L"BrokenEngine" / L"AgentCli" / L"v2" / L"AgentCli.exe";
		std::error_code error;
		if (std::filesystem::equivalent(sourcePath, destinationPath, error) && !error)
		{
			std::wcout << destinationPath.native() << L'\n';
			return kiExitOk;
		}

		error.clear();
		std::filesystem::create_directories(destinationPath.parent_path(), error);
		if (error)
		{
			Fail("could not create install directory");
			return kiExitFailure;
		}

		std::filesystem::path temporaryPath = destinationPath;
		temporaryPath += L".tmp." + std::to_wstring(::GetCurrentProcessId());
		::DeleteFileW(temporaryPath.c_str());
		if (::CopyFileW(sourcePath.c_str(), temporaryPath.c_str(), TRUE) == FALSE)
		{
			FailWindows("copy AgentCli install image");
			return kiExitFailure;
		}
		if (::MoveFileExW(temporaryPath.c_str(), destinationPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
		{
			FailWindows("activate AgentCli install image");
			::DeleteFileW(temporaryPath.c_str());
			return kiExitFailure;
		}

		std::wcout << destinationPath.native() << L'\n';
		return kiExitOk;
	}
}
