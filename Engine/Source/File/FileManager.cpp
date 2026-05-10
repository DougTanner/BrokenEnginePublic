#include "FileManager.h"

#include "Profile/ProfileManager.h"

#include "Game.h"

#include "Data/Data.h"

namespace engine
{

using enum FileFlags;

FileManager::FileManager()
{
	gpFileManager = this;

	// Get Windows AppData directory and append game name
	PWSTR pWideChar = nullptr;
	SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &pWideChar);
	mAppDataDirectory = pWideChar;
	CoTaskMemFree(pWideChar);
	mAppDataDirectory.append(game::kGameName);
	std::filesystem::create_directory(mAppDataDirectory);
	LOG(kLoading, kDebug, "AppData directory: \"{}\"", mAppDataDirectory.string());

	// Get Windows temp directory and append game name
	char pcDirectory[MAX_PATH] {};
	GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	mTempDirectory = pcDirectory;
	mTempDirectory.append(game::kGameName);
	LOG(kLoading, kDebug, "Temp directory: \"{}\"", mTempDirectory.string());
	std::filesystem::create_directory(mTempDirectory);

	// Get the file path of the executable, the /Data/ folder will be beside it
	GetModuleFileName(nullptr, pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
	mDataDirectory = pcDirectory;
	mDataDirectory.remove_filename();
	mDataDirectory /= "Data";
	LOG(kLoading, kDebug, "Data directory: \"{}\"", mDataDirectory.string());

	LoadPackFiles();
}

FileManager::~FileManager()
{
	// Shutdown background loading thread
	{
		std::unique_lock lock(mQueueMutex);
		mShutdown = true;
	}
	mWakeCondition.notify_one();
	mLoadingThread.join();

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

	gpFileManager = nullptr;
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
		DEBUG_BREAK();
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
	ASSERT(!copyEc);
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
	return data::kDataTypeCount;
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
		ASSERT(dataHeader.iMagic == common::DataHeader::kiMagic && dataHeader.iVersion == common::DataHeader::kiVersion);

		manifestStream.seekg(common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::DataHeader))));
		mpChunkLocations[i].resize(dataHeader.iChunkCount);
		manifestStream.read(reinterpret_cast<char*>(mpChunkLocations[i].data()), dataHeader.iChunkCount * sizeof(common::ChunkLocation));
		manifestStream.close();

		if (IsEagerChunk(static_cast<data::DataTypes>(i)))
		{
			continue;
		}

		std::fstream packStream(mPackFilePaths[i], std::ios::in | std::ios::binary);
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
	}

	// Allocate pre-faulted sector-aligned read buffer (one sub-read + sector padding)
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

				auto [it, bInserted] = mEagerChunkMap.try_emplace(rChunkLocation.crc, EagerChunk { .pHeader = pChunkHeader, .pData = &rPackBytes[uiDataOffset], });
				if (!bInserted)
				{
					LOG(kLoading, kDebug, "Duplicate chunk CRC {:#018x} found in {}", rChunkLocation.crc, data::kpcDataTypeNames[i]);
					DEBUG_BREAK();
				}

				LOG(kLoading, kDebug, "Eager chunk {} \"{}\" size {}", rChunkLocation.crc, std::string_view(pChunkHeader->pcPath), rChunkLocation.uiSize);

				// Log GLTF chunk info for debugging animation loading
				if (pChunkHeader->flags & common::ChunkFlags::kScene)
				{
					LOG(kLoading, kDebug, "GLTF chunk CRC {:#018x}: hasAnimation {}, materialCount {}, sizeof(MaterialShaderData) {}", rChunkLocation.crc, pChunkHeader->sceneHeader.bHasAnimation, pChunkHeader->sceneHeader.uiMaterialCount, sizeof(common::MaterialShaderData));
				}

				// Load animation data for GLTF chunks that have it
				if (pChunkHeader->flags & common::ChunkFlags::kScene && pChunkHeader->sceneHeader.bHasAnimation)
				{
					// Animation data comes after scene arrays and material data (aligned to 16 bytes, matching export)
					// Scene chunk data layout: [textureCrcs ALIGN16] [indexStarts ALIGN16] [MaterialShaderData ALIGN16] [AnimationData]
					int64_t iSceneArraysSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(pChunkHeader->sceneHeader.uiTextureCount * static_cast<int64_t>(sizeof(common::crc_t)))
					                         + common::RoundUp<int64_t, common::kiAlignmentBytes>(pChunkHeader->sceneHeader.uiMaterialCount * static_cast<int64_t>(sizeof(uint32_t)));
					int64_t iMaterialDataSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(pChunkHeader->sceneHeader.uiMaterialCount * static_cast<int64_t>(sizeof(common::MaterialShaderData)));
					[[maybe_unused]] const std::byte* pAnimationData = &rPackBytes[uiDataOffset + iSceneArraysSize + iMaterialDataSize];
					LOG(kLoading, kDebug, "  Animation data offset: dataOffset {} + sceneArraysSize {} + materialDataSize {} = {}", uiDataOffset, iSceneArraysSize, iMaterialDataSize, uiDataOffset + iSceneArraysSize + iMaterialDataSize);

	#if defined(BT_CLIENT)
				AnimationData& rAnimData = gAnimationDataMap.try_emplace(rChunkLocation.crc).first->second;
					rAnimData.Load(pAnimationData, rChunkLocation.crc);
					LOG(kLoading, kDebug, "Loaded animation data for GLTF CRC {:#018x}: {} nodes, {} skin joints, {} animations", rChunkLocation.crc, rAnimData.mHeader.skeleton.uiNodeCount, rAnimData.mHeader.skeleton.uiSkinJointCount, rAnimData.mHeader.uiAnimationCount);
#endif
				}
			}
		}
