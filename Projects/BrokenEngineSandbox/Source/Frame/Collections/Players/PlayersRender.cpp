#if defined(BT_CLIENT)

#include "Players.h"

#include "Data/Scene.h"
#include "Game.h"

#include "Graphics/Debug/DebugRender.h"
#include "Profile/ProfileManager.h"
#include "Frame/Collections/Spaceships/Spaceships.h"

namespace game
{

// Player model (also used by Players.cpp for animation lookup in PlayersInterpolate::Update)
#if 1
extern const common::crc_t kPlayerModel = data::kModelsspaceship2scenegltfCrc;
constexpr float kfModelScale = 0.3667f;
#endif

#if 0
extern const common::crc_t kPlayerModel = data::kModelsblack_dragon_with_idle_animationscenegltfCrc;
constexpr float kfModelScale = 2.0f;
#endif
#if 0
extern const common::crc_t kPlayerModel = data::kModelschernovan_nemesisscenegltfCrc;
constexpr float kfModelScale = 2.0f;
#endif
#if 0
extern const common::crc_t kPlayerModel = data::kModelsmirascenegltfCrc;
constexpr float kfModelScale = 0.0667f;
#endif
#if 0
extern const common::crc_t kPlayerModel = data::kModelsDamagedHelmetDamagedHelmetgltfCrc;
constexpr float kfModelScale = 13.333f;
#endif
#if 0
extern const common::crc_t kPlayerModel = data::kModelsSpaceshipscenegltfCrc;
constexpr float kfModelScale = 0.0667f;
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
		float fSize = kfPlayerRadius * kfModelScale;
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

void PlayersInterpolate::DebugRender(const FrameInterpolate& __restrict rFrameInterpolate, engine::GridCoord coord)
{
	if constexpr (!kbDebugRender) return;

	using enum PlayerFlags;

	// Entity positions MUST be read from rFrameInterpolate (the fully-interpolated frame),
	// never from PostRender. PostRender positions lag behind the rendered frame.
	// Only flags, metadata, and static world positions (nav waypoints, island destinations) come from PostRender.
	const PlayersInterpolate& rPlayers = *rFrameInterpolate.pPlayers;
	const SpaceshipsInterpolate& rSpaceships = *rFrameInterpolate.pSpaceships;
	const PlayersPostRender& rPostRender = *gpGame->RenderFrame(coord).postRender.pPlayers;

	int64_t iCount = (rFrameInterpolate.gameFlags & GameFlags::kMainMenu) ? 0 : rPlayers.iCount;

	for (int64_t i = 0; i < iCount; ++i)
	{
		XMVECTOR vecPosition = rPlayers.pVecPositions[i];

		// Red line to nearest alive spaceship
		float fClosestDistanceSq = std::numeric_limits<float>::max();
		XMVECTOR vecClosestPosition = XMVectorZero();
		bool bFound = false;

		for (int64_t j = 0; j < rSpaceships.iCount; ++j)
		{
			if (rSpaceships.pfDestroyedTimes[j] != -1.0f)
			{
				continue;
			}

			float fDistanceSq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vecPosition, rSpaceships.pVecPositions[j])));
			if (fDistanceSq < fClosestDistanceSq)
			{
				fClosestDistanceSq = fDistanceSq;
				vecClosestPosition = rSpaceships.pVecPositions[j];
				bFound = true;
			}
		}

		if (bFound)
		{
			XMFLOAT3A f3Start {};
			XMFLOAT3A f3End {};
			XMStoreFloat3A(&f3Start, vecPosition);
			XMStoreFloat3A(&f3End, vecClosestPosition);
			engine::DebugRender::Line(f3Start, f3End, {1.0f, 0.0f, 0.0f, 1.0f});
		}

		// Find flagship interpolated position (for mode 5 nav line + destination circle)
		int8_t iNavDirection = GetNavDirection(rPostRender.pFlags[i]);
		XMVECTOR vecFlagshipPosition = XMVectorZero();
		if (iNavDirection == 5)
		{
			for (int64_t j = 0; j < iCount; ++j)
			{
				if (j == i) continue;
				if (!(rPostRender.pFlags[j] & kIsFlagship)) continue;
				vecFlagshipPosition = rPlayers.pVecPositions[j];
				break;
			}
		}

		// Green line and circle at nav waypoint (when navigating)
		if (XMVectorGetW(rPostRender.pVecDebugNavWaypoints[i]) > 0.0f)
		{
			// In mode 5 (flagship follow), use the flagship's interpolated position as the waypoint
			XMVECTOR vecWaypoint = (iNavDirection == 5) ? vecFlagshipPosition : rPostRender.pVecDebugNavWaypoints[i];
			XMFLOAT3A f3NavStart {};
			XMFLOAT3A f3NavEnd {};
			XMStoreFloat3A(&f3NavStart, vecPosition);
			XMStoreFloat3A(&f3NavEnd, XMVectorSetZ(vecWaypoint, engine::gBaseHeight.Get()));
			engine::DebugRender::Line(f3NavStart, f3NavEnd, {0.0f, 1.0f, 0.0f, 1.0f});
			engine::DebugRender::Circle(f3NavEnd, kfPlayerRadius * 0.5f, {0.0f, 1.0f, 0.0f, 1.0f});
		}

		// Green circle at island destination
		if (iNavDirection == 5)
		{
			// Flagship follow: use the flagship's interpolated position
			XMFLOAT3A f3Dest {};
			XMStoreFloat3A(&f3Dest, XMVectorSetZ(vecFlagshipPosition, engine::gBaseHeight.Get()));
			engine::DebugRender::Circle(f3Dest, kfPlayerRadius * 2.0f, {0.0f, 1.0f, 0.0f, 1.0f});
		}
		else if (XMVectorGetW(rPostRender.pVecIslandDestinations[i]) > 0.0f)
		{
			XMFLOAT3A f3Dest {};
			XMStoreFloat3A(&f3Dest, XMVectorSetZ(rPostRender.pVecIslandDestinations[i], engine::gBaseHeight.Get()));
			engine::DebugRender::Circle(f3Dest, kfPlayerRadius * 2.0f, {0.0f, 1.0f, 0.0f, 1.0f});
		}
	}
}

} // namespace game

#endif // BT_CLIENT
