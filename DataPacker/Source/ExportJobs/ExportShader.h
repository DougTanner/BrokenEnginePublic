#pragma once

#include "ExportJob.h"

class ExportShader : public ExportJob
{
public:

	static inline constexpr std::string_view kName = "Shader";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportShader(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
		if (rFile.extension() == ".comp")
		{
			mChunkFlags.Set(common::ChunkFlags::kCompute);
		}
		else if (rFile.extension() == ".frag")
		{
			mChunkFlags.Set(common::ChunkFlags::kFragment);
		}
		else if (rFile.extension() == ".vert")
		{
			mChunkFlags.Set(common::ChunkFlags::kVertex);
		}
	}

	virtual ~ExportShader() = default;

	virtual int64_t GetVersion() const override { return Version(14 + VK_HEADER_VERSION); }

	bool CheckDirty(const std::filesystem::path& rPackFile) override;

protected:

	void Export() override;
	void CleanupOnFailure() override;

private:

	std::filesystem::path PreprocessShader();
	std::filesystem::path CompileShader(const std::filesystem::path& rPreProcessedFile);
	std::filesystem::path OptimizeShader(const std::filesystem::path& rSpirvFile);
	void ReflectAndWriteShader(const std::filesystem::path& rSpirvFile);

protected:

	std::vector<std::filesystem::path> mIntermediateFiles;
};
