#include "ExportRaw.h"

#include "FileManager.h"

using enum common::ChunkFlags;

std::optional<common::ChunkFlags_t> ExportRaw::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	if (!rDirectoryEntry.is_regular_file())
	{
		return std::nullopt;
	}

	// Check if any parent directory is named "Raw"
	for (const std::filesystem::path& rPart : rDirectoryEntry.path())
	{
		if (rPart == "Raw")
		{
			return common::ChunkFlags::kRaw;
		}
	}

	return std::nullopt;
}

void ExportRaw::Export()
{
	// Read file size
	int64_t iFileSize = std::filesystem::file_size(mInputPath);

	// Allocate header and data
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iFileSize);

	// Copy file contents verbatim
	std::fstream fileStream(mInputPath, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(dataSpan.data()), iFileSize);
}
