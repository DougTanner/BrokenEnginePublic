#include "ShaderManager.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

ShaderManager::ShaderManager()
{
	gpShaderManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerShaderManager);

	const std::unordered_map<common::crc_t, EagerChunk>& rChunkMap = gpFileManager->GetEagerChunkMap();
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kShader))
		{
			continue;
		}

		if constexpr (!kbEnableDebugPrintf)
		{
			if (strcmp(rChunk.pHeader->pcPath, "Shaders\\Log.vert") == 0)
			{
				continue;
			}
		}

		const common::ShaderHeader& sh = rChunk.pHeader->shaderHeader;
		int64_t iBindingsSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(sh.iDescriptorSetLayoutBindings * static_cast<int64_t>(sizeof(VkDescriptorSetLayoutBinding)));
		int64_t iSetIndicesSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(sh.iDescriptorSetLayoutBindings * static_cast<int64_t>(sizeof(uint32_t)));
		int64_t iAttrsSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(sh.iVertexInputAttributeDescriptions * static_cast<int64_t>(sizeof(VkVertexInputAttributeDescription)));

		ShaderInfo info
		{
			.pChunkHeader = rChunk.pHeader,
			.pDescriptorBindings = reinterpret_cast<const VkDescriptorSetLayoutBinding*>(rChunk.pData),
			.pDescriptorSetIndices = reinterpret_cast<const uint32_t*>(rChunk.pData + iBindingsSize),
			.pVertexAttributes = reinterpret_cast<const VkVertexInputAttributeDescription*>(rChunk.pData + iBindingsSize + iSetIndicesSize),
			.iSpirvSize = rChunk.pHeader->iSize - iBindingsSize - iSetIndicesSize - iAttrsSize,
		};
		auto [it, bInserted] = mShaders.try_emplace(rCrc, info, rChunk.pData + iBindingsSize + iSetIndicesSize + iAttrsSize);
		ASSERT(bInserted);
	}
}

ShaderManager::~ShaderManager()
{
	gpShaderManager = nullptr;
}

} // namespace engine
