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
}

FileManager::~FileManager()
{
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
		common::ThreadLocal threadLocal(0, common::kThreadDataFile);

		for (uint32_t i = 0; i < data::kDataTypeCount; ++i)
		{
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

			// Load chunk data from pack file
			std::vector<byte>& rPackBytes = mPackFiles[i];
			rPackBytes.resize(std::filesystem::file_size(packPath));
			std::fstream packStream(packPath, std::ios::in | std::ios::binary);
			packStream.read(reinterpret_cast<char*>(rPackBytes.data()), rPackBytes.size());
			packStream.close();

			// Process chunks from pack file
			for (const auto& rChunkLocation : chunkLocations)
			{
				auto pChunkHeader = reinterpret_cast<common::ChunkHeader*>(&rPackBytes[rChunkLocation.uiOffset]);
				ASSERT(pChunkHeader->iMagic == common::ChunkHeader::kiMagic && pChunkHeader->crc == rChunkLocation.crc);
				uint64_t uiDataOffset = rChunkLocation.uiOffset + common::RoundUp(static_cast<int64_t>(sizeof(common::ChunkHeader)), common::kiAlignmentBytes);

				// Add to unified chunk map
				auto [it, bInserted] = mChunkMap.try_emplace(pChunkHeader->crc, Chunk { .pHeader = pChunkHeader, .pData = &rPackBytes[uiDataOffset], });
				if (!bInserted)
				{
					LOG("Duplicate chunk CRC {:#018x} found in {}", pChunkHeader->crc, packPath.filename().string());
					DEBUG_BREAK();
				}
			}
		}
	});
}

std::unordered_map<common::crc_t, Chunk>& FileManager::GetChunkMap()
{
	BOOT_TIMER_START(kBootTimerWaitForDataFile);
	if (gpFileManager->mLoadingFuture.valid())
	{
		gpFileManager->mLoadingFuture.get();
	}
	BOOT_TIMER_STOP(kBootTimerWaitForDataFile);

	return mChunkMap;
}

} // namespace engine
