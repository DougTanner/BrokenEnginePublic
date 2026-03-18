#include "FrameTick.h"

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

	Frame& rNext = *rRef.pNext;
	const Frame& rCurrent = *rRef.pCurrent;

	// Phase 1: Interpolate
	FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
	FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime);
	rNext.interpolate.iTick = iTickCounter;
	rNext.interpolate.fCurrentTime = fCurrentTime;

	// Phase 2: PostRender
	FramePostRender::AllocateAndCopy(rNext.postRender, rCurrent.postRender);
	FramePostRender::Update(rNext, rCurrent, *rRef.pFrameInput);

	// Phase 3: Collision
	FramePostRender::PreCollision(rNext, rCurrent);
	engine::Collision::Collide(rNext.postRender.alignments, rNext.postRender.vecArea);
	FramePostRender::PostCollision(rNext, rCurrent);
	FramePostRender::AreaDamage(rNext, rCurrent);

	// Phase 4: Transfer
	FramePostRender::Transfer(rNext);

	// Phase 5: Destroy/Spawn
	FramePostRender::Destroy(rNext);
	FramePostRender::Spawn(rNext, *rRef.pFrameInput);

	// Compute CRCs after all phases complete
	rNext.postRender.previousCrc = rCurrent.postRender.crc;
	rNext.postRender.previousInputCrc = rRef.pFrameInput->ServerInputCrc();
	rNext.postRender.crc = rNext.Crc();
	rNext.postRender.serverCrc = rNext.ServerCrc();
}

} // namespace game
