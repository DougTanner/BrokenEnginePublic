#include "ExportShader.h"

#include "FileManager.h"

namespace
{

constexpr int64_t kiDependencyMetadataMagic = 0x53484445504D5431;
constexpr int64_t kiDependencyMetadataVersion = 2;
constexpr size_t kuiFingerprintCharacters = 64;

struct CachedDependencyFingerprint
{
	int64_t iInputRoot = 0;
	std::filesystem::path relativePath;
	std::string fingerprint;
};

bool IsDependencyInInputRoot(const std::filesystem::path& rDependency)
{
	if (!std::filesystem::exists(rDependency))
	{
		return false;
	}
	std::filesystem::path dependency = std::filesystem::weakly_canonical(rDependency);
	for (const std::filesystem::path& rInputRoot : gpFileManager->mpInputDirectories)
	{
		std::error_code error;
		std::filesystem::path relativePath = std::filesystem::relative(dependency, rInputRoot, error);
		if (!error && !relativePath.empty() && *relativePath.begin() != "..")
		{
			return true;
		}
	}
	return false;
}

std::optional<std::vector<CachedDependencyFingerprint>> ReadDependencyMetadata(const std::filesystem::path& rPath)
{
	std::fstream stream(rPath, std::ios::in | std::ios::binary);
	int64_t iMagic = 0;
	int64_t iVersion = 0;
	int64_t iCount = 0;
	stream.read(reinterpret_cast<char*>(&iMagic), sizeof(iMagic));
	stream.read(reinterpret_cast<char*>(&iVersion), sizeof(iVersion));
	stream.read(reinterpret_cast<char*>(&iCount), sizeof(iCount));
	if (!stream || iMagic != kiDependencyMetadataMagic || iVersion != kiDependencyMetadataVersion || iCount < 0 || iCount > 10'000)
	{
		return std::nullopt;
	}

	std::vector<CachedDependencyFingerprint> dependencies;
	dependencies.reserve(static_cast<size_t>(iCount));
	for (int64_t iDependency = 0; iDependency < iCount; ++iDependency)
	{
		int64_t iInputRoot = 0;
		int64_t iPathCharacters = 0;
		stream.read(reinterpret_cast<char*>(&iInputRoot), sizeof(iInputRoot));
		stream.read(reinterpret_cast<char*>(&iPathCharacters), sizeof(iPathCharacters));
		if (!stream || iInputRoot < 0 || iInputRoot >= static_cast<int64_t>(std::size(gpFileManager->mpInputDirectories)) || iPathCharacters <= 0 || iPathCharacters > MAX_PATH * 4)
		{
			return std::nullopt;
		}
		std::string relativePath(static_cast<size_t>(iPathCharacters), '\0');
		std::string fingerprint(kuiFingerprintCharacters, '\0');
		stream.read(relativePath.data(), relativePath.size());
		stream.read(fingerprint.data(), fingerprint.size());
		if (!stream)
		{
			return std::nullopt;
		}
		std::filesystem::path relativeDependencyPath = std::filesystem::path(relativePath).lexically_normal();
		if (relativeDependencyPath.is_absolute() || relativeDependencyPath.empty() || *relativeDependencyPath.begin() == "..")
		{
			return std::nullopt;
		}
		dependencies.emplace_back(CachedDependencyFingerprint
		{
			.iInputRoot = iInputRoot,
			.relativePath = std::move(relativeDependencyPath),
			.fingerprint = std::move(fingerprint),
		});
	}
	return dependencies;
}

}

static std::vector<std::filesystem::path> ParseDependencyFile(const std::filesystem::path& rDependencyFilePath)
{
	std::fstream fileStream(rDependencyFilePath, std::ios::in);
	std::string content((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
	if (!fileStream && !fileStream.eof())
	{
		throw std::runtime_error(std::format("Failed to read shader dependency file \"{}\"", rDependencyFilePath.string()));
	}

	// shaderc emits the target separator literally as ": ". A Windows drive colon is followed by a
	// slash, so searching for the full separator avoids cutting the target at "C:".
	size_t uiTargetSeparator = content.find(": ");
	if (uiTargetSeparator == std::string::npos)
	{
		throw std::runtime_error(std::format("Shader dependency file \"{}\" has no ': ' target separator", rDependencyFilePath.string()));
	}
	content.erase(0, uiTargetSeparator + 2);
	while (!content.empty() && (content.back() == '\r' || content.back() == '\n'))
	{
		content.pop_back();
	}
	if (content.empty() || content.find_first_of("\r\n") != std::string::npos)
	{
		throw std::runtime_error(std::format("Shader dependency file \"{}\" is empty or contains an unsupported continuation", rDependencyFilePath.string()));
	}

	// shaderc writes dependency names verbatim with spaces between entries; it does not escape spaces
	// inside filenames. Known canonical input-root prefixes are therefore the only lossless delimiters.
	std::vector<std::string> rootPrefixes;
	for (const std::filesystem::path& rInputRoot : gpFileManager->mpInputDirectories)
	{
		for (std::string rootPrefix : {rInputRoot.string(), rInputRoot.generic_string()})
		{
			std::string lowerPrefix = common::ToLower(rootPrefix);
			if (std::ranges::none_of(rootPrefixes, [&lowerPrefix](const std::string& rExisting)
			{
				return common::ToLower(rExisting) == lowerPrefix;
			}))
			{
				rootPrefixes.push_back(std::move(rootPrefix));
			}
		}
	}
	std::ranges::sort(rootPrefixes, [](const std::string& rLeft, const std::string& rRight)
	{
		return rLeft.size() > rRight.size();
	});
	std::string lowerContent = common::ToLower(content);
	std::function<const std::string*(size_t)> findMatchingRoot = [&rootPrefixes, &lowerContent](size_t uiOffset) -> const std::string*
	{
		for (const std::string& rRootPrefix : rootPrefixes)
		{
			std::string lowerPrefix = common::ToLower(rRootPrefix);
			if (uiOffset + lowerPrefix.size() <= lowerContent.size()
				&& lowerContent.compare(uiOffset, lowerPrefix.size(), lowerPrefix) == 0
				&& (uiOffset + lowerPrefix.size() == lowerContent.size()
					|| lowerContent[uiOffset + lowerPrefix.size()] == '\\'
					|| lowerContent[uiOffset + lowerPrefix.size()] == '/'))
			{
				return &rRootPrefix;
			}
		}
		return nullptr;
	};

	std::vector<std::filesystem::path> dependencies;
	if (findMatchingRoot(0) != nullptr)
	{
		size_t uiDependencyStart = 0;
		while (uiDependencyStart < content.size())
		{
			if (findMatchingRoot(uiDependencyStart) == nullptr)
			{
				throw std::runtime_error(std::format("Shader dependency file \"{}\" contains an ambiguous or outside-root entry near \"{}\"", rDependencyFilePath.string(), content.substr(uiDependencyStart)));
			}

			size_t uiDependencyEnd = content.size();
			for (size_t uiSpace = content.find(' ', uiDependencyStart); uiSpace != std::string::npos; uiSpace = content.find(' ', uiSpace + 1))
			{
				if (findMatchingRoot(uiSpace + 1) != nullptr)
				{
					uiDependencyEnd = uiSpace;
					break;
				}
			}

			std::filesystem::path dependency(content.substr(uiDependencyStart, uiDependencyEnd - uiDependencyStart));
			if (!IsDependencyInInputRoot(dependency))
			{
				throw std::runtime_error(std::format("Shader dependency \"{}\" is ambiguous, missing, or outside DataPacker input roots", dependency.string()));
			}
			dependencies.push_back(std::move(dependency));
			uiDependencyStart = uiDependencyEnd == content.size() ? content.size() : uiDependencyEnd + 1;
		}
		return dependencies;
	}

	// Retain the old whitespace parser only when every resulting token independently names an
	// existing dependency under an input root. A filename containing spaces fails this proof loudly.
	std::istringstream stream(content);
	std::string token;
	while (stream >> token)
	{
		std::filesystem::path dependency(token);
		if (!IsDependencyInInputRoot(dependency))
		{
			throw std::runtime_error(std::format("Shader dependency file \"{}\" cannot unambiguously delimit \"{}\" using DataPacker input roots", rDependencyFilePath.string(), content));
		}
		dependencies.push_back(std::move(dependency));
	}
	return dependencies;
}

bool ExportShader::CheckDirty(const std::filesystem::path& rPackFile)
{
	if (ExportJob::CheckDirty(rPackFile))
	{
		return true;
	}

	std::optional<std::vector<CachedDependencyFingerprint>> dependencies = ReadDependencyMetadata(mDependencyMetadataFile);
	if (!dependencies.has_value())
	{
		mbDirty = true;
		return true;
	}

	for (const CachedDependencyFingerprint& rDependency : dependencies.value())
	{
		std::filesystem::path dependencyPath = gpFileManager->mpInputDirectories[rDependency.iInputRoot] / rDependency.relativePath;
		if (!std::filesystem::exists(dependencyPath))
		{
			mbDirty = true;
			return true;
		}
		if (gpFileManager->GetFingerprint(dependencyPath) != rDependency.fingerprint)
		{
			LOG(kDefault, kDebug, "Shader dependency changed: \"{}\"", dependencyPath.string());
			mbDirty = true;
			return mbDirty;
		}
	}

	return mbDirty;
}

void ExportShader::CaptureDependencies()
{
	mDependencyFingerprints.clear();
	std::vector<std::filesystem::path> dependencies = ParseDependencyFile(mDependencyFile);
	if (dependencies.empty())
	{
		throw std::runtime_error(std::format("Shader dependency file \"{}\" is empty or invalid", mDependencyFile.string()));
	}
	for (const std::filesystem::path& rDependency : dependencies)
	{
		std::filesystem::path dependency = std::filesystem::weakly_canonical(rDependency);
		bool bFoundRoot = false;
		for (int64_t iRoot = 0; iRoot < static_cast<int64_t>(std::size(gpFileManager->mpInputDirectories)); ++iRoot)
		{
			std::error_code error;
			std::filesystem::path relativePath = std::filesystem::relative(dependency, gpFileManager->mpInputDirectories[iRoot], error);
			if (error || relativePath.empty() || *relativePath.begin() == "..")
			{
				continue;
			}
			mDependencyFingerprints.emplace_back(DependencyFingerprint
			{
				.iInputRoot = iRoot,
				.relativePath = relativePath,
				.fingerprint = gpFileManager->GetFingerprint(dependency),
			});
			bFoundRoot = true;
			break;
		}
		if (!bFoundRoot)
		{
			throw std::runtime_error(std::format("Shader dependency \"{}\" is outside DataPacker input roots", dependency.string()));
		}
	}
}

void ExportShader::UpdateCacheMetadata()
{
	std::filesystem::path temporaryPath = mDependencyMetadataFile;
	temporaryPath += ".tmp";
	std::fstream stream(temporaryPath, std::ios::out | std::ios::binary);
	int64_t iCount = static_cast<int64_t>(mDependencyFingerprints.size());
	stream.write(reinterpret_cast<const char*>(&kiDependencyMetadataMagic), sizeof(kiDependencyMetadataMagic));
	stream.write(reinterpret_cast<const char*>(&kiDependencyMetadataVersion), sizeof(kiDependencyMetadataVersion));
	stream.write(reinterpret_cast<const char*>(&iCount), sizeof(iCount));
	for (const DependencyFingerprint& rDependency : mDependencyFingerprints)
	{
		std::string relativePath = rDependency.relativePath.generic_string();
		int64_t iPathCharacters = static_cast<int64_t>(relativePath.size());
		stream.write(reinterpret_cast<const char*>(&rDependency.iInputRoot), sizeof(rDependency.iInputRoot));
		stream.write(reinterpret_cast<const char*>(&iPathCharacters), sizeof(iPathCharacters));
		stream.write(relativePath.data(), relativePath.size());
		stream.write(rDependency.fingerprint.data(), rDependency.fingerprint.size());
	}
	stream.close();
	VERIFY_SUCCESS(stream.good());
	VERIFY_SUCCESS(MoveFileExW(temporaryPath.native().c_str(), mDependencyMetadataFile.native().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
}
