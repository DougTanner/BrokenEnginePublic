#pragma once

#include "File/DifferenceStream.h"
#include "Frame/TimeStep.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInputHeld;
struct MenuInput;

}

namespace engine
{

struct RawInput;

enum class MenuFlags : uint64_t
{
	kMouseVisible = 0x01,
	kUpdateFrame  = 0x02,
};
using MenuFlags_t = common::Flags<MenuFlags>;

class GameBase
{
public:

	GameBase();
	virtual ~GameBase() = default;

	virtual void Reset() = 0;
	virtual bool ShouldUpdateFrame() = 0;
	virtual void EndReplay(game::FrameInput& rFrameInput) = 0;

	void ResetRealTime();
	bool Update(bool bSingleStep, bool bLostFocus, const engine::RawInput& rRawInput, game::MenuInput& rMenuInput, game::FrameInput& rFrameInput);

	game::Frame& CurrentFrame()
	{
		return *mpCurrentFrame;
	}

	game::Frame& NextFrame()
	{
		return *mpNextFrame;
	}

	TimeStep mTimeStep;

	std::unique_ptr<DifferenceStreamWriter<game::Frame, game::FrameInput>> mpDifferenceStreamWriter;
	std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInput>> mpDifferenceStreamReader;

protected:

	MenuFlags_t mMenuFlags {MenuFlags::kMouseVisible, MenuFlags::kUpdateFrame};

	std::unique_ptr<game::Frame> mpCurrentFrame;
	std::unique_ptr<game::Frame> mpNextFrame;

private:

	void UpdatePhysicsSteps(int64_t iUpdates, const engine::RawInput& rRawInput, game::MenuInput& rMenuInput, game::FrameInput& rFrameInput);
	void UpdateSinglePhysicsStep(bool bFirstStep, game::FrameInput& rFrameInput);
	void HandleReplay(game::FrameInput& rFrameInput);
	void SwapFrames(game::FrameInput& rFrameInput);
	void CreateInterpolatedFrame(int64_t iUpdates, const engine::RawInput& rRawInput, game::MenuInput& rMenuInput, game::FrameInput& rFrameInput);
};

} // namespace engine
