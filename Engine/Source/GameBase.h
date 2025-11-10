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

	void ResetRealTime();
	void UpdateFramesAndRender(game::FrameInput& rFrameInput, bool bLostFocus);

	game::Frame& CurrentFrame()
	{
		return *mpCurrentFrame;
	}

	game::Frame& NextFrame()
	{
		return *mpNextFrame;
	}

	bool mbQuit = false;

	TimeStep mTimeStep;

	std::unique_ptr<DifferenceStreamWriter<game::Frame, game::FrameInput>> mpDifferenceStreamWriter;
	std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInput>> mpDifferenceStreamReader;

protected:

	MenuFlags_t mMenuFlags {MenuFlags::kMouseVisible, MenuFlags::kUpdateFrame};

	std::unique_ptr<game::Frame> mpCurrentFrame;
	std::unique_ptr<game::Frame> mpNextFrame;

	bool mbPreviousFrameUpdated = false;

private:

	void HandleReplay(int64_t iFrame, const game::Frame& rFrame, game::FrameInput& rFrameInput);
};

} // namespace engine
