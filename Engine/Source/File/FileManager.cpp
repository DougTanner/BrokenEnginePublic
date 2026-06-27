#include "FileManager.h"

#include "Profile/ProfileManager.h"

#include "Game.h"

#if defined(BT_CLIENT)
#include "Graphics/Managers/TextureUploadManager.h"
#endif

namespace engine
{

using enum FileFlags;

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

	// Get the file path of the executable, the /Data/ folder will be beside it
	GetModuleFileNameW(nullptr, pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
	mDataDirectory = pcDirectory;
	mDataDirectory.remove_filename();
	mDataDirectory /= "Data";
	LOG(kLoading, kDebug, "Data directory: \"{}\"", mDataDirectory.string());

	LoadPackFiles();
}

FileManager::~FileManager()
{
	// Drain the eager-load task first: mLoadingThread is assigned inside it (see LoadPackFiles), so a join
	// before the task runs would hit a not-yet-joinable thread. On the client GetEagerChunkMap() already
	// drained it during boot (valid() is then false); on the server nothing else ever drains it. get()
	// rethrows if the task threw (corrupt-pack ASSERT, OOM, thread-create failure); swallow it so a
	// destructor never terminates the process, and let the joinable() guard below skip the never-started thread.
	if (mLoadingFuture.valid())
	{
		try
		{
			mLoadingFuture.get();
		}
		catch (...)
		{
			LOG(kLoading, kError, "Eager-load task threw during FileManager shutdown");
			DEBUG_BREAK();
		}
	}

	// Shutdown background loading thread
	{
		std::unique_lock lock(mQueueMutex);
		mShutdown = true;
	}
	mWakeCondition.notify_one();
	// joinable() is false only if the eager-load task threw before assigning mLoadingThread (catch above).
	if (mLoadingThread.joinable())
	{
		mLoadingThread.join();
	}

	// Close persistent pack file handles and free read buffer
	for (HANDLE& rHandle : mLazyPackFileHandles)
	{
		if (rHandle != nullptr && rHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(rHandle);
		}
		rHandle = nullptr;
	}
	_aligned_free(mpReadBuffer);
	VirtualFree(mpLazyPool, 0, MEM_RELEASE);
	if (mpDecompressScratch != nullptr)
	{
		VirtualFree(mpDecompressScratch, 0, MEM_RELEASE);
	}

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
	if (!std::filesystem::exists(file))
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

// Build full path to a data file (pack or manifest) for the given data type
std::filesystem::path FileManager::GetDataFilePath(data::DataTypes eDataType, std::string_view extension) const
{
	return mDataDirectory / (std::string(data::kpcDataTypeNames[eDataType]) + std::string(extension));
}

constexpr bool IsEagerChunk(data::DataTypes eDataType)
{
	return eDataType == data::kDataTypeFont || eDataType == data::kDataTypeScene || eDataType == data::kDataTypeModel || eDataType == data::kDataTypeShader || eDataType == data::kDataTypeRaw;
}

constexpr bool IsServerChunk(data::DataTypes eDataType)
{
	// Server simulates terrain/physics from Islands only; Audio + Texture are client-only consumers.
	return eDataType == data::kDataTypeIslands;
}

// Derive data type from chunk flags for pack file handle lookup
constexpr data::DataTypes DataTypeFromFlags(const common::ChunkFlags_t& rFlags)
{
	if (rFlags & common::ChunkFlags::kFont)    return data::kDataTypeFont;
	if (rFlags & common::ChunkFlags::kScene)   return data::kDataTypeScene;
	if (rFlags & common::ChunkFlags::kIsland)  return data::kDataTypeIslands;
	if (rFlags & common::ChunkFlags::kModel)   return data::kDataTypeModel;
	if (rFlags & common::ChunkFlags::kShader)  return data::kDataTypeShader;
	if (rFlags & common::ChunkFlags::kTexture) return data::kDataTypeTexture;
	if (rFlags & common::ChunkFlags::kChunkAudio)   return data::kDataTypeAudio;
	if (rFlags & common::ChunkFlags::kRaw)     return data::kDataTypeRaw;
	// External-data trust boundary: flags come from .pack chunk headers; no type flag means a corrupt pack.
	// kDataTypeCount is one past the last valid pack array index — callers must treat it as load failure.
	return data::kDataTypeCount;
}

// External-data trust boundary: a wholly missing or corrupt required .manifest/.pack is unrecoverable —
// limping with an empty/garbage chunk set defers the failure to every later consumer and ships a broken game.
// LoadPackFiles runs in the FileManager ctor (Main.cpp), constructed before MainThread's try/catch, so a thrown
// ASSERT here std::terminates with no crash report. Fail loud (user-facing) and exit cleanly instead.
[[noreturn]] static void FailMissingRequiredAsset(const std::filesystem::path& rAssetPath, std::string_view reason)
{
	LOG(kLoading, kError, "Required asset \"{}\" is missing or corrupt: {}", rAssetPath.string(), reason);
	DEBUG_BREAK();
	std::string message = "A required game data file is missing or corrupt:\n\n";
	message += rAssetPath.string();
	message += "\n\n";
	message += reason;
	message += "\n\nPlease reinstall or verify your game files.";
	MessageBox(nullptr, message.c_str(), game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
	ExitProcess(0);
}

void FileManager::LoadPackFiles()
{
	for (int64_t i = 0; i < data::kDataTypeCount; ++i)
	{
#if defined(BT_SERVER)
		// Server consumes only Islands among lazy chunks; skipping the rest keeps Audio.pack/Texture.pack
		// unopened (and therefore unlocked) so DataPacker can rewrite them while the server is running.
		if (!IsServerChunk(static_cast<data::DataTypes>(i)))
		{
			continue;
		}
#endif

		// Cache pack file path for reuse across loading stages
		mPackFilePaths[i] = GetDataFilePath(static_cast<data::DataTypes>(i), ".pack");
		// Read chunk locations from manifest
		std::filesystem::path manifestPath = GetDataFilePath(static_cast<data::DataTypes>(i), ".manifest");
		std::fstream manifestStream(manifestPath, std::ios::in | std::ios::binary);
		common::DataHeader dataHeader {};
		manifestStream.read(reinterpret_cast<char*>(&dataHeader), sizeof(dataHeader));
		// Trust boundary: a missing manifest leaves manifestStream failed and dataHeader zero-filled; a corrupt one
		// mismatches magic/version. Either way the asset type is unusable — fail loud rather than ASSERT-terminate.
		if (!manifestStream || dataHeader.iMagic != common::DataHeader::kiMagic || dataHeader.iVersion != common::DataHeader::kiVersion)
		{
			FailMissingRequiredAsset(manifestPath, "manifest header missing or version mismatch");
		}

		// Trust boundary: iChunkCount comes from the manifest header; a garbage count would drive an unbounded resize
		// (bad_alloc terminate) or a torn read. Bound it by what the file can actually hold before allocating.
		int64_t iChunkTableOffset = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::DataHeader)));
		manifestStream.seekg(0, std::ios::end);
		int64_t iManifestSize = static_cast<int64_t>(manifestStream.tellg());
		int64_t iMaxChunks = (iManifestSize - iChunkTableOffset) / static_cast<int64_t>(sizeof(common::ChunkLocation));
		if (!manifestStream || dataHeader.iChunkCount < 0 || dataHeader.iChunkCount > iMaxChunks)
		{
			FailMissingRequiredAsset(manifestPath, "manifest chunk-count out of range");
		}

		manifestStream.seekg(iChunkTableOffset);
		mpChunkLocations[i].resize(dataHeader.iChunkCount);
		manifestStream.read(reinterpret_cast<char*>(mpChunkLocations[i].data()), dataHeader.iChunkCount * sizeof(common::ChunkLocation));
		if (!manifestStream)
		{
			FailMissingRequiredAsset(manifestPath, "manifest chunk table truncated");
		}
		manifestStream.close();

		if (IsEagerChunk(static_cast<data::DataTypes>(i)))
		{
			continue;
		}

		std::fstream packStream(mPackFilePaths[i], std::ios::in | std::ios::binary);
		// Trust boundary: a missing/locked lazy .pack leaves packStream closed; reading headers from it would build
		// the lazy map (and pool layout) from garbage. A required pack is as fatal as a missing manifest.
		if (!packStream)
		{
			FailMissingRequiredAsset(mPackFilePaths[i], "pack file missing or unreadable");
		}
		for (const common::ChunkLocation& rChunkLocation : mpChunkLocations[i])
		{
			// Read the header
			common::ChunkHeader chunkHeader {};
			packStream.seekg(rChunkLocation.uiOffset);
			packStream.read(reinterpret_cast<char*>(&chunkHeader), sizeof(chunkHeader));

			// Add to lazy chunk map. iDataSize is the size of the data in pData after any decompression
			// (i.e., what consumers see). For zlib-compressed chunks, that's the uncompressed size; otherwise
			// it's the on-disk chunk-data size. The on-disk size is always recoverable from `location.uiSize`.
			int64_t iOnDiskSize = rChunkLocation.uiSize - common::kiChunkDataOffset;
			bool bCompressed = chunkHeader.flags & common::ChunkFlags::kZlibCompressed;
			int64_t iDataSize = bCompressed ? chunkHeader.iUncompressedSize : iOnDiskSize;
			auto [it, bInserted] = mLazyChunkMap.try_emplace(rChunkLocation.crc, LazyChunk {.location = rChunkLocation, .header = chunkHeader, .iDataSize = iDataSize});
			if (!bInserted)
			{
				LOG(kLoading, kDebug, "Duplicate chunk CRC {:#018x} found in {}", rChunkLocation.crc, data::kpcDataTypeNames[i]);
				DEBUG_BREAK();
			}

			// Track largest compressed-chunk on-disk size for the loading-thread scratch buffer.
			if (bCompressed && iOnDiskSize > miDecompressScratchSize)
			{
				miDecompressScratchSize = iOnDiskSize;
			}
		}
		// Trust boundary: a truncated pack lets a per-chunk header seek/read run past EOF (failbit) — the headers
		// already emplaced would be garbage. Treat the whole pack as corrupt rather than building a bad pool.
		if (!packStream)
		{
			FailMissingRequiredAsset(mPackFilePaths[i], "pack header table truncated");
		}
	}

