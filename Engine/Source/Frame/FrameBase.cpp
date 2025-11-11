#include "FrameBase.h"

#include "Graphics/Islands.h"

#include "Frame/Frame.h"
#include "Input/Input.h"


namespace engine
{

FrameBase::FrameBase(IslandsFlip eInitialIslandsFlip)
{
	gpIslands->SetIslandsFlip(eInitialIslandsFlip);

	camera.f4GlobalArea = gpIslands->mf4GlobalArea;

	Navmesh::SetupGrid(camera.f4GlobalArea, postRender.navmesh);
}

void WriteFrameCameraBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	SCOPED_CPU_PROFILE(kCpuTimerFrameCamera);

	auto& rCamera = rFrame.camera;
	const auto& rPreviousCamera = rPreviousFrame.camera;

	ASSERT(rPreviousCamera.eFrameType == FrameType::kPostRender);
	gCurrentFrameTypeProcessing = FrameType::kCamera;

	rCamera.fDeltaTime = fDeltaTime;

	rCamera.iFrame = rPreviousCamera.iFrame + 1;
	rCamera.eFrameType = FrameType::kCamera;

	rCamera.fCurrentTime = rPreviousCamera.fCurrentTime + rCamera.fDeltaTime;
	rCamera.randomEngine = rPreviousCamera.randomEngine;

	WriteFrameCamera(rFrame, rPreviousFrame, game::FrameInputHeld(), rCamera.fDeltaTime);

	GlobalList(rFrame, rPreviousFrame, game::FrameInputHeld(), rCamera.fDeltaTime, UPDATE_LIST);
}

// Interpolation phase: Copy pools from previous frame and interpolate positions/rotations
void WriteFrameInterpolateBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld)
{
	SCOPED_CPU_PROFILE(kCpuTimerFrameInterpolate);

	const auto& rCamera = rFrame.camera;
	auto& rInterpolate = rFrame.interpolate;
	const auto& rPreviousInterpolate = rPreviousFrame.interpolate;

	ASSERT(rCamera.eFrameType == FrameType::kCamera);
	gCurrentFrameTypeProcessing = FrameType::kInterpolate;
	rFrame.camera.eFrameType = FrameType::kInterpolate;

	Areas::Copy(rInterpolate.enemyAreas, rPreviousInterpolate.enemyAreas);
	Areas::Copy(rInterpolate.playerAreas, rPreviousInterpolate.playerAreas);
	AreaLights::Copy(rInterpolate.areaLights, rPreviousInterpolate.areaLights);
	Billboards::Copy(rInterpolate.billboards, rPreviousInterpolate.billboards);
	Explosions::Copy(rInterpolate.explosions, rPreviousInterpolate.explosions);
	HexShields::Copy(rInterpolate.hexShields, rPreviousInterpolate.hexShields);
	PointLights::Copy(rInterpolate.pointLights, rPreviousInterpolate.pointLights);
		rInterpolate.pointLightControllers2.UpdateMain(rInterpolate.pointLightControllers2, rPreviousInterpolate.pointLightControllers2, rInterpolate.pointLights, rCamera.fCurrentTime);
		rInterpolate.pointLightControllers3.UpdateMain(rInterpolate.pointLightControllers3, rPreviousInterpolate.pointLightControllers3, rInterpolate.pointLights, rCamera.fCurrentTime);
	Puffs::Copy(rInterpolate.puffs, rPreviousInterpolate.puffs);
		rInterpolate.puffControllers2.UpdateMain(rInterpolate.puffControllers2, rPreviousInterpolate.puffControllers2, rInterpolate.puffs, rCamera.fCurrentTime);
		rInterpolate.puffControllers3.UpdateMain(rInterpolate.puffControllers3, rPreviousInterpolate.puffControllers3, rInterpolate.puffs, rCamera.fCurrentTime);
	Pullers::Copy(rInterpolate.pullers, rPreviousInterpolate.pullers);
	Pushers::Copy(rInterpolate.pushers, rPreviousInterpolate.pushers);
	Sounds::Copy(rInterpolate.sounds, rPreviousInterpolate.sounds);
	Splashes::Copy(rInterpolate.splashes, rPreviousInterpolate.splashes);
	Targets::Copy(rInterpolate.targets, rPreviousInterpolate.targets);
	Trails::Copy(rInterpolate.trails, rPreviousInterpolate.trails);

	WriteFrameInterpolate(rFrame, rPreviousFrame, rFrameInputHeld, rCamera.fDeltaTime);

	InterpolateList(rFrame, rPreviousFrame, rFrameInputHeld, rCamera.fDeltaTime, UPDATE_LIST);

	Explosions::Interpolate(rFrame);
	Targets::Interpolate(rFrame);
}

// PostRender phase: PostRender, collision, spawning, and destruction
void WriteFramePostRenderBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, const game::FrameInputPressed& __restrict rFrameInputPressed)
{
	SCOPED_CPU_PROFILE(kCpuTimerFramePostRender);

	const auto& rCamera = rFrame.camera;
	auto& rInterpolate = rFrame.interpolate;
	auto& rPostRender = rFrame.postRender;

	ASSERT(rCamera.eFrameType == FrameType::kInterpolate);
	gCurrentFrameTypeProcessing = FrameType::kPostRender;
	rFrame.camera.eFrameType = FrameType::kPostRender;

	rPostRender.navmesh.SetupPlayerDistances(rFrame, rPreviousFrame);
	rInterpolate.pushers.SetupZones(rFrame);

	WriteFramePostRender(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rCamera.fDeltaTime);

	PostRenderList(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rCamera.fDeltaTime, UPDATE_LIST);
	Splashes::PostRender(rFrame, rCamera.fDeltaTime);

	CollideList(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rCamera.fDeltaTime, UPDATE_LIST);

	// Spawn second to last, because a spawned object has no information in the previous frame
	WriteFramePostRenderSpawn(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rCamera.fDeltaTime);
	SpawnList(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rCamera.fDeltaTime, UPDATE_LIST);

	// Destroy last, because this desynchronizes indices from previous frame
	WriteFramePostRenderDestroy(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rCamera.fDeltaTime);
	DestroyList(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, rCamera.fDeltaTime, UPDATE_LIST);

#if defined(ENABLE_PROFILING)
	gpProfileManager->mUpdatesInTheLastSecond.Set();
#endif
}

} // namespace engine
