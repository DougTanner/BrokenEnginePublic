#pragma once

class ExportJob
{
public:

	// Version magic number for export format validation
	static constexpr int64_t kiMagic = 0xDA7ACCCC;

	// Folds in the chunk-header size so a header-layout change auto-invalidates every cache.
	static constexpr int64_t Version(int64_t iRaw) { return iRaw + sizeof(common::ChunkHeader); }

	ExportJob(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile);
	ExportJob(ExportJob&& rToMove) noexcept = default;
	ExportJob& operator=(ExportJob&& rToMove) noexcept = default;
	virtual ~ExportJob() = default;

	ExportJob() = delete;
	ExportJob(const ExportJob& rToCopy) = delete;
	ExportJob& operator=(const ExportJob& rToCopy) = delete;

	virtual bool CheckDirty(const std::filesystem::path& rPackFile);
	std::vector<std::byte>& RunExport();

	// Get export format version for this job type
	virtual int64_t GetVersion() const = 0;

	int64_t miId = 0;
	bool mbDirty = false;
	common::ChunkFlags_t mChunkFlags;

	std::filesystem::path mInputPath;
	std::filesystem::path mRelativeDirectory;
	std::string mRelativeFile;
	std::filesystem::path mChunkFile;
	std::filesystem::path mCacheMetadataFile;
	std::filesystem::path mLastModifiedTimeFile;

	std::future<std::vector<std::byte>&> mFuture;

	common::crc_t mCrc = 0;

protected:

	virtual void Export() = 0;
	virtual void CleanupOnFailure() {}
	virtual std::string GetInputFingerprint() const;
	virtual void UpdateCacheMetadata() {}

	std::tuple<common::ChunkHeader*, std::span<std::byte>> AllocateHeaderAndData(int64_t iDataSize);

	std::vector<std::byte> mHeaderAndData;
};

template <typename T>
concept IsExportJob =
	std::derived_from<T, ExportJob> &&
	requires
	{
		{ T::kName } -> std::convertible_to<std::string_view>;
	} &&
	requires (const std::filesystem::directory_entry& rEntry)
	{
		{ T::Handles(rEntry) } -> std::same_as<std::optional<common::ChunkFlags_t>>;
	};
