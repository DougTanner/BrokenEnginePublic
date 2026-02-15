#include "ExportJob.h"

#include "FileManager.h"

using enum common::ChunkFlags;

static bool ShaderHeadersChanged()
{
	static bool sbComputed = false;
	static bool sbChanged = false;
	if (sbComputed)
		return sbChanged;
	sbComputed = true;

	// Find most recent modification time across all shader files
	std::filesystem::file_time_type maxWriteTime;
	for (int64_t i = 0; i < 2; ++i)
	{
		std::filesystem::path shadersDir = gpFileManager->mpInputDirectories[i] / "Shaders";
		for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(shadersDir))
		{
			if (!rEntry.is_regular_file())
				continue;
			std::filesystem::file_time_type writeTime = rEntry.last_write_time();
			if (writeTime > maxWriteTime)
				maxWriteTime = writeTime;
		}
	}

	// Compare with stored time
	std::filesystem::path storedTimePath = gpFileManager->mTempDirectory / "ShaderHeadersModifiedTime.bin";
	if (std::filesystem::exists(storedTimePath))
	{
		int64_t iStoredTime = 0;
		std::fstream fileStream(storedTimePath, std::ios::in | std::ios::binary);
		fileStream.read(reinterpret_cast<char*>(&iStoredTime), sizeof(iStoredTime));
		if (maxWriteTime.time_since_epoch().count() <= iStoredTime)
		{
			sbChanged = false;
			return sbChanged;
		}
	}

	// Something changed — update stored time
	int64_t iMaxTime = maxWriteTime.time_since_epoch().count();
	std::fstream fileStream(storedTimePath, std::ios::out | std::ios::binary);
	fileStream.write(reinterpret_cast<char*>(&iMaxTime), sizeof(iMaxTime));

	sbChanged = true;
	return sbChanged;
}

static void CollectShaderIncludes(const std::filesystem::path& rFile, const std::filesystem::path& rIncludeDir0, const std::filesystem::path& rIncludeDir1, std::vector<std::filesystem::path>& rResolvedIncludes)
{
	std::fstream fileStream(rFile, std::ios::in);
	std::string line;
	while (std::getline(fileStream, line))
	{
		size_t uiIncludePos = line.find("#include");
		if (uiIncludePos == std::string::npos)
			continue;

		size_t uiFirstQuote = line.find('"', uiIncludePos);
		if (uiFirstQuote == std::string::npos)
			continue;

		size_t uiSecondQuote = line.find('"', uiFirstQuote + 1);
		if (uiSecondQuote == std::string::npos)
			continue;

		std::string includePath = line.substr(uiFirstQuote + 1, uiSecondQuote - uiFirstQuote - 1);

		// Resolve: relative to file, then includeDir0, then includeDir1
		std::filesystem::path resolved;
		if (std::filesystem::path candidate0 = rFile.parent_path() / includePath; std::filesystem::exists(candidate0))
			resolved = std::filesystem::canonical(candidate0);
		else if (std::filesystem::path candidate1 = rIncludeDir0 / includePath; std::filesystem::exists(candidate1))
			resolved = std::filesystem::canonical(candidate1);
		else if (std::filesystem::path candidate2 = rIncludeDir1 / includePath; std::filesystem::exists(candidate2))
			resolved = std::filesystem::canonical(candidate2);
		else
			continue;

		if (std::find(rResolvedIncludes.begin(), rResolvedIncludes.end(), resolved) != rResolvedIncludes.end())
			continue;

		rResolvedIncludes.push_back(resolved);
		CollectShaderIncludes(resolved, rIncludeDir0, rIncludeDir1, rResolvedIncludes);
	}
}

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

// Move constructor
ExportJob::ExportJob(ExportJob&& rToMove) noexcept
: miId(rToMove.miId)
, mbDirty(rToMove.mbDirty)
, mChunkFlags(std::move(rToMove.mChunkFlags))
, mInputPath(std::move(rToMove.mInputPath))
, mRelativeDirectory(std::move(rToMove.mRelativeDirectory))
, mRelativeFile(std::move(rToMove.mRelativeFile))
, mChunkFile(std::move(rToMove.mChunkFile))
, mLastModifiedTimeFile(std::move(rToMove.mLastModifiedTimeFile))
, mFuture(std::move(rToMove.mFuture))
, mCrc(rToMove.mCrc)
, mHeaderAndData(std::move(rToMove.mHeaderAndData))
{
}

