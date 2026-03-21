#pragma once

namespace engine
{

class GameBase;

class GameSaveLoad
{
public:

	GameSaveLoad(GameBase& rGameBase);

	void Quicksave(const game::MenuInput& rMenuInput);
	bool Quickload(const game::MenuInput& rMenuInput);
	void ServerSave();
	bool ServerLoad();
	void SaveLoadReplay(const game::MenuInput& rMenuInput);
	void SyncReplay(game::Frame& rFrame, game::FrameInput& rFrameInput);

	bool IsReplaying() const { return mpDifferenceStreamReader != nullptr; }
	bool IsRecording() const { return mpDifferenceStreamWriter != nullptr; }
	void ResetStreams();

	std::unique_ptr<DifferenceStreamWriter<game::Frame, game::FrameInput>> mpDifferenceStreamWriter;
	std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInput>> mpDifferenceStreamReader;

private:

	void WriteGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord humanGridCoord);
	bool ReadGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord& rHumanGridCoord);

	GameBase& mrGameBase;
};

} // namespace engine