#endif

		// Start background loading thread
		mLoadingThread = std::thread(&FileManager::LoadingThread, this);
	});

	// Queue up any priority loads already set (these vectors may be expanded later and RequestChunkLoad will be called again)
	RequestChunkLoad(IslandTerrain::smPriorityIslands, LoadPriority::kRealtime);
}

const std::unordered_map<common::crc_t, EagerChunk>& FileManager::GetEagerChunkMap() const
{
	if (gpFileManager->mLoadingFuture.valid()) [[unlikely]]
	{
		gpProfileManager->BootStart(kBootTimerWaitForDataFile);
		gpFileManager->mLoadingFuture.get();
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
	ASSERT(mEagerChunkMap.find(crc) == mEagerChunkMap.end());
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
	
	SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);

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

	// Read in sub-chunks, yielding between each to reduce main-thread scheduling latency
	HANDLE hFile = mLazyPackFileHandles[DataTypeFromFlags(rLazyChunk.header.flags)];
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
		std::byte* pSrc = mpReadBuffer + iSrcOffset;
		std::byte* pDst = pReadDst + iDataCopied;

		if (bCompressed)
		{
			// Compressed reads land in scratch; the decompress pass below will pull them back through cache anyway,
			// so use a regular memcpy (not _mm_stream_si128) so the bytes stay hot for inflate().
			memcpy(pDst, pSrc, iCopySize);
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
					memcpy(pDst + iStreamBytes, pSrc + iStreamBytes, iCopySize - iStreamBytes);
				}
			}
			else
			{
				memcpy(pDst, pSrc, iCopySize);
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
		// External-data trust boundary: a corrupted .pack or producer/runtime contract drift
		// (e.g. raw payload tagged kZlibCompressed) will silently produce garbage texels
		// without this check. Catch it deterministically at chunk-load instead of via visual inspection.
		ASSERT(iZlibResult == Z_OK && static_cast<int64_t>(uiUncompressedSize) == rLazyChunk.iDataSize);
	}

	LOG(kLoading, kDebug, "Lazy chunk {} \"{}\" size {}", rRequest.crc, std::string_view(rLazyChunk.header.pcPath), rLazyChunk.location.uiSize);

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
	// Restore pool pointers for all lazy chunks (ProcessPendingTextures clears pData/iDataSize for adopted textures)
	// Must iterate ALL chunks (not just textures) because pool offsets are cumulative
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
		}
	}
}

bool FileManager::ReadChunkData(common::crc_t crc, uint64_t uiOffset, std::span<std::byte> buffer)
{
	// Check eager chunks first (no locking needed as they're read-only after initialization)
	auto eagerIt = mEagerChunkMap.find(crc);
	if (eagerIt != mEagerChunkMap.end())
	{
		const EagerChunk& rEagerChunk = eagerIt->second;
		int64_t iDataSize = rEagerChunk.pHeader->iSize - common::kiChunkDataOffset;

		// Validate read bounds
		if (uiOffset + buffer.size() > static_cast<uint64_t>(iDataSize))
		{
			return false;
		}

		// Copy data from eager chunk
		memcpy(buffer.data(), rEagerChunk.pData + uiOffset, buffer.size());
		return true;
	}
	
	// Check lazy chunks
	auto lazyIt = mLazyChunkMap.find(crc);
	if (lazyIt != mLazyChunkMap.end())
	{
		LazyChunk& rLazyChunk = lazyIt->second;
		
		// If chunk is loaded, read from memory
		{
			std::unique_lock lock(mQueueMutex);
			if (rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kDiskLoaded)
			{
				// Validate read bounds
				if (uiOffset + buffer.size() > static_cast<uint64_t>(rLazyChunk.iDataSize))
				{
					return false;
				}

				// Copy data from lazy chunk
				memcpy(buffer.data(), rLazyChunk.pData + uiOffset, buffer.size());
				return true;
			}
		}
		
		// Chunk not loaded - read directly from pack file
		// This path is used for streaming audio data without loading entire chunk
		std::fstream packStream(mPackFilePaths[DataTypeFromFlags(rLazyChunk.header.flags)], std::ios::in | std::ios::binary);
		
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
	std::unique_lock lock(mQueueMutex);
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
		stats.iBytes = static_cast<int64_t>(mPackFileData[eDataType].size());
		stats.iCount = static_cast<int64_t>(mpChunkLocations[eDataType].size());
	}
	else
	{
		std::unique_lock lock(mQueueMutex);
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