	// Pre-allocate memory pool for all lazy chunk data (eliminates heap lock contention during background loading)
	int64_t iPoolOffset = 0;
	for (auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		iPoolOffset += common::RoundUp<int64_t, common::kiAlignmentBytes>(rLazyChunk.iDataSize);
	}
	miLazyPoolSize = iPoolOffset;
	mpLazyPool = static_cast<std::byte*>(VirtualAlloc(nullptr, miLazyPoolSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));

	// Assign each lazy chunk its pre-allocated region in the pool
	iPoolOffset = 0;
	for (auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		rLazyChunk.pData = mpLazyPool + iPoolOffset;
		iPoolOffset += common::RoundUp<int64_t, common::kiAlignmentBytes>(rLazyChunk.iDataSize);
	}

	// Decompress scratch (sized to largest compressed chunk on disk; only allocated if any chunks are compressed)
	if (miDecompressScratchSize > 0)
	{
		mpDecompressScratch = static_cast<std::byte*>(VirtualAlloc(nullptr, miDecompressScratchSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	}

	// Query disk sector size for FILE_FLAG_NO_BUFFERING alignment requirements
	DWORD uiSectorsPerCluster = 0, uiBytesPerSector = 0, uiNumberOfFreeClusters = 0, uiTotalNumberOfClusters = 0;
	GetDiskFreeSpaceW(mDataDirectory.root_path().c_str(), &uiSectorsPerCluster, &uiBytesPerSector, &uiNumberOfFreeClusters, &uiTotalNumberOfClusters);
	miSectorSize = uiBytesPerSector;

	// Open persistent unbuffered handles for lazy pack files
	for (int64_t i = 0; i < data::kDataTypeCount; ++i)
	{
		if (IsEagerChunk(static_cast<data::DataTypes>(i)))
		{
			continue;
		}
#if defined(BT_SERVER)
		if (!IsServerChunk(static_cast<data::DataTypes>(i)))
		{
			continue;
		}
#endif

		mLazyPackFileHandles[i] = CreateFileW(mPackFilePaths[i].c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		// Trust boundary: a missing required .pack yields INVALID_HANDLE_VALUE; storing it unchecked would later
		// spin LoadChunk's sub-read loop forever on 0-byte reads. Same severity as a missing manifest.
		if (mLazyPackFileHandles[i] == INVALID_HANDLE_VALUE)
		{
			FailMissingRequiredAsset(mPackFilePaths[i], "pack file could not be opened for loading");
		}
	}

	// Allocate sector-aligned read buffer (one sub-read + sector padding)
	miReadBufferSize = common::RoundUp(kiSubReadSize + miSectorSize, miSectorSize);
	mpReadBuffer = static_cast<std::byte*>(_aligned_malloc(miReadBufferSize, static_cast<size_t>(miSectorSize)));

	mLoadingFuture = std::async(std::launch::async, [this]()
	{
		common::ThreadLocal threadLocal(0, common::kThreadEagerLoad);

#if defined(BT_CLIENT)
		for (uint32_t i = 0; i < data::kDataTypeCount; ++i)
		{
			if (!IsEagerChunk(static_cast<data::DataTypes>(i)))
			{
				continue;
			}

			std::vector<std::byte>& rPackBytes = mPackFileData[i];

			// If eager loading, read the entire .pack file into memory
			rPackBytes.resize(std::filesystem::file_size(mPackFilePaths[i]));
			std::fstream packStream(mPackFilePaths[i], std::ios::in | std::ios::binary);
			packStream.read(reinterpret_cast<char*>(rPackBytes.data()), rPackBytes.size());
			packStream.close();

			// Process chunks from pack file
			for (const common::ChunkLocation& rChunkLocation : mpChunkLocations[i])
			{
				// Add to eager chunk map
				common::ChunkHeader* pChunkHeader = reinterpret_cast<common::ChunkHeader*>(&rPackBytes[rChunkLocation.uiOffset]);
				ASSERT(pChunkHeader->iMagic == common::ChunkHeader::kiMagic && pChunkHeader->crc == rChunkLocation.crc);
				uint64_t uiDataOffset = rChunkLocation.uiOffset + common::kiChunkDataOffset;

				auto [it, bInserted] = mEagerChunkMap.try_emplace(rChunkLocation.crc, EagerChunk { .pHeader = pChunkHeader, .pData = &rPackBytes[uiDataOffset], .iDataSize = static_cast<int64_t>(rChunkLocation.uiSize - common::kiChunkDataOffset), });
				if (!bInserted)
				{
					LOG(kLoading, kDebug, "Duplicate chunk CRC {:#018x} found in {}", rChunkLocation.crc, data::kpcDataTypeNames[i]);
					DEBUG_BREAK();
				}

				LOG(kLoading, kDebug, "Eager chunk {} \"{}\" size {}", rChunkLocation.crc, std::string_view(pChunkHeader->pcPath), rChunkLocation.uiSize);
			}
		}
#endif

		// Eager map + pack buffers are now fully populated; publish to acquiring readers.
		mbEagerLoadComplete.store(true, std::memory_order_release);

		// Start background loading thread
		mLoadingThread = std::thread(&FileManager::LoadingThread, this);
	});
}

const std::unordered_map<common::crc_t, EagerChunk>& FileManager::GetEagerChunkMap() const
{
	if (mLoadingFuture.valid()) [[unlikely]]
	{
		gpProfileManager->BootStart(kBootTimerWaitForDataFile);
		mLoadingFuture.get();
		gpProfileManager->BootStop(kBootTimerWaitForDataFile);
	}

	return mEagerChunkMap;
}

const std::unordered_map<common::crc_t, LazyChunk>& FileManager::GetLazyChunkMap() const
{
	return mLazyChunkMap;
}

bool FileManager::IsChunkReady(common::crc_t crc) const
{
	// Eager map is async-populated; only assert the not-an-eager-chunk invariant once it is published.
	if (mbEagerLoadComplete.load(std::memory_order_acquire))
	{
		ASSERT(mEagerChunkMap.find(crc) == mEagerChunkMap.end());
	}
	auto it = mLazyChunkMap.find(crc);
	return it != mLazyChunkMap.end() ? it->second.eState.load(std::memory_order_acquire) >= ChunkState::kReady : false;
}

void FileManager::RequestChunkLoad(std::span<const common::crc_t> crcs, LoadPriority ePriority)
{
	bool bAddedAny = false;

	{
		std::unique_lock lock(mQueueMutex);

		for (common::crc_t crc : crcs)
		{
			LazyChunk& rLazyChunk = mLazyChunkMap.at(crc);
			if (rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kDiskLoaded)
			{
				continue;
			}

			if (rLazyChunk.eState.load(std::memory_order_acquire) < ChunkState::kLoadRequested)
			{
				// Heap: priority_queue insertion may allocate. Items must persist until the loading thread pops them,
				//   so a workbuffer (frame-scoped) can't own them, and the queue grows/shrinks unpredictably
				ScopedSuppressAllocationTracking suppress;

				mRequestQueue.push({crc, ePriority});
				rLazyChunk.eState.store(ChunkState::kLoadRequested, std::memory_order_release);
				bAddedAny = true;
			}
		}
	}

	if (bAddedAny)
	{
		mWakeCondition.notify_one();
	}
}

void FileManager::WaitForChunks(std::span<const common::crc_t> crcs)
{
	RequestChunkLoad(crcs, LoadPriority::kRealtime);

	std::unique_lock lock(mQueueMutex);
	mCompletionCondition.wait(lock, [&]
	{
		for (common::crc_t crc : crcs)
		{
			if (mLazyChunkMap.at(crc).eState.load(std::memory_order_acquire) < ChunkState::kReady)
			{
				return false;
			}
		}
		return true;
	});
}

void FileManager::LoadingThread()
{
	common::ThreadLocal threadLocal(0, common::kThreadLazyLoad);
	
	// Note: do NOT use THREAD_MODE_BACKGROUND_BEGIN. That mode sets `IoPriorityVeryLow`, which during
	// app startup (or any contention with OS-level foreground I/O such as Defender, indexing, OneDrive)
	// causes large `ReadFile`s to stall for many seconds behind foreground requests. BELOW_NORMAL keeps
	// the thread out of frame-critical CPU paths without throttling its disk I/O.
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	while (!mShutdown)
	{
		LoadRequest loadRequest {};

		{
			std::unique_lock lock(mQueueMutex);

			if (!mShutdown && mRequestQueue.empty())
			{
				// Wait for requests
				mWakeCondition.wait(lock, [this] { return mShutdown || !mRequestQueue.empty(); });
			}
			
			if (mShutdown)
			{
				break;
			}
			
			loadRequest = mRequestQueue.top();
			mRequestQueue.pop();
		}
		
		LoadChunk(loadRequest);
	}
}

void FileManager::LoadChunk(const LoadRequest& rRequest)
{
	LazyChunk& rLazyChunk = mLazyChunkMap.at(rRequest.crc);

	bool bCompressed = rLazyChunk.header.flags & common::ChunkFlags::kZlibCompressed;

	// Calculate sector-aligned read parameters for unbuffered I/O
	int64_t iFileOffset = rLazyChunk.location.uiOffset + common::kiChunkDataOffset;
	int64_t iOnDiskSize = rLazyChunk.location.uiSize - common::kiChunkDataOffset;
	int64_t iAlignedOffset = common::RoundDown(iFileOffset, miSectorSize);
	int64_t iPrefix = iFileOffset - iAlignedOffset;

	// Compressed chunks read into the scratch and decompress into pData; uncompressed chunks read directly into pData.
	std::byte* pReadDst = bCompressed ? mpDecompressScratch : rLazyChunk.pData;

	// Corrupt chunk header (no type flag): fail the load instead of indexing past the handle array.
	// Mark ready (pool data stays zero-filled) so WaitForChunks callers don't block forever on the chunk.
	data::DataTypes eDataType = DataTypeFromFlags(rLazyChunk.header.flags);
	if (eDataType == data::kDataTypeCount) [[unlikely]]
	{
		LOG(kLoading, kError, "Corrupt chunk header flags for chunk {}", rRequest.crc);
		DEBUG_BREAK();
		rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);
		NotifyChunkCompletion();
		return;
	}

	// Read in sub-chunks, yielding between each to reduce main-thread scheduling latency
	HANDLE hFile = mLazyPackFileHandles[eDataType];
	int64_t iFilePos = iAlignedOffset;
	int64_t iDataCopied = 0;

	while (iDataCopied < iOnDiskSize)
	{
		// Read one sector-aligned sub-chunk from disk
		int64_t iSrcOffset = (iDataCopied == 0) ? iPrefix : 0;
		DWORD uiReadSize = static_cast<DWORD>(common::RoundUp(std::min(kiSubReadSize, iOnDiskSize - iDataCopied) + iSrcOffset, miSectorSize));
		LARGE_INTEGER seekPos {};
		seekPos.QuadPart = iFilePos;
		SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN);
		DWORD uiBytesRead = 0;
		static_cast<void>(ReadFile(hFile, mpReadBuffer, uiReadSize, &uiBytesRead, nullptr));

		int64_t iCopySize = std::min(static_cast<int64_t>(uiBytesRead) - iSrcOffset, iOnDiskSize - iDataCopied);
		// Trust boundary: a truncated/locked .pack can return a 0-byte read (non-positive copy) that never advances
		// iDataCopied — an infinite loop on the loading thread. Fail the chunk soft (ready, pool stays zero-filled).
		if (iCopySize <= 0) [[unlikely]]
		{
			LOG(kLoading, kError, "Truncated read for chunk {}; marking ready zero-filled", rRequest.crc);
			DEBUG_BREAK();
			rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);
			NotifyChunkCompletion();
			return;
		}
		std::byte* pSrc = mpReadBuffer + iSrcOffset;
		std::byte* pDst = pReadDst + iDataCopied;

		if (bCompressed)
		{
			// Compressed reads land in scratch; the decompress pass below will pull them back through cache anyway,
			// so use a regular memcpy (not _mm_stream_si128) so the bytes stay hot for inflate().
			std::memcpy(pDst, pSrc, iCopySize);
		}
		else
		{
			// Non-temporal copy: bypass L3 cache for destination writes (consumed later by upload thread)
			bool bAligned = (reinterpret_cast<uintptr_t>(pSrc) % 16 == 0) && (reinterpret_cast<uintptr_t>(pDst) % 16 == 0);
			if (bAligned)
			{
				int64_t iStreamBytes = iCopySize & ~15LL;
				for (int64_t i = 0; i < iStreamBytes; i += 16)
				{
					_mm_stream_si128(reinterpret_cast<__m128i*>(pDst + i), _mm_loadu_si128(reinterpret_cast<const __m128i*>(pSrc + i)));
				}
				if (iCopySize > iStreamBytes)
				{
					std::memcpy(pDst + iStreamBytes, pSrc + iStreamBytes, iCopySize - iStreamBytes);
				}
			}
			else
			{
				std::memcpy(pDst, pSrc, iCopySize);
			}
			_mm_sfence();
		}

		iDataCopied += iCopySize;
		iFilePos += uiBytesRead;
	}

