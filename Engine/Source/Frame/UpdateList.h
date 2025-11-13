#pragma once

namespace game
{

struct Frame;
struct FrameInputHeld;
struct FrameInputPressed;

} // namespace game

namespace engine
{

struct UpdateList
{
	// Update
	static void MoveCamera(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, float) {};
	static void Interpolate(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, float) {};
	static void PostRender(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, const game::FrameInputPressed& __restrict, float) {};
	static void Collide(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, const game::FrameInputPressed& __restrict, float) {};
	static void Spawn(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, const game::FrameInputPressed& __restrict, float) {};
	static void Destroy(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, const game::FrameInputPressed& __restrict, float) {};

	// Render
	static void RenderMoveCamera(int64_t, const game::Frame& __restrict) {};
	static void RenderMain(int64_t, const game::Frame& __restrict) {};
};

} // namespace engine
