#include "FileManager.h"

#include "Graphics/Graphics.h"
#include "Graphics/AnimationData.h"
#include "Graphics/ComparisonLog.h"
#include "Graphics/Managers/TextureUploadManager.h"
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
	mLogFileStream.open(LogFile(), std::ofstream::out);
	common::gpLogFileStream = &mLogFileStream;
	Log("AppData directory: \"{}\"", mAppDataDirectory.string());

	// Get Windows temp directory and append game name
	char pcDirectory[MAX_PATH] {};
	GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	mTempDirectory = pcDirectory;
	mTempDirectory.append(game::kGameName);
	Log("Temp directory: \"{}\"", mTempDirectory.string());
	std::filesystem::create_directory(mTempDirectory);

	// Get the file path of the executable, the /Data/ folder will be beside it
	GetModuleFileName(nullptr, pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
	mDataDirectory = pcDirectory;
	mDataDirectory.remove_filename();
	mDataDirectory /= "Data";
	Log("Data directory: \"{}\"", mDataDirectory.string());

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

	common::gpLogFileStream = nullptr;

	gpFileManager = nullptr;
}

bool FileManager::Exists(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	return std::filesystem::exists(GetFilePath(rFlags, rFilename));
}

int64_t FileManager::GetFileSize(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	return file_size(GetFilePath(rFlags, rFilename));
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
		common::DebugBreak();
		return "";
	}

	filePath /= rFilename;
	return filePath;
}

std::fstream FileManager::OpenFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	std::filesystem::path file = GetFilePath(rFlags, rFilename);

	if (rFlags & kBackup && std::filesystem::exists(file))
	{
		ASSERT((rFlags & kWrite) != 0);
		std::filesystem::path backupFile(file);
		std::time_t time = std::time(nullptr);
		std::tm timeStruct = *std::localtime(&time);
		std::ostringstream oss;
		oss << std::put_time(&timeStruct, ".%d-%m-%Y-%H-%M-%S");
		std::string timeString = oss.str();
		backupFile += timeString;
		std::filesystem::copy_file(file, backupFile);
	}

	std::fstream fileStream(file, (rFlags & kRead ? std::ios::in : std::ios::out) | std::ios::binary);
	Log("{} \"{}\" at \"{}\"", fileStream.is_open() ? (rFlags & kRead ? "Reading" : "Writing") : "Failed to open", rFilename.string(), file.string());
	return fileStream;
}

void FileManager::RemoveFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	std::filesystem::path file = GetFilePath(rFlags, rFilename);
	Log("Remove \"{}\" at \"{}\"", rFilename.string(), file.string());
	std::filesystem::remove(file);
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

