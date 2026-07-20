#pragma once

#if defined(BT_SERVER)

namespace engine
{

class GameBase;

}

namespace game
{

class GameSaveLoad
{
public:

	enum class ReplayPersistenceFailurePoint : uint8_t
	{
		kNone,
		kManifestInvalidation,
		kGrid,
		kCoordinateWriter,
		kMetadata,
		kFinalManifest,
	};

	GameSaveLoad(engine::GameBase& rGameBase);

	bool ServerSave();
	bool ServerLoad();
	bool ServerSave(const std::filesystem::path& rFilename); // appdata-relative; caller validates the bare filename
	bool ServerLoad(const std::filesystem::path& rFilename);
	void ServerReset();
	void Autosave();
	void TickAutosave();
	bool Autoload();
	void SaveLoadReplay();
	bool SyncReplayTick(); // false when final replay reader retires and this fixed-tick iteration must stop before dispatch

	bool IsReplaying() const { return !mReplayReaders.empty(); }
	bool IsRecording() const { return !mReplayWriters.empty(); }
	void ResetStreams();
	void RetainReplayEndFrame(engine::GridCoord coord, std::unique_ptr<game::Frame>& rpFrame);
	bool DropRetainedReplayEndFrame(engine::GridCoord coord);
	bool ArmReplayPersistenceFailure(ReplayPersistenceFailurePoint eFailurePoint, engine::GridCoord coord = {});

	const std::unordered_map<engine::GridCoord, std::unique_ptr<engine::DifferenceStreamReader<game::Frame, game::FrameInput>>>& GetReplayReaders() const { return mReplayReaders; }

private:

	bool WriteGrid(const engine::FileFlags_t& rFlags, const std::filesystem::path& rFilename, engine::GridCoord clientGridCoord);
	bool ReadGrid(const engine::FileFlags_t& rFlags, const std::filesystem::path& rFilename, engine::GridCoord& rClientGridCoord);

	engine::GameBase& mrGameBase;

	static constexpr std::chrono::seconds kAutosaveInterval = 3600s;
	common::Timer mAutosaveTimer;

	struct ReplayWriterState
	{
		std::unique_ptr<engine::DifferenceStreamWriter<game::Frame, game::FrameInput>> pWriter;
		std::unique_ptr<game::Frame> pRetainedEndFrame;
		bool bTerminal = false;
	};

	std::unordered_map<engine::GridCoord, ReplayWriterState> mReplayWriters;
	std::unordered_map<engine::GridCoord, std::unique_ptr<engine::DifferenceStreamReader<game::Frame, game::FrameInput>>> mReplayReaders;
	ReplayPersistenceFailurePoint meReplayPersistenceFailurePoint = ReplayPersistenceFailurePoint::kNone;
	engine::GridCoord mReplayPersistenceFailureCoord {};

	bool ConsumeReplayPersistenceFailure(ReplayPersistenceFailurePoint eFailurePoint, engine::GridCoord coord = {});
};

} // namespace game

#endif // defined(BT_SERVER)