// Move assignment operator
ExportJob& ExportJob::operator=(ExportJob&& rToMove) noexcept
{
	if (this != &rToMove)
	{
		miId = rToMove.miId;
		mbDirty = rToMove.mbDirty;
		mChunkFlags = std::move(rToMove.mChunkFlags);
		mInputPath = std::move(rToMove.mInputPath);
		mRelativeDirectory = std::move(rToMove.mRelativeDirectory);
		mRelativeFile = std::move(rToMove.mRelativeFile);
		mChunkFile = std::move(rToMove.mChunkFile);
		mLastModifiedTimeFile = std::move(rToMove.mLastModifiedTimeFile);
		mFuture = std::move(rToMove.mFuture);
		mCrc = rToMove.mCrc;
		mHeaderAndData = std::move(rToMove.mHeaderAndData);
	}

	return *this;
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
		Log("Input file \"{}\" is out of date: {} {}", mInputPath.string(), date, time);
		mbDirty = true;
		return mbDirty;
	}

	// Does the chunk file exist?
	if (!std::filesystem::exists(mChunkFile))
	{
		Log("Chunk file \"{}\" does not exist", mChunkFile.string());
		mbDirty = true;
		return mbDirty;
	}

	// Verify chunk file magic and version
	std::fstream chunkFileStream(mChunkFile, std::ios::in | std::ios::binary);

	int64_t piMagicAndVersion[2] = {};
	chunkFileStream.read(reinterpret_cast<char*>(piMagicAndVersion), sizeof(piMagicAndVersion));
	chunkFileStream.close();

	if (piMagicAndVersion[0] != kiMagic || piMagicAndVersion[1] != GetVersion())
	{
		Log("Chunk file \"{}\" has invalid magic {:#018x} or version {}", mChunkFile.string(), piMagicAndVersion[0], piMagicAndVersion[1]);
		mbDirty = true;
		return mbDirty;
	}

	// Does the last modified time file exist?
	if (!std::filesystem::exists(mLastModifiedTimeFile))
	{
		Log("Last modified time file \"{}\" does not exist", mLastModifiedTimeFile.string());
		mbDirty = true;
		return mbDirty;
	}

	// Compare last modified time
	int64_t lastModifiedTime = inputFileLastModifiedTime.time_since_epoch().count();
	int64_t loadedLastModifiedTime = 0;
	std::fstream lastModifiedTimeFileStream(mLastModifiedTimeFile, std::ios::in | std::ios::binary);
	lastModifiedTimeFileStream.read(reinterpret_cast<char*>(&loadedLastModifiedTime), sizeof(loadedLastModifiedTime));
	if (lastModifiedTime != loadedLastModifiedTime)
	{
		Log("Last modified time does not match {} != {}", lastModifiedTime, loadedLastModifiedTime);
		mbDirty = true;
		return mbDirty;
	}

	// Has the input file been modified more recently than the chunk file?
	std::filesystem::file_time_type chunkFileLastWriteTime = std::filesystem::last_write_time(mChunkFile);
	if (inputFileLastModifiedTime > chunkFileLastWriteTime)
	{
		auto [pcDate, pcTime] = common::FileTimeString(chunkFileLastWriteTime);
		Log("Chunk file \"{}\" is out of date: {} {}", mChunkFile.string(), pcDate, pcTime);
		mbDirty = true;
		return mbDirty;
	}

	if (mChunkFlags & kShader)
	{
		if (ShaderHeadersChanged())
		{
			std::filesystem::path includeDir0 = gpFileManager->mpInputDirectories[0] / "Shaders";
			std::filesystem::path includeDir1 = gpFileManager->mpInputDirectories[1] / "Shaders";

			std::vector<std::filesystem::path> resolvedIncludes;
			CollectShaderIncludes(mInputPath, includeDir0, includeDir1, resolvedIncludes);

			for (const std::filesystem::path& rHeaderFile : resolvedIncludes)
			{
				std::filesystem::file_time_type headerFileLastWriteTime = std::filesystem::last_write_time(rHeaderFile);
				if (headerFileLastWriteTime > chunkFileLastWriteTime)
				{
					auto [pcDate, pcTime] = common::FileTimeString(chunkFileLastWriteTime);
					Log("Chunk file \"{}\" is out of date (header modified): {} {}", mChunkFile.string(), pcDate, pcTime);
					mbDirty = true;
					return mbDirty;
				}
			}
		}
	}

	mbDirty = false;
	return mbDirty;
}

std::vector<std::byte>& ExportJob::RunExport()
{
	static char spLogBuffer[common::kiLogBufferSize] {};
	static std::vector<std::byte> sWorkbufferMemory(4 * 1024);
	common::ThreadLocal threadLocal(spLogBuffer, sWorkbufferMemory, miId, false);
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
	Log("\"{}\" -> {:#018x}", relativeFile.string(), pChunkHeader->crc);
	pChunkHeader->flags = mChunkFlags;
	std::string relativeFileString = common::ToString(relativeFile.native());
	ASSERT(relativeFileString.length() < MAX_PATH);
	memcpy(pChunkHeader->pcPath, relativeFileString.c_str(), sizeof(*relativeFileString.c_str()) * relativeFileString.length());

	// Write chunk file with magic and version
	std::fstream fileStream(mChunkFile, std::ios::out | std::ios::binary);
	int64_t piMagicAndVersion[2] = { kiMagic, GetVersion() };
	fileStream.write(reinterpret_cast<char*>(piMagicAndVersion), sizeof(piMagicAndVersion));
	fileStream.write(reinterpret_cast<char*>(mHeaderAndData.data()), mHeaderAndData.size());

	int64_t lastModifiedTime = std::filesystem::last_write_time(mInputPath).time_since_epoch().count();
	std::fstream lastModifiedTimeFileStream(mLastModifiedTimeFile, std::ios::out | std::ios::binary);
	lastModifiedTimeFileStream.write(reinterpret_cast<char*>(&lastModifiedTime), sizeof(lastModifiedTime));

	return mHeaderAndData;
}
