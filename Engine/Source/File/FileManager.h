#pragma once

namespace engine
{

#if defined(BT_DEBUG)
inline constexpr char kpcLogFile[] = "Debug.txt";
#elif defined(BT_PROFILE)
inline constexpr char kpcLogFile[] = "Profile.txt";
#elif defined(BT_RELEASE)
inline constexpr char kpcLogFile[] = "Release.txt";
#endif

enum class FileFlags : uint64_t
{
	kAppDataDirectory = 0x01,
	kTempDirectory    = 0x02,

	kRead  = 0x08,
	kWrite = 0x10,
		kBackup = 0x20,
};
using FileFlags_t = common::Flags<FileFlags>;

struct Chunk
{
	common::ChunkHeader* pHeader = nullptr;
	byte* pData = nullptr;
};

class FileManager
{
public:

	FileManager();
	~FileManager();

	bool Exists(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	int64_t GetFileSize(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	std::fstream OpenFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	void RemoveFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);

	std::filesystem::path LogFile()
	{
		std::filesystem::path logFile(mAppDataDirectory);
		logFile /= std::string("Log.") + kpcLogFile;
		return logFile;
	}

	void WriteLogFile(std::ofstream& rOfstream)
	{
		std::ifstream logFileStream(LogFile(), std::ofstream::in);
		rOfstream << logFileStream.rdbuf() << std::flush;
	}

	std::unordered_map<common::crc_t, Chunk>& GetDataChunkMap();
	std::unordered_map<common::crc_t, Chunk>& GetTexturesChunkMap();

	std::future<void> mDataFuture;
	std::future<void> mTexturesFuture;

private:

	void ReadDataFile();
	std::filesystem::path GetFilePath(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);

	std::filesystem::path mAppDataDirectory;
	std::filesystem::path mTempDirectory;

	std::filesystem::path mDataFile;
	std::vector<byte> mDataBytes;
	std::unordered_map<common::crc_t, Chunk> mDataChunkMap;

	std::filesystem::path mTexturesFile;
	std::vector<byte> mTexturesBytes;
	std::unordered_map<common::crc_t, Chunk> mTexturesChunkMap;

	std::ofstream mLogFileStream;
};

inline FileManager* gpFileManager = nullptr;

template <typename STRUCT_TYPE>
bool ExistsVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	if (!gpFileManager->Exists(rFlags, rFilename))
	{
		return false;
	}

	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);
	int64_t iVersion = 0;
	fileStream.read(reinterpret_cast<char*>(&iVersion), sizeof(iVersion));
	int64_t iSize = 0;
	fileStream.read(reinterpret_cast<char*>(&iSize), sizeof(iSize));

	return iVersion == STRUCT_TYPE::kiVersion && iSize == sizeof(STRUCT_TYPE);
}

template <typename STRUCT_TYPE>
void WriteVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, STRUCT_TYPE& rStructure)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);
	int64_t iVersion = STRUCT_TYPE::kiVersion;
	fileStream.write(reinterpret_cast<char*>(&iVersion), sizeof(iVersion));
	int64_t iSize = sizeof(STRUCT_TYPE);
	fileStream.write(reinterpret_cast<char*>(&iSize), sizeof(iSize));
	LOG("Write iSize: {}", iSize);
	fileStream.write(reinterpret_cast<char*>(&rStructure), sizeof(rStructure));
}

template <typename STRUCT_TYPE>
bool ReadVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, STRUCT_TYPE& rStructure)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);

	int64_t iVersion = 0;
	fileStream.read(reinterpret_cast<char*>(&iVersion), sizeof(iVersion));
	int64_t iSize = 0;
	fileStream.read(reinterpret_cast<char*>(&iSize), sizeof(iSize));
	LOG("    iVersion: {} == {} iSize: {} == {}", iVersion, STRUCT_TYPE::kiVersion, iSize, sizeof(STRUCT_TYPE));
	if (iVersion == STRUCT_TYPE::kiVersion && iSize == sizeof(STRUCT_TYPE))
	{
		int64_t iBytesRead = fileStream.read(reinterpret_cast<char*>(&rStructure), sizeof(rStructure)).gcount();
		int64_t iExpectedBytes = sizeof(STRUCT_TYPE);
		return iBytesRead == iExpectedBytes;
	}

	LOG("    Failed to load versioned file");

	if (iVersion == STRUCT_TYPE::kiVersion && iSize != sizeof(STRUCT_TYPE))
	{
		// If this is hit, Frame::kiVersion might be missing a sub-version
		DEBUG_BREAK();
	}

	return false;
}

} // namespace engine