void FileManager::LoadPackFiles()
{
	for (int64_t i = 0; i < data::kDataTypeCount; ++i)
	{
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

		for (const common::ChunkLocation& rChunkLocation : mpChunkLocations[i])
		{
			// Read the header
			common::ChunkHeader chunkHeader {};
			std::fstream packStream(GetDataFilePath(static_cast<data::DataTypes>(i), ".pack"), std::ios::in | std::ios::binary);
			packStream.seekg(rChunkLocation.uiOffset);
			packStream.read(reinterpret_cast<char*>(&chunkHeader), sizeof(chunkHeader));

			// Add to lazy chunk map
			auto [it, bInserted] = mLazyChunkMap.try_emplace(rChunkLocation.crc, LazyChunk {.eDataType = static_cast<data::DataTypes>(i), .location = rChunkLocation, .header = chunkHeader});
			if (!bInserted)
			{
				Log("Duplicate chunk CRC {:#018x} found in {}", rChunkLocation.crc, data::kpcDataTypeNames[i]);
				common::DebugBreak();
			}

		}
	}

	// Pre-allocate memory pool for all lazy chunk data (eliminates heap lock contention during background loading)
	constexpr int64_t iHeaderAlignedSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader)));
	int64_t iPoolOffset = 0;
	for (auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		int64_t iDataSize = rLazyChunk.location.uiSize - iHeaderAlignedSize;
		iPoolOffset += common::RoundUp<int64_t, common::kiAlignmentBytes>(iDataSize);
	}
	miLazyPoolSize = iPoolOffset;
	mpLazyPool = static_cast<byte*>(VirtualAlloc(nullptr, miLazyPoolSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	memset(mpLazyPool, 0, miLazyPoolSize);

	// Assign each lazy chunk its pre-allocated region in the pool
	iPoolOffset = 0;
	for (auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		int64_t iDataSize = rLazyChunk.location.uiSize - iHeaderAlignedSize;
		rLazyChunk.pData = mpLazyPool + iPoolOffset;
		rLazyChunk.iDataSize = iDataSize;
		iPoolOffset += common::RoundUp<int64_t, common::kiAlignmentBytes>(iDataSize);
	}

	// Query disk sector size for FILE_FLAG_NO_BUFFERING alignment requirements
	DWORD sectorsPerCluster = 0, bytesPerSector = 0, numberOfFreeClusters = 0, totalNumberOfClusters = 0;
	GetDiskFreeSpaceW(mDataDirectory.root_path().c_str(), &sectorsPerCluster, &bytesPerSector, &numberOfFreeClusters, &totalNumberOfClusters);
	miSectorSize = bytesPerSector;

	// Open persistent unbuffered handles for lazy pack files
	for (int64_t i = 0; i < data::kDataTypeCount; ++i)
	{
		if (IsEagerChunk(static_cast<data::DataTypes>(i)))
		{
			continue;
		}

		std::filesystem::path packPath = GetDataFilePath(static_cast<data::DataTypes>(i), ".pack");
		mLazyPackFileHandles[i] = CreateFileW(packPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	}

	// Allocate pre-faulted sector-aligned read buffer (one sub-read + sector padding)
	miReadBufferSize = common::RoundUp(kiSubReadSize + miSectorSize, miSectorSize);
	mpReadBuffer = static_cast<byte*>(_aligned_malloc(miReadBufferSize, static_cast<size_t>(miSectorSize)));
	memset(mpReadBuffer, 0, miReadBufferSize);

	mLoadingFuture = std::async(std::launch::async, [this]()
	{
		common::ThreadLocal threadLocal(0, common::kThreadEagerLoad);

		for (uint32_t i = 0; i < data::kDataTypeCount; ++i)
		{
			if (!IsEagerChunk(static_cast<data::DataTypes>(i)))
			{
				continue;
			}

			std::filesystem::path packPath = GetDataFilePath(static_cast<data::DataTypes>(i), ".pack");
			std::vector<byte>& rPackBytes = mPackFileData[i];

			// If eager loading, read the entire .pack file into memory
			rPackBytes.resize(std::filesystem::file_size(packPath));
			std::fstream packStream(packPath, std::ios::in | std::ios::binary);
			packStream.read(reinterpret_cast<char*>(rPackBytes.data()), rPackBytes.size());
			packStream.close();

			// Process chunks from pack file
			for (const common::ChunkLocation& rChunkLocation : mpChunkLocations[i])
			{
				// Add to eager chunk map
				auto pChunkHeader = reinterpret_cast<common::ChunkHeader*>(&rPackBytes[rChunkLocation.uiOffset]);
				ASSERT(pChunkHeader->iMagic == common::ChunkHeader::kiMagic && pChunkHeader->crc == rChunkLocation.crc);
				uint64_t uiDataOffset = rChunkLocation.uiOffset + common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader)));

				auto [it, bInserted] = mEagerChunkMap.try_emplace(rChunkLocation.crc, EagerChunk { .pHeader = pChunkHeader, .pData = &rPackBytes[uiDataOffset], });
				if (!bInserted)
				{
					Log("Duplicate chunk CRC {:#018x} found in {}", rChunkLocation.crc, data::kpcDataTypeNames[i]);
					common::DebugBreak();
				}

				// Log GLTF chunk info for debugging animation loading
				if (pChunkHeader->flags & common::ChunkFlags::kScene)
				{
					Log("GLTF chunk CRC {:#018x}: bHasAnimation={}, uiMaterialCount={}, sizeof(MaterialShaderData)={}", rChunkLocation.crc, pChunkHeader->sceneHeader.bHasAnimation, pChunkHeader->sceneHeader.uiMaterialCount, sizeof(common::MaterialShaderData));
				}

				// Load animation data for GLTF chunks that have it
				if (pChunkHeader->flags & common::ChunkFlags::kScene && pChunkHeader->sceneHeader.bHasAnimation)
				{
					// Animation data comes after the material data (aligned to 16 bytes, matching export)
					int64_t iMaterialDataSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(pChunkHeader->sceneHeader.uiMaterialCount * sizeof(common::MaterialShaderData));
					const byte* pAnimationData = &rPackBytes[uiDataOffset + iMaterialDataSize];
					Log("  Animation data offset: uiDataOffset={} + iMaterialDataSize={} = {}", uiDataOffset, iMaterialDataSize, uiDataOffset + iMaterialDataSize);

					// Initialize comparison logging before Load() so SKELETON_LOAD gets logged
					InitComparisonLog(rChunkLocation.crc);
					if (gbComparisonLoggingEnabled)
					{
						CompLog("PACK_LOAD:");
						CompLog("  crc: %llu", rChunkLocation.crc);
						CompLog("  chunk_path: %s", pChunkHeader->pcPath);
						CompLog("  has_animation: true");
						CompLog("  material_count: %u", pChunkHeader->sceneHeader.uiMaterialCount);
					}

					AnimationData& rAnimData = gAnimationDataMap[rChunkLocation.crc];
					rAnimData.Load(pAnimationData, rChunkLocation.crc);
					Log("Loaded animation data for GLTF CRC {:#018x}: {} nodes, {} skin joints, {} animations", rChunkLocation.crc, rAnimData.GetHeader().skeleton.uiNodeCount, rAnimData.GetHeader().skeleton.uiSkinJointCount, rAnimData.GetHeader().uiAnimationCount);
				}
			}
		}

		// Start background loading thread
		mLoadingThread = std::thread(&FileManager::LoadingThread, this);
	});

	// Queue up any priority loads already set (these vectors may be expanded later and RequestChunkLoad will be called again)
	RequestChunkLoad(Islands::smPriorityIslands, LoadPriority::kRealtime);
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
	return it != mLazyChunkMap.end() ? it->second.eState.load(std::memory_order_acquire) >= ChunkState::kDiskLoaded : false;
}