	if (bCompressed)
	{
		uLongf uiUncompressedSize = static_cast<uLongf>(rLazyChunk.iDataSize);
		int iZlibResult = uncompress(reinterpret_cast<Bytef*>(rLazyChunk.pData), &uiUncompressedSize, reinterpret_cast<const Bytef*>(mpDecompressScratch), static_cast<uLong>(iOnDiskSize));
		// External-data trust boundary: a corrupted .pack or producer/runtime contract drift (e.g. raw payload
		// tagged kZlibCompressed) makes uncompress fail or under-fill. Fail the chunk soft (ready, pool stays
		// zero-filled) instead of throwing on the loading thread, where there is no try/catch to catch it.
		if (iZlibResult != Z_OK || static_cast<int64_t>(uiUncompressedSize) != rLazyChunk.iDataSize) [[unlikely]]
		{
			LOG(kLoading, kError, "Zlib decompress failed for chunk {} (result {})", rRequest.crc, iZlibResult);
			DEBUG_BREAK();
			rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);
			NotifyChunkCompletion();
			return;
		}
	}

	LOG(kLoading, kDebug, "Lazy chunk {} \"{}\" size {}", rRequest.crc, std::string_view(rLazyChunk.header.pcPath), rLazyChunk.location.uiSize);

	// The disk-read bytes in pData are published to ReadChunkData's resident-copy path and the stats
	// readers by the eState release-stores below; that release / acquire pairing — not mQueueMutex — is
	// the memory-visibility contract for lazy chunk data.
