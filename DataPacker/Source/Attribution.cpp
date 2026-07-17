#include "Attribution.h"

#include "DiagnosticReporter.h"
#include "FileManager.h"

namespace attribution
{

namespace
{
bool PathLess(const std::filesystem::path& rLeft, const std::filesystem::path& rRight)
{
	int iResult = CompareStringOrdinal(rLeft.native().c_str(), -1, rRight.native().c_str(), -1, TRUE);
	return iResult == CSTR_LESS_THAN || (iResult == CSTR_EQUAL && CompareStringOrdinal(rLeft.native().c_str(), -1, rRight.native().c_str(), -1, FALSE) == CSTR_LESS_THAN);
}

bool IsReparsePoint(const std::filesystem::path& rPath)
{
	DWORD uiAttributes = GetFileAttributesW(rPath.native().c_str());
	return uiAttributes != INVALID_FILE_ATTRIBUTES && (uiAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
}

void CopyThirdPartyLicenses()
{
	std::filesystem::path thirdPartyDirectory = gpFileManager->mThirdPartyDirectory;
	std::filesystem::path attributionDirectory = gpFileManager->GetAttributionDirectory();
	struct PendingCopy
	{
		std::filesystem::path mSource;
		std::filesystem::path mDestination;
		std::string mLibraryName;
	};
	std::vector<PendingCopy> pendingCopies;

	auto AddPendingCopy = [&pendingCopies](const std::filesystem::path& rSourceFile, const std::filesystem::path& rDestination, const std::string& rLibraryName)
	{
		if (!std::filesystem::exists(rDestination) || std::filesystem::last_write_time(rSourceFile) > std::filesystem::last_write_time(rDestination))
		{
			pendingCopies.push_back({.mSource = rSourceFile, .mDestination = rDestination, .mLibraryName = rLibraryName});
		}
	};

	std::vector<std::filesystem::directory_entry> libraries;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(thirdPartyDirectory))
	{
		if (IsReparsePoint(rEntry.path()) || rEntry.symlink_status().type() == std::filesystem::file_type::symlink)
		{
			throw std::runtime_error(std::format("Unsupported ThirdParty reparse point: {}", rEntry.path().string()));
		}
		libraries.push_back(rEntry);
	}
	std::sort(libraries.begin(), libraries.end(), [](const std::filesystem::directory_entry& rLeft, const std::filesystem::directory_entry& rRight)
	{
		return PathLess(rLeft.path(), rRight.path());
	});

	for (const std::filesystem::directory_entry& rDirectoryEntry : libraries)
	{
		std::filesystem::file_status directoryStatus = rDirectoryEntry.symlink_status();
		if (!std::filesystem::is_directory(directoryStatus))
		{
			if (!std::filesystem::is_regular_file(directoryStatus))
			{
				throw std::runtime_error(std::format("Unsupported ThirdParty entry: {}", rDirectoryEntry.path().string()));
			}
			continue;
		}

		std::string libraryName = rDirectoryEntry.path().filename().string();
		if (libraryName == "Prebuilts")
		{
			continue;
		}
		std::filesystem::path libraryAttributionDirectory = attributionDirectory / libraryName;
		std::vector<std::filesystem::directory_entry> files;
		for (const std::filesystem::directory_entry& rFile : std::filesystem::directory_iterator(rDirectoryEntry.path()))
		{
			std::filesystem::file_status fileStatus = rFile.symlink_status();
			if (IsReparsePoint(rFile.path()) || fileStatus.type() == std::filesystem::file_type::symlink)
			{
				throw std::runtime_error(std::format("Unsupported attribution reparse point: {}", rFile.path().string()));
			}
			if (std::filesystem::is_directory(fileStatus))
			{
				continue;
			}
			if (!std::filesystem::is_regular_file(fileStatus))
			{
				throw std::runtime_error(std::format("Unsupported attribution entry: {}", rFile.path().string()));
			}
			files.push_back(rFile);
		}
		std::sort(files.begin(), files.end(), [](const std::filesystem::directory_entry& rLeft, const std::filesystem::directory_entry& rRight)
		{
			return PathLess(rLeft.path().filename(), rRight.path().filename());
		});

		// Priority 1: Look for primary license files (LICENSE, LICENSE.md, LICENSE.txt)
		bool bFoundLicense = false;
		std::filesystem::path primaryLicenseFile;
		for (std::string_view priority : { "license", "license.md", "license.txt" })
		{
			for (const std::filesystem::directory_entry& rFileEntry : files)
			{
				if (common::ToLower(rFileEntry.path().filename().string()) == priority)
				{
					primaryLicenseFile = rFileEntry.path();
					bFoundLicense = true;
					break;
				}
			}
			if (bFoundLicense)
			{
				break;
			}
		}

		// If primary license found, copy it and skip fallback search
		if (bFoundLicense)
		{
			AddPendingCopy(primaryLicenseFile, libraryAttributionDirectory / primaryLicenseFile.filename(), libraryName);
			continue;
		}

		// Fallback: Search for alternative license/attribution files (copying, readme, manual.md)
		for (const std::filesystem::directory_entry& rFileEntry : files)
		{
			std::string filenameLower = common::ToLower(rFileEntry.path().filename().string());
			if (filenameLower.find("copying") != std::string::npos || filenameLower == "manual.md" || filenameLower.find("readme") != std::string::npos)
			{
				bFoundLicense = true;
				AddPendingCopy(rFileEntry.path(), libraryAttributionDirectory / rFileEntry.path().filename(), libraryName);
			}
		}

		ASSERT(bFoundLicense);
	}

	if (pendingCopies.empty())
	{
		return;
	}
	if (gpFileManager->EnsureLocal(FileManager::OutputRoot::kAttribution) == FileManager::EnsureLocalResult::kCancelled)
	{
		throw diagnostic::AlreadyReportedError("Attribution materialization cancelled");
	}
	LOG(kDefault, kDebug, "\nCopying ThirdParty attribution files");
	ScopedLogIndent scopedLogIndent;
	for (const PendingCopy& rPending : pendingCopies)
	{
		std::filesystem::create_directories(rPending.mDestination.parent_path());
		std::filesystem::copy_file(rPending.mSource, rPending.mDestination, std::filesystem::copy_options::overwrite_existing);
		LOG(kDefault, kDebug, "Copied: {}/{}", rPending.mLibraryName, rPending.mSource.filename().string());
	}
}

}
