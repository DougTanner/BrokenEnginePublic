#include "FileManager.h"

#include "Profile/ProfileManager.h"

#include "Game.h"

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
	mAppDataDirectory.append(game::kpcGameName);
	std::filesystem::create_directory(mAppDataDirectory);
	mLogFileStream.open(LogFile(), std::ofstream::out);
	common::gpLogFileStream = &mLogFileStream;
	LOG("AppData directory: \"{}\"", mAppDataDirectory.string());

	// Get Windows temp directory and append game name
	char pcDirectory[MAX_PATH] {};
	GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	mTempDirectory = pcDirectory;
	mTempDirectory.append(game::kpcGameName);
	LOG("Temp directory: \"{}\"", mTempDirectory.string());
	std::filesystem::create_directory(mTempDirectory);

	// Get the file path of the executable, the /Data/ folder will be beside it
	GetModuleFileName(nullptr, pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
	mDataDirectory = pcDirectory;
	mDataDirectory.remove_filename();
	mDataDirectory /= "Data";
	LOG("Data directory: \"{}\"\n", mDataDirectory.string());

	LoadPackFiles();
	
	// Start background loading thread
	mLoaderThread = std::thread(&FileManager::LoaderThreadFunc, this);
}

FileManager::~FileManager()
{
	// Shutdown background loading thread
	{
		std::unique_lock lock(mQueueMutex);
		mShutdown = true;
	}
	mWakeCondition.notify_one();
	mLoaderThread.join();
	
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
		DEBUG_BREAK();
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
	LOG("{} \"{}\" at \"{}\"", fileStream.is_open() ? (rFlags & kRead ? "Reading" : "Writing") : "Failed to open", rFilename.string(), file.string());
	return fileStream;
}

void FileManager::RemoveFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	std::filesystem::path file = GetFilePath(rFlags, rFilename);
	LOG("Remove \"{}\" at \"{}\"", rFilename.string(), file.string());
	std::filesystem::remove(file);
}

void FileManager::LoadPackFiles()
{
	mLoadingFuture = std::async(std::launch::async, [this]()
	{
		common::ThreadLocal threadLocal(0, common::kThreadEagerLoad);

		for (uint32_t i = 0; i < data::kDataTypeCount; ++i)
		{
			std::vector<byte>& rPackBytes = mPackFiles[i];

			std::filesystem::path manifestPath = mDataDirectory / (std::string(data::kpcDataTypeNames[i]) + ".manifest");
			std::filesystem::path packPath = mDataDirectory / (std::string(data::kpcDataTypeNames[i]) + ".pack");

			// Read chunk locations from manifest
			std::fstream manifestStream(manifestPath, std::ios::in | std::ios::binary);
			common::DataHeader dataHeader {};
			manifestStream.read(reinterpret_cast<char*>(&dataHeader), sizeof(dataHeader));
			ASSERT(dataHeader.iMagic == common::DataHeader::kiMagic && dataHeader.iVersion == common::DataHeader::kiVersion);

			std::vector<common::ChunkLocation> chunkLocations(dataHeader.iChunkCount);
			manifestStream.seekg(common::RoundUp(static_cast<int64_t>(sizeof(common::DataHeader)), common::kiAlignmentBytes));
			manifestStream.read(reinterpret_cast<char*>(chunkLocations.data()), dataHeader.iChunkCount * sizeof(common::ChunkLocation));
			manifestStream.close();

			// Determine if this data type should be eager or lazy loaded
			bool bEagerLoad = i == data::kDataTypeFont || i == data::kDataTypeGltf || i == data::kDataTypeIslands || i == data::kDataTypeModel || data::kDataTypeShader;
			if (bEagerLoad)
			{
				// Load chunk data from pack file
				rPackBytes.resize(std::filesystem::file_size(packPath));
				std::fstream packStream(packPath, std::ios::in | std::ios::binary);
				packStream.read(reinterpret_cast<char*>(rPackBytes.data()), rPackBytes.size());
				packStream.close();
			}

			// Process chunks from pack file
			for (const auto& rChunkLocation : chunkLocations)
			{
				if (bEagerLoad)
				{
					// Add to eager chunk map
					auto pChunkHeader = reinterpret_cast<common::ChunkHeader*>(&rPackBytes[rChunkLocation.uiOffset]);
					ASSERT(pChunkHeader->iMagic == common::ChunkHeader::kiMagic && pChunkHeader->crc == rChunkLocation.crc);
					uint64_t uiDataOffset = rChunkLocation.uiOffset + common::RoundUp(static_cast<int64_t>(sizeof(common::ChunkHeader)), common::kiAlignmentBytes);

					auto [it, bInserted] = mEagerChunkMap.try_emplace(rChunkLocation.crc, EagerChunk { .pHeader = pChunkHeader, .pData = &rPackBytes[uiDataOffset], });
					if (!bInserted)
					{
						LOG("Duplicate chunk CRC {:#018x} found in {}", rChunkLocation.crc, packPath.filename().string());
						DEBUG_BREAK();
					}
				}
				else
				{
					// Add to lazy chunk map (just location for now)
					auto [it, bInserted] = mLazyChunkMap.try_emplace(rChunkLocation.crc, LazyChunk { .eDataType = static_cast<data::DataTypes>(i), .chunkLocation = rChunkLocation, });
					if (!bInserted)
					{
						LOG("Duplicate chunk CRC {:#018x} found in {}", rChunkLocation.crc, packPath.filename().string());
						DEBUG_BREAK();
					}
				}
			}
		}
	});
}

