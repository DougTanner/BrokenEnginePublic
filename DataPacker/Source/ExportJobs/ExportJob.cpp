#include "ExportJob.h"

#include "FileManager.h"

using enum common::ChunkFlags;

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

std::tuple<common::ChunkHeader*, std::span<byte>> ExportJob::AllocateHeaderAndData(int64_t iDataSize)
{
	int64_t iTotalSizeAligned = common::RoundUp(static_cast<int64_t>(sizeof(common::ChunkHeader)), common::kiAlignmentBytes);
	int64_t iDataOffset = iTotalSizeAligned;
	iTotalSizeAligned += common::RoundUp(iDataSize, common::kiAlignmentBytes);

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
		LOG("Pack file does not exist", rPackFile.string());
		mbDirty = true;
		return mbDirty;
	}

	// Has the input file been modified more recently than the pack file?
	std::filesystem::file_time_type inputFileLastModifiedTime = std::filesystem::last_write_time(mInputPath);
	std::filesystem::file_time_type packFileLastWriteTime = std::filesystem::last_write_time(rPackFile);
	if (inputFileLastModifiedTime > packFileLastWriteTime)
	{
		auto [date, time] = common::FileTimeString(inputFileLastModifiedTime);
		LOG("Input file \"{}\" is out of date: {} {}", mInputPath.string(), date, time);
		mbDirty = true;
		return mbDirty;
	}

	// Does the chunk file exist?
	if (!std::filesystem::exists(mChunkFile))
	{
		LOG("Chunk file \"{}\" does not exist", mChunkFile.string());
		mbDirty = true;
		return mbDirty;
	}

	// Does the last modified time file exist?
	if (!std::filesystem::exists(mLastModifiedTimeFile))
	{
		LOG("Last modified time file \"{}\" does not exist", mLastModifiedTimeFile.string());
		mbDirty = true;
		return mbDirty;
	}

	// Compare last modified time
	std::string lastModifiedTimeString = std::format("{}", inputFileLastModifiedTime);
	std::string loadedLastModifiedTimeString;
	loadedLastModifiedTimeString.resize(std::filesystem::file_size(mLastModifiedTimeFile));
	std::fstream lastModifiedTimeFileStream(mLastModifiedTimeFile, std::ios::in);
	lastModifiedTimeFileStream.read(reinterpret_cast<char*>(loadedLastModifiedTimeString.data()), loadedLastModifiedTimeString.size());
	if (lastModifiedTimeString != loadedLastModifiedTimeString)
	{
		LOG("Last modified time does not match \"{}\" != \"{}\"", lastModifiedTimeString, loadedLastModifiedTimeString);
		mbDirty = true;
		return mbDirty;
	}

	// Has the input file been modified more recently than the chunk file?
	std::filesystem::file_time_type chunkFileLastWriteTime = std::filesystem::last_write_time(mChunkFile);
	if (inputFileLastModifiedTime > chunkFileLastWriteTime)
	{
		auto [pcDate, pcTime] = common::FileTimeString(chunkFileLastWriteTime);
		LOG("Chunk file \"{}\" is out of date: {} {}", mChunkFile.string(), pcDate, pcTime);
		mbDirty = true;
		return mbDirty;
	}

	if (mChunkFlags & kShader)
	{
		std::vector<std::filesystem::path> shaderHeaderFiles;

		std::filesystem::path shaderLayoutsBaseFile(gpFileManager->mpInputDirectories[0]);
		shaderLayoutsBaseFile /= "Shaders/ShaderLayoutsBase.h";
		shaderHeaderFiles.emplace_back(std::move(shaderLayoutsBaseFile));
		std::filesystem::path shaderFunctionsFile(gpFileManager->mpInputDirectories[0]);
		shaderFunctionsFile /= "Shaders/ShaderFunctions.h";
		shaderHeaderFiles.emplace_back(std::move(shaderFunctionsFile));

		std::filesystem::path shaderLayoutsFile(gpFileManager->mpInputDirectories[1]);
		shaderLayoutsFile /= "Shaders/ShaderLayouts.h";
		shaderHeaderFiles.emplace_back(std::move(shaderLayoutsFile));

		for (const std::filesystem::path& rHeaderFile : shaderHeaderFiles)
		{
			std::filesystem::file_time_type headerFileLastWriteTime = std::filesystem::last_write_time(rHeaderFile);
			if (headerFileLastWriteTime > chunkFileLastWriteTime)
			{
				auto [pcDate, pcTime] = common::FileTimeString(chunkFileLastWriteTime);
				LOG("Chunk file \"{}\" is out of date (Shader*.h modified): {} {}", mChunkFile.string(), pcDate, pcTime);
				mbDirty = true;
				return mbDirty;
			}
		}
	}

	mbDirty = false;
	return mbDirty;
}

std::vector<byte>& ExportJob::RunExport()
{
	common::ThreadLocal threadLocal(4 * 1024, miId);
	LOG_INDENT(2);

	if (!mbDirty)
	{
		mHeaderAndData.resize(std::filesystem::file_size(mChunkFile));
		std::fstream fileStream(mChunkFile, std::ios::in | std::ios::binary);
		fileStream.read(reinterpret_cast<char*>(mHeaderAndData.data()), mHeaderAndData.size());
		return mHeaderAndData;
	}

	Export();

	std::filesystem::path relativeFile = mRelativeDirectory;
	relativeFile /= mInputPath.filename();

	common::ChunkHeader* pChunkHeader = reinterpret_cast<common::ChunkHeader*>(mHeaderAndData.data());
	pChunkHeader->iMagic = common::ChunkHeader::kiMagic;
	pChunkHeader->crc = common::Crc(relativeFile.string());
	LOG("\"{}\" -> {:#018x}", relativeFile.string(), pChunkHeader->crc);
	pChunkHeader->flags = mChunkFlags;
	std::string relativeFileString = common::ToString(relativeFile.native());
	ASSERT(relativeFileString.length() < MAX_PATH);
	memcpy(pChunkHeader->pcPath, relativeFileString.c_str(), sizeof(*relativeFileString.c_str()) * relativeFileString.length());

	std::fstream fileStream(mChunkFile, std::ios::out | std::ios::binary);
	fileStream.write(reinterpret_cast<char*>(mHeaderAndData.data()), mHeaderAndData.size());

	std::string lastModifiedTime = std::format("{}", std::filesystem::last_write_time(mInputPath));
	std::fstream lastModifiedTimeFileStream(mLastModifiedTimeFile, std::ios::out);
	lastModifiedTimeFileStream.write(reinterpret_cast<char*>(lastModifiedTime.data()), lastModifiedTime.size());

	return mHeaderAndData;
}
