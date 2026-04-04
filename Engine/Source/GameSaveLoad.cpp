#include "GameSaveLoad.h"

#if defined(BT_SERVER)

#include "GameBase.h"
#include "Game.h"
#include "Network/ServerSession.h"
#include "Profile/ProfileManager.h"

namespace engine
{

GameSaveLoad::GameSaveLoad(GameBase& rGameBase)
	: mrGameBase(rGameBase)
{
}

void GameSaveLoad::ResetStreams()
{
	mReplayWriters.clear();
	mReplayReaders.clear();
}

void GameSaveLoad::Quicksave([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	// Heap: fstream and Frame serialization (stream must stay open across the full write so push/pop
	//   lifecycle doesn't apply, and SOA collection data must persist in the Frame after deserialization)
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuicksave)
		{
			WriteGrid({FileFlags::kAppDataDirectory, FileFlags::kWrite}, mrGameBase.QuicksaveFile(), game::gpGame->mClientGridCoord);
		}
	}
}

void GameSaveLoad::ServerSave()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	WriteGrid({FileFlags::kAppDataDirectory, FileFlags::kWrite}, mrGameBase.QuicksaveFile(), game::gpGame->mClientGridCoord);
}

bool GameSaveLoad::ServerLoad()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	GridCoord loadedClientGridCoord {};
	if (!ReadGrid({FileFlags::kAppDataDirectory, FileFlags::kRead}, mrGameBase.QuicksaveFile(), loadedClientGridCoord))
	{
		return false;
	}

	mrGameBase.Reset();
	game::gpGame->mClientGridCoord = loadedClientGridCoord;
	game::gpServerSession->ResetClientsForLoad();
	game::gpServerSession->ComputeActiveSet();

	return true;
}

void GameSaveLoad::ServerReset()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	game::gpGame->CreateNewFrame(game::GameFlags::kGame);
	mrGameBase.miNextGlobalId = 1;
	mrGameBase.Reset();
	game::gpServerSession->ResetClientsForLoad();
	game::gpServerSession->ComputeActiveSet();
}

void GameSaveLoad::Autosave()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	WriteGrid({FileFlags::kAppDataDirectory, FileFlags::kWrite}, std::filesystem::path("ServerAutosave.save"), game::gpGame->mClientGridCoord);
}

bool GameSaveLoad::Autoload()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	GridCoord loadedClientGridCoord {};
	if (!ReadGrid({FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("ServerAutosave.save"), loadedClientGridCoord))
	{
		return false;
	}

	game::gpGame->mClientGridCoord = loadedClientGridCoord;
	return true;
}

bool GameSaveLoad::Quickload([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	// Heap: fstream and Frame deserialization allocate vectors for variable-size SOA collections.
	//   Stream must stay open across the read, and collection data must persist in the Frame afterward
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuickload || rMenuInput.flags & game::MenuInputFlags::kResetFrame)
		{
			GridCoord loadedClientGridCoord {};
			bool bQuickloaded = false;

			if (rMenuInput.flags & game::MenuInputFlags::kQuickload)
			{
				if (!ReadGrid({FileFlags::kAppDataDirectory, FileFlags::kRead}, mrGameBase.QuicksaveFile(), loadedClientGridCoord))
				{
					game::gpGame->CreateNewFrame(game::GameFlags::kGame);
				}
				else
				{
					bQuickloaded = true;
				}
			}
			else
			{
				game::gpGame->CreateNewFrame(game::GameFlags::kGame);
			}

			mrGameBase.Reset();

			if (bQuickloaded)
			{
				game::gpGame->mClientGridCoord = loadedClientGridCoord;
				ASSERT(mrGameBase.mCoordFrames.contains(game::gpGame->mClientGridCoord));
				game::gpServerSession->ResetClientsForLoad();
			}

			return true;
		}
	}

	return false;
}

