#include "ExportJob.h"

#include "FileManager.h"

using namespace DirectX;

using enum common::ChunkFlags;

int64_t giNextJobId = 0;

ExportJob::ExportJob(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
: miId(giNextJobId++)
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
	std::filesystem::path chunkFilename(mInputPath.filename());
	chunkFilename.concat(".chunk");
	mChunkFile /= chunkFilename;
}

ExportJob::ExportJob(ExportJob&& rToMove) noexcept
{
	*this = std::move(rToMove);
}

ExportJob& ExportJob::operator=(ExportJob&& rToMove) noexcept
{
	miId = rToMove.miId;
	mbDirty = rToMove.mbDirty;
	mChunkFlags = std::move(rToMove.mChunkFlags);

	mInputPath = std::move(rToMove.mInputPath);
	mRelativeDirectory = std::move(rToMove.mRelativeDirectory);

	mChunkFile = std::move(rToMove.mChunkFile);

	mHeaderAndData = std::move(rToMove.mHeaderAndData);

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

bool ExportJob::CheckDirty(bool bCleanExport)
{
	// Clean export?
	if (bCleanExport)
	{
		mbDirty = true;
		return mbDirty;
	}

	// Has the input file been modified more recently than the chunk file?
	std::filesystem::file_time_type inputFileLastWriteTime = std::filesystem::last_write_time(mInputPath);
	std::filesystem::file_time_type compareLastWriteTime = mChunkFlags & kTexture ? gpFileManager->mTexturesFileLastWriteTime : gpFileManager->mDataFileLastWriteTime;
	if (inputFileLastWriteTime > compareLastWriteTime)
	{
		auto [date, time] = common::FileTimeString(inputFileLastWriteTime);
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

	// Has the input file been modified more recently than the chunk file?
	std::filesystem::file_time_type chunkFileLastWriteTime = std::filesystem::last_write_time(mChunkFile);
	if (inputFileLastWriteTime > chunkFileLastWriteTime)
	{
		auto [pcDate, pcTime] = common::FileTimeString(chunkFileLastWriteTime);
		LOG("Chunk file \"{}\" is out of date: {} {}", mChunkFile.string(), pcDate, pcTime);
		mbDirty = true;
		return mbDirty;
	}

	if (mChunkFlags & kShaderCompute || mChunkFlags & kShaderFragment || mChunkFlags & kShaderVertex)
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

void ExportJob::RunPreExport()
{
	common::ThreadLocal threadLocal(4 * 1024, miId);

	LOG_INDENT(2);

	PreExport();
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

	return mHeaderAndData;
}