void FileManager::RequestChunkLoad(std::span<const common::crc_t> crcs, LoadPriority priority)
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
				mRequestQueue.push({crc, priority});
				rLazyChunk.eState.store(ChunkState::kLoadRequested, std::memory_order_release);
				if (rLazyChunk.eDataType == data::DataTypes::kDataTypeTexture)
				{
					Log("FileManager {} kNotLoaded -> kLoadRequested (priority {})", crc, static_cast<uint32_t>(priority));
				}
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
			if (mLazyChunkMap.at(crc).eState.load(std::memory_order_acquire) < ChunkState::kDiskLoaded)
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
			if (mLazyChunkMap.at(loadRequest.crc).eDataType == data::DataTypes::kDataTypeTexture)
			{
				Log("FileManager {} Loading (priority {})", loadRequest.crc, static_cast<uint32_t>(loadRequest.priority));
			}
		}
		
		LoadChunk(loadRequest);
	}
}

void FileManager::LoadChunk(const LoadRequest& rRequest)
{
	LazyChunk& rLazyChunk = mLazyChunkMap.at(rRequest.crc);

	// Calculate sector-aligned read parameters for unbuffered I/O
	constexpr int64_t iDataOffset = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader)));
	int64_t iFileOffset = rLazyChunk.location.uiOffset + iDataOffset;
	int64_t iDataSize = rLazyChunk.location.uiSize - iDataOffset;
	int64_t iAlignedOffset = common::RoundDown(iFileOffset, miSectorSize);
	int64_t iPrefix = iFileOffset - iAlignedOffset;

	// Read in sub-chunks, yielding between each to reduce main-thread scheduling latency
	HANDLE hFile = mLazyPackFileHandles[rLazyChunk.eDataType];
	int64_t iFilePos = iAlignedOffset;
	int64_t iDataCopied = 0;

	while (iDataCopied < iDataSize)
	{
		// Read one sector-aligned sub-chunk from disk
		int64_t iSrcOffset = (iDataCopied == 0) ? iPrefix : 0;
		DWORD uiReadSize = static_cast<DWORD>(common::RoundUp(std::min(kiSubReadSize, iDataSize - iDataCopied) + iSrcOffset, miSectorSize));
		LARGE_INTEGER seekPos {};
		seekPos.QuadPart = iFilePos;
		SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN);
		DWORD uiBytesRead = 0;
		ReadFile(hFile, mpReadBuffer, uiReadSize, &uiBytesRead, nullptr);

		// Non-temporal copy: bypass L3 cache for destination writes
		int64_t iCopySize = std::min(static_cast<int64_t>(uiBytesRead) - iSrcOffset, iDataSize - iDataCopied);
		byte* pSrc = mpReadBuffer + iSrcOffset;
		byte* pDst = rLazyChunk.pData + iDataCopied;
		int64_t iStreamBytes = iCopySize & ~15LL;
		for (int64_t i = 0; i < iStreamBytes; i += 16)
		{
			_mm_stream_si128(reinterpret_cast<__m128i*>(pDst + i), _mm_load_si128(reinterpret_cast<const __m128i*>(pSrc + i)));
		}
		if (iCopySize > iStreamBytes)
		{
			memcpy(pDst + iStreamBytes, pSrc + iStreamBytes, iCopySize - iStreamBytes);
		}

		_mm_sfence();
		iDataCopied += iCopySize;
		iFilePos += uiBytesRead;
	}

	if (rLazyChunk.header.flags & common::ChunkFlags::kTexture)
	{
		// Request GPU upload on the dedicated upload thread (texture only)
		Log("FileManager {} kLoadRequested -> kUploading", rRequest.crc);
		rLazyChunk.eState.store(ChunkState::kUploading, std::memory_order_release);
		gpTextureUploadManager->RequestUpload(rRequest.crc, rRequest.priority);
	}
	else
	{
		// Mark disk loaded and notify waiters
		Log("FileManager {} kLoadRequested -> kDiskLoaded ({}KB)", rRequest.crc, rLazyChunk.iDataSize / 1024);
		rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
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

bool FileManager::ReadChunkData(common::crc_t crc, uint64_t offset, std::span<byte> buffer)
{
	// Check eager chunks first (no locking needed as they're read-only after initialization)
	auto eagerIt = mEagerChunkMap.find(crc);
	if (eagerIt != mEagerChunkMap.end())
	{
		const EagerChunk& rEagerChunk = eagerIt->second;
		int64_t iDataSize = rEagerChunk.pHeader->iSize - common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader)));

		// Validate read bounds
		if (offset + buffer.size() > static_cast<uint64_t>(iDataSize))
		{
			return false;
		}

		// Copy data from eager chunk
		memcpy(buffer.data(), rEagerChunk.pData + offset, buffer.size());
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
				if (offset + buffer.size() > static_cast<uint64_t>(rLazyChunk.iDataSize))
				{
					return false;
				}

				// Copy data from lazy chunk
				memcpy(buffer.data(), rLazyChunk.pData + offset, buffer.size());
				return true;
			}
		}
		
		// Chunk not loaded - read directly from pack file
		// This path is used for streaming audio data without loading entire chunk
		std::filesystem::path packPath = GetDataFilePath(rLazyChunk.eDataType, ".pack");
		std::fstream packStream(packPath, std::ios::in | std::ios::binary);
		
		if (!packStream.is_open())
		{
			return false;
		}
		
		// Calculate actual data offset in pack file
		constexpr int64_t iHeaderSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader)));
		int64_t iDataOffset = rLazyChunk.location.uiOffset + iHeaderSize;
		int64_t iDataSize = rLazyChunk.location.uiSize - iHeaderSize;
		
		// Validate read bounds
		if (offset + buffer.size() > static_cast<uint64_t>(iDataSize))
		{
			packStream.close();
			return false;
		}

		// Seek and read requested data
		packStream.seekg(iDataOffset + offset);
		packStream.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
		packStream.close();
		
		return packStream.good();
	}
	
	// Chunk not found
	return false;
}

