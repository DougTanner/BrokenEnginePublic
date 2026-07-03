#pragma once

#include "ExportJob.h"

class ExportTexture : public ExportJob
{
public:

	static inline constexpr std::string_view kName = "Texture";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportTexture(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportTexture() = default;

	// Bumped 11 -> 12 for the zlib -> LZ4 texture-chunk payload switch: the per-job .chunk cache stores the
	// encoded chunk, so an encoding change must dirty every cache to force re-export (the DataHeader::kiVersion
	// bump dirties the whole type, but RunExport still streams a per-job-clean .chunk back verbatim otherwise).
	virtual int64_t GetVersion() const override { return Version(12); }

protected:

	virtual void Export() override;

private:

	void ProcessKtxCubemap();
	void ProcessRawTexture(VkFormat vkFormat);
	void ProcessLiveCubemap(VkFormat vkFormat);
	void ProcessRegularTexture(VkFormat vkFormat);
};
