#if defined(BT_CLIENT)

#include "Players.h"

#include "Data/Scene.h"

#include "Profile/ProfileManager.h"

namespace game
{

// Player model (also used by PlayersUpdate.cpp for animation lookup)
#if 1
extern const common::crc_t kPlayerModel = data::kModelsspaceship2scenegltfCrc;
constexpr float kfSize = 0.55f;
#endif

#if 0
extern const common::crc_t kPlayerModel = data::kModelsblack_dragon_with_idle_animationscenegltfCrc;
constexpr float kfSize = 3.0f;
#endif
#if 0
extern const common::crc_t kPlayerModel = data::kModelschernovan_nemesisscenegltfCrc;
constexpr float kfSize = 3.0f;
#endif
#if 0
extern const common::crc_t kPlayerModel = data::kModelsmirascenegltfCrc;
constexpr float kfSize = 0.1f;
#endif
#if 0
extern const common::crc_t kPlayerModel = data::kModelsDamagedHelmetDamagedHelmetgltfCrc;
constexpr float kfSize = 20.0f;
#endif
#if 0
extern const common::crc_t kPlayerModel = data::kModelsSpaceshipscenegltfCrc;
constexpr float kfSize = 0.1f;
#endif

// Render
constexpr float kfDeathShrinkPower = 2.0f;

void PlayersInterpolate::GraphicsResources()
{
	engine::Buffer* pStorageBuffers = engine::gpBufferManager->CreateDynamicBuffer(kCrc, engine::kBufferMain, kName, sizeof(shaders::ModelLayout));
	engine::gpPipelineManager->mDynamicPipelines.CreateModelPipeline(kCrc, kName, kPlayerModel, pStorageBuffers);
	engine::gpPipelineManager->mDynamicPipelines.CreateModelPipelineShadow(kCrc, kName, kPlayerModel, pStorageBuffers);
}

static int64_t siRendered = 0;

void PlayersInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords)
{
	siRendered = 0;

	int64_t iTotalCount = 0;
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it != rRenderInterpolates.end())
		{
			// Players uses iCount not iCapacity for buffer sizing since count is always small
			const game::FrameInterpolate& rInterp = it->second;
			int64_t iCount = (rInterp.gameFlags & GameFlags::kMainMenu) ? 0 : rInterp.pPlayers->iCount;
			iTotalCount += iCount;
		}
	}

	if (iTotalCount == 0)
	{
		return;
	}

	VkDeviceSize requiredSize = iTotalCount * sizeof(shaders::ModelLayout);
	engine::Buffer& rBuffer = engine::gpBufferManager->mDynamicStorageBuffers[engine::kBufferMain].at(kCrc).at(iCommandBuffer);
	if (rBuffer.mInfo.dataVkDeviceSize < requiredSize)
	{
		engine::gpBufferManager->ResizeDynamicBuffer(kCrc, engine::kBufferMain, kName, requiredSize, iCommandBuffer);
		int64_t iFramebuffer = iCommandBuffer;
		engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
		engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
	}
}

void PlayersInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerRenderPlayer);

	const PlayersInterpolate& rCurrent = *rFrameInterpolate.pPlayers;

	int64_t iCount = (rFrameInterpolate.gameFlags & GameFlags::kMainMenu) ? 0 : rCurrent.iCount;

	if (iCount == 0)
	{
		return;
	}

	auto [pPlayerLayouts, iBufferCapacity] = engine::gpBufferManager->GetDynamicStorageBuffer<shaders::ModelLayout>(kCrc, engine::kBufferMain, iCommandBuffer);

	for (int64_t i = 0; i < iCount; ++i)
	{
		float fSize = kfSize;
		if (rCurrent.pfDestroyedTimes[i] > 0.0f)
		{
			fSize *= std::pow(rCurrent.pfDestroyedTimes[i] / kfDestroyTime, kfDeathShrinkPower);
		}

		auto matScaling = XMMatrixScaling(fSize, fSize, fSize);
		auto matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);
		auto matRotationX = XMMatrixRotationX(XM_PIDIV2);
		auto matRotationY = XMMatrixRotationY(0.0f);
		auto matRotationZ = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));
		auto matRotationAccelerationX = XMMatrixRotationY(rCurrent.pfRotationAccelerationXs[i]);
		auto matRotationAccelerationY = XMMatrixRotationX(rCurrent.pfRotationAccelerationYs[i]);
		auto matTransform = XMMatrixMultiply(matRotationX, XMMatrixMultiply(matRotationY, XMMatrixMultiply(matRotationZ, XMMatrixMultiply(matRotationAccelerationX, XMMatrixMultiply(matRotationAccelerationY, XMMatrixMultiply(matScaling, matTranslation))))));

		shaders::ModelLayout& rPlayerLayout = pPlayerLayouts[siRendered];
		XMStoreFloat4(&rPlayerLayout.f4Position, rCurrent.pVecPositions[i]);

		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rPlayerLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rPlayerLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
		rPlayerLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 0.0f};
		rPlayerLayout.uiMeshDataBase = 0;

		// Evaluate animation and upload mesh shader data (only if model has skeletal animation)
		if (engine::gAnimationDataMap.contains(kPlayerModel))
		{
			const engine::AnimationData& rAnimationData = engine::gAnimationDataMap.at(kPlayerModel);
			const engine::EagerChunk& rChunk = engine::gpFileManager->GetEagerChunkMap().at(kPlayerModel);
			uint32_t uiMaterialCount = rChunk.pHeader->sceneHeader.uiMaterialCount;

			// Allocate mesh data region
			int64_t iMeshDataBase = engine::gpBufferManager->AllocateMeshData(iCommandBuffer, uiMaterialCount);
			rPlayerLayout.uiMeshDataBase = static_cast<uint32_t>(iMeshDataBase);

			// Get mesh data buffer
			common::MeshData* pMeshData = reinterpret_cast<common::MeshData*>(engine::gpBufferManager->mMeshDataStorageBuffers.at(iCommandBuffer).mpMappedMemory) + iMeshDataBase;

			// Count skinned materials for joint matrix allocation
			int64_t iSkinnedMaterialCount = rAnimationData.SkinnedMaterialCount(uiMaterialCount);

			// Allocate joint matrix region
			int64_t iJointMatrixOffset = engine::gpBufferManager->AllocateJointMatrices(iCommandBuffer, iSkinnedMaterialCount * rAnimationData.mHeader.skeleton.uiSkinJointCount);

			// Get joint matrix buffer
			common::JointMatrix* pJointMatrices = reinterpret_cast<common::JointMatrix*>(engine::gpBufferManager->mJointMatrixStorageBuffers.at(iCommandBuffer).mpMappedMemory);

			// Evaluate animation for all materials
			rAnimationData.EvaluateAnimation(0, rCurrent.pfAnimationTimes[i], uiMaterialCount, pMeshData, pJointMatrices, iJointMatrixOffset);
		}

		++siRendered;
	}
}

void PlayersInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
	engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
}

} // namespace game

#endif // BT_CLIENT