void GameSaveLoad::SaveLoadReplay()
{
	if constexpr (kbDebugInput)
	{
		if (mrGameBase.mGameFlags & GameFlags::kLoadReplay)
		{
			// Heap: DifferenceStream reader + Frame deserialization + ReplayMeta file I/O
			ScopedSuppressAllocationTracking suppressAllocationTracking;

			mrGameBase.mGameFlags.Clear(GameFlags::kLoadReplay);

			if (!mReplayReaders.empty())
			{
				mReplayReaders.clear();
				return;
			}

			game::ReplayMeta meta {};
			if (!ReadVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay.meta"), meta))
			{
				Log(kError, "Failed to read replay metadata");
				return;
			}

			// Read manifest to get recorded coord list
			std::fstream manifestStream = gpFileManager->OpenFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay.manifest"));
			if (!manifestStream)
			{
				Log(kError, "Failed to read replay manifest");
				return;
			}
			int64_t iCoordCount = 0;
			common::Read(manifestStream, iCoordCount);
			std::vector<GridCoord> recordedCoords;
			recordedCoords.reserve(iCoordCount);
			for (int64_t i = 0; i < iCoordCount; ++i)
			{
				GridCoord coord;
				coord.Read(manifestStream);
				recordedCoords.push_back(coord);
			}

			// Load initial grid state
			GridCoord loadedClientGridCoord {};
			if (!ReadGrid({FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay.grid"), loadedClientGridCoord))
			{
				Log(kError, "Failed to read replay grid");
				return;
			}

			mrGameBase.Reset();

			// Create one DifferenceStreamReader per recorded coord
			for (const GridCoord& rCoord : recordedCoords)
			{
				CoordFrames& rSub = mrGameBase.mCoordFrames.try_emplace(rCoord).first->second;
				if (rSub.pCurrent == nullptr)
				{
					rSub.pCurrent = std::make_unique<game::Frame>();
				}
				if (rSub.pNext == nullptr)
				{
					rSub.pNext = std::make_unique<game::Frame>();
				}

				std::filesystem::path coordReplayPath = std::filesystem::path("F7.replay." + std::to_string(rCoord.ToKey()));
				game::FrameInput initialFrameInput {};
				std::unique_ptr<DifferenceStreamReader<game::Frame, game::FrameInput>> pReader = std::make_unique<DifferenceStreamReader<game::Frame, game::FrameInput>>(FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, coordReplayPath, *rSub.pCurrent, initialFrameInput);

				if (!pReader->Loaded())
				{
					Log(kError, "Failed to load replay for coord ({},{})", rCoord.x, rCoord.y);
					mReplayReaders.clear();
					return;
				}

				mReplayReaders.emplace(rCoord, std::move(pReader));
			}

			if (!recordedCoords.empty())
			{
				const GridCoord& rFirstCoord = recordedCoords.front();
				mrGameBase.miTickCounter = mrGameBase.CurrentFrame(rFirstCoord).interpolate.iTick;
				mrGameBase.mfCurrentTime = mrGameBase.CurrentFrame(rFirstCoord).interpolate.fCurrentTime;
			}

			game::gpGame->RestoreReplayMeta(meta);
			game::gpServerSession->ResetClientsForLoad();
			game::gpServerSession->ComputeActiveSet();
		}
	}
}

void GameSaveLoad::SyncReplayTick()
{
	// Heap: DifferenceStream reader/writer persist across frames, growing vectors for diffs and checksums.
	//   Workbuffer is popped each frame so can't hold cross-frame state; size depends on recording length
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbDebugInput)
	{
		// Recording start: create one writer per active coord
		if ((mrGameBase.mGameFlags & GameFlags::kSaveReplay) && mReplayWriters.empty())
		{
			mrGameBase.mGameFlags.Clear(GameFlags::kSaveReplay);
			mReplayReaders.clear();

			WriteGrid({FileFlags::kAppDataDirectory, FileFlags::kWrite}, std::filesystem::path("F7.replay.grid"), game::gpGame->mClientGridCoord);

			for (const auto& [rCoord, rFrames] : mrGameBase.mCoordFrames)
			{
				game::FrameInput& rFrameInput = game::gpGame->mFrameInputs.try_emplace(rCoord).first->second;
				mReplayWriters.emplace(rCoord, std::make_unique<DifferenceStreamWriter<game::Frame, game::FrameInput>>(*rFrames.pCurrent, rFrameInput));
			}

			Log("Recording started for {} coords", mReplayWriters.size());
			return;
		}

		// Recording stop: save all writers
		if ((mrGameBase.mGameFlags & GameFlags::kSaveReplay) && !mReplayWriters.empty())
		{
			mrGameBase.mGameFlags.Clear(GameFlags::kSaveReplay);

			// Write manifest listing all recorded coords
			std::fstream manifestStream = gpFileManager->OpenFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, std::filesystem::path("F7.replay.manifest"));
			int64_t iCoordCount = static_cast<int64_t>(mReplayWriters.size());
			common::Write(manifestStream, iCoordCount);

			for (auto& [rCoord, rpWriter] : mReplayWriters)
			{
				rCoord.Write(manifestStream);

				std::filesystem::path coordReplayPath = std::filesystem::path("F7.replay." + std::to_string(rCoord.ToKey()));
				rpWriter->Save({FileFlags::kAppDataDirectory, FileFlags::kWrite, FileFlags::kBackup}, coordReplayPath, mrGameBase.CurrentFrame(rCoord));
			}

			mReplayWriters.clear();

			// Write replay metadata for F8 load
			game::ReplayMeta meta {
				.clientGridCoord = game::gpGame->mClientGridCoord,
				.iClientPlayerIdValue = game::gpGame->ClientPlayerId().iValue,
				.fPreviousClientArmor = game::gpGame->PreviousClientArmor(),
			};
			WriteVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, std::filesystem::path("F7.replay.meta"), meta);

			Log("Recording stopped");
			return;
		}

		// Recording tick: update all writers
		if (!mReplayWriters.empty()) [[unlikely]]
		{
			for (auto& [rCoord, rpWriter] : mReplayWriters)
			{
				if (mrGameBase.mCoordFrames.contains(rCoord))
				{
					game::FrameInput& rFrameInput = game::gpGame->mFrameInputs.at(rCoord);
					rpWriter->Update(mrGameBase.miTickCounter, rFrameInput, mrGameBase.CurrentFrame(rCoord));
				}
			}
		}

		// Playback tick: load differences for all readers
		if (!mReplayReaders.empty()) [[unlikely]]
		{
			bool bEndReached = false;
			for (auto& [rCoord, rpReader] : mReplayReaders)
			{
				game::FrameInput& rFrameInput = game::gpGame->mFrameInputs.at(rCoord);
				if (!rpReader->LoadDifference(mrGameBase.miTickCounter, rFrameInput))
				{
					bEndReached = true;
					break;
				}

				game::gpGame->ApplyTransferStatusChanges(mrGameBase.CurrentFrame(rCoord), rFrameInput);
				rpReader->ValidateChecksum(mrGameBase.miTickCounter, mrGameBase.CurrentFrame(rCoord));
			}

			if (bEndReached)
			{
				Log("End replay {}, looping", mrGameBase.miTickCounter);
				mReplayReaders.clear();
				mrGameBase.mGameFlags.Set(GameFlags::kLoadReplay);
			}
		}
	}
}

void GameSaveLoad::WriteGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord clientGridCoord)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);
	int64_t iVersion = game::Frame::kiVersion;
	common::Write(fileStream, iVersion);
	int64_t iSize = 0;
	common::Write(fileStream, iSize);

	int64_t iFrameCount = static_cast<int64_t>(mrGameBase.mCoordFrames.size());
	common::Write(fileStream, iFrameCount);
	clientGridCoord.Write(fileStream);
	common::Write(fileStream, mrGameBase.miNextGlobalId);

	// Sort by coord key for deterministic output
	std::vector<uint64_t> keys;
	keys.reserve(mrGameBase.mCoordFrames.size());
	for (const auto& [rCoord, rFrames] : mrGameBase.mCoordFrames)
	{
		keys.push_back(rCoord.ToKey());
	}
	std::sort(keys.begin(), keys.end());

	for (uint64_t uiKey : keys)
	{
		GridCoord coord = GridCoord::FromKey(uiKey);
		coord.Write(fileStream);
		mrGameBase.mCoordFrames.at(coord).staticData.Write(fileStream);
		fileStream << *mrGameBase.mCoordFrames.at(coord).pCurrent;
	}

	Log("WriteGrid {} iVersion: {} iFrameCount: {}", rFilename, iVersion, iFrameCount);
}

