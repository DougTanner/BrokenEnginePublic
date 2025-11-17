#pragma once

#include "Graphics/Camera.h"
#include "File/DifferenceStream.h"
#include "Frame/TimeStep.h"

namespace game
{

struct Frame;
struct MenuInput;
struct FrameInput;

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
	virtual std::filesystem::path AutosaveFile() = 0;
	virtual std::filesystem::path QuicksaveFile() = 0;
	virtual std::filesystem::path ReplayFile() = 0;

	void ResetRealTime();
	void UpdateFramesAndRender(const game::MenuInput& rMenuInput, bool bLostFocus);

	void Quicksave(const game::MenuInput& rMenuInput);
	bool Quickload(const game::MenuInput& rMenuInput);
	void SaveLoadReplay(const game::MenuInput& rMenuInput);
	void SyncReplay(game::Frame& rFrame, game::FrameInput& rFrameInput);
	
	game::Frame& CurrentFrame() const
	{
		return *mpCurrentFrame;
	}

	game::Frame& NextFrame()
	{
		return *mpNextFrame;
	}

	// DT: TODO Bools to flags
	bool mbQuit = false;

	TimeStep mTimeStep;

	bool mbSaveReplay = false;
	bool mbLoadReplay = false;
	std::unique_ptr<DifferenceStreamWriter<game::Frame, game::FrameInput>> mpDifferenceStreamWriter;
	std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInput>> mpDifferenceStreamReader;

protected:

	MenuFlags_t mMenuFlags {MenuFlags::kMouseVisible, MenuFlags::kUpdateFrame};

	std::unique_ptr<game::Frame> mpCurrentFrame;
	std::unique_ptr<game::Frame> mpNextFrame;

	bool mbPreviousFrameUpdated = false;
};

} // namespace engine
