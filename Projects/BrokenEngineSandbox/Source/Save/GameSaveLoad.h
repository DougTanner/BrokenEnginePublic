#pragma once

namespace engine
{

class GameBase;

}

namespace game
{

class GameSaveLoad
{
public:

	GameSaveLoad(engine::GameBase& rGameBase);

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

	const std::unordered_map<engine::GridCoord, std::unique_ptr<engine::DifferenceStreamReader<game::Frame, game::FrameInput>>>& GetReplayReaders() const { return mReplayReaders; }

private:

	void WriteGrid(const engine::FileFlags_t& rFlags, const std::filesystem::path& rFilename, engine::GridCoord clientGridCoord);
	bool ReadGrid(const engine::FileFlags_t& rFlags, const std::filesystem::path& rFilename, engine::GridCoord& rClientGridCoord);

	engine::GameBase& mrGameBase;

	static constexpr std::chrono::seconds kAutosaveInterval = 3600s;
	common::Timer mAutosaveTimer;

	std::unordered_map<engine::GridCoord, std::unique_ptr<engine::DifferenceStreamWriter<game::Frame, game::FrameInput>>> mReplayWriters;
	std::unordered_map<engine::GridCoord, std::unique_ptr<engine::DifferenceStreamReader<game::Frame, game::FrameInput>>> mReplayReaders;
};

} // namespace game
