#include "Attribution.h"

#include "DiagnosticReporter.h"
#include "FileManager.h"

namespace attribution
{

namespace
{
using ScopedHandle = std::unique_ptr<void, decltype(&CloseHandle)>;
struct SourceIdentity
{
	uint64_t uiVolumeSerial = 0;
	FILE_ID_128 mFileId {};
	uint64_t uiSize = 0;
	FILETIME mLastWrite {};
};

SourceIdentity GetIdentity(const std::filesystem::path& rPath)
{
	ScopedHandle file(CreateFileW(rPath.native().c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr), CloseHandle);
	FILE_ID_INFO idInfo {};
	FILE_BASIC_INFO basicInfo {};
	FILE_STANDARD_INFO standardInfo {};
	if (file.get() == nullptr || file.get() == INVALID_HANDLE_VALUE || !GetFileInformationByHandleEx(file.get(), FileIdInfo, &idInfo, sizeof(idInfo)) || !GetFileInformationByHandleEx(file.get(), FileBasicInfo, &basicInfo, sizeof(basicInfo)) || !GetFileInformationByHandleEx(file.get(), FileStandardInfo, &standardInfo, sizeof(standardInfo)))
	{
		throw std::runtime_error(std::format("Unable to snapshot attribution source: {}", rPath.string()));
	}
	return {.uiVolumeSerial = idInfo.VolumeSerialNumber, .mFileId = idInfo.FileId, .uiSize = static_cast<uint64_t>(standardInfo.EndOfFile.QuadPart), .mLastWrite = {basicInfo.LastWriteTime.LowPart, static_cast<DWORD>(basicInfo.LastWriteTime.HighPart)}};
}

bool IdentityEqual(const SourceIdentity& rLeft, const SourceIdentity& rRight)
{
	return rLeft.uiVolumeSerial == rRight.uiVolumeSerial && std::memcmp(&rLeft.mFileId, &rRight.mFileId, sizeof(FILE_ID_128)) == 0 && rLeft.uiSize == rRight.uiSize && CompareFileTime(&rLeft.mLastWrite, &rRight.mLastWrite) == 0;
}

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

void CopyThirdPartyLicenses(bool bRecomputed)
{
	std::filesystem::path thirdPartyDirectory = gpFileManager->mThirdPartyDirectory;
	std::filesystem::path attributionDirectory = gpFileManager->GetAttributionDirectory();
	std::vector<std::pair<std::filesystem::path, SourceIdentity>> inventory;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(thirdPartyDirectory))
	{
		std::filesystem::file_status status = rEntry.symlink_status();
		if (IsReparsePoint(rEntry.path()) || status.type() == std::filesystem::file_type::symlink)
		{
			throw std::runtime_error(std::format("Unsupported ThirdParty reparse point: {}", rEntry.path().string()));
		}
		if (std::filesystem::is_regular_file(status))
		{
			inventory.emplace_back(rEntry.path(), GetIdentity(rEntry.path()));
		}
		else if (!std::filesystem::is_directory(status))
		{
			throw std::runtime_error(std::format("Unsupported ThirdParty entry: {}", rEntry.path().string()));
		}
	}
	std::sort(inventory.begin(), inventory.end(), [](const std::pair<std::filesystem::path, SourceIdentity>& rLeft, const std::pair<std::filesystem::path, SourceIdentity>& rRight)
	{
		return PathLess(rLeft.first, rRight.first);
	});
	struct PendingCopy
	{
		std::filesystem::path mSource;
		std::filesystem::path mDestination;
		std::string mLibraryName;
		SourceIdentity mIdentity;
	};
	std::vector<PendingCopy> pendingCopies;

	auto AddPendingCopy = [&pendingCopies](const std::filesystem::path& rSourceFile, const std::filesystem::path& rDestination, const std::string& rLibraryName)
	{
		if (!std::filesystem::exists(rDestination) || std::filesystem::last_write_time(rSourceFile) > std::filesystem::last_write_time(rDestination))
		{
			pendingCopies.push_back({.mSource = rSourceFile, .mDestination = rDestination, .mLibraryName = rLibraryName, .mIdentity = GetIdentity(rSourceFile)});
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
	std::vector<std::pair<std::filesystem::path, SourceIdentity>> currentInventory;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(thirdPartyDirectory))
	{
		std::filesystem::file_status status = rEntry.symlink_status();
		if (IsReparsePoint(rEntry.path()) || status.type() == std::filesystem::file_type::symlink)
		{
			throw std::runtime_error(std::format("Unsupported ThirdParty reparse point: {}", rEntry.path().string()));
		}
		if (std::filesystem::is_regular_file(status))
		{
			currentInventory.emplace_back(rEntry.path(), GetIdentity(rEntry.path()));
		}
		else if (!std::filesystem::is_directory(status))
		{
			throw std::runtime_error(std::format("Unsupported ThirdParty entry: {}", rEntry.path().string()));
		}
	}
	std::sort(currentInventory.begin(), currentInventory.end(), [](const std::pair<std::filesystem::path, SourceIdentity>& rLeft, const std::pair<std::filesystem::path, SourceIdentity>& rRight)
	{
		return PathLess(rLeft.first, rRight.first);
	});
	bool bInventoryChanged = inventory.size() != currentInventory.size();
	for (size_t uiIndex = 0; !bInventoryChanged && uiIndex < inventory.size(); ++uiIndex)
	{
		bInventoryChanged = inventory.at(uiIndex).first != currentInventory.at(uiIndex).first || !IdentityEqual(inventory.at(uiIndex).second, currentInventory.at(uiIndex).second);
	}
	if (bInventoryChanged)
	{
		if (bRecomputed)
		{
			throw std::runtime_error("Attribution sources changed twice during discovery");
		}
		CopyThirdPartyLicenses(true);
		return;
	}
	LOG(kDefault, kDebug, "\nCopying ThirdParty attribution files");
	ScopedLogIndent scopedLogIndent;
	for (const PendingCopy& rPending : pendingCopies)
	{
		if (!std::filesystem::is_regular_file(rPending.mSource) || !IdentityEqual(GetIdentity(rPending.mSource), rPending.mIdentity))
		{
			throw std::runtime_error(std::format("Attribution source changed during discovery: {}", rPending.mSource.string()));
		}
		std::filesystem::create_directories(rPending.mDestination.parent_path());
		std::filesystem::copy_file(rPending.mSource, rPending.mDestination, std::filesystem::copy_options::overwrite_existing);
		LOG(kDefault, kDebug, "Copied: {}/{}", rPending.mLibraryName, rPending.mSource.filename().string());
	}
}

void CopyThirdPartyLicenses()
{
	CopyThirdPartyLicenses(false);
}

}
