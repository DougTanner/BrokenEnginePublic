#pragma once

namespace utils
{

struct ChunkHeader;

} // namespace utils

class ExportJob
{
public:

	// Version magic number for export format validation
	static constexpr int64_t kiMagic = 0xDA7ACCCC;

	ExportJob(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile);
	ExportJob(ExportJob&& rToMove) noexcept;
	ExportJob& operator=(ExportJob&& rToMove) noexcept;
	virtual ~ExportJob() = default;

	ExportJob() = delete;
	ExportJob(const ExportJob& rToCopy) = delete;
	ExportJob& operator=(const ExportJob& rToCopy) = delete;

	bool CheckDirty(const std::filesystem::path& rPackFile);
	std::vector<byte>& RunExport();

	// Get export format version for this job type
	virtual int64_t GetVersion() const = 0;

	int64_t miId = 0;
	bool mbDirty = false;
	common::ChunkFlags_t mChunkFlags;

	std::filesystem::path mInputPath;
	std::filesystem::path mRelativeDirectory;
	std::string mRelativeFile;
	std::filesystem::path mChunkFile;
	std::filesystem::path mLastModifiedTimeFile;

	std::future<std::vector<byte>&> mFuture;

	common::crc_t mCrc = 0;

protected:

	virtual void Export() = 0;

	std::tuple<common::ChunkHeader*, std::span<byte>> AllocateHeaderAndData(int64_t iDataSize);

	std::vector<byte> mHeaderAndData;
};

#include "ExportAudio.h"
#include "ExportFont.h"
#include "ExportGltf.h"
#include "ExportIsland.h"
#include "ExportModel.h"
#include "ExportShader.h"
#include "ExportTexture.h"
