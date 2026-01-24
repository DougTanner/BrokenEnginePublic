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

		auto [it, bInserted] = mShaders.try_emplace(rCrc, ShaderInfo {.pChunkHeader = rChunk.pHeader}, rChunk.pData);
		Assert(bInserted);
	}
}

ShaderManager::~ShaderManager()
{
	gpShaderManager = nullptr;
}

} // namespace engine
