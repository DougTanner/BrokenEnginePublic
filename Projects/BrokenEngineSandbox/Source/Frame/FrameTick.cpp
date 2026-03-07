#include "FrameTick.h"

#include "Frame/FrameCollections.h"

namespace game
{

void RunFrameTick(const ActiveFrameRef& rRef, int64_t iFrameCounter, float fCurrentTime)
{
	Frame& rNext = *rRef.pNext;
	const Frame& rCurrent = *rRef.pCurrent;

	// Phase 1: Interpolate
	FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
	FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime);
	rNext.interpolate.iFrame = iFrameCounter;
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
}

} // namespace game
