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

	auto& rChunkMap = gpFileManager->GetDataChunkMap();
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kShaderCompute) && !(rChunk.pHeader->flags & common::ChunkFlags::kShaderFragment) && !(rChunk.pHeader->flags & common::ChunkFlags::kShaderVertex))
		{
			continue;
		}

		auto [it, bInserted] = mShaders.try_emplace(rCrc, ShaderInfo {.pChunkHeader = rChunk.pHeader}, rChunk.pData);
		ASSERT(bInserted);
	}
}

ShaderManager::~ShaderManager()
{
	gpShaderManager = nullptr;
}

} // namespace engine
