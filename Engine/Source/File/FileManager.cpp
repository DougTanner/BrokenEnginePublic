#include "FileManager.h"

#include "LaunchOptions.h"

#include "PackChunks.h"

#include "Game.h"

namespace engine
{

using enum FileFlags;

namespace
{

class Sha256Hasher
{
public:

	Sha256Hasher()
	{
		if (::BCryptOpenAlgorithmProvider(&mpAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
		{
			return;
		}

		DWORD uiObjectLength = 0;
		DWORD uiResultLength = 0;
		if (::BCryptGetProperty(mpAlgorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&uiObjectLength), sizeof(uiObjectLength), &uiResultLength, 0) < 0 ||
			uiResultLength != sizeof(uiObjectLength))
		{
			return;
		}
		mHashObject.resize(uiObjectLength);
		if (::BCryptCreateHash(mpAlgorithm, &mpHash, mHashObject.data(), uiObjectLength, nullptr, 0, 0) < 0)
		{
			return;
		}
		mbValid = true;
	}

	~Sha256Hasher()
	{
		if (mpHash != nullptr)
		{
			::BCryptDestroyHash(mpHash);
		}
		if (mpAlgorithm != nullptr)
		{
			::BCryptCloseAlgorithmProvider(mpAlgorithm, 0);
		}
	}

	Sha256Hasher(const Sha256Hasher&) = delete;
	Sha256Hasher& operator=(const Sha256Hasher&) = delete;

	bool Update(std::span<const std::byte> bytes)
	{
		return mbValid && bytes.size() <= std::numeric_limits<ULONG>::max() &&
			(bytes.empty() || ::BCryptHashData(mpHash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0) >= 0);
	}

	bool Finish(std::array<uint8_t, 32>& rDigest)
	{
		return mbValid && ::BCryptFinishHash(mpHash, rDigest.data(), static_cast<ULONG>(rDigest.size()), 0) >= 0;
	}

private:

	BCRYPT_ALG_HANDLE mpAlgorithm = nullptr;
	BCRYPT_HASH_HANDLE mpHash = nullptr;
	std::vector<uint8_t> mHashObject;
	bool mbValid = false;
};

class FileHandle
{
public:

	explicit FileHandle(HANDLE hFile)
		: mhFile(hFile)
	{
	}
	~FileHandle()
	{
		if (mhFile != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(mhFile);
		}
	}

	HANDLE Get() const
	{
		return mhFile;
	}

private:

	HANDLE mhFile = INVALID_HANDLE_VALUE;
};

}

FileManager::FileManager()
{
	ASSERT(gpFileManager == nullptr);

	gpFileManager = this;

	// Get Windows AppData directory and append game name
	PWSTR pWideChar = nullptr;
	HRESULT hresult = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &pWideChar);
	if (SUCCEEDED(hresult) && pWideChar != nullptr)
	{
		mAppDataDirectory = pWideChar;
	}
	else
	{
		// OS failure (trust boundary): leave mAppDataDirectory empty so the append below yields a working-directory-relative path instead of constructing a std::filesystem::path from null.
		LOG(kLoading, kError, "SHGetKnownFolderPath(FOLDERID_RoamingAppData) failed (hresult {}); falling back to a working-directory-relative AppData path", static_cast<int32_t>(hresult));
	}
	CoTaskMemFree(pWideChar);
	mAppDataDirectory.append(game::kGameName);
	std::filesystem::create_directory(mAppDataDirectory);
	LOG(kLoading, kDebug, "AppData directory: \"{}\"", mAppDataDirectory.string());

