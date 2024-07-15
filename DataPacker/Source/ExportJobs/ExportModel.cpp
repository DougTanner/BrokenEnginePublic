#include "ExportModel.h"

#include "tinyobjloader/tiny_obj_loader.h"

using namespace DirectX;

using enum common::ChunkFlags;

struct Key : public XMFLOAT4
{
    constexpr Key(float _x, float _y, float _z, float _w) noexcept
	: XMFLOAT4(_x, _y, _z, _w)
	{
	}

	bool operator==(const Key &other) const
	{ 
		return x == other.x && y == other.y && z == other.z;
	}
};

namespace std
{
  template <>
  struct hash<Key>
  {
    std::size_t operator()(const Key& k) const
    {
      return ((hash<float>()(k.x) ^ (hash<float>()(k.y) << 1)) >> 1) ^ (hash<float>()(k.z) << 1);
    }
  };
}

void ExportModel::Export()
{
	int64_t iStride = sizeof(common::VertexPos);
	std::vector<uint32_t> materialIndexPositions;
	std::vector<uint16_t> indices16;
	std::vector<uint32_t> indices32;
	std::vector<byte> vertices;

	if (mInputPath.extension() == ".GLTF_MODEL")
	{
		iStride = sizeof(common::GltfVertex);

		size_t uiMaterialCount = 0;
		size_t uiIndexCount = 0;
		size_t uiVertexCount = 0;

		std::fstream fileStream(mInputPath, std::ios::in | std::ios::binary);
		fileStream.read(reinterpret_cast<char*>(&uiMaterialCount), sizeof(uiMaterialCount));
		materialIndexPositions.resize(uiMaterialCount);
		fileStream.read(reinterpret_cast<char*>(materialIndexPositions.data()), common::VectorByteSize(materialIndexPositions));
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
		vertices.resize(uiVertexCount * sizeof(common::GltfVertex));
		fileStream.read(reinterpret_cast<char*>(vertices.data()), common::VectorByteSize(vertices));
		fileStream.close();
	}
	else
	{
		if (mInputPath.native().find(L"[FN]") != std::wstring::npos)
		{
			mChunkFlags |= kNormals;
			mChunkFlags |= kFaceNormals;
			iStride = sizeof(common::VertexPosNorm);
		}
		else if (mInputPath.native().find(L"[N]") != std::wstring::npos)
		{
			mChunkFlags |= kNormals;
			iStride = sizeof(common::VertexPosNorm);
		}
		else if (mInputPath.native().find(L"[T]") != std::wstring::npos)
		{
			mChunkFlags |= kTexcoords;
			iStride = sizeof(common::VertexPosTex);
		}
		else if (mInputPath.native().find(L"[NT]") != std::wstring::npos)
		{
			mChunkFlags |= kNormals;
			mChunkFlags |= kTexcoords;
			iStride = sizeof(common::VertexPosNormTex);
		}

		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warnings;
		std::string errors;
		VERIFY_SUCCESS(tinyobj::LoadObj(&attrib, &shapes, &materials, &warnings, &errors, mInputPath.string().c_str()));

		int64_t iShapeCount = shapes.size();
		std::unordered_map<Key, std::vector<XMFLOAT4A>> vertexNormalMap;
		std::vector<std::vector<XMFLOAT4A>> vertexNormals(iShapeCount);
		if (mChunkFlags & kNormals && attrib.normals.size() == 0)
		{
			for (int64_t i = 0; i < iShapeCount; ++i)
			{
				const tinyobj::shape_t& rShape = shapes[i];
				std::vector<XMFLOAT4A>& rVertexNormals = vertexNormals[i];

				int64_t iIndexCount = rShape.mesh.indices.size();
				ASSERT(iIndexCount % 3 == 0);
				rVertexNormals.reserve(iIndexCount);

				for (int64_t j = 0; j < iIndexCount; j += 3)
				{
					const tinyobj::index_t& rIndexA = rShape.mesh.indices[j + 0];
					const tinyobj::index_t& rIndexB = rShape.mesh.indices[j + 1];
					const tinyobj::index_t& rIndexC = rShape.mesh.indices[j + 2];

					auto vecA = XMVectorSet(attrib.vertices[3 * rIndexA.vertex_index + 0], attrib.vertices[3 * rIndexA.vertex_index + 1], attrib.vertices[3 * rIndexA.vertex_index + 2], 0.0f);
					auto vecB = XMVectorSet(attrib.vertices[3 * rIndexB.vertex_index + 0], attrib.vertices[3 * rIndexB.vertex_index + 1], attrib.vertices[3 * rIndexB.vertex_index + 2], 0.0f);
					auto vecC = XMVectorSet(attrib.vertices[3 * rIndexC.vertex_index + 0], attrib.vertices[3 * rIndexC.vertex_index + 1], attrib.vertices[3 * rIndexC.vertex_index + 2], 0.0f);

					auto vecAB = XMVectorSubtract(vecB, vecA);
					auto vecAC = XMVectorSubtract(vecC, vecA);

					auto vecNormal = XMVector3Normalize(XMVector3Cross(vecAB, vecAC));
					XMFLOAT4A f4Normal {};
					XMStoreFloat4A(&f4Normal, vecNormal);

					if (mChunkFlags & kFaceNormals)
					{
						rVertexNormals.emplace_back(f4Normal);
						rVertexNormals.emplace_back(f4Normal);
						rVertexNormals.emplace_back(f4Normal);
					}
					else
					{
						vertexNormalMap[Key(attrib.vertices[3 * rIndexA.vertex_index + 0], attrib.vertices[3 * rIndexA.vertex_index + 1], attrib.vertices[3 * rIndexA.vertex_index + 2], 0.0f)].push_back(f4Normal);
						vertexNormalMap[Key(attrib.vertices[3 * rIndexB.vertex_index + 0], attrib.vertices[3 * rIndexB.vertex_index + 1], attrib.vertices[3 * rIndexB.vertex_index + 2], 0.0f)].push_back(f4Normal);
						vertexNormalMap[Key(attrib.vertices[3 * rIndexC.vertex_index + 0], attrib.vertices[3 * rIndexC.vertex_index + 1], attrib.vertices[3 * rIndexC.vertex_index + 2], 0.0f)].push_back(f4Normal);
					}
				}
			}
		}

		XMFLOAT4A f4Min {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 0.0f};
		XMFLOAT4A f4Max {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), 0.0f};
		auto UpdateMinMax = [&](float* pfPosition)
		{
			f4Min.x = std::min(f4Min.x, pfPosition[0]);
			f4Min.y = std::min(f4Min.y, pfPosition[1]);
			f4Min.z = std::min(f4Min.z, pfPosition[2]);

			f4Max.x = std::max(f4Max.x, pfPosition[0]);
			f4Max.y = std::max(f4Max.y, pfPosition[1]);
			f4Max.z = std::max(f4Max.z, pfPosition[2]);
		};

		LOG("{} shapes", shapes.size());
		for (int64_t i = 0; i < iShapeCount; ++i)
		{
			const tinyobj::shape_t& rShape = shapes[i];
			std::vector<XMFLOAT4A>& rVertexNormals = vertexNormals[i];

			int64_t iIndexCount = rShape.mesh.indices.size();
			LOG("  {} indicies", iIndexCount);

			for (int64_t j = 0; j < iIndexCount; ++j)
			{
				const tinyobj::index_t& rIndex = rShape.mesh.indices[j];

				float pfVertex[sizeof(common::VertexPosNormTex)] {};
				float* pfCurrent = pfVertex;

				*(pfCurrent++) = attrib.vertices[3 * rIndex.vertex_index + 0];
				*(pfCurrent++) = attrib.vertices[3 * rIndex.vertex_index + 1];
				*(pfCurrent++) = attrib.vertices[3 * rIndex.vertex_index + 2];

				if (mChunkFlags & kNormals)
				{
					if (attrib.normals.size() > 0)
					{
						ASSERT(rIndex.normal_index >= 0);
						*(pfCurrent++) = attrib.normals[3 * rIndex.normal_index + 0];
						*(pfCurrent++) = attrib.normals[3 * rIndex.normal_index + 1];
						*(pfCurrent++) = attrib.normals[3 * rIndex.normal_index + 2];
					}
					else if (mChunkFlags & kFaceNormals)
					{
						XMFLOAT4A f4Normal = rVertexNormals[j];
						*(pfCurrent++) = f4Normal.x;
						*(pfCurrent++) = f4Normal.y;
						*(pfCurrent++) = f4Normal.z;
					}
					else
					{
						const std::vector<XMFLOAT4A>& normals = vertexNormalMap.at(Key(attrib.vertices[3 * rIndex.vertex_index + 0], attrib.vertices[3 * rIndex.vertex_index + 1], attrib.vertices[3 * rIndex.vertex_index + 2], 0.0f));
						auto vecFinalNormal = XMVectorZero();
						for (const XMFLOAT4A& rf4Normal : normals)
						{
							auto vecNormal = XMLoadFloat4A(&rf4Normal);
							vecFinalNormal = XMVectorAdd(vecFinalNormal, vecNormal);
						}
						float fNormalCount = static_cast<float>(normals.size());
						vecFinalNormal = XMVector3Normalize(XMVectorDivide(vecFinalNormal, XMVectorSet(fNormalCount, fNormalCount, fNormalCount, fNormalCount)));

						XMFLOAT4A f4Normal {};
						XMStoreFloat4A(&f4Normal, vecFinalNormal);
						*(pfCurrent++) = f4Normal.x;
						*(pfCurrent++) = f4Normal.y;
						*(pfCurrent++) = f4Normal.z;
					}
				}

				if (mChunkFlags & kTexcoords)
				{
					if (attrib.texcoords.size() > 0 && rIndex.texcoord_index > 0)
					{
						*(pfCurrent++) = attrib.texcoords[2 * rIndex.texcoord_index + 0];
						*(pfCurrent++) = attrib.texcoords[2 * rIndex.texcoord_index + 1];
					}
					else
					{
						*(pfCurrent++) = 0.0f;
						*(pfCurrent++) = 0.0f;
					}
				}

				int64_t iIndex = -1;
				int64_t iVertices = vertices.size() / iStride;
				for (int64_t k = 0; k < iVertices; ++k)
				{
					if (memcmp(pfVertex, &vertices.data()[k * iStride], iStride) == 0)
					{
						iIndex = k;
					}
				}

				if (iIndex == -1)
				{
					iIndex = vertices.size() / iStride;
					ASSERT(iIndex < std::numeric_limits<uint16_t>::max());
					vertices.resize(vertices.size() + iStride);
					memcpy(&vertices.data()[iIndex * iStride], pfVertex, iStride);
					UpdateMinMax(pfVertex);
				}

				indices16.push_back(static_cast<uint16_t>(iIndex));
			}
		}

		XMFLOAT4A f4Center {0.5f * (f4Max.x + f4Min.x), 0.5f * (f4Max.y + f4Min.y), 0.5f * (f4Max.z + f4Min.z), 0.0f};
		LOG("Indices: {} Vertices: {} Min: {}, Max: {} Center: {}", indices16.size() > 0 ? indices16.size() : indices32.size(), vertices.size() / iStride, f4Min, f4Max, f4Center);

		for (int64_t i = 0; i < static_cast<int64_t>(vertices.size()) / iStride; ++i)
		{
			float* pfPosition = reinterpret_cast<float*>(vertices.data() + iStride * i);
			pfPosition[0] -= f4Center.x;
			pfPosition[1] -= f4Center.y;
			pfPosition[2] -= f4Center.z;
		}
	}

	int64_t iIndicesSize = common::RoundUp(indices16.size() > 0 ? common::VectorByteSize(indices16) : common::VectorByteSize(indices32), 4ll);
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iIndicesSize + vertices.size());
	pHeader->modelHeader.iIndexCount = indices16.size() > 0 ? indices16.size() : indices32.size();
	pHeader->modelHeader.iVertexCount = vertices.size() / iStride;
	pHeader->modelHeader.iStride = iStride;
	if (indices16.size() > 0)
	{
		memcpy(dataSpan.data(), indices16.data(), common::VectorByteSize(indices16));
	}
	else
	{
		memcpy(dataSpan.data(), indices32.data(), common::VectorByteSize(indices32));
	}
	memcpy(dataSpan.data() + iIndicesSize, vertices.data(), common::VectorByteSize(vertices));
}
