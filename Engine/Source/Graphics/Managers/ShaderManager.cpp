#include "ShaderManager.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

ShaderManager::ShaderManager()
{
	gpShaderManager = this;

	SCOPED_BOOT_TIMER(kBootTimerShaderManager);

	const std::unordered_map<common::crc_t, EagerChunk>& rChunkMap = gpFileManager->GetEagerChunkMap();
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kShader))
		{
			continue;
		}

	#if !defined(ENABLE_DEBUG_PRINTF_EXT)
		if (strcmp(rChunk.pHeader->pcPath, "Shaders\\Log.vert") == 0)
		{
			continue;
		}
	#endif

		auto [it, bInserted] = mShaders.try_emplace(rCrc, ShaderInfo {.pChunkHeader = rChunk.pHeader}, rChunk.pData);
		ASSERT(bInserted);
	}
}

ShaderManager::~ShaderManager()
{
	gpShaderManager = nullptr;
}

} // namespace engine
