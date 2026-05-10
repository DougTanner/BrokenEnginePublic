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
	void ServerReset();
	void Autosave();
	void TickAutosave();
	bool Autoload();
	void SaveLoadReplay();
	void SyncReplayTick();

	bool IsReplaying() const { return !mReplayReaders.empty(); }
	bool IsRecording() const { return !mReplayWriters.empty(); }
	void ResetStreams();

	const std::unordered_map<GridCoord, std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInput>>>& GetReplayReaders() const { return mReplayReaders; }

private:

	void WriteGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord clientGridCoord);
	bool ReadGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord& rClientGridCoord);

	GameBase& mrGameBase;

	static constexpr std::chrono::seconds kAutosaveInterval = 3600s;
	common::Timer mAutosaveTimer;

	std::unordered_map<GridCoord, std::unique_ptr<DifferenceStreamWriter<game::Frame, game::FrameInput>>> mReplayWriters;
	std::unordered_map<GridCoord, std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInput>>> mReplayReaders;
};

} // namespace engine
