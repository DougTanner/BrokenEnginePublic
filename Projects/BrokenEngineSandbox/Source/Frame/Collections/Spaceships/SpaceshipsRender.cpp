#if defined(BT_CLIENT)

#include "Spaceships.h"

#include "Data/Scene.h"

#include "Profile/ProfileManager.h"

namespace game
{

// Spaceship model (also used by Spaceships.cpp for animation lookup)
#if 1
extern const common::crc_t kSpaceshipModel = data::kModelsSpaceshipscenegltfCrc;
constexpr float kfModelScale = 0.00175f;
#endif
#if 0
extern const common::crc_t kSpaceshipModel = data::kModelschernovan_nemesisscenegltfCrc;
constexpr float kfModelScale = 0.15f;
#endif

// Spaceship rendering
constexpr float kfRoll = 0.2f;

void SpaceshipsInterpolate::GraphicsResources()
{
	engine::Buffer* pStorageBuffers = engine::gpBufferManager->CreateDynamicBuffer(kCrc, engine::kBufferMain, kName, sizeof(shaders::ModelLayout));
	engine::gpPipelineManager->mDynamicPipelines.CreateModelPipeline(kCrc, kName, kSpaceshipModel, pStorageBuffers);
	engine::gpPipelineManager->mDynamicPipelines.CreateModelPipelineShadow(kCrc, kName, kSpaceshipModel, pStorageBuffers);
}

static int64_t siRendered = 0;
// Non-concurrency tripwire: per-frame Render calls must run sequentially across active coords — siRendered
// accumulates across them and offsets each call's slab writes. Parallelizing coord renders would race.
static std::atomic<bool> sbRenderActive = false;

namespace
{
// RAII tripwire guard: sets sbRenderActive on entry, clears it on scope exit — so an exception between the
// capacity ASSERT and the slab writes unwinds it instead of wedging it true (every later Render would else false-assert).
struct RenderActiveGuard
{
	RenderActiveGuard()
	{
		ASSERT(!sbRenderActive.exchange(true));
	}

	~RenderActiveGuard()
	{
		sbRenderActive.store(false);
	}
};
} // namespace

void SpaceshipsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords)
{
	siRendered = 0;

	int64_t iTotalCapacity = 0;
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it != rRenderInterpolates.end())
		{
			iTotalCapacity += it->second.pSpaceships->iCapacity;
		}
	}

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (engine::Buffer* pBuffer = engine::gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, engine::kBufferMain, kName, sizeof(shaders::ModelLayout), iTotalCapacity, iCommandBuffer))
	{
		engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->UpdateStorageBufferDescriptors(iCommandBuffer, 2, pBuffer);
		engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->UpdateStorageBufferDescriptors(iCommandBuffer, 2, pBuffer);
	}
}

void SpaceshipsInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerRenderSpaceships);

	const SpaceshipsInterpolate& rCurrent = *rFrameInterpolate.pSpaceships;
	gpProfileManager->SetCount(game::kCpuCounterSpaceships, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		return;
	}

	RenderActiveGuard renderActiveGuard;

	static const XMMATRIX sMatPreRotate = XMMatrixRotationX(XM_PIDIV2) * XMMatrixRotationY(0.0f) * XMMatrixRotationZ(XM_PIDIV2);

	auto [pLayouts, iBufferCapacity] = engine::gpBufferManager->GetDynamicStorageBuffer<shaders::ModelLayout>(kCrc, engine::kBufferMain, iCommandBuffer);
	ASSERT(siRendered + rCurrent.iCount <= iBufferCapacity);

	// Look up animation data and chunk info (hoisted outside loop)
	const engine::AnimationData* pAnimationData = nullptr;
	uint32_t uiMaterialCount = 0;
	int64_t iSkinnedMaterialCount = 0;
	if (engine::gAnimationDataMap.contains(kSpaceshipModel))
	{
		pAnimationData = &engine::gAnimationDataMap.at(kSpaceshipModel);
		uiMaterialCount = engine::gpFileManager->GetEagerChunkMap().at(kSpaceshipModel).pHeader->sceneHeader.uiMaterialCount;

		iSkinnedMaterialCount = pAnimationData->SkinnedMaterialCount(uiMaterialCount);
	}

	// Pass 1: Visibility cull (main thread) — build compacted visible index list
	auto pVisibleIndices = common::gpThreadLocal->mWorkbuffer.PushBuffer<int64_t*>(rCurrent.iCount * static_cast<int64_t>(sizeof(int64_t)));
	int64_t iVisibleCount = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		if (rCurrent.pfDestroyedTimes[i] == 0.0f)
		{
			continue;
		}

		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rCurrent.pVecPositions[i]);
		if (!gpCamera->InVisibleArea(gpCamera->f4RenderVisibleArea, f4Position))
		{
			continue;
		}

		pVisibleIndices[iVisibleCount++] = i;
	}

	// Bulk skinning pre-allocation (main thread) — one call instead of N per-spaceship calls
	int64_t iMeshDataBase = 0;
	int64_t iJointBase = 0;
	common::MeshData* pMeshDataBuffer = nullptr;
	common::JointMatrix* pJointMatricesBuffer = nullptr;
	int64_t iJointsPerShip = 0;

	if (pAnimationData != nullptr && iVisibleCount > 0)
	{
		iMeshDataBase = engine::gpBufferManager->AllocateMeshData(iCommandBuffer, iVisibleCount * uiMaterialCount);
		pMeshDataBuffer = reinterpret_cast<common::MeshData*>(engine::gpBufferManager->mMeshDataStorageBuffers.at(iCommandBuffer).mpMappedMemory);

		iJointsPerShip = iSkinnedMaterialCount * pAnimationData->mHeader.skeleton.uiSkinJointCount;
		int64_t iTotalJoints = iVisibleCount * iJointsPerShip;
		if (iTotalJoints > 0)
		{
			iJointBase = engine::gpBufferManager->AllocateJointMatrices(iCommandBuffer, iTotalJoints);
		}
		pJointMatricesBuffer = reinterpret_cast<common::JointMatrix*>(engine::gpBufferManager->mJointMatrixStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	}

	// Capture current siRendered offset for this frame's writes
	int64_t iRenderedOffset = siRendered;

	// Per-range processing lambda — each visible index j writes to deterministic non-overlapping output slots
	auto processRange = [&](int64_t iStart, int64_t iEnd)
	{
		for (int64_t j = iStart; j < iEnd; ++j)
		{
			int64_t i = pVisibleIndices[j];

			float fSize = kfSpaceshipRadius * kfModelScale;
			if (rCurrent.pfDestroyedTimes[i] > 0.0f)
			{
				fSize *= std::pow(rCurrent.pfDestroyedTimes[i] / kfSpaceshipDestroyTime, 0.75f);
			}

			XMFLOAT4A f4Position {};
			XMStoreFloat4A(&f4Position, rCurrent.pVecPositions[i]);

			XMMATRIX matScaling = XMMatrixScaling(fSize, fSize, fSize);
			XMMATRIX matRoll = XMMatrixRotationX(-kfRoll * rCurrent.pfDeltaRotations[i]);
			XMMATRIX matYaw = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
			XMMATRIX matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);
			XMMATRIX matTransform = matScaling * sMatPreRotate * matRoll * matYaw * matTranslation;

			shaders::ModelLayout& rModelLayout = pLayouts[iRenderedOffset + j];
			rModelLayout.f4Position = f4Position;
			XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rModelLayout.f3x4Transform[0]), matTransform);
			XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rModelLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));

			rModelLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 0.0f};
			rModelLayout.uiMeshDataBase = 0;

			if (pAnimationData != nullptr)
			{
				int64_t iShipMeshDataBase = iMeshDataBase + j * uiMaterialCount;
				rModelLayout.uiMeshDataBase = static_cast<uint32_t>(iShipMeshDataBase);

				common::MeshData* pMeshData = pMeshDataBuffer + iShipMeshDataBase;

				int64_t iJointMatrixOffset = iJointBase + j * iJointsPerShip;

				pAnimationData->EvaluateAnimation(0, rCurrent.pfAnimationTimes[i], uiMaterialCount, pMeshData, pJointMatricesBuffer, iJointMatrixOffset);
			}
		}
	};

	gpProfileManager->GetCpuTimer(game::kCpuTimerRenderSpaceships).iThreads = common::gpMultithreading->WorkerCount() + 1;
	common::gpMultithreading->Dispatch(iVisibleCount, processRange);

	siRendered += iVisibleCount;
}

void SpaceshipsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(game::kCpuCounterSpaceshipsRendered, siRendered);
	engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
	engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
}

} // namespace game

#endif // BT_CLIENT
