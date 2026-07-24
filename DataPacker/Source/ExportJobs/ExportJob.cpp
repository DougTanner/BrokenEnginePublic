#include "ExportJob.h"

#include "FileManager.h"

static int64_t siNextJobId = 0;

namespace
{

constexpr int64_t kiFingerprintMetadataMagic = 0x465052494E544D31;
constexpr int64_t kiFingerprintMetadataVersion = 1;
std::optional<std::string> ReadFingerprintMetadata(const std::filesystem::path& rPath)
{
	std::fstream stream(rPath, std::ios::in | std::ios::binary);
	int64_t iMagic = 0;
	int64_t iVersion = 0;
	int64_t iFingerprintCharacters = 0;
	stream.read(reinterpret_cast<char*>(&iMagic), sizeof(iMagic));
	stream.read(reinterpret_cast<char*>(&iVersion), sizeof(iVersion));
	stream.read(reinterpret_cast<char*>(&iFingerprintCharacters), sizeof(iFingerprintCharacters));
	if (!stream || iMagic != kiFingerprintMetadataMagic || iVersion != kiFingerprintMetadataVersion || iFingerprintCharacters <= 0 || iFingerprintCharacters > 1024 * 1024)
	{
		return std::nullopt;
	}
	std::string fingerprint(static_cast<size_t>(iFingerprintCharacters), '\0');
	stream.read(fingerprint.data(), fingerprint.size());
	return stream ? std::optional(std::move(fingerprint)) : std::nullopt;
}

void WriteFingerprintMetadata(const std::filesystem::path& rPath, std::string_view fingerprint)
{
	std::filesystem::path temporaryPath = rPath;
	temporaryPath += ".tmp";
	std::fstream stream(temporaryPath, std::ios::out | std::ios::binary);
	int64_t iFingerprintCharacters = static_cast<int64_t>(fingerprint.size());
	stream.write(reinterpret_cast<const char*>(&kiFingerprintMetadataMagic), sizeof(kiFingerprintMetadataMagic));
	stream.write(reinterpret_cast<const char*>(&kiFingerprintMetadataVersion), sizeof(kiFingerprintMetadataVersion));
	stream.write(reinterpret_cast<const char*>(&iFingerprintCharacters), sizeof(iFingerprintCharacters));
	stream.write(fingerprint.data(), fingerprint.size());
	stream.close();
	VERIFY_SUCCESS(stream.good());
	VERIFY_SUCCESS(MoveFileExW(temporaryPath.native().c_str(), rPath.native().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
}

}

ExportJob::ExportJob(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
: miId(siNextJobId++)
, mChunkFlags(rChunkFlags)
, mInputPath(rFile)
{
	if (mInputPath.native().starts_with(gpFileManager->mpInputDirectories[0].native()))
	{
		mRelativeDirectory = mInputPath.native().substr(gpFileManager->mpInputDirectories[0].native().size() + 1);
	}
	else
	{
		mRelativeDirectory = mInputPath.native().substr(gpFileManager->mpInputDirectories[1].native().size() + 1);
	}
	mRelativeDirectory.remove_filename();

	mChunkFile = gpFileManager->mCacheDirectory;
	mChunkFile /= mRelativeDirectory;
	std::filesystem::create_directories(mChunkFile);
	mChunkFile /= mInputPath.filename();
	mChunkFile += ".chunk";

	mCacheMetadataFile = gpFileManager->mCacheDirectory;
	mCacheMetadataFile /= mRelativeDirectory;
	mCacheMetadataFile /= mInputPath.filename();
	mCacheMetadataFile += ".meta";

	mLastModifiedTimeFile = gpFileManager->mCacheDirectory;
	mLastModifiedTimeFile /= mRelativeDirectory;
	mLastModifiedTimeFile /= mInputPath.filename();
	mLastModifiedTimeFile += ".txt";

	mRelativeFile = mRelativeDirectory.string() + mInputPath.filename().string();
	mCrc = common::Crc(mRelativeFile);
}

std::tuple<common::ChunkHeader*, std::span<std::byte>> ExportJob::AllocateHeaderAndData(int64_t iDataSize)
{
	int64_t iDataOffset = common::kiChunkDataOffset;
	int64_t iTotalSizeAligned = iDataOffset;
	iTotalSizeAligned += common::RoundUp<int64_t, common::kiAlignmentBytes>(iDataSize);

	mHeaderAndData.resize(iTotalSizeAligned);
	reinterpret_cast<common::ChunkHeader*>(mHeaderAndData.data())->iSize = iDataSize;
	return std::make_tuple(reinterpret_cast<common::ChunkHeader*>(mHeaderAndData.data()), std::span(&mHeaderAndData.at(iDataOffset), mHeaderAndData.size() - iDataOffset));
}

bool ExportJob::CheckDirty(const std::filesystem::path& rPackFile)
{
	// The pack is not inspected per job: RunExportJobs compares the published pack's timestamp against
	// every clean job's .meta fingerprint, so an export killed before the pack rename stays dirty there.
	(void)rPackFile;

	// Clean export?
	if (gpFileManager->mbCleanExport)
	{
		mbDirty = true;
		return mbDirty;
	}

	// Does the chunk file exist?
	if (!std::filesystem::exists(mChunkFile))
	{
		LOG(kDefault, kDebug, "Chunk file \"{}\" does not exist", mChunkFile.string());
		mbDirty = true;
		return mbDirty;
	}

	// Verify chunk file magic and version
	if (std::filesystem::file_size(mChunkFile) < sizeof(kiMagic) + sizeof(int64_t) + common::kiChunkDataOffset)
	{
		LOG(kDefault, kWarning, "Chunk file \"{}\" is truncated", mChunkFile.string());
		mbDirty = true;
		return mbDirty;
	}
	std::fstream chunkFileStream(mChunkFile, std::ios::in | std::ios::binary);

	int64_t piMagicAndVersion[2] = {};
	chunkFileStream.read(reinterpret_cast<char*>(piMagicAndVersion), sizeof(piMagicAndVersion));
	chunkFileStream.close();

	if (!chunkFileStream || piMagicAndVersion[0] != kiMagic || piMagicAndVersion[1] != GetVersion())
	{
		LOG(kDefault, kWarning, "Chunk file \"{}\" has invalid magic {:#018x} or version {}", mChunkFile.string(), piMagicAndVersion[0], piMagicAndVersion[1]);
		mbDirty = true;
		return mbDirty;
	}

	std::string inputFingerprint = GetInputFingerprint();
	std::optional<std::string> cachedFingerprint = ReadFingerprintMetadata(mCacheMetadataFile);
	if (!cachedFingerprint.has_value() && std::filesystem::exists(mLastModifiedTimeFile))
	{
		int64_t iLegacyLastModifiedTime = 0;
		std::fstream legacyStream(mLastModifiedTimeFile, std::ios::in | std::ios::binary);
		legacyStream.read(reinterpret_cast<char*>(&iLegacyLastModifiedTime), sizeof(iLegacyLastModifiedTime));
		int64_t iCurrentLastModifiedTime = std::filesystem::last_write_time(mInputPath).time_since_epoch().count();
		if (legacyStream && iLegacyLastModifiedTime == iCurrentLastModifiedTime)
		{
			WriteFingerprintMetadata(mCacheMetadataFile, inputFingerprint);
			std::filesystem::remove(mLastModifiedTimeFile);
			cachedFingerprint = inputFingerprint;
			LOG(kDefault, kDebug, "Upgraded legacy cache metadata \"{}\"", mCacheMetadataFile.string());
		}
	}

	if (!cachedFingerprint.has_value() || cachedFingerprint.value() != inputFingerprint)
	{
		LOG(kDefault, kDebug, "Input fingerprint changed for \"{}\"", mInputPath.string());
		mbDirty = true;
		return mbDirty;
	}

	mbDirty = false;
	return mbDirty;
}

std::vector<std::byte>& ExportJob::RunExport()
{
	common::ThreadLocal threadLocal(4 * 1024, miId, false);
	ScopedLogIndent scopedLogIndentOuter;
	ScopedLogIndent scopedLogIndentInner;

	// Load cached chunk file
	if (!mbDirty)
	{
		int64_t iChunkFileSize = std::filesystem::file_size(mChunkFile);
		int64_t iHeaderAndDataSize = iChunkFileSize - sizeof(kiMagic) - sizeof(int64_t);
		mHeaderAndData.resize(iHeaderAndDataSize);

		std::fstream fileStream(mChunkFile, std::ios::in | std::ios::binary);
		fileStream.seekg(sizeof(kiMagic) + sizeof(int64_t)); // Skip magic and version
		fileStream.read(reinterpret_cast<char*>(mHeaderAndData.data()), mHeaderAndData.size());

		// CheckDirty only validated the 16-byte header, and resize() zero-inits the buffer, so a short read
		// (truncated/interrupted chunk) would silently pack a zero tail. Verify the full body was read; if not,
		// discard the cache and fall through to a full dirty re-export rather than shipping the zeroed bytes.
		if (fileStream.good() && fileStream.gcount() == static_cast<std::streamsize>(mHeaderAndData.size()))
		{
			return mHeaderAndData;
		}
		LOG(kDefault, kWarning, "Cached chunk file \"{}\" is truncated ({} of {} bytes read); re-exporting", mChunkFile.string(), fileStream.gcount(), mHeaderAndData.size());
		mbDirty = true;
	}

	std::string inputFingerprint = GetInputFingerprint();
	try
	{
		Export();
	}
	catch (...)
	{
		CleanupOnFailure();
		throw;
	}

	std::filesystem::path relativeFile = mRelativeDirectory;
	relativeFile /= mInputPath.filename();

	common::ChunkHeader* pChunkHeader = reinterpret_cast<common::ChunkHeader*>(mHeaderAndData.data());
	pChunkHeader->iMagic = common::ChunkHeader::kiMagic;
	pChunkHeader->crc = common::Crc(relativeFile.string());
	LOG(kDefault, kDebug, "\"{}\" -> {:#018x}", relativeFile.string(), pChunkHeader->crc);
	pChunkHeader->flags = mChunkFlags;
	std::string relativeFileString = common::ToString(relativeFile.native());
	ASSERT(relativeFileString.length() < MAX_PATH);
	std::memcpy(pChunkHeader->pcPath, relativeFileString.c_str(), sizeof(*relativeFileString.c_str()) * relativeFileString.length());

	// The primary fingerprint is the cache completion marker. Remove it (and the legacy fallback) before
	// mutating the chunk so any write or derived-metadata failure leaves the job unambiguously dirty.
	std::filesystem::remove(mCacheMetadataFile);
	std::filesystem::remove(mLastModifiedTimeFile);

	// Write chunk file with magic and version
	std::fstream fileStream(mChunkFile, std::ios::out | std::ios::binary);
	int64_t piMagicAndVersion[2] = { kiMagic, GetVersion() };
	fileStream.write(reinterpret_cast<char*>(piMagicAndVersion), sizeof(piMagicAndVersion));
	fileStream.write(reinterpret_cast<char*>(mHeaderAndData.data()), mHeaderAndData.size());
	fileStream.close();
	VERIFY_SUCCESS(fileStream.good());

	UpdateCacheMetadata();
	WriteFingerprintMetadata(mCacheMetadataFile, inputFingerprint);

	return mHeaderAndData;
}

std::string ExportJob::GetInputFingerprint() const
{
	return gpFileManager->GetFingerprint(mInputPath);
}
