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
		kInventory,
		kFinalManifest,
	};

	struct ReplayTransferCaptureInfo
	{
		int64_t iRecordingEventTick = -1;
		int64_t iPlaybackEventTick = -1;
		int64_t iWriterInputTick = -1;
		int64_t iFollowingEmptyInputTick = -1;
		int64_t iFirstWriterInputTick = -1;
		int64_t iWriterInputCount = 0;
		int64_t iPlayerCount = 0;
		int64_t iSpaceshipCount = 0;
		int64_t iBlasterCount = 0;
		int64_t iMissileCount = 0;
		int64_t iPauseAfterWriterInputCount = -1;
	};

	ReplayTransferCaptureInfo mReplayTransferCaptureInfo;

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

	bool IsReplaying() const
	{
		return !mReplayReaders.empty() || !mPendingReplayReaders.empty();
	}
	bool IsRecording() const { return !mReplayWriters.empty(); }
	void ResetStreams();
	void CaptureHarvestedTransfers(engine::GridCoord coord, std::span<const StatusChange> transfers, const game::Frame& rPreTransferFrame);
	void RetainReplayEndFrame(engine::GridCoord coord, std::unique_ptr<game::Frame>& rpFrame);
	bool DropRetainedReplayEndFrame(engine::GridCoord coord);
	bool ArmReplayPersistenceFailure(ReplayPersistenceFailurePoint eFailurePoint, engine::GridCoord coord = {});

	const std::unordered_map<engine::GridCoord, std::unique_ptr<engine::DifferenceStreamReader<game::Frame, game::FrameInput>>>& GetReplayReaders() const { return mReplayReaders; }

private:

	struct StagedGrid;

	bool WriteGrid(const engine::FileFlags_t& rFlags, const std::filesystem::path& rFilename, engine::GridCoord clientGridCoord);
	bool ReadGrid(const engine::FileFlags_t& rFlags, const std::filesystem::path& rFilename, engine::GridCoord& rClientGridCoord);
	bool ReadGrid(const engine::FileFlags_t& rFlags, const std::filesystem::path& rFilename, StagedGrid& rStagedGrid);
	void AdoptGrid(StagedGrid&& rStagedGrid);

	engine::GameBase& mrGameBase;

	static constexpr std::chrono::seconds kAutosaveInterval = 3600s;
	common::Timer mAutosaveTimer;

	struct ReplayWriterState
	{
		std::unique_ptr<engine::DifferenceStreamWriter<game::Frame, game::FrameInput>> pWriter;
		std::unique_ptr<game::Frame> pRetainedEndFrame;
		std::vector<StatusChange> pendingTransfers;
		int64_t iActivationTick = 0;
		bool bTerminal = false;
	};

	struct PendingReplayReader
	{
		std::unique_ptr<engine::DifferenceStreamReader<game::Frame, game::FrameInput>> pReader;
		std::unique_ptr<game::Frame> pSavedStart;
		game::FrameInput initialInput;
		int64_t iActivationTick = 0;
	};

	std::unordered_map<engine::GridCoord, ReplayWriterState> mReplayWriters;
	std::unordered_map<engine::GridCoord, std::unique_ptr<engine::DifferenceStreamReader<game::Frame, game::FrameInput>>> mReplayReaders;
	std::unordered_map<engine::GridCoord, PendingReplayReader> mPendingReplayReaders;
	int64_t miReplayInitialTick = 0;
	ReplayPersistenceFailurePoint meReplayPersistenceFailurePoint = ReplayPersistenceFailurePoint::kNone;
	engine::GridCoord mReplayPersistenceFailureCoord {};

	bool ConsumeReplayPersistenceFailure(ReplayPersistenceFailurePoint eFailurePoint, engine::GridCoord coord = {});
	game::FrameInput ComposeReplayInput(const game::FrameInput& rLiveInput, ReplayWriterState& rWriterState);
	bool UpdateTerminalReplayWriter(engine::GridCoord coord, ReplayWriterState& rWriterState, const game::Frame& rEndFrame);
	void ActivateReplayReader(engine::GridCoord coord, PendingReplayReader&& rPendingReader);
};

} // namespace game

#endif // defined(BT_SERVER)
