#include "ExportJob.h"

#include "FileManager.h"

static int64_t siNextJobId = 0;

ExportJob::ExportJob(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
: miId(siNextJobId++)
, mChunkFlags(rChunkFlags)
, mInputPath(rFile)
{
	if (mInputPath.native().find(gpFileManager->mpInputDirectories[0].native()) != std::string::npos)
	{
		mRelativeDirectory = mInputPath.native().substr(gpFileManager->mpInputDirectories[0].native().size() + 1);
	}
	else
	{
		mRelativeDirectory = mInputPath.native().substr(gpFileManager->mpInputDirectories[1].native().size() + 1);
	}
	mRelativeDirectory.remove_filename();

	mChunkFile = gpFileManager->mTempDirectory;
	mChunkFile /= mRelativeDirectory;
	std::filesystem::create_directories(mChunkFile);
	mChunkFile /= mInputPath.filename();
	mChunkFile += ".chunk";

	mLastModifiedTimeFile = gpFileManager->mTempDirectory;
	mLastModifiedTimeFile /= mRelativeDirectory;
	mLastModifiedTimeFile /= mInputPath.filename();
	mLastModifiedTimeFile += ".txt";

	mRelativeFile = mRelativeDirectory.string() + mInputPath.filename().string();
	mCrc = common::Crc(mRelativeFile);
}

std::tuple<common::ChunkHeader*, std::span<std::byte>> ExportJob::AllocateHeaderAndData(int64_t iDataSize)
{
	int64_t iDataOffset = common::kiChunkDataOffset;
	int64_t iTotalSizeAligned = iDataOffset;
	iTotalSizeAligned += common::RoundUp<int64_t, common::kiAlignmentBytes>(iDataSize);

	mHeaderAndData.resize(iTotalSizeAligned);
	reinterpret_cast<common::ChunkHeader*>(mHeaderAndData.data())->iSize = iDataSize;
	return std::make_tuple(reinterpret_cast<common::ChunkHeader*>(mHeaderAndData.data()), std::span(&mHeaderAndData.at(iDataOffset), mHeaderAndData.size() - iDataOffset));
}

bool ExportJob::CheckDirty(const std::filesystem::path& rPackFile)
{
	// Clean export?
	if (gpFileManager->mbCleanExport)
	{
		mbDirty = true;
		return mbDirty;
	}

	// Does the pack file exist?
	if (!std::filesystem::exists(rPackFile))
	{
		mbDirty = true;
		return mbDirty;
	}

	// Has the input file been modified more recently than the pack file?
	std::filesystem::file_time_type inputFileLastModifiedTime = std::filesystem::last_write_time(mInputPath);
	std::filesystem::file_time_type packFileLastWriteTime = std::filesystem::last_write_time(rPackFile);
	if (inputFileLastModifiedTime > packFileLastWriteTime)
	{
		auto [date, time] = common::FileTimeString(inputFileLastModifiedTime);
		LOG(kDefault, kDebug, "Input file \"{}\" is out of date: {} {}", mInputPath.string(), date, time);
		mbDirty = true;
		return mbDirty;
	}

	// Does the chunk file exist?
	if (!std::filesystem::exists(mChunkFile))
	{
		LOG(kDefault, kDebug, "Chunk file \"{}\" does not exist", mChunkFile.string());
		mbDirty = true;
		return mbDirty;
	}

	// Verify chunk file magic and version
	std::fstream chunkFileStream(mChunkFile, std::ios::in | std::ios::binary);

	int64_t piMagicAndVersion[2] = {};
	chunkFileStream.read(reinterpret_cast<char*>(piMagicAndVersion), sizeof(piMagicAndVersion));
	chunkFileStream.close();

	if (!chunkFileStream || piMagicAndVersion[0] != kiMagic || piMagicAndVersion[1] != GetVersion())
	{
		LOG(kDefault, kWarning, "Chunk file \"{}\" has invalid magic {:#018x} or version {}", mChunkFile.string(), piMagicAndVersion[0], piMagicAndVersion[1]);
		mbDirty = true;
		return mbDirty;
	}

	// Does the last modified time file exist?
	if (!std::filesystem::exists(mLastModifiedTimeFile))
	{
		LOG(kDefault, kDebug, "Last modified time file \"{}\" does not exist", mLastModifiedTimeFile.string());
		mbDirty = true;
		return mbDirty;
	}

	// Compare last modified time
	int64_t iLastModifiedTime = inputFileLastModifiedTime.time_since_epoch().count();
	int64_t iLoadedLastModifiedTime = 0;
	std::fstream lastModifiedTimeFileStream(mLastModifiedTimeFile, std::ios::in | std::ios::binary);
	lastModifiedTimeFileStream.read(reinterpret_cast<char*>(&iLoadedLastModifiedTime), sizeof(iLoadedLastModifiedTime));
	if (!lastModifiedTimeFileStream || iLastModifiedTime != iLoadedLastModifiedTime)
	{
		LOG(kDefault, kDebug, "Last modified time does not match {} != {}", iLastModifiedTime, iLoadedLastModifiedTime);
		mbDirty = true;
		return mbDirty;
	}

	// Has the input file been modified more recently than the chunk file?
	std::filesystem::file_time_type chunkFileLastWriteTime = std::filesystem::last_write_time(mChunkFile);
	if (inputFileLastModifiedTime > chunkFileLastWriteTime)
	{
		auto [date, time] = common::FileTimeString(chunkFileLastWriteTime);
		LOG(kDefault, kDebug, "Chunk file \"{}\" is out of date: {} {}", mChunkFile.string(), date, time);
		mbDirty = true;
		return mbDirty;
	}

	mbDirty = false;
	return mbDirty;
}

std::vector<std::byte>& ExportJob::RunExport()
{
	common::ThreadLocal threadLocal(4 * 1024, miId, false);
	LogIndent(2);

	// Load cached chunk file
	if (!mbDirty)
	{
		int64_t iChunkFileSize = std::filesystem::file_size(mChunkFile);
		int64_t iHeaderAndDataSize = iChunkFileSize - sizeof(kiMagic) - sizeof(int64_t);
		mHeaderAndData.resize(iHeaderAndDataSize);

		std::fstream fileStream(mChunkFile, std::ios::in | std::ios::binary);
		fileStream.seekg(sizeof(kiMagic) + sizeof(int64_t)); // Skip magic and version
		fileStream.read(reinterpret_cast<char*>(mHeaderAndData.data()), mHeaderAndData.size());
		return mHeaderAndData;
	}

	try
	{
		Export();
	}
	catch (...)
	{
		CleanupOnFailure();
		throw;
	}

	std::filesystem::path relativeFile = mRelativeDirectory;
	relativeFile /= mInputPath.filename();

	common::ChunkHeader* pChunkHeader = reinterpret_cast<common::ChunkHeader*>(mHeaderAndData.data());
	pChunkHeader->iMagic = common::ChunkHeader::kiMagic;
	pChunkHeader->crc = common::Crc(relativeFile.string());
	LOG(kDefault, kDebug, "\"{}\" -> {:#018x}", relativeFile.string(), pChunkHeader->crc);
	pChunkHeader->flags = mChunkFlags;
	std::string relativeFileString = common::ToString(relativeFile.native());
	ASSERT(relativeFileString.length() < MAX_PATH);
	std::memcpy(pChunkHeader->pcPath, relativeFileString.c_str(), sizeof(*relativeFileString.c_str()) * relativeFileString.length());

	// Write chunk file with magic and version
	std::fstream fileStream(mChunkFile, std::ios::out | std::ios::binary);
	int64_t piMagicAndVersion[2] = { kiMagic, GetVersion() };
	fileStream.write(reinterpret_cast<char*>(piMagicAndVersion), sizeof(piMagicAndVersion));
	fileStream.write(reinterpret_cast<char*>(mHeaderAndData.data()), mHeaderAndData.size());

	int64_t iLastModifiedTime = std::filesystem::last_write_time(mInputPath).time_since_epoch().count();
	std::fstream lastModifiedTimeFileStream(mLastModifiedTimeFile, std::ios::out | std::ios::binary);
	lastModifiedTimeFileStream.write(reinterpret_cast<char*>(&iLastModifiedTime), sizeof(iLastModifiedTime));

	return mHeaderAndData;
}
