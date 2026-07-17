#pragma once

#include "ExportJob.h"

class ExportShader : public ExportJob
{
public:

	static inline constexpr std::string_view kName = "Shader";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportShader(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile);

	virtual ~ExportShader() = default;

	virtual int64_t GetVersion() const override { return Version(15 + VK_HEADER_VERSION); }

	bool CheckDirty(const std::filesystem::path& rPackFile) override;

protected:

	void Export() override;
	void CleanupOnFailure() override;
	void UpdateCacheMetadata() override;

	std::vector<std::filesystem::path> mIntermediateFiles;

private:
	struct DependencyFingerprint
	{
		int64_t iInputRoot = 0;
		std::filesystem::path relativePath;
		std::string fingerprint;
	};

	std::filesystem::path RunVulkanTool(const std::filesystem::path& rExecutable, std::wstring& rParameters, const std::filesystem::path& rOutputFile, bool bThrowOnAnyOutput);
	void CaptureDependencies();

	std::filesystem::path PreprocessShader();
	std::filesystem::path CompileShader(const std::filesystem::path& rPreProcessedFile);
	std::filesystem::path OptimizeShader(const std::filesystem::path& rSpirvFile);
	void ReflectAndWriteShader(const std::filesystem::path& rSpirvFile);

	std::filesystem::path mDependencyFile;
	std::filesystem::path mDependencyMetadataFile;
	std::vector<DependencyFingerprint> mDependencyFingerprints;
};
