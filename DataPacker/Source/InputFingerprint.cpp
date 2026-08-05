#include "InputFingerprint.h"

namespace
{

// Opening the provider per hash is expensive; one process-lifetime handle serves every hash (BCrypt
// algorithm handles are thread-safe) and is deliberately never closed.
BCRYPT_ALG_HANDLE GetSha256Algorithm()
{
	static BCRYPT_ALG_HANDLE spAlgorithm = []()
	{
		BCRYPT_ALG_HANDLE pAlgorithm = nullptr;
		if (BCryptOpenAlgorithmProvider(&pAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
		{
			throw std::runtime_error("Unable to initialize SHA-256");
		}
		return pAlgorithm;
	}();
	return spAlgorithm;
}

class Sha256Hasher
{
public:

	Sha256Hasher()
	{
		if (BCryptCreateHash(GetSha256Algorithm(), &mpHash, nullptr, 0, nullptr, 0, 0) < 0)
		{
			throw std::runtime_error("Unable to initialize SHA-256");
		}
	}

	~Sha256Hasher()
	{
		if (mpHash != nullptr)
		{
			BCryptDestroyHash(mpHash);
		}
	}

	Sha256Hasher(const Sha256Hasher& rToCopy) = delete;
	Sha256Hasher& operator=(const Sha256Hasher& rToCopy) = delete;

	void Update(const std::byte* pBytes, size_t uiBytes)
	{
		if (uiBytes > 0 && BCryptHashData(mpHash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(pBytes)), static_cast<ULONG>(uiBytes), 0) < 0)
		{
			throw std::runtime_error("Unable to hash fingerprint data");
		}
	}

	void Update(std::string_view text)
	{
		Update(reinterpret_cast<const std::byte*>(text.data()), text.size());
	}

	std::string Finish()
	{
		uint8_t pDigest[32] {};
		if (BCryptFinishHash(mpHash, pDigest, sizeof(pDigest), 0) < 0)
		{
			throw std::runtime_error("Unable to finish fingerprint hash");
		}
		std::string result;
		result.reserve(64);
		for (uint8_t uiByte : pDigest)
		{
			result += std::format("{:02x}", uiByte);
		}
		return result;
	}

private:

	BCRYPT_HASH_HANDLE mpHash = nullptr;
};

std::string HashFileContents(const std::filesystem::path& rPath, InputFingerprintMode eMode)
{
	Sha256Hasher hasher;
	std::fstream stream(rPath, std::ios::in | std::ios::binary);
	std::vector<std::byte> buffer(64 * 1024);
	std::vector<std::byte> normalizedBuffer;
	if (eMode == InputFingerprintMode::kTextCrLf)
	{
		normalizedBuffer.resize(buffer.size() * 2 + 2);
	}
	bool bPendingCarriageReturn = false;
	while (stream)
	{
		stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
		size_t uiBytes = static_cast<size_t>(stream.gcount());
		if (eMode == InputFingerprintMode::kRaw)
		{
			hasher.Update(buffer.data(), uiBytes);
			continue;
		}

		size_t uiNormalizedBytes = 0;
		for (size_t i = 0; i < uiBytes; ++i)
		{
			std::byte uiByte = buffer.at(i);
			if (bPendingCarriageReturn)
			{
				normalizedBuffer.at(uiNormalizedBytes++) = static_cast<std::byte>('\r');
				normalizedBuffer.at(uiNormalizedBytes++) = static_cast<std::byte>('\n');
				bPendingCarriageReturn = false;
				if (uiByte == static_cast<std::byte>('\n'))
				{
					continue;
				}
			}
			if (uiByte == static_cast<std::byte>('\r'))
			{
				bPendingCarriageReturn = true;
			}
			else if (uiByte == static_cast<std::byte>('\n'))
			{
				normalizedBuffer.at(uiNormalizedBytes++) = static_cast<std::byte>('\r');
				normalizedBuffer.at(uiNormalizedBytes++) = static_cast<std::byte>('\n');
			}
			else
			{
				normalizedBuffer.at(uiNormalizedBytes++) = uiByte;
			}
		}
		hasher.Update(normalizedBuffer.data(), uiNormalizedBytes);
	}
	if (!stream.eof())
	{
		throw std::runtime_error(std::format("Failed to fingerprint \"{}\"", rPath.string()));
	}
	if (bPendingCarriageReturn)
	{
		hasher.Update("\r\n");
	}
	return hasher.Finish();
}

constexpr const char* kpcPersistentFingerprintMagic = "DataPackerInputFingerprint";
constexpr int64_t kiPersistentFingerprintVersion = 2;
constexpr uintmax_t kuiMaximumPersistentFingerprintBytes = 4 * 1024;

constexpr const char* kpcFingerprintCacheMagic = "DataPackerFingerprintCache";
constexpr int64_t kiFingerprintCacheVersion = 1;
constexpr uintmax_t kuiMaximumFingerprintCacheBytes = 64 * 1024 * 1024;

bool IsSha256(const std::string& rFingerprint)
{
	return rFingerprint.size() == 64 && std::ranges::all_of(rFingerprint, [](char cCharacter)
	{
		return (cCharacter >= '0' && cCharacter <= '9') || (cCharacter >= 'a' && cCharacter <= 'f');
	});
}

}

InputFingerprintCache::InputFingerprintCache(const std::filesystem::path& rCacheFile)
: mCacheFile(rCacheFile)
{
	Load();
}

InputFingerprintCache::~InputFingerprintCache()
{
	try
	{
		Save();
	}
	catch (const std::exception& rException)
	{
		LOG(kDefault, kWarning, "Failed to save fingerprint cache \"{}\": {}", mCacheFile.string(), rException.what());
	}
}

void InputFingerprintCache::Load()
{
	try
	{
		std::error_code cacheSizeError;
		uintmax_t uiCacheSize = std::filesystem::file_size(mCacheFile, cacheSizeError);
		if (cacheSizeError || uiCacheSize > kuiMaximumFingerprintCacheBytes)
		{
			return;
		}
		std::ifstream cacheStream(mCacheFile);
		nlohmann::json cache;
		cacheStream >> cache;
		if (cache.at("magic") != kpcFingerprintCacheMagic || cache.at("version") != kiFingerprintCacheVersion)
		{
			return;
		}
		for (const nlohmann::json& rEntry : cache.at("entries"))
		{
			std::string fingerprint = rEntry.at("fingerprint").get<std::string>();
			if (!IsSha256(fingerprint))
			{
				continue;
			}
			FileSnapshot snapshot
			{
				.uiSize = rEntry.at("size").get<uintmax_t>(),
				.iLastWriteTime = rEntry.at("lastWriteTime").get<int64_t>(),
				.iCreationTime = rEntry.at("creationTime").get<int64_t>(),
				.iChangeTime = rEntry.at("changeTime").get<int64_t>(),
				.uiFileId = rEntry.at("fileId").get<uint64_t>(),
				.uiVolumeSerialNumber = rEntry.at("volumeSerialNumber").get<uint32_t>(),
			};
			mCachedFingerprints.insert_or_assign(rEntry.at("path").get<std::string>(), CachedFingerprint {.snapshot = snapshot, .fingerprint = std::move(fingerprint)});
		}
	}
	catch (const std::exception&)
	{
		// A missing or malformed cache falls back to content hashing.
		mCachedFingerprints.clear();
	}
}

void InputFingerprintCache::Save()
{
	if (!mbDirty)
	{
		return;
	}
	// Sorted output keeps a rewrite of unchanged content byte-identical in the cross-worktree shared cache.
	std::vector<const std::pair<const std::string, CachedFingerprint>*> sortedEntries;
	sortedEntries.reserve(mCachedFingerprints.size());
	for (const std::pair<const std::string, CachedFingerprint>& rEntry : mCachedFingerprints)
	{
		sortedEntries.push_back(&rEntry);
	}
	std::ranges::sort(sortedEntries, [](const auto* pLeft, const auto* pRight)
	{
		return pLeft->first < pRight->first;
	});
	nlohmann::json entries = nlohmann::json::array();
	for (const std::pair<const std::string, CachedFingerprint>* pEntry : sortedEntries)
	{
		const CachedFingerprint& rCached = pEntry->second;
		entries.push_back(nlohmann::json
		{
			{"path", pEntry->first},
			{"size", rCached.snapshot.uiSize},
			{"lastWriteTime", rCached.snapshot.iLastWriteTime},
			{"creationTime", rCached.snapshot.iCreationTime},
			{"changeTime", rCached.snapshot.iChangeTime},
			{"fileId", rCached.snapshot.uiFileId},
			{"volumeSerialNumber", rCached.snapshot.uiVolumeSerialNumber},
			{"fingerprint", rCached.fingerprint},
		});
	}
	nlohmann::json cache
	{
		{"magic", kpcFingerprintCacheMagic},
		{"version", kiFingerprintCacheVersion},
		{"entries", std::move(entries)},
	};
	std::filesystem::path temporaryPath = mCacheFile;
	temporaryPath += std::format(".{}.tmp", GetCurrentProcessId());
	{
		std::ofstream cacheStream(temporaryPath, std::ios::trunc);
		cacheStream << cache.dump();
		if (!cacheStream)
		{
			throw std::runtime_error(std::format("Failed to write fingerprint cache \"{}\"", temporaryPath.string()));
		}
	}
	if (!MoveFileExW(temporaryPath.native().c_str(), mCacheFile.native().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DWORD uiError = GetLastError();
		std::filesystem::remove(temporaryPath);
		throw std::system_error(static_cast<int>(uiError), std::system_category(), std::format("Failed to publish fingerprint cache \"{}\"", mCacheFile.string()));
	}
	mbDirty = false;
}

std::string InputFingerprintCache::Get(const std::filesystem::path& rPath, InputFingerprintMode eMode)
{
	std::scoped_lock lock(mMutex);
	return GetUnlocked(rPath, eMode);
}

std::string InputFingerprintCache::GetPersistent(const std::filesystem::path& rPath)
{
	std::scoped_lock lock(mMutex);
	return GetPersistentFile(rPath);
}

std::string InputFingerprintCache::GetUnlocked(const std::filesystem::path& rPath, InputFingerprintMode eMode)
{
	if (std::filesystem::is_directory(rPath))
	{
		return GetDirectory(rPath);
	}
	return GetFile(rPath, eMode);
}

std::string InputFingerprintCache::GetFile(const std::filesystem::path& rPath, InputFingerprintMode eMode)
{
	std::string key = PathKey(rPath, eMode);
	FileSnapshot snapshot = Snapshot(rPath);
	auto cachedIterator = mCachedFingerprints.find(key);
	if (cachedIterator != mCachedFingerprints.end() && cachedIterator->second.snapshot == snapshot)
	{
		return cachedIterator->second.fingerprint;
	}

	std::string fingerprint = HashFileContents(rPath, eMode);
	mCachedFingerprints.insert_or_assign(key, CachedFingerprint {.snapshot = snapshot, .fingerprint = fingerprint});
	mbDirty = true;
	return fingerprint;
}

std::string InputFingerprintCache::GetPersistentFile(const std::filesystem::path& rPath)
{
	std::filesystem::path metadataPath = rPath;
	metadataPath += ".fingerprint.meta";
	std::string key = PathKey(rPath, InputFingerprintMode::kRaw);
	FileSnapshot snapshot = Snapshot(rPath);
	auto cachedIterator = mCachedFingerprints.find(key);
	if (cachedIterator != mCachedFingerprints.end() && cachedIterator->second.snapshot == snapshot)
	{
		return cachedIterator->second.fingerprint;
	}

	try
	{
		std::error_code metadataSizeError;
		uintmax_t uiMetadataSize = std::filesystem::file_size(metadataPath, metadataSizeError);
		if (metadataSizeError || uiMetadataSize > kuiMaximumPersistentFingerprintBytes)
		{
			throw std::runtime_error("Invalid fingerprint metadata size");
		}
		std::ifstream metadataStream(metadataPath);
		nlohmann::json metadata;
		metadataStream >> metadata;
		FileSnapshot recordedSnapshot
		{
			.uiSize = metadata.at("size").get<uintmax_t>(),
			.iLastWriteTime = metadata.at("lastWriteTime").get<int64_t>(),
			.iCreationTime = metadata.at("creationTime").get<int64_t>(),
			.iChangeTime = metadata.at("changeTime").get<int64_t>(),
			.uiFileId = metadata.at("fileId").get<uint64_t>(),
			.uiVolumeSerialNumber = metadata.at("volumeSerialNumber").get<uint32_t>(),
		};
		std::string fingerprint = metadata.at("fingerprint").get<std::string>();
		if (metadata.at("magic") == kpcPersistentFingerprintMagic
			&& metadata.at("version") == kiPersistentFingerprintVersion
			&& IsSha256(fingerprint)
			&& recordedSnapshot == snapshot)
		{
			mCachedFingerprints.insert_or_assign(key, CachedFingerprint {.snapshot = snapshot, .fingerprint = fingerprint});
			mbDirty = true;
			return fingerprint;
		}
	}
	catch (const std::exception&)
	{
		// Missing and malformed metadata both fall through to content hashing.
	}

	std::string fingerprint = HashFileContents(rPath, InputFingerprintMode::kRaw);
	nlohmann::json metadata
	{
		{"magic", kpcPersistentFingerprintMagic},
		{"version", kiPersistentFingerprintVersion},
		{"size", snapshot.uiSize},
		{"lastWriteTime", snapshot.iLastWriteTime},
		{"creationTime", snapshot.iCreationTime},
		{"changeTime", snapshot.iChangeTime},
		{"fileId", snapshot.uiFileId},
		{"volumeSerialNumber", snapshot.uiVolumeSerialNumber},
		{"fingerprint", fingerprint},
	};
	std::filesystem::path temporaryPath = metadataPath;
	temporaryPath += std::format(".{}.tmp", GetCurrentProcessId());
	{
		std::ofstream metadataStream(temporaryPath, std::ios::trunc);
		metadataStream << metadata.dump();
		if (!metadataStream)
		{
			throw std::runtime_error(std::format("Failed to write fingerprint metadata \"{}\"", temporaryPath.string()));
		}
	}
	if (!MoveFileExW(temporaryPath.native().c_str(), metadataPath.native().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DWORD uiError = GetLastError();
		std::filesystem::remove(temporaryPath);
		throw std::system_error(static_cast<int>(uiError), std::system_category(), std::format("Failed to publish fingerprint metadata \"{}\"", metadataPath.string()));
	}
	mCachedFingerprints.insert_or_assign(key, CachedFingerprint {.snapshot = snapshot, .fingerprint = fingerprint});
	mbDirty = true;
	return fingerprint;
}

std::string InputFingerprintCache::GetDirectory(const std::filesystem::path& rPath)
{
	std::vector<std::filesystem::path> files;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(rPath))
	{
		if (rEntry.is_regular_file())
		{
			files.push_back(rEntry.path());
		}
	}
	std::sort(files.begin(), files.end(), [&rPath](const std::filesystem::path& rLeft, const std::filesystem::path& rRight)
	{
		return std::filesystem::relative(rLeft, rPath).generic_string() < std::filesystem::relative(rRight, rPath).generic_string();
	});

	Sha256Hasher hasher;
	for (const std::filesystem::path& rFile : files)
	{
		std::string relativePath = std::filesystem::relative(rFile, rPath).generic_string();
		hasher.Update(relativePath);
		hasher.Update(std::string_view("\0", 1));
		hasher.Update(GetFile(rFile));
		hasher.Update(std::string_view("\0", 1));
	}
	return hasher.Finish();
}

InputFingerprintCache::FileSnapshot InputFingerprintCache::Snapshot(const std::filesystem::path& rPath)
{
	HANDLE hFile = CreateFileW(rPath.native().c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), std::format("Failed to open \"{}\" for fingerprinting", rPath.string()));
	}
	std::unique_ptr<void, decltype(&CloseHandle)> fileHandle(hFile, &CloseHandle);
	FILE_BASIC_INFO basicInfo {};
	BY_HANDLE_FILE_INFORMATION fileInfo {};
	if (!GetFileInformationByHandleEx(fileHandle.get(), FileBasicInfo, &basicInfo, sizeof(basicInfo))
		|| !GetFileInformationByHandle(fileHandle.get(), &fileInfo))
	{
		throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), std::format("Failed to inspect \"{}\" for fingerprinting", rPath.string()));
	}
	return
	{
		.uiSize = (static_cast<uintmax_t>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow,
		.iLastWriteTime = basicInfo.LastWriteTime.QuadPart,
		.iCreationTime = basicInfo.CreationTime.QuadPart,
		.iChangeTime = basicInfo.ChangeTime.QuadPart,
		.uiFileId = (static_cast<uint64_t>(fileInfo.nFileIndexHigh) << 32) | fileInfo.nFileIndexLow,
		.uiVolumeSerialNumber = fileInfo.dwVolumeSerialNumber,
	};
}

std::string InputFingerprintCache::PathKey(const std::filesystem::path& rPath, InputFingerprintMode eMode)
{
	std::string path = common::ToLower(std::filesystem::absolute(rPath).lexically_normal().string());
	return eMode == InputFingerprintMode::kRaw ? path : "text-crlf-v1:" + path;
}
