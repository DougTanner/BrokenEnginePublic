#include "ExportGltf.h"

#include "Texture.h"

using namespace DirectX;

using enum common::ChunkFlags;

tinygltf::TinyGLTF gGltfContext;

VkFilter ToVkFilter(int iFilterMode)
{
	switch (iFilterMode)
	{
		case 9728:
			return VK_FILTER_NEAREST;
		case 9729:
			return VK_FILTER_LINEAR;
		case 9984:
			return VK_FILTER_NEAREST;
		case 9985:
			return VK_FILTER_NEAREST;
		case 9986:
			return VK_FILTER_LINEAR;
		case 9987:
			return VK_FILTER_LINEAR;
		case -1:
			return VK_FILTER_LINEAR;
		default:
			DEBUG_BREAK();
			return VK_FILTER_LINEAR;
	}
}

VkSamplerAddressMode ToVkSamplerAddressMode(int iWrapMode)
{
	switch (iWrapMode)
	{
		case 10497:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case 33071:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case 33648:
			return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case -1:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		default:
			DEBUG_BREAK();
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

tinygltf::Model ExportGltf::LoadGltfModel()
{
	tinygltf::Model gltfModel;

	std::string filename = mInputPath.string();
	int64_t iExtensionPosition = filename.rfind('.', filename.length());
	bool bBinary = false;
	if (iExtensionPosition != std::string::npos)
	{
		bBinary = (filename.substr(iExtensionPosition + 1, filename.length() - iExtensionPosition) == "glb");
	}

	std::string error;
	std::string warning;
	bool fileLoaded = bBinary ? gGltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, filename.c_str()) : gGltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename.c_str());
	ASSERT(fileLoaded);

	return gltfModel;
}

void GetIndexVertexCount(const tinygltf::Node& rNode, const tinygltf::Model& rModel, int64_t& riIndexCount, int64_t& riVertexCount)
{
	if (rNode.children.size() > 0)
	{
		for (int64_t i = 0; i < static_cast<int64_t>(rNode.children.size()); ++i)
		{
			GetIndexVertexCount(rModel.nodes[rNode.children[i]], rModel, riIndexCount, riVertexCount);
		}
	}

	if (rNode.mesh > -1)
	{
		const tinygltf::Mesh mesh = rModel.meshes[rNode.mesh];
		for (int64_t i = 0; i < static_cast<int64_t>(mesh.primitives.size()); ++i)
		{
			auto primitive = mesh.primitives[i];
			riVertexCount += rModel.accessors[primitive.attributes.find("POSITION")->second].count;
			if (primitive.indices > -1)
			{
				riIndexCount += rModel.accessors[primitive.indices].count;
			}
		}
	}
}

struct Node
{
	Node* pParent = nullptr;
};

struct Material
{
	std::vector<common::GltfVertex> vertexBuffer;
	std::vector<uint32_t> indexBuffer;
};

struct Parent
{
	Parent* pParent = nullptr;
	XMMATRIX matNode {};
};