bool GameSaveLoad::ReadGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord& rClientGridCoord)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);

	int64_t iVersion = 0;
	common::Read(fileStream, iVersion);
	int64_t iSize = 0;
	common::Read(fileStream, iSize);

	if (iVersion != game::Frame::kiVersion)
	{
		Log(kError, "ReadGrid {} failed: version {} != {}", rFilename, iVersion, game::Frame::kiVersion);
		return false;
	}

	int64_t iFrameCount = 0;
	common::Read(fileStream, iFrameCount);
	rClientGridCoord.Read(fileStream);
	common::Read(fileStream, mrGameBase.miNextGlobalId);

	mrGameBase.mCoordFrames.clear();

	for (int64_t i = 0; i < iFrameCount; ++i)
	{
		GridCoord coord;
		coord.Read(fileStream);
		CoordFrames& rSub = mrGameBase.mCoordFrames.try_emplace(coord).first->second;
		rSub.staticData.Read(fileStream);
		auto pFrame = std::make_unique<game::Frame>();
		fileStream >> *pFrame;
		rSub.pCurrent = std::move(pFrame);
		rSub.pNext = std::make_unique<game::Frame>();
	}

	if (!mrGameBase.mCoordFrames.empty())
	{
		mrGameBase.miTickCounter = mrGameBase.mCoordFrames.begin()->second.pCurrent->interpolate.iTick;
		mrGameBase.mfCurrentTime = mrGameBase.mCoordFrames.begin()->second.pCurrent->interpolate.fCurrentTime;
	}

	Log("ReadGrid {} iVersion: {} iFrameCount: {}", rFilename, iVersion, iFrameCount);
	return fileStream.good();
}

} // namespace engine

#endif // BT_SERVER