	// Get Windows temp directory and append game name
	wchar_t pcDirectory[MAX_PATH] {};
	GetTempPathW(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	mTempDirectory = pcDirectory;
	mTempDirectory.append(game::kGameName);
	LOG(kLoading, kDebug, "Temp directory: \"{}\"", mTempDirectory.string());
	std::filesystem::create_directory(mTempDirectory);

	std::filesystem::path dataDirectory;
	if (!gLaunchOptions.dataDirectory.empty())
	{
		dataDirectory = gLaunchOptions.dataDirectory;
		LOG(kLoading, kInfo, "Using explicit data directory: \"{}\"", dataDirectory);
	}
	else
	{
		// Get the file path of the executable, the /Data/ folder will be beside it
		GetModuleFileNameW(nullptr, pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
		dataDirectory = pcDirectory;
		dataDirectory.remove_filename();
		dataDirectory /= "Data";
		LOG(kLoading, kDebug, "Data directory: \"{}\"", dataDirectory.string());
	}

	mpPackChunks = std::make_unique<PackChunks>(dataDirectory);
}

FileManager::~FileManager()
{
	// Tear down PackChunks first (drains/joins loading threads, closes pack handles, frees the pool/buffers) before
	// nulling gpFileManager; the loading threads never touch gpFileManager, so this keeps them stopped before it clears.
	mpPackChunks.reset();

	if (gpFileManager == this)
	{
		gpFileManager = nullptr;
	}
}

bool FileManager::Exists(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	return std::filesystem::exists(GetFilePath(rFlags, rFilename));
}

std::filesystem::path FileManager::GetFilePath(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	std::filesystem::path filePath;
	if (rFlags & kAppDataDirectory)
	{
		filePath = mAppDataDirectory;
	}
	else if (rFlags & kTempDirectory)
	{
		filePath = mTempDirectory;
	}
	else
	{
		ASSERT(false);
		return "";
	}

	filePath /= rFilename;
	return filePath;
}

std::fstream FileManager::OpenFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	// kWrite must opt into raw streaming via kStreaming; one-shot writes use WriteFileAtomically.
	ASSERT(!(rFlags & kWrite) || (rFlags & kStreaming));

	if (rFlags & kBackup)
	{
		BackupExistingFile(rFlags, rFilename);
	}

	std::filesystem::path file = GetFilePath(rFlags, rFilename);
	std::fstream fileStream(file, (rFlags & kRead ? std::ios::in : std::ios::out) | std::ios::binary);
	LOG(kLoading, kDebug, "{} \"{}\" at \"{}\"", fileStream.is_open() ? (rFlags & kRead ? "Reading" : "Writing") : "Failed to open", rFilename.string(), file.string());
	return fileStream;
}

void FileManager::BackupExistingFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	ASSERT((rFlags & kWrite) != 0);

	std::filesystem::path file = GetFilePath(rFlags, rFilename);
	std::error_code existsErrorCode;
	const bool bExists = std::filesystem::exists(file, existsErrorCode);
	if (existsErrorCode)
	{
		// OS trust boundary (permissions, unavailable media): the atomic write of the main file is unaffected, so continue without the backup
		LOG(kLoading, kError, "Backup status query for \"{}\" failed: {}", file, existsErrorCode.value());
		DEBUG_BREAK();
		return;
	}
	if (!bExists)
	{
		return;
	}

	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	std::time_t time = std::chrono::system_clock::to_time_t(now);
	int64_t iEpochMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
	std::tm timeStruct = *std::localtime(&time);
	std::ostringstream timeStringStream;
	timeStringStream << "-" << std::put_time(&timeStruct, "%Y-%m-%d") << "-" << iEpochMs;
	std::filesystem::path backupFile = file.parent_path() / (file.stem().string() + timeStringStream.str() + file.extension().string());

	std::error_code copyEc;
	std::filesystem::copy_file(file, backupFile, copyEc);
	if (copyEc)
	{
		// OS trust boundary (disk full, permissions, antivirus): the atomic write of the main file is unaffected, so continue without the backup
		LOG(kLoading, kError, "Backup copy to \"{}\" failed: {}", backupFile, copyEc.value());
		DEBUG_BREAK();
	}
}

void FileManager::RemoveFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	std::filesystem::path file = GetFilePath(rFlags, rFilename);
	LOG(kLoading, kDebug, "Remove \"{}\" at \"{}\"", rFilename.string(), file.string());
	std::filesystem::remove(file);
}

bool FileManager::ComputeSha256(std::span<const std::byte> bytes, std::array<uint8_t, 32>& rOut)
{
	Sha256Hasher hasher;
	std::array<uint8_t, 32> digest {};
	while (!bytes.empty())
	{
		const size_t uiChunkSize = std::min(bytes.size(), static_cast<size_t>(std::numeric_limits<ULONG>::max()));
		if (!hasher.Update(bytes.first(uiChunkSize)))
		{
			return false;
		}
		bytes = bytes.subspan(uiChunkSize);
	}
	if (!hasher.Finish(digest))
	{
		return false;
	}
	rOut = digest;
	return true;
}