#if defined(BT_CLIENT)
	if (rLazyChunk.header.flags & common::ChunkFlags::kTexture)
	{
		// Request GPU upload on the dedicated upload thread (texture only)
		rLazyChunk.eState.store(ChunkState::kUploading, std::memory_order_release);
		gpTextureUploadManager->RequestUpload(rRequest.crc, rRequest.ePriority);
	}
	else
#endif // BT_CLIENT
	{
		// Non-texture chunks are ready immediately after disk load
		rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);
		NotifyChunkCompletion();
	}
}

void FileManager::NotifyChunkCompletion()
{
	std::unique_lock lock(mQueueMutex);
	mCompletionCondition.notify_all();
}

LazyChunk& FileManager::GetLazyChunk(common::crc_t crc)
{
	return mLazyChunkMap.at(crc);
}

void FileManager::ResetTextureChunkStates()
{
	// Wholesale reset for device-loss recovery: restores pool pointers + resets state for every texture chunk.
	ResetTextureChunkStates({});
}

void FileManager::ResetTextureChunkStates(std::span<const common::crc_t> targetCrcs)
{
	// Restore pool pointers for all lazy chunks (ProcessPendingTextures clears pData/iDataSize for adopted textures)
	// Must iterate ALL chunks (not just textures) because pool offsets are cumulative.
	// When targetCrcs is non-empty, only texture chunks in the span get state/handle reset; pool-pointer
	// restoration is idempotent for chunks already pointing at the correct offset, so it is safe to apply
	// to everything. This per-chunk path is used by Phase 5 LRU eviction.
	//
	// Thread-safety precondition (relied upon, NOT enforced here): no other thread may concurrently access a
	// chunk this rewrites. Two classes of concurrent access exist, excluded by ordering, not an in-code guard:
	//   * The transfer thread (TextureUploadManager::UploadThread) writing rLazyChunk.vkImage during a
	//     kUploading chunk's vmaCreateImage — must not be uploading any chunk this nulls.
	//   * The audio fill worker (kThreadStreamingVoiceFill, via StreamingVoice::FillSlot -> ReadChunkData)
	//     reading pData/iDataSize lock-free. The pool-pointer restoration below rewrites those for EVERY
	//     lazy chunk (cumulative offsets), but to identical values for any chunk not being evicted, so a
	//     racing audio read of a non-evicted chunk is benign.
	// Both callers guarantee the transfer-thread exclusion:
	//   * Whole-pool variant (device-loss): runs at Graphics::Destroy after DestroyTransferResources()
	//     has set mbShutdown and joined the transfer thread.
	//   * Scoped per-island variant (LRU eviction): runs inside RenderGlobal's drained descriptor-patch
	//     window, on already-resident (not uploading) chunks.
	// No cheap idle check is reachable from here (mbShutdown is private to TextureUploadManager), so the
	// guarantee is documented rather than asserted. Keep the two callers' transition logic in sync.
	bool bResetAll = targetCrcs.empty();
	int64_t iPoolOffset = 0;
	for (auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		bool bCompressed = rLazyChunk.header.flags & common::ChunkFlags::kZlibCompressed;
		int64_t iOnDiskSize = rLazyChunk.location.uiSize - common::kiChunkDataOffset;
		rLazyChunk.iDataSize = bCompressed ? rLazyChunk.header.iUncompressedSize : iOnDiskSize;
		rLazyChunk.pData = mpLazyPool + iPoolOffset;
		iPoolOffset += common::RoundUp<int64_t, common::kiAlignmentBytes>(rLazyChunk.iDataSize);

		if (!(rLazyChunk.header.flags & common::ChunkFlags::kTexture))
		{
			continue;
		}

		if (!bResetAll && std::find(targetCrcs.begin(), targetCrcs.end(), crc) == targetCrcs.end())
		{
			continue;
		}

		ChunkState eState = rLazyChunk.eState.load(std::memory_order_acquire);

		rLazyChunk.vkImage = VK_NULL_HANDLE;
		rLazyChunk.vmaAllocation = VK_NULL_HANDLE;
		rLazyChunk.vkDeviceMemory = VK_NULL_HANDLE;

		if (eState == ChunkState::kReady)
		{
			// CPU data was cleared, need full reload from disk
			rLazyChunk.eState.store(ChunkState::kNotLoaded, std::memory_order_release);
		}
		else if (eState == ChunkState::kGpuUploadComplete || eState == ChunkState::kUploading)
		{
			// CPU data still valid, just needs re-upload
			rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
#if defined(BT_CLIENT)
			// Maintain the pending-adoption counter: kUploading (uncounted) -> kDiskLoaded (counted) arms it;
			// kGpuUploadComplete -> kDiskLoaded stays adoptable (already counted), so leave it unchanged. Both reset
			// callers run with the upload thread idle (whole-pool: after the join; per-island: in the drained window),
			// and gpTextureUploadManager outlives the device-loss Graphics recreate, so it is always valid here.
			if (eState == ChunkState::kUploading)
			{
				gpTextureUploadManager->NotifyChunkAdoptable();
			}
#endif // BT_CLIENT
		}
	}
}

