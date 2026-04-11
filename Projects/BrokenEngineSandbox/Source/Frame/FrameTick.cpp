#include "FrameTick.h"

#include "Frame/FrameStaticData.h"

#include "Frame/FrameCollections.h"

namespace game
{

void RunFrameTick(const ActiveFrameRef& rRef, int64_t iTickCounter, float fCurrentTime)
{
	// Verify MXCSR has not been corrupted by external calls (audio, Vulkan, etc.)
	unsigned int uiControlWord = 0;
	_controlfp_s(&uiControlWord, 0, 0);
	ASSERT((uiControlWord & _MCW_DN) == _DN_FLUSH);
	ASSERT((uiControlWord & _MCW_RC) == _RC_NEAR);

	ASSERT(rRef.pNext != nullptr);
	ASSERT(rRef.pCurrent != nullptr);
	ASSERT(rRef.pStaticData != nullptr);
	Frame& rNext = *rRef.pNext;
	const Frame& rCurrent = *rRef.pCurrent;
	const engine::FrameStaticData& rStaticData = *rRef.pStaticData;

	// Phase 1: Interpolate
	FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
	FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime);
	rNext.interpolate.iTick = iTickCounter;
	rNext.interpolate.fCurrentTime = fCurrentTime;

	// Phase 2: PostRender
	FramePostRender::AllocateAndCopy(rNext.postRender, rCurrent.postRender);
	FramePostRender::Update(rNext, rCurrent, *rRef.pFrameInput, rStaticData);

	// Phase 3: Collision
	FramePostRender::PreCollision(rNext, rCurrent, rStaticData);
	engine::Collision::Collide(rNext.postRender.alignments, rStaticData.vecArea);
	FramePostRender::PostCollision(rNext, rCurrent, rStaticData);
	FramePostRender::AreaDamage(rNext, rCurrent, rStaticData);

	// Phase 4: Transfer
	FramePostRender::Transfer(rNext, rStaticData);

	// Phase 5: Destroy/Spawn
	FramePostRender::Destroy(rNext, rStaticData);
	FramePostRender::Spawn(rNext, *rRef.pFrameInput, rStaticData);

	// Compute CRCs after all phases complete
	rNext.postRender.sharedCrc = rNext.Crcs();
}

} // namespace game
