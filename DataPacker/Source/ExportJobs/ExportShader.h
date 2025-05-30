#pragma once

#include "ExportJob.h"

class ExportShader : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Shader";
	static inline constexpr common::ChunkFlags kChunkFlags = common::ChunkFlags::kShader;

	static bool Handles(const std::filesystem::directory_entry& rDirectoryEntry)
	{
		return rDirectoryEntry.path().extension() == ".comp" || rDirectoryEntry.path().extension() == ".frag" || rDirectoryEntry.path().extension() == ".vert";
	}

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

protected:

	virtual void Export() override;
};
