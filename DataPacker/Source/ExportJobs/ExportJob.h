#pragma once

namespace utils
{

struct ChunkHeader;

} // namespace utils

class ExportJob
{
public:

	ExportJob(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile);
	ExportJob(ExportJob&& rToMove) noexcept;
	ExportJob& operator=(ExportJob&& rToMove) noexcept;
	virtual ~ExportJob() = default;

	ExportJob() = delete;
	ExportJob(const ExportJob& rToCopy) = delete;
	ExportJob& operator=(const ExportJob& rToCopy) = delete;

	void RunPreExport();
	bool CheckDirty(bool bCleanExport);
	std::vector<byte>& RunExport();

	int64_t miId = 0;
	bool mbDirty = false;
	common::ChunkFlags_t mChunkFlags;

	std::filesystem::path mInputPath;
	std::filesystem::path mRelativeDirectory;
	std::filesystem::path mChunkFile;

protected:

	virtual void PreExport() {};
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