bool FileManager::ReadChunkData(common::crc_t crc, uint64_t uiOffset, std::span<std::byte> buffer)
{
	// Check eager chunks first. No locking needed: they are read-only once the async eager load publishes
	// mbEagerLoadComplete — skip the lookup until then so a boot-window read can't race the populating task.
	if (mbEagerLoadComplete.load(std::memory_order_acquire))
	{
		auto eagerIt = mEagerChunkMap.find(crc);
		if (eagerIt != mEagerChunkMap.end())
		{
			const EagerChunk& rEagerChunk = eagerIt->second;
			// Use the chunk-table data extent, not pHeader->iSize: a scene chunk's iSize excludes its appended animation section
			int64_t iDataSize = rEagerChunk.iDataSize;

			// Validate read bounds
			if (uiOffset + buffer.size() > static_cast<uint64_t>(iDataSize))
			{
				return false;
			}

			// Copy data from eager chunk
			std::memcpy(buffer.data(), rEagerChunk.pData + uiOffset, buffer.size());
			return true;
		}
	}
	
	// Check lazy chunks
	auto lazyIt = mLazyChunkMap.find(crc);
	if (lazyIt != mLazyChunkMap.end())
	{
		LazyChunk& rLazyChunk = lazyIt->second;
		
		// If chunk is loaded, read from memory. No lock: pData visibility comes from the eState release
		// (LoadChunk) / acquire (here) pair, not mQueueMutex — the lockless writers never take it.
		// pData/iDataSize are stable once kDiskLoaded except under ResetTextureChunkStates, whose
		// drained-window precondition excludes concurrent readers.
		if (rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kDiskLoaded)
		{
			// Validate read bounds
			if (uiOffset + buffer.size() > static_cast<uint64_t>(rLazyChunk.iDataSize))
			{
				return false;
			}

			// Copy data from lazy chunk
			std::memcpy(buffer.data(), rLazyChunk.pData + uiOffset, buffer.size());
			return true;
		}
		
		// Corrupt chunk header (no type flag): fail the read instead of indexing past the path array
		data::DataTypes eDataType = DataTypeFromFlags(rLazyChunk.header.flags);
		if (eDataType == data::kDataTypeCount) [[unlikely]]
		{
			LOG(kLoading, kError, "Corrupt chunk header flags for chunk {}", crc);
			DEBUG_BREAK();
			return false;
		}

		// Chunk not loaded - read directly from pack file
		// This path is used for streaming audio data without loading entire chunk
		std::fstream packStream(mPackFilePaths[eDataType], std::ios::in | std::ios::binary);
		
		if (!packStream.is_open())
		{
			return false;
		}
		
		// Calculate actual data offset in pack file
		int64_t iDataOffset = rLazyChunk.location.uiOffset + common::kiChunkDataOffset;
		int64_t iDataSize = rLazyChunk.location.uiSize - common::kiChunkDataOffset;
		
		// Validate read bounds
		if (uiOffset + buffer.size() > static_cast<uint64_t>(iDataSize))
		{
			packStream.close();
			return false;
		}

		// Seek and read requested data
		packStream.seekg(iDataOffset + uiOffset);
		packStream.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
		bool bSuccess = packStream.good();
		packStream.close();

		return bSuccess;
	}
	
	// Chunk not found
	return false;
}

