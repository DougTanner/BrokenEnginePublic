#if defined(BT_CLIENT)

#include "Missiles.h"

#include "Data/Scene.h"

#include "Profile/ProfileManager.h"

namespace game
{

// Missile rendering
constexpr float kfMissileScale = 0.5f;
constexpr float kfMissileWidth = 2.0f;

constexpr common::crc_t kMissileModelCrc = data::kModelsaim9_missilescenegltfCrc;

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

void MissilesInterpolate::GraphicsResources()
{
	engine::Buffer* pStorageBuffers = engine::gpBufferManager->CreateDynamicBuffer(kCrc, engine::kBufferMain, kName, sizeof(shaders::ModelLayout));
	engine::gpPipelineManager->mDynamicPipelines.CreateModelPipeline(kCrc, kName, kMissileModelCrc, pStorageBuffers);
	engine::gpPipelineManager->mDynamicPipelines.CreateModelPipelineShadow(kCrc, kName, kMissileModelCrc, pStorageBuffers);
}

void MissilesInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords)
{
	siRendered = 0;

	int64_t iTotalCapacity = 0;
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it != rRenderInterpolates.end())
		{
			iTotalCapacity += it->second.pMissiles->iCapacity;
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

void MissilesInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	const MissilesInterpolate& rCurrent = *rFrameInterpolate.pMissiles;
	gpProfileManager->SetCount(game::kCpuCounterMissiles, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		return;
	}

	RenderActiveGuard renderActiveGuard;

	static const XMMATRIX sMatPreMove = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
	static const XMMATRIX sMatPreRotate = XMMatrixRotationX(XM_PIDIV2) * XMMatrixRotationZ(XM_PIDIV2);

	auto [pLayouts, iBufferCapacity] = engine::gpBufferManager->GetDynamicStorageBuffer<shaders::ModelLayout>(kCrc, engine::kBufferMain, iCommandBuffer);
	ASSERT(siRendered + rCurrent.iCount <= iBufferCapacity);

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rCurrent.pVecPositions[i]);
		if (!gpCamera->InVisibleArea(gpCamera->f4RenderVisibleArea, f4Position))
		{
			continue;
		}

		// Sentinel value: 0.0f means explosion finished, skip rendering
		if (rCurrent.pfDestroyedTimes[i] == 0.0f)
		{
			continue;
		}

		float fScale = kfMissileScale;
		if (rCurrent.pfDestroyedTimes[i] > 0.0f)
		{
			fScale *= std::pow(rCurrent.pfDestroyedTimes[i] / kfMissileDestroyTime, 0.5f);
		}

		XMMATRIX matScaling = XMMatrixScaling(kfMissileWidth * fScale, fScale, fScale);
		XMMATRIX matYaw = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
		XMMATRIX matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);
		XMMATRIX matTransform = sMatPreMove * matScaling * sMatPreRotate * matYaw * matTranslation;

		shaders::ModelLayout& rModelLayout = pLayouts[siRendered++];
		rModelLayout.f4Position = f4Position;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rModelLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rModelLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
		rModelLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 0.0f};
		rModelLayout.uiMeshDataBase = 0;
	}
}

void MissilesInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(game::kCpuCounterMissilesRendered, siRendered);
	engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
	engine::gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
}

} // namespace game

#endif // BT_CLIENT