const std::unordered_map<common::crc_t, EagerChunk>& FileManager::GetEagerChunkMap() const
{
	if (gpFileManager->mLoadingFuture.valid())
	{
		BOOT_TIMER_START(kBootTimerWaitForDataFile);
		gpFileManager->mLoadingFuture.get();
		BOOT_TIMER_STOP(kBootTimerWaitForDataFile);
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
	return it != mLazyChunkMap.end() ? it->second.bLoaded == true : false;
}

void FileManager::RequestChunkLoad(common::crc_t crc, LoadPriority priority)
{
	LazyChunk& rLazyChunk = mLazyChunkMap.at(crc);
	if (rLazyChunk.bLoaded)
	{
		return;
	}
	
	// Add to request queue if it does not exist
	bool bAdded = false;

	{
		std::unique_lock lock(mQueueMutex);

		if (!rLazyChunk.bLoadRequested)
		{
			LOG("Lazy loading chunk CRC {:#018x}", crc);
			mRequestQueue.push({crc, priority});
			rLazyChunk.bLoadRequested = true;
			bAdded = true;
		}
	}

	if (bAdded)
	{
		mWakeCondition.notify_one();
	}
}

void FileManager::LoaderThreadFunc()
{
	common::ThreadLocal threadLocal(0, common::kThreadLazyLoad);
	
	while (!mShutdown)
	{
		LoadRequest request;

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
			
			request = mRequestQueue.top();
			mRequestQueue.pop();
		}
		
		LoadChunk(request);
	}
}

void FileManager::LoadChunk(const LoadRequest& rRequest)
{
	LazyChunk& rLazyChunk = mLazyChunkMap.at(rRequest.crc);

	// Load the chunk header and data from the pack file
	const common::ChunkLocation& rChunkLocation = rLazyChunk.chunkLocation;
	std::vector<byte>& rPackBytes = mPackFiles[rLazyChunk.eDataType];
	
	// If pack file not loaded yet (lazy types start empty), load it now
	if (rPackBytes.empty())
	{
		std::filesystem::path packPath = mDataDirectory / (std::string(data::kpcDataTypeNames[rLazyChunk.eDataType]) + ".pack");
		rPackBytes.resize(std::filesystem::file_size(packPath));
		std::fstream packStream(packPath, std::ios::in | std::ios::binary);
		packStream.read(reinterpret_cast<char*>(rPackBytes.data()), rPackBytes.size());
		packStream.close();
	}
	
	// Copy the chunk header from the pack file
	const common::ChunkHeader* pPackChunkHeader = reinterpret_cast<const common::ChunkHeader*>(&rPackBytes[rChunkLocation.uiOffset]);
	ASSERT(pPackChunkHeader->iMagic == common::ChunkHeader::kiMagic && pPackChunkHeader->crc == rChunkLocation.crc);
	rLazyChunk.header = *pPackChunkHeader;
	
	// Calculate data offset (header is aligned to kiAlignmentBytes)
	uint64_t uiDataOffset = rChunkLocation.uiOffset + common::RoundUp(static_cast<int64_t>(sizeof(common::ChunkHeader)), common::kiAlignmentBytes);
	uint64_t uiDataSize = rChunkLocation.uiSize - common::RoundUp(static_cast<int64_t>(sizeof(common::ChunkHeader)), common::kiAlignmentBytes);
	
	// Copy the chunk data from the pack file
	rLazyChunk.data.resize(uiDataSize);
	std::memcpy(rLazyChunk.data.data(), &rPackBytes[uiDataOffset], uiDataSize);

	{
		std::unique_lock lock(mQueueMutex);
		rLazyChunk.bLoaded = true;
	}

	LOG("  Lazy loaded chunk CRC {:#018x}", rRequest.crc);
}

} // namespace engine