MemoryStats FileManager::GetEagerStats() const
{
	MemoryStats stats;
	// mPackFileData / mEagerChunkMap are async-populated; report nothing until the eager load publishes.
	if (!mbEagerLoadComplete.load(std::memory_order_acquire))
	{
		return stats;
	}
	for (uint32_t i = 0; i < data::kDataTypeCount; ++i)
	{
		if (IsEagerChunk(static_cast<data::DataTypes>(i)))
		{
			stats.iBytes += static_cast<int64_t>(mPackFileData[i].size());
		}
	}
	stats.iCount = static_cast<int64_t>(mEagerChunkMap.size());
	return stats;
}

MemoryStats FileManager::GetLazyStats() const
{
	MemoryStats stats;
	// No lock: mLazyChunkMap structure is frozen after boot, eState is atomic (acquire), and iDataSize is
	// stable except under ResetTextureChunkStates' drained-window precondition. mQueueMutex never guarded
	// these value reads — the lockless writers never take it.
	for (const auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		if (rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kDiskLoaded)
		{
			stats.iBytes += rLazyChunk.iDataSize;
			++stats.iCount;
		}
	}
	return stats;
}

MemoryStats FileManager::GetMemoryStats(data::DataTypes eDataType) const
{
	MemoryStats stats;

	if (IsEagerChunk(eDataType))
	{
		// mPackFileData is async-populated; report 0 resident bytes until published. mpChunkLocations is set
		// synchronously in LoadPackFiles, so the chunk count is always safe to read.
		stats.iBytes = mbEagerLoadComplete.load(std::memory_order_acquire) ? static_cast<int64_t>(mPackFileData[eDataType].size()) : 0;
		stats.iCount = static_cast<int64_t>(mpChunkLocations[eDataType].size());
	}
	else
	{
		// No lock: see GetLazyStats — mQueueMutex never guarded these value reads.
		for (const auto& [crc, rLazyChunk] : mLazyChunkMap)
		{
			if (DataTypeFromFlags(rLazyChunk.header.flags) == eDataType && rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kDiskLoaded)
			{
				stats.iBytes += rLazyChunk.iDataSize;
				++stats.iCount;
			}
		}
	}
	return stats;
}

void RequestTextureChunkLoad(common::crc_t crc)
{
	gpFileManager->RequestChunkLoad(std::span(&crc, 1));
}

} // namespace engine
