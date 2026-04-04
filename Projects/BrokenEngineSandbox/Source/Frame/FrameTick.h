#pragma once

namespace engine { struct FrameStaticData; }

namespace game
{

struct Frame;
struct FrameInput;

struct ActiveFrameRef
{
	Frame* pNext = nullptr;
	Frame* pCurrent = nullptr;
	FrameInput* pFrameInput = nullptr;
	const engine::FrameStaticData* pStaticData = nullptr;
};

// Runs all tick phases for a single Frame (Interpolate, PostRender, Collision, Transfer, Destroy/Spawn)
void RunFrameTick(const ActiveFrameRef& rRef, int64_t iTickCounter, float fCurrentTime);

} // namespace game
