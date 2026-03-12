#include "ExportModel.h"

using enum common::ChunkFlags;

std::optional<common::ChunkFlags_t> ExportModel::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	return rDirectoryEntry.path().extension() == ".MODEL" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kModel) : std::nullopt;
}

void ExportModel::Export()
{
	int64_t iStride = sizeof(common::ModelVertex);
	std::vector<uint32_t> materialIndexPositions;
	std::vector<uint16_t> indices16;
	std::vector<uint32_t> indices32;
	std::vector<std::byte> vertices;

	size_t uiMaterialCount = 0;
	size_t uiIndexCount = 0;
	size_t uiVertexCount = 0;

	std::fstream fileStream(mInputPath, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(&uiMaterialCount), sizeof(uiMaterialCount));
	materialIndexPositions.resize(uiMaterialCount);
	fileStream.read(reinterpret_cast<char*>(materialIndexPositions.data()), common::VectorByteSize(materialIndexPositions));
	// Skip past material info data (not needed for model export)
	fileStream.seekg(uiMaterialCount * sizeof(common::MaterialInfo), std::ios::cur);
	fileStream.read(reinterpret_cast<char*>(&uiIndexCount), sizeof(uiIndexCount));
	fileStream.read(reinterpret_cast<char*>(&uiVertexCount), sizeof(uiVertexCount));
	if (uiVertexCount < std::numeric_limits<uint16_t>::max())
	{
		indices16.resize(uiIndexCount);
		fileStream.read(reinterpret_cast<char*>(indices16.data()), common::VectorByteSize(indices16));
	}
	else
	{
		indices32.resize(uiIndexCount);
		fileStream.read(reinterpret_cast<char*>(indices32.data()), common::VectorByteSize(indices32));
	}
	vertices.resize(uiVertexCount * sizeof(common::ModelVertex));
	fileStream.read(reinterpret_cast<char*>(vertices.data()), common::VectorByteSize(vertices));
	fileStream.close();

	int64_t iIndicesSize = common::RoundUp<int64_t, 4>(indices16.size() > 0 ? common::VectorByteSize(indices16) : common::VectorByteSize(indices32));
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iIndicesSize + vertices.size());
	pHeader->modelHeader.iIndexCount = indices16.size() > 0 ? indices16.size() : indices32.size();
	pHeader->modelHeader.iVertexCount = vertices.size() / iStride;
	pHeader->modelHeader.iStride = iStride;
	if (indices16.size() > 0)
	{
		std::memcpy(dataSpan.data(), indices16.data(), common::VectorByteSize(indices16));
	}
	else
	{
		std::memcpy(dataSpan.data(), indices32.data(), common::VectorByteSize(indices32));
	}
	std::memcpy(dataSpan.data() + iIndicesSize, vertices.data(), common::VectorByteSize(vertices));
}
