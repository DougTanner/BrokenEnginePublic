#pragma once

#include "Graphics/Camera.h"
#include "File/DifferenceStream.h"
#include "Frame/TimeStep.h"

namespace game
{

struct Frame;
struct MenuInput;
struct FrameInputHeld;
struct FrameInputPressed;

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
	void SyncReplay(game::FrameInputHeld& rFrameInputHeld, game::FrameInputPressed& rFrameInputPressed);
	void LoadFromReplayHeld(game::FrameInputHeld& rFrameInputHeld);

	template<typename DIFFERENCE_TYPE>
	void UpdateDifferenceStream(int64_t iFrame, DIFFERENCE_TYPE& rDifference, bool bIterate, std::unique_ptr<DifferenceStreamWriter<game::Frame, DIFFERENCE_TYPE>>& rpWriter, std::unique_ptr<DifferenceStreamReader<game::Frame, DIFFERENCE_TYPE>>& rpReader, const char* szLogSuffix);

	game::Frame& CurrentFrame() const
	{
		return *mpCurrentFrame;
	}

	game::Frame& NextFrame()
	{
		return *mpNextFrame;
	}

	bool mbQuit = false;

	TimeStep mTimeStep;

	bool mbSaveReplay = false;
	bool mbLoadReplay = false;
	std::unique_ptr<DifferenceStreamWriter<game::Frame, game::FrameInputHeld>> mpDifferenceStreamWriterHeld;
	std::unique_ptr<DifferenceStreamWriter<game::Frame, game::FrameInputPressed>> mpDifferenceStreamWriterPressed;
	std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInputHeld>> mpDifferenceStreamReaderHeld;
	std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInputPressed>> mpDifferenceStreamReaderPressed;

protected:

	MenuFlags_t mMenuFlags {MenuFlags::kMouseVisible, MenuFlags::kUpdateFrame};

	std::unique_ptr<game::Frame> mpCurrentFrame;
	std::unique_ptr<game::Frame> mpNextFrame;

	bool mbPreviousFrameUpdated = false;
};

template<typename DIFFERENCE_TYPE>
void GameBase::UpdateDifferenceStream(int64_t iFrame, DIFFERENCE_TYPE& rDifference, bool bIterate, std::unique_ptr<DifferenceStreamWriter<game::Frame, DIFFERENCE_TYPE>>& rpWriter, std::unique_ptr<DifferenceStreamReader<game::Frame, DIFFERENCE_TYPE>>& rpReader, const char* szLogSuffix)
{
	if (rpWriter != nullptr) [[unlikely]]
	{
		rpWriter->Update(iFrame, rDifference);
	}
	else if (rpReader != nullptr) [[unlikely]]
	{
		if (!rpReader->Update(iFrame, rDifference, bIterate))
		{
			LOG("End replay ({}) at {}", szLogSuffix, iFrame);
			common::BreakOnNotEqual(CurrentFrame(), rpReader->mHeader.savedEnd);
			rpReader.reset();
		}
	}
}

} // namespace engine