bool FileManager::ComputeOrdinaryFileSha256(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, FileContentDigest& rOut)
{
	const std::filesystem::path filePath = GetFilePath(rFlags, rFilename);
	const DWORD uiAttributes = ::GetFileAttributesW(filePath.c_str());
	if (uiAttributes == INVALID_FILE_ATTRIBUTES || (uiAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
	{
		return false;
	}

	FileHandle file(::CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
	if (file.Get() == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	BY_HANDLE_FILE_INFORMATION information {};
	if (::GetFileInformationByHandle(file.Get(), &information) == FALSE || (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
	{
		return false;
	}

	Sha256Hasher hasher;
	std::array<std::byte, 64 * 1024> buffer {};
	int64_t iByteCount = 0;
	for (;;)
	{
		DWORD uiBytesRead = 0;
		if (::ReadFile(file.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &uiBytesRead, nullptr) == FALSE ||
			uiBytesRead > buffer.size() || iByteCount > std::numeric_limits<int64_t>::max() - static_cast<int64_t>(uiBytesRead) ||
			!hasher.Update(std::span<const std::byte>(buffer.data(), uiBytesRead)))
		{
			return false;
		}
		iByteCount += uiBytesRead;
		if (uiBytesRead == 0)
		{
			break;
		}
	}

	FileContentDigest digest {.iByteCount = iByteCount};
	if (!hasher.Finish(digest.sha256))
	{
		return false;
	}
	rOut = digest;
	return true;
}

bool FileManager::CommitAtomicWrite(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, bool bWriteSucceeded)
{
	std::filesystem::path tmpFilename = rFilename;
	tmpFilename += ".tmp";
	std::filesystem::path tmpPath = GetFilePath(rFlags, tmpFilename);
	std::filesystem::path destPath = GetFilePath(rFlags, rFilename);

	if (!bWriteSucceeded)
	{
		LOG(kLoading, kError, "WriteFileAtomically stream bad after lambda for \"{}\"", rFilename.string());
		std::error_code removeEc;
		std::filesystem::remove(tmpPath, removeEc);
		return false;
	}

	std::error_code renameEc;
	std::filesystem::rename(tmpPath, destPath, renameEc);
	if (renameEc)
	{
		LOG(kLoading, kError, "WriteFileAtomically rename failed for \"{}\": {}", rFilename.string(), renameEc.message());
		std::error_code removeEc;
		std::filesystem::remove(tmpPath, removeEc);
		return false;
	}

	LOG(kLoading, kDebug, "WriteFileAtomically committed \"{}\"", rFilename.string());
	return true;
}

// The packed-asset chunk API forwards to the owned PackChunks engine.
const std::unordered_map<common::crc_t, EagerChunk>& FileManager::GetEagerChunkMap() const
{
	return mpPackChunks->GetEagerChunkMap();
}

const std::unordered_map<common::crc_t, LazyChunk>& FileManager::GetLazyChunkMap() const
{
	return mpPackChunks->GetLazyChunkMap();
}

common::crc_t FileManager::GetPackIntegrityToken() const
{
	return mpPackChunks->GetPackIntegrityToken();
}

bool FileManager::IsChunkReady(common::crc_t crc) const
{
	return mpPackChunks->IsChunkReady(crc);
}

void FileManager::RequestChunkLoad(std::span<const common::crc_t> crcs, LoadPriority ePriority)
{
	mpPackChunks->RequestChunkLoad(crcs, ePriority);
}

void FileManager::WaitForChunks(std::span<const common::crc_t> crcs)
{
	mpPackChunks->WaitForChunks(crcs);
}

bool FileManager::ReadChunkData(common::crc_t crc, uint64_t uiOffset, std::span<std::byte> buffer)
{
	return mpPackChunks->ReadChunkData(crc, uiOffset, buffer);
}

void FileManager::NotifyChunkCompletion()
{
	mpPackChunks->NotifyChunkCompletion();
}

LazyChunk& FileManager::GetLazyChunk(common::crc_t crc)
{
	return mpPackChunks->GetLazyChunk(crc);
}

void FileManager::ResetTextureChunkStates()
{
	mpPackChunks->ResetTextureChunkStates();
}

void FileManager::ResetTextureChunkStates(std::span<const common::crc_t> targetCrcs)
{
	mpPackChunks->ResetTextureChunkStates(targetCrcs);
}

void FileManager::DecommitChunkRange(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength)
{
	mpPackChunks->DecommitChunkRange(crc, uiOffset, uiLength);
}

bool FileManager::RecommitAndReloadChunkRange(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength)
{
	return mpPackChunks->RecommitAndReloadChunkRange(crc, uiOffset, uiLength);
}

void FileManager::RequestChunkRangeReload(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength, LoadPriority ePriority)
{
	mpPackChunks->RequestChunkRangeReload(crc, uiOffset, uiLength, ePriority);
}

ChunkRangeReloadState FileManager::GetChunkRangeReloadState(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength) const
{
	return mpPackChunks->GetChunkRangeReloadState(crc, uiOffset, uiLength);
}

void FileManager::ResetChunkRangeReloadState(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength)
{
	mpPackChunks->ResetChunkRangeReloadState(crc, uiOffset, uiLength);
}

MemoryStats FileManager::GetEagerStats() const
{
	return mpPackChunks->GetEagerStats();
}

MemoryStats FileManager::GetLazyStats() const
{
	return mpPackChunks->GetLazyStats();
}

MemoryStats FileManager::GetMemoryStats(data::DataTypes eDataType) const
{
	return mpPackChunks->GetMemoryStats(eDataType);
}

} // namespace engine
