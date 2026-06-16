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

	virtual int64_t GetVersion() const override { return Version(9); }

protected:

	virtual void Export() override;

private:

	void ProcessKtxCubemap();
	void ProcessRawTexture(VkFormat vkFormat);
	void ProcessLiveCubemap(VkFormat vkFormat);
	void ProcessRegularTexture(VkFormat vkFormat);
};
