#include "Attribution.h"

namespace attribution
{

void CopyThirdPartyLicenses(const std::filesystem::path& rOutputDirectory)
{
	std::filesystem::path thirdPartyDirectory = rOutputDirectory / "../../../../../../ThirdParty";
	std::filesystem::path attributionDirectory = rOutputDirectory / "../Attribution";
	std::filesystem::create_directories(attributionDirectory);
	bool bAnyCopied = false;

	auto CopyLicenseFile = [&bAnyCopied](const std::filesystem::path& rSourceFile, const std::filesystem::path& rLibraryAttributionDirectory, const std::string& rLibraryName)
	{
		std::filesystem::path destinationFile = rLibraryAttributionDirectory / rSourceFile.filename();
		bool bNeedsCopy = !std::filesystem::exists(destinationFile) || std::filesystem::last_write_time(rSourceFile) > std::filesystem::last_write_time(destinationFile);

		if (bNeedsCopy)
		{
			std::filesystem::create_directories(rLibraryAttributionDirectory);

			if (!bAnyCopied)
			{
				LOG(kDefault, kDebug, "\nCopying ThirdParty attribution files");
				LogIndent(1);
				bAnyCopied = true;
			}

			std::filesystem::copy_file(rSourceFile, destinationFile, std::filesystem::copy_options::overwrite_existing);
			LOG(kDefault, kDebug, "Copied: {}/{}", rLibraryName, rSourceFile.filename().string());
		}
	};

	for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::directory_iterator(thirdPartyDirectory))
	{
		if (!rDirectoryEntry.is_directory())
		{
			continue;
		}

		std::string libraryName = rDirectoryEntry.path().filename().string();
		if (libraryName == "Prebuilts")
		{
			continue;
		}
		std::filesystem::path libraryAttributionDirectory = attributionDirectory / libraryName;

		// Priority 1: Look for primary license files (LICENSE, LICENSE.md, LICENSE.txt)
		bool bFoundLicense = false;
		std::filesystem::path primaryLicenseFile;
		for (const std::filesystem::directory_entry& rFileEntry : std::filesystem::directory_iterator(rDirectoryEntry.path()))
		{
			if (!rFileEntry.is_regular_file())
			{
				continue;
			}

			std::string filenameLower = common::ToLower(rFileEntry.path().filename().string());
			if (filenameLower == "license" || filenameLower == "license.md" || filenameLower == "license.txt")
			{
				primaryLicenseFile = rFileEntry.path();
				bFoundLicense = true;
				break;
			}
		}

		// If primary license found, copy it and skip fallback search
		if (bFoundLicense)
		{
			CopyLicenseFile(primaryLicenseFile, libraryAttributionDirectory, libraryName);
			continue;
		}

		// Fallback: Search for alternative license/attribution files (copying, readme, manual.md)
		for (const std::filesystem::directory_entry& rFileEntry : std::filesystem::directory_iterator(rDirectoryEntry.path()))
		{
			if (!rFileEntry.is_regular_file())
			{
				continue;
			}

			std::string filenameLower = common::ToLower(rFileEntry.path().filename().string());
			if (filenameLower.find("copying") != std::string::npos || filenameLower == "manual.md" || filenameLower.find("readme") != std::string::npos)
			{
				bFoundLicense = true;
				CopyLicenseFile(rFileEntry.path(), libraryAttributionDirectory, libraryName);
			}
		}

		ASSERT(bFoundLicense);
	}

	if (bAnyCopied)
	{
		LogIndent(-1);
	}
}

}
