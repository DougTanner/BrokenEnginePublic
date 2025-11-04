#pragma once

#include "ExportJob.h"

class ExportShader : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Shader";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportShader(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
		if (rFile.extension() == ".comp")
		{
			mChunkFlags |= common::ChunkFlags::kCompute;
		}
		else if (rFile.extension() == ".frag")
		{
			mChunkFlags |= common::ChunkFlags::kFragment;
		}
		else if (rFile.extension() == ".vert")
		{
			mChunkFlags |= common::ChunkFlags::kVertex;
		}
	}

	virtual ~ExportShader() = default;

	virtual int64_t GetVersion() const override { return 1 + VK_HEADER_VERSION; }

protected:

	virtual void Export() override;
};
