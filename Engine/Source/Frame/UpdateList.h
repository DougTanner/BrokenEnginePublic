#pragma once

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInputHeld;

} // namespace game

namespace engine
{

struct UpdateList
{
	// Update
	static void Global(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, float) {};
	static void Interpolate(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, float) {};
	static void PostRender(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInput& __restrict, float) {};
	static void Collide(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInput& __restrict, float) {};
	static void Spawn(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInput& __restrict, float) {};
	static void Destroy(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInput& __restrict, float) {};

	// Render
	static void RenderGlobal(int64_t, const game::Frame& __restrict) {};
	static void RenderMain(int64_t, const game::Frame& __restrict) {};
};

} // namespace engine