int64_t FileManager::GetEagerMemoryBytes() const
{
	int64_t iTotalBytes = 0;
	for (uint32_t i = 0; i < data::kDataTypeCount; ++i)
	{
		if (IsEagerChunk(static_cast<data::DataTypes>(i)))
		{
			iTotalBytes += static_cast<int64_t>(mPackFileData[i].size());
		}
	}
	return iTotalBytes;
}

int64_t FileManager::GetLazyMemoryBytes() const
{
	int64_t iTotalBytes = 0;
	std::unique_lock lock(mQueueMutex);
	for (const auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		if (rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kDiskLoaded)
		{
			iTotalBytes += rLazyChunk.iDataSize;
		}
	}
	return iTotalBytes;
}

int64_t FileManager::GetEagerAllocationCount() const
{
	return static_cast<int64_t>(mEagerChunkMap.size());
}

int64_t FileManager::GetLazyAllocationCount() const
{
	int64_t iCount = 0;
	std::unique_lock lock(mQueueMutex);
	for (const auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		if (rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kDiskLoaded)
		{
			++iCount;
		}
	}
	return iCount;
}

MemoryStats FileManager::GetMemoryStats(data::DataTypes eDataType) const
{
	MemoryStats stats {};
	return stats;

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
			if (rLazyChunk.eDataType == eDataType && rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kDiskLoaded)
			{
				stats.iBytes += rLazyChunk.iDataSize;
				++stats.iCount;
			}
		}
	}
	return stats;
}

} // namespace engine