// Based on https://github.com/SaschaWillems/Vulkan-glTF-PBR
void LoadVertices(Parent* pParent, const tinygltf::Node& rNode, const tinygltf::Model& rModel, std::vector<Material>& rMaterials)
{
	XMMATRIX matNode = XMMatrixIdentity();
	if (rNode.matrix.size() == 16)
	{
		matNode = XMMATRIX(static_cast<float>(rNode.matrix[0]), static_cast<float>(rNode.matrix[1]), static_cast<float>(rNode.matrix[2]), static_cast<float>(rNode.matrix[3]), static_cast<float>(rNode.matrix[4]), static_cast<float>(rNode.matrix[5]), static_cast<float>(rNode.matrix[6]), static_cast<float>(rNode.matrix[7]), static_cast<float>(rNode.matrix[8]), static_cast<float>(rNode.matrix[9]), static_cast<float>(rNode.matrix[10]), static_cast<float>(rNode.matrix[11]), static_cast<float>(rNode.matrix[12]), static_cast<float>(rNode.matrix[13]), static_cast<float>(rNode.matrix[14]), static_cast<float>(rNode.matrix[15]));
	}

	for (size_t i = 0; i < rNode.children.size(); ++i)
	{
		Parent parent {pParent, matNode};
		LoadVertices(&parent, rModel.nodes[rNode.children[i]], rModel, rMaterials);
	}

	if (rNode.mesh < 0)
	{
		return;
	}

	XMMATRIX matLocal = XMMatrixIdentity();
	Parent* pCurrentParent = pParent;
	while (pCurrentParent != nullptr)
	{
		matLocal = pCurrentParent->matNode * matLocal;
		pCurrentParent = pCurrentParent->pParent;
	}

	const tinygltf::Mesh& rMesh = rModel.meshes[rNode.mesh];
	for (size_t i = 0; i < rMesh.primitives.size(); ++i)
	{
		const tinygltf::Primitive& rPrimitive = rMesh.primitives[i];
		ASSERT(rPrimitive.attributes.find("POSITION") != rPrimitive.attributes.end());
		ASSERT(rPrimitive.attributes.find("COLOR_0") == rPrimitive.attributes.end());
		std::vector<std::string> otherTexcoords = {"TEXCOORD_4", "TEXCOORD_5"};
		for (const std::string& rOtherTexcoord : otherTexcoords)
		{
			if (rPrimitive.attributes.find(rOtherTexcoord) != rPrimitive.attributes.end())
			{
				LOG("WARNING: Found {}", rOtherTexcoord);
			}
		}

		ASSERT(rPrimitive.material >= 0);
		Material& rMaterial = rMaterials[rPrimitive.material];
		int64_t iVertexStart = rMaterial.vertexBuffer.size();

		// Position
		const tinygltf::Accessor& rPositionAccessor = rModel.accessors[rPrimitive.attributes.find("POSITION")->second];
		const tinygltf::BufferView& rPositionBufferView = rModel.bufferViews[rPositionAccessor.bufferView];
		const float* pfPositions = reinterpret_cast<const float*>(&(rModel.buffers[rPositionBufferView.buffer].data[rPositionAccessor.byteOffset + rPositionBufferView.byteOffset]));
		int iPositionStride = rPositionAccessor.ByteStride(rPositionBufferView) ? (rPositionAccessor.ByteStride(rPositionBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

		// Normal
		const float* pfNormals = nullptr;
		int iNormalStride = 0;
		if (rPrimitive.attributes.find("NORMAL") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("NORMAL")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfNormals = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iNormalStride = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
		}

		// Texcoord 0
		const float* pfTexcoords0 = nullptr;
		int iTexcoordStride0 = 0;
		if (rPrimitive.attributes.find("TEXCOORD_0") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("TEXCOORD_0")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfTexcoords0 = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iTexcoordStride0 = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
		}

		// Texcoord 1
		const float* pfTexcoords1 = nullptr;
		int iTexcoordStride1 = 0;
		if (rPrimitive.attributes.find("TEXCOORD_1") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("TEXCOORD_1")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfTexcoords1 = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iTexcoordStride1 = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
		}

		// Texcoord 2
		const float* pfTexcoords2 = nullptr;
		int iTexcoordStride2 = 0;
		if (rPrimitive.attributes.find("TEXCOORD_2") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("TEXCOORD_2")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfTexcoords2 = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iTexcoordStride2 = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
		}

		// Texcoord 3
		const float* pfTexcoords3 = nullptr;
		int iTexcoordStride3 = 0;
		if (rPrimitive.attributes.find("TEXCOORD_3") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("TEXCOORD_3")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfTexcoords3 = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iTexcoordStride3 = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
		}

		// Joints
		const uint16_t* puiJoints = nullptr;
		int iJointsStride = 0;
		if (rPrimitive.attributes.find("JOINTS_0") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("JOINTS_0")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			ASSERT(rAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
			puiJoints = reinterpret_cast<const uint16_t*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iJointsStride = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / tinygltf::GetComponentSizeInBytes(rAccessor.componentType)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
		}

		std::vector<std::string> weights = {"WEIGHTS_0"};
		for (const std::string& rJointsWeight : weights)
		{
			if (rPrimitive.attributes.find(rJointsWeight) != rPrimitive.attributes.end())
			{
				LOG("WARNING: Found {}", rJointsWeight);
			}
		}

		for (int64_t j = 0; j < static_cast<int64_t>(rPositionAccessor.count); ++j)
		{
			common::GltfVertex& rVertex = rMaterial.vertexBuffer.emplace_back();

			auto vecPosition = XMVectorSet(pfPositions[j * iPositionStride + 0], pfPositions[j * iPositionStride + 1], pfPositions[j * iPositionStride + 2], 1.0f);
			vecPosition = XMVector4Transform(vecPosition, matLocal);
			XMFLOAT3 f3Position {};
			XMStoreFloat3(&rVertex.f3Pos, vecPosition);

			XMStoreFloat3(&rVertex.f3Normal, XMVector3Normalize(pfNormals ? XMVECTOR {pfNormals[j * iNormalStride], pfNormals[j * iNormalStride + 1], pfNormals[j * iNormalStride + 2], 0.0f} : XMVECTOR {0.0f, 0.0f, 1.0f, 0.0f}));

			rVertex.f2Uv = pfTexcoords0 != nullptr ? XMFLOAT2(&pfTexcoords0[j * iTexcoordStride0]) : XMFLOAT2(0.0f, 0.0f);

			if (pfTexcoords1 != nullptr)
			{
				DirectX::XMFLOAT2 f2Uv1 = pfTexcoords1 != nullptr ? XMFLOAT2(&pfTexcoords1[j * iTexcoordStride1]) : XMFLOAT2(0.0f, 0.0f);
				ASSERT(rVertex.f2Uv == f2Uv1);
			}

			if (pfTexcoords2 != nullptr)
			{
				DirectX::XMFLOAT2 f2Uv2 = pfTexcoords2 != nullptr ? XMFLOAT2(&pfTexcoords2[j * iTexcoordStride2]) : XMFLOAT2(0.0f, 0.0f);
				ASSERT(rVertex.f2Uv == f2Uv2);
			}

			if (pfTexcoords3 != nullptr)
			{
				DirectX::XMFLOAT2 f2Uv3 = pfTexcoords3 != nullptr ? XMFLOAT2(&pfTexcoords3[j * iTexcoordStride3]) : XMFLOAT2(0.0f, 0.0f);
				ASSERT(rVertex.f2Uv == f2Uv3);
			}

			if (puiJoints != nullptr)
			{
				rVertex.fJoint = static_cast<float>(puiJoints[j * iJointsStride]);
			}
		}

		if (rPrimitive.indices > -1)
		{
			const tinygltf::Accessor& rIndicesAccessor = rModel.accessors[rPrimitive.indices];
			const tinygltf::BufferView& rIndiciesBufferView = rModel.bufferViews[rIndicesAccessor.bufferView];
			const tinygltf::Buffer& rIndiciesBuffer = rModel.buffers[rIndiciesBufferView.buffer];
			const void* pIndices = &(rIndiciesBuffer.data[rIndicesAccessor.byteOffset + rIndiciesBufferView.byteOffset]);

			switch (rIndicesAccessor.componentType)
			{
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
				{
					const uint32_t* puiIndices = static_cast<const uint32_t*>(pIndices);
					for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
					{
						rMaterial.indexBuffer.push_back(puiIndices[j] + static_cast<uint32_t>(iVertexStart));
					}
					break;
				}

				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
				{
					const uint16_t* puiIndices = static_cast<const uint16_t*>(pIndices);
					for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
					{
						rMaterial.indexBuffer.push_back(puiIndices[j] + static_cast<uint32_t>(iVertexStart));
					}
					break;
				}

				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
				{
					const uint8_t* puiIndices = static_cast<const uint8_t*>(pIndices);
					for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
					{
						rMaterial.indexBuffer.push_back(puiIndices[j] + static_cast<uint32_t>(iVertexStart));
					}
					break;
				}

				default:
					ASSERT(false);
					return;
				}
		}
	}
}

bool IsOcclusion(int64_t iIndex, const tinygltf::Material& rMaterial)
{
	bool bOcculsion = rMaterial.additionalValues.find("occlusionTexture") != rMaterial.additionalValues.end() && rMaterial.additionalValues.at("occlusionTexture").TextureIndex() == iIndex;

	if (bOcculsion &&
		(rMaterial.values.find("baseColorTexture") != rMaterial.values.end() && rMaterial.values.at("baseColorTexture").TextureIndex() == iIndex ||
		 rMaterial.additionalValues.find("normalTexture") != rMaterial.additionalValues.end() && rMaterial.additionalValues.at("normalTexture").TextureIndex() == iIndex ||
		 rMaterial.values.find("metallicRoughnessTexture") != rMaterial.values.end() && rMaterial.values.at("metallicRoughnessTexture").TextureIndex() == iIndex ||
		 rMaterial.additionalValues.find("emissiveTexture") != rMaterial.additionalValues.end() && rMaterial.additionalValues.at("emissiveTexture").TextureIndex() == iIndex))
	{
		bOcculsion = false;
	}

	return bOcculsion;
}

void ExportGltf::PreExport()
{
	std::filesystem::path preExportPath(mInputPath);
	preExportPath += ".PreExport";
	if (std::filesystem::exists(preExportPath))
	{
		return;
	}

	LOG("PreExport Gltf: {}", mInputPath.string());
	tinygltf::Model gltfModel = LoadGltfModel();

	ASSERT(gltfModel.textures.size() <= common::GltfHeader::kiMaxTextures);
	int64_t iTextureIndex = 0;
	LOG("Pre-processing {} textures", gltfModel.textures.size());
	for (const tinygltf::Texture& rTexture : gltfModel.textures)
	{
		// DT: TODO Check all materials
		bool bOcclusion = IsOcclusion(rTexture.source, gltfModel.materials[0]);

		std::filesystem::path path(mInputPath);
		path += ".Texture";
		path += std::to_string(rTexture.source);
		path += bOcclusion ? ".BC4_UNORM_BLOCK" : ".BC7_UNORM_BLOCK";
		if (std::filesystem::exists(path))
		{
			continue;
		}

		const tinygltf::Image& rImage = gltfModel.images[rTexture.source];
		Texture texture(reinterpret_cast<const std::byte*>(rImage.image.data()), rImage.width, rImage.height, rImage.component);
		texture.MakeMipmaps(bOcclusion ? VK_FORMAT_BC4_UNORM_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK);

		texture.Save(path, bOcclusion ? VK_FORMAT_BC4_UNORM_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK, false);

		LOG("  {}: Texture {} -> {}", iTextureIndex++, rImage.uri, path.filename().native());
	}

	std::filesystem::path path(mInputPath);
	path += ".GLTF_MODEL";
	if (!std::filesystem::exists(path))
	{
		LOG("Loading {} materials", gltfModel.materials.size());
		std::vector<Material> materials(gltfModel.materials.size());
		const tinygltf::Scene& rScene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
		for (size_t i = 0; i < rScene.nodes.size(); ++i)
		{
			const tinygltf::Node& rNode = gltfModel.nodes[rScene.nodes[i]];
			Parent parent {nullptr, XMMatrixIdentity()};
			LoadVertices(&parent, rNode, gltfModel, materials);
		}

		int64_t iMaterialVertexCount = 0;
		for (int64_t i = 0; i < static_cast<int64_t>(materials.size()); ++i)
		{
			tinygltf::Material& tinygltfMaterial = gltfModel.materials[i];
			Material& rMaterial = materials[i];
			LOG("  {}: \"{}\", {} {} {} {} {} textures, {} vertices", i, tinygltfMaterial.name, tinygltfMaterial.pbrMetallicRoughness.baseColorTexture.index, tinygltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index, tinygltfMaterial.normalTexture.index, tinygltfMaterial.occlusionTexture.index, tinygltfMaterial.emissiveTexture.index, rMaterial.vertexBuffer.size());
			iMaterialVertexCount += rMaterial.vertexBuffer.size();
		}

		std::vector<common::GltfVertex> vertices;
		vertices.reserve(iMaterialVertexCount);
	#if 0
		for (int64_t i = 0; i < static_cast<int64_t>(materials.size()); ++i)
		{
			std::vector<common::GltfVertex>& rMaterialVertexBuffer = materials[i].vertexBuffer;
			vertices.insert(vertices.end(), rMaterialVertexBuffer.begin(), rMaterialVertexBuffer.end());
		}
	#else
		for (int64_t i = 0; i < static_cast<int64_t>(materials.size()); ++i)
		{
			std::vector<common::GltfVertex>& rMaterialVertexBuffer = materials[i].vertexBuffer;
			ASSERT((materials[i].indexBuffer.size() % 3) == 0);

			for (uint32_t& ruiIndex : materials[i].indexBuffer)
			{
				common::GltfVertex& rMaterialGltfVertex = rMaterialVertexBuffer[ruiIndex];

				int64_t iFoundIndex = -1;
				for (int64_t j = 0; j < static_cast<int64_t>(vertices.size()); ++j)
				{
					common::GltfVertex& rGltfVertex = vertices[j];
					if (rGltfVertex == rMaterialGltfVertex)
					{
						iFoundIndex = j;
					}
				}

				if (iFoundIndex == -1)
				{
					vertices.push_back(rMaterialGltfVertex);
					iFoundIndex = vertices.size() - 1;
				}

				ruiIndex = static_cast<uint32_t>(iFoundIndex);
			}
		}
	#endif

		LOG("{} material vertices -> {}", iMaterialVertexCount, vertices.size());

		std::map<float, int64_t> jointsMap;
		XMFLOAT3 f3Min = vertices[0].f3Pos;
		XMFLOAT3 f3Max = vertices[0].f3Pos;
		for (common::GltfVertex& rVertex : vertices)
		{
			f3Min.x = std::min(f3Min.x, rVertex.f3Pos.x);
			f3Min.y = std::min(f3Min.y, rVertex.f3Pos.y);
			f3Min.z = std::min(f3Min.z, rVertex.f3Pos.z);
			f3Max.x = std::max(f3Max.x, rVertex.f3Pos.x);
			f3Max.y = std::max(f3Max.y, rVertex.f3Pos.y);
			f3Max.z = std::max(f3Max.z, rVertex.f3Pos.z);

			++jointsMap[rVertex.fJoint];
		}
		LOG("f3Min: {} f3Max: {}", f3Min, f3Max);
		LOG("Joints:");
		for (const auto& rElement : jointsMap)
		{
			LOG("  {}: {}", rElement.first, rElement.second);
		}

		std::vector<uint32_t> indices32;
		std::vector<uint32_t> materialIndexPositions(materials.size());
		for (int64_t i = 0; i < static_cast<int64_t>(materials.size()); ++i)
		{
			materialIndexPositions[i] = static_cast<uint32_t>(indices32.size());
			indices32.insert(indices32.end(), materials[i].indexBuffer.begin(), materials[i].indexBuffer.end());
		}

		std::vector<uint16_t> indices16;
		if (vertices.size() < std::numeric_limits<uint16_t>::max())
		{
			indices16.reserve(indices32.size());
			for (uint32_t uiIndex : indices32)
			{
				indices16.push_back(static_cast<uint16_t>(uiIndex));
			}
		}

		std::filesystem::remove(path);
		std::fstream fileStreamOut(path, std::ios::out | std::ios::binary);
		size_t uiMaterialCount = materials.size();
		size_t uiIndexCount = indices32.size();
		size_t uiVertexCount = vertices.size();
		fileStreamOut.write(reinterpret_cast<const char*>(&uiMaterialCount), sizeof(uiMaterialCount));
		fileStreamOut.write(reinterpret_cast<const char*>(materialIndexPositions.data()), common::VectorByteSize(materialIndexPositions));
		fileStreamOut.write(reinterpret_cast<const char*>(&uiIndexCount), sizeof(uiIndexCount));
		fileStreamOut.write(reinterpret_cast<const char*>(&uiVertexCount), sizeof(uiVertexCount));
		if (indices16.size() > 0)
		{
			fileStreamOut.write(reinterpret_cast<const char*>(indices16.data()), common::VectorByteSize(indices16));
		}
		else
		{
			fileStreamOut.write(reinterpret_cast<const char*>(indices32.data()), common::VectorByteSize(indices32));
		}
		fileStreamOut.write(reinterpret_cast<const char*>(vertices.data()), common::VectorByteSize(vertices));
		fileStreamOut.flush();
		fileStreamOut.close();
	}

	std::fstream fileStreamOut(preExportPath, std::ios::out);
	fileStreamOut << "PreExport" << std::endl;
	fileStreamOut.flush();
	fileStreamOut.close();
}

void ExportGltf::Export()
{
	tinygltf::Model gltfModel = LoadGltfModel();

	LOG("Samplers: {}", gltfModel.samplers.size());
	for (const tinygltf::Sampler& rSampler : gltfModel.samplers)
	{
		LOG("  {} {} {} {}", ToVkFilter(rSampler.minFilter), ToVkFilter(rSampler.magFilter), ToVkSamplerAddressMode(rSampler.wrapS), ToVkSamplerAddressMode(rSampler.wrapT));
		ASSERT(ToVkSamplerAddressMode(rSampler.wrapS) == VK_SAMPLER_ADDRESS_MODE_REPEAT);
	}

	auto [pHeader, dataSpan] = AllocateHeaderAndData(gltfModel.materials.size() * sizeof(common::GltfShaderData));
	common::GltfShaderData* pGltfShaderDatas = reinterpret_cast<common::GltfShaderData*>(dataSpan.data());

	LOG("Textures: {}", gltfModel.textures.size());
	pHeader->gltfHeader.uiTextureCount = 0;
	for (const tinygltf::Texture& rTexture : gltfModel.textures)
	{
		bool bOcclusion = IsOcclusion(pHeader->gltfHeader.uiTextureCount, gltfModel.materials[0]);

		std::filesystem::path relativeFile = mRelativeDirectory;
		relativeFile /= mInputPath.filename();
		relativeFile += ".Texture";
		relativeFile += std::to_string(rTexture.source);
		relativeFile += bOcclusion ? ".BC4_UNORM_BLOCK" : ".BC7_UNORM_BLOCK";
		pHeader->gltfHeader.pTextureCrcs[pHeader->gltfHeader.uiTextureCount++] = common::Crc(relativeFile.string());
	}

	LOG("Materials: {}", gltfModel.materials.size());

	pHeader->gltfHeader.uiMaterialCount = static_cast<uint32_t>(gltfModel.materials.size());

	size_t uiMaterialCount = 0;
	std::filesystem::path modelPath(mInputPath);
	modelPath += ".GLTF_MODEL";
	std::fstream fileStream(modelPath, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(&uiMaterialCount), sizeof(uiMaterialCount));
	ASSERT(uiMaterialCount == pHeader->gltfHeader.uiMaterialCount);
	fileStream.read(reinterpret_cast<char*>(&pHeader->gltfHeader.puiIndexStarts[0]), uiMaterialCount * sizeof(uint32_t));

	int64_t iMaterialIndex = 0;
	for (const tinygltf::Material& rMaterial : gltfModel.materials)
	{
		LOG("  {}: {}", iMaterialIndex++, rMaterial.name);
		if (rMaterial.doubleSided == true)
		{
			LOG("  Warning! Material is double sided");
		}

		common::GltfShaderData gltfShaderData {};

		if (rMaterial.values.find("baseColorFactor") != rMaterial.values.end())
		{
			double* pfData = rMaterial.values.at("baseColorFactor").ColorFactor().data();
			gltfShaderData.f4BaseColorFactor = XMFLOAT4(static_cast<float>(pfData[0]), static_cast<float>(pfData[1]), static_cast<float>(pfData[2]), static_cast<float>(pfData[3]));
		}

		gltfShaderData.f4EmissiveFactor = XMFLOAT4(static_cast<float>(rMaterial.emissiveFactor[0]), static_cast<float>(rMaterial.emissiveFactor[1]), static_cast<float>(rMaterial.emissiveFactor[2]), 1.0f);

		ASSERT(rMaterial.extensions.find("KHR_materials_pbrSpecularGlossiness") == rMaterial.extensions.end());
		gltfShaderData.fWorkflow = 0; // PBR_WORKFLOW_METALLIC_ROUGHNESS

		if (rMaterial.values.find("baseColorTexture") != rMaterial.values.end())
		{
			gltfShaderData.uiColorTextureIndex = static_cast<uint8_t>(rMaterial.values.at("baseColorTexture").TextureIndex());
			LOG("  baseColorTexture: {}", gltfShaderData.uiColorTextureIndex);
			gltfShaderData.iColorTextureSet = rMaterial.values.at("baseColorTexture").TextureTexCoord();
		}

		if (rMaterial.values.find("metallicRoughnessTexture") != rMaterial.values.end())
		{
			gltfShaderData.uiPhysicalDescriptorTextureIndex = static_cast<uint8_t>(rMaterial.values.at("metallicRoughnessTexture").TextureIndex());
			LOG("  metallicRoughnessTexture: {}", gltfShaderData.uiPhysicalDescriptorTextureIndex);
			gltfShaderData.iPhysicalDescriptorTextureSet = rMaterial.values.at("metallicRoughnessTexture").TextureTexCoord();
		}

		if (rMaterial.additionalValues.find("normalTexture") != rMaterial.additionalValues.end())
		{
			gltfShaderData.uiNormalTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("normalTexture").TextureIndex());
			LOG("  normalTexture: {}", gltfShaderData.uiNormalTextureIndex);
			gltfShaderData.iNormalTextureSet = rMaterial.additionalValues.at("normalTexture").TextureTexCoord();
		}		

		if (rMaterial.additionalValues.find("occlusionTexture") != rMaterial.additionalValues.end())
		{
			gltfShaderData.uiOcclusionTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("occlusionTexture").TextureIndex());
			LOG("  occlusionTexture: {}", gltfShaderData.uiOcclusionTextureIndex);
			gltfShaderData.iOcclusionTextureSet = rMaterial.additionalValues.at("occlusionTexture").TextureTexCoord();
		}

		if (rMaterial.additionalValues.find("emissiveTexture") != rMaterial.additionalValues.end())
		{
			gltfShaderData.uiEmissiveTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("emissiveTexture").TextureIndex());
			LOG("  emissiveTexture: {}", gltfShaderData.uiEmissiveTextureIndex);
			gltfShaderData.iEmissiveTextureSet = rMaterial.additionalValues.at("emissiveTexture").TextureTexCoord();
		}

		if (rMaterial.values.find("metallicFactor") != rMaterial.values.end())
		{
			gltfShaderData.fMetallicFactor = static_cast<float>(rMaterial.values.at("metallicFactor").Factor());
		}

		if (rMaterial.values.find("roughnessFactor") != rMaterial.values.end())
		{
			gltfShaderData.fRoughnessFactor = static_cast<float>(rMaterial.values.at("roughnessFactor").Factor());
		}

		if (rMaterial.alphaMode != "OPAQUE")
		{
			LOG("Warning: Material alphaMode is not OPAQUE (not yet supported in engine)");
		}
		ASSERT(rMaterial.alphaCutoff == 0.5f);
		if (rMaterial.additionalValues.find("alphaMode") != rMaterial.additionalValues.end())
		{
			LOG("Warning: Found alphaMode in material (not yet supported in engine)");
		}
		gltfShaderData.fAlphaMask = 0; // ALPHAMODE_OPAQUE
		gltfShaderData.fAlphaMaskCutoff = static_cast<float>(rMaterial.alphaCutoff);

		*(pGltfShaderDatas++) = gltfShaderData;
	}

#if 0
	if (gltfModel.animations.size() > 0)
	{
		loadAnimations(gltfModel);
	}
	
	loadSkins(gltfModel);

	for (auto& rNode : linearNodes)
	{
		if (rNode->skinIndex > -1)
		{
			rNode->skin = skins[rNode->skinIndex];
		}

		if (rNode->mesh)
		{
			rNode->update();
		}
	}
#endif
}
