#pragma once

#include "tinygltf/tiny_gltf.h"

#include "DataFile.h"
#include "ExportJob.h"

class ExportGltf : public ExportJob
{
public:

	static inline constexpr std::string_view kName = "Gltf";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportGltf(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportGltf() = default;

	virtual int64_t GetVersion() const override { return 45 + sizeof(common::ChunkHeader); }

	virtual bool CheckDirty(const std::filesystem::path& rPackFile) override;

protected:

	virtual void Export() override;

private:

	tinygltf::Model LoadGltfModel();
	std::filesystem::path GetPreExportMarkerPath() const;
	std::filesystem::path GetTextureIntermediatePath(int64_t iTextureIndex, bool bOcclusion) const;

	void CleanupOnFailure() override;

	std::vector<std::filesystem::path> mIntermediateFiles;
};
