#include "FileManager.h"

#include "Profile/ProfileManager.h"

#include "Game.h"

namespace engine
{

using enum FileFlags;

FileManager::FileManager()
{
	gpFileManager = this;

	PWSTR pWideChar = nullptr;
	SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &pWideChar);
	mAppDataDirectory = pWideChar;
	CoTaskMemFree(pWideChar);
	mAppDataDirectory.append(game::kpcGameName);
	std::filesystem::create_directory(mAppDataDirectory);
	mLogFileStream.open(LogFile(), std::ofstream::out);
	common::gpLogFileStream = &mLogFileStream;
	LOG("AppData directory: \"{}\"", mAppDataDirectory.string());

	char pcDirectory[MAX_PATH] {};
	GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	mTempDirectory = pcDirectory;
	mTempDirectory.append(game::kpcGameName);
	LOG("Temp directory: \"{}\"", mTempDirectory.string());
	std::filesystem::create_directory(mTempDirectory);

	GetCurrentDirectory(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	mDataFile = pcDirectory;
	mDataFile.append(common::kpcDataFilename);
	mTexturesFile = pcDirectory;
	mTexturesFile.append(common::kpcTexturesFilename);
	if (!std::filesystem::exists(mDataFile) || !std::filesystem::exists(mTexturesFile))
	{
		mDataFile = pcDirectory;
		mDataFile.append("Output");
		mDataFile.append(common::kpcDataFilename);
		if (!std::filesystem::exists(mDataFile))
		{
			throw std::exception("Cannot find Data.bin");
		}

		mTexturesFile = pcDirectory;
		mTexturesFile.append("Output");
		mTexturesFile.append(common::kpcTexturesFilename);
		if (!std::filesystem::exists(mTexturesFile))
		{
			throw std::exception("Cannot find Textures.bin");
		}
	}
	LOG("Data file path: \"{}\"", mDataFile.string());
	LOG("Textures file path: \"{}\"\n", mTexturesFile.string());

	ReadDataFile();
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
		auto t = std::time(nullptr);
		auto tm = *std::localtime(&t);
		std::ostringstream oss;
		oss << std::put_time(&tm, ".%d-%m-%Y-%H-%M-%S");
		auto str = oss.str();
		backupFile += str;
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

void ReadChunkFile(const std::filesystem::path& rDataFile, std::vector<byte>& rData, std::unordered_map<common::crc_t, Chunk>& rDataChunkMap)
{
	rData.resize(std::filesystem::file_size(rDataFile));
	std::fstream fileStream(rDataFile, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(rData.data()), rData.size());

	int64_t iCurrentPosition = 0;
	auto pDataHeader = reinterpret_cast<common::DataHeader*>(&rData[iCurrentPosition]);
	iCurrentPosition += common::RoundUp(static_cast<int64_t>(sizeof(common::DataHeader)), common::kiAlignmentBytes);
	ASSERT(pDataHeader->iMagic == common::DataHeader::kiMagic);
	ASSERT(pDataHeader->iVersion == common::DataHeader::kiVersion);

	LOG("Found {} chunks in the data file", pDataHeader->iChunkCount);
	for (int64_t i = 0; i < pDataHeader->iChunkCount; ++i)
	{
		auto pChunkHeader = reinterpret_cast<common::ChunkHeader*>(&rData[iCurrentPosition]);
		// LOG("  {}, {}: ({:#018x}) {:#018x} {}", i, iCurrentPosition, pChunkHeader->crc, pChunkHeader->flags.muiUnderlying, pChunkHeader->iSize);
		iCurrentPosition += common::RoundUp(static_cast<int64_t>(sizeof(common::ChunkHeader)), common::kiAlignmentBytes);
		ASSERT(pChunkHeader->iMagic == common::ChunkHeader::kiMagic);

		Chunk chunk
		{
			.pHeader = pChunkHeader,
			.pData = &rData[iCurrentPosition],
		};
		iCurrentPosition += common::RoundUp(pChunkHeader->iSize, common::kiAlignmentBytes);

		auto [it, bInserted] = rDataChunkMap.try_emplace(pChunkHeader->crc, std::move(chunk));
		ASSERT(bInserted);
	}
}

void FileManager::ReadDataFile()
{
	mDataFuture = std::async(std::launch::async, [this]()
	{
		common::ThreadLocal threadLocal(0, common::kThreadDataFile);
		ReadChunkFile(mDataFile, mDataBytes, mDataChunkMap);

		mTexturesFuture = std::async(std::launch::async, [this]()
		{
			common::ThreadLocal threadLocal(0, common::kThreadTexturesFile);
			ReadChunkFile(mTexturesFile, mTexturesBytes, mTexturesChunkMap);
		});
	});
}

std::unordered_map<common::crc_t, Chunk>& FileManager::GetDataChunkMap()
{
	BOOT_TIMER_START(kBootTimerWaitForDataFile);
	if (gpFileManager->mDataFuture.valid())
	{
		gpFileManager->mDataFuture.get();
	}
	BOOT_TIMER_STOP(kBootTimerWaitForDataFile);

	return mDataChunkMap;
}

std::unordered_map<common::crc_t, Chunk>& FileManager::GetTexturesChunkMap()
{
	BOOT_TIMER_START(kBootTimerWaitForTexturesFile);
	if (gpFileManager->mTexturesFuture.valid())
	{
		gpFileManager->mTexturesFuture.get();
	}
	BOOT_TIMER_STOP(kBootTimerWaitForTexturesFile);

	return mTexturesChunkMap;
}

} // namespace engine
