#include "Pch.h"

#if defined(BT_SERVER)

#include "Save/GameSaveLoad.h"

#include "GameBase.h"
#include "Game.h"
#include "Network/Server/ServerFleetManager.h"
#include "Network/Server/ServerSession.h"
#include "Profile/ProfileManager.h"

namespace game
{

// On-disk version of the F7.replay.manifest coord list. Bump on layout change; old manifests are rejected on read.
static constexpr int64_t kiReplayManifestVersion = 1;

GameSaveLoad::GameSaveLoad(engine::GameBase& rGameBase)
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
	ScopedSuppressAllocationTracking suppress;

	if constexpr (kbDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuicksave)
		{
			WriteGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, mrGameBase.QuicksaveFile(), game::gpGame->mClientGridCoord);
		}
	}
}

void GameSaveLoad::ServerSave()
{
	ScopedSuppressAllocationTracking suppress;
	WriteGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, mrGameBase.QuicksaveFile(), game::gpGame->mClientGridCoord);
}

bool GameSaveLoad::ServerLoad()
{
	ScopedSuppressAllocationTracking suppress;

	engine::GridCoord loadedClientGridCoord {};
	if (!ReadGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, mrGameBase.QuicksaveFile(), loadedClientGridCoord))
	{
		return false;
	}

	mrGameBase.Reset();
	game::gpGame->SetClientGridCoord(loadedClientGridCoord);
	game::gpServerSession->ResetClientsForLoad();
	game::gpServerSession->ComputeActiveSet();

	return true;
}

void GameSaveLoad::ServerReset()
{
	ScopedSuppressAllocationTracking suppress;

	game::gpGame->CreateNewFrame(game::GameFlags::kGame);
	mrGameBase.SetNextGlobalId(1);
	mrGameBase.Reset();
	// Fresh-game wipe of fleet manager state. Load path leaves mFleets populated by ReadFleetData;
	// fresh-game has no save to restore from, so explicitly clear before ResetClientsForLoad runs.
	game::gpServerSession->mpFleetManager->ResetState();
	game::gpServerSession->ResetClientsForLoad();
	game::gpServerSession->ComputeActiveSet();
}

void GameSaveLoad::Autosave()
{
	ScopedSuppressAllocationTracking suppress;
	WriteGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite, engine::FileFlags::kBackup}, std::filesystem::path("ServerAutosave.save"), game::gpGame->mClientGridCoord);
}

void GameSaveLoad::TickAutosave()
{
	if (IsReplaying() || IsRecording())
	{
		return;
	}

	if (mAutosaveTimer.GetDeltaNs(false) >= kAutosaveInterval)
	{
		Autosave();
		mAutosaveTimer.Reset();
		LOG(kDefault, kInfo, "Autosave fired (interval {}s)", kAutosaveInterval.count());
	}
}

bool GameSaveLoad::Autoload()
{
	ScopedSuppressAllocationTracking suppress;

	engine::GridCoord loadedClientGridCoord {};
	if (!ReadGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, std::filesystem::path("ServerAutosave.save"), loadedClientGridCoord))
	{
		return false;
	}

	game::gpGame->SetClientGridCoord(loadedClientGridCoord);
	return true;
}

bool GameSaveLoad::Quickload([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	// Heap: fstream and Frame deserialization allocate vectors for variable-size SOA collections.
	//   Stream must stay open across the read, and collection data must persist in the Frame afterward
	ScopedSuppressAllocationTracking suppress;

	if constexpr (kbDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuickload || rMenuInput.flags & game::MenuInputFlags::kResetFrame)
		{
			engine::GridCoord loadedClientGridCoord {};
			bool bQuickloaded = false;

			if (rMenuInput.flags & game::MenuInputFlags::kQuickload)
			{
				if (!ReadGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, mrGameBase.QuicksaveFile(), loadedClientGridCoord))
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
				game::gpGame->SetClientGridCoord(loadedClientGridCoord);
				game::gpServerSession->ResetClientsForLoad();
			}
			else
			{
				// Fresh-game (kResetFrame without kQuickload) wipe of fleet manager state. No ReadFleetData ran,
				// so mFleets must be cleared explicitly before any reconnect re-walks it.
				game::gpServerSession->mpFleetManager->ResetState();
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
		if (mrGameBase.mGameFlags & engine::GameFlags::kLoadReplay)
		{
			// Heap: DifferenceStream reader + Frame deserialization + ReplayMeta file I/O
			ScopedSuppressAllocationTracking suppress;

			mrGameBase.mGameFlags.Clear(engine::GameFlags::kLoadReplay);

			try
			{
				if (!mReplayReaders.empty())
				{
					mReplayReaders.clear();
					return;
				}

				game::ReplayMeta meta {};
				if (!engine::ReadVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, std::filesystem::path("F7.replay.meta"), meta))
				{
					LOG(kDefault, kError, "Failed to read replay metadata");
					return;
				}

				// Read manifest to get recorded coord list
				std::fstream manifestStream = engine::gpFileManager->OpenFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, std::filesystem::path("F7.replay.manifest"));
				if (!manifestStream)
				{
					LOG(kDefault, kError, "Failed to read replay manifest");
					return;
				}
				int64_t iManifestVersion = 0;
				common::Read(manifestStream, iManifestVersion);
				if (iManifestVersion != kiReplayManifestVersion)
				{
					LOG(kDefault, kError, "Replay manifest version {} != {}", iManifestVersion, kiReplayManifestVersion);
					return;
				}

				int64_t iCoordCount = 0;
				common::Read(manifestStream, iCoordCount);
				// Trust boundary (replay manifest file): bound the coord count against the stream before reserve.
				common::ValidateDeserializedCount(iCoordCount, sizeof(engine::GridCoord), manifestStream, "ReplayManifest coords");
				std::vector<engine::GridCoord> recordedCoords;
				recordedCoords.reserve(iCoordCount);
				for (int64_t i = 0; i < iCoordCount; ++i)
				{
					engine::GridCoord coord;
					coord.Read(manifestStream);
					recordedCoords.push_back(coord);
				}

				// Load initial grid state
				engine::GridCoord loadedClientGridCoord {};
				if (!ReadGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, std::filesystem::path("F7.replay.grid"), loadedClientGridCoord))
				{
					LOG(kDefault, kError, "Failed to read replay grid");
					return;
				}

				mrGameBase.Reset();

				// Create one DifferenceStreamReader per recorded coord
				for (const engine::GridCoord& rCoord : recordedCoords)
				{
					engine::CoordFrames& rSub = mrGameBase.mCoordFrames.try_emplace(rCoord).first->second;
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
					std::unique_ptr<engine::DifferenceStreamReader<game::Frame, game::FrameInput>> pReader = std::make_unique<engine::DifferenceStreamReader<game::Frame, game::FrameInput>>(engine::FileFlags_t {engine::FileFlags::kAppDataDirectory, engine::FileFlags::kRead}, coordReplayPath, *rSub.pCurrent, initialFrameInput);

					if (!pReader->Loaded())
					{
						LOG(kDefault, kError, "Failed to load replay for coord ({},{})", rCoord.x, rCoord.y);
						mReplayReaders.clear();
						return;
					}

					mReplayReaders.emplace(rCoord, std::move(pReader));
				}

				if (!recordedCoords.empty())
				{
					const engine::GridCoord& rFirstCoord = recordedCoords.front();
					mrGameBase.SetTickCounter(mrGameBase.CurrentFrame(rFirstCoord).interpolate.iTick);
					mrGameBase.SetCurrentTime(mrGameBase.CurrentFrame(rFirstCoord).interpolate.fCurrentTime);
				}

				game::gpGame->RestoreReplayMeta(meta);
				game::gpServerSession->ResetClientsForLoad();
				game::gpServerSession->ComputeActiveSet();
			}
			catch (const std::exception& rException)
			{
				LOG(kDefault, kError, "SaveLoadReplay aborted: corrupt replay data: {}", rException.what());
				mReplayReaders.clear();
				return;
			}
		}
	}
}

void GameSaveLoad::SyncReplayTick()
{
	// Heap: DifferenceStream reader/writer persist across frames, growing vectors for diffs and checksums.
	//   Workbuffer is popped each frame so can't hold cross-frame state; size depends on recording length
	ScopedSuppressAllocationTracking suppress;

	if constexpr (kbDebugInput)
	{
		// Recording start: create one writer per active coord
		if ((mrGameBase.mGameFlags & engine::GameFlags::kSaveReplay) && mReplayWriters.empty())
		{
			mrGameBase.mGameFlags.Clear(engine::GameFlags::kSaveReplay);
			mReplayReaders.clear();

			WriteGrid({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, std::filesystem::path("F7.replay.grid"), game::gpGame->mClientGridCoord);

			for (const auto& [rCoord, rFrames] : mrGameBase.mCoordFrames)
			{
				game::FrameInput& rFrameInput = game::gpGame->mFrameInputs.try_emplace(rCoord).first->second;
				mReplayWriters.emplace(rCoord, std::make_unique<engine::DifferenceStreamWriter<game::Frame, game::FrameInput>>(*rFrames.pCurrent, rFrameInput));
			}

			LOG(kDefault, kDebug, "Recording started for {} coords", mReplayWriters.size());
			return;
		}

		// Recording stop: save all writers
		if ((mrGameBase.mGameFlags & engine::GameFlags::kSaveReplay) && !mReplayWriters.empty())
		{
			mrGameBase.mGameFlags.Clear(engine::GameFlags::kSaveReplay);

			// Write manifest listing all recorded coords
			static_cast<void>(engine::gpFileManager->WriteFileAtomically({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, std::filesystem::path("F7.replay.manifest"), [&](std::fstream& rManifestStream)
			{
				common::Write(rManifestStream, kiReplayManifestVersion);

				int64_t iCoordCount = static_cast<int64_t>(mReplayWriters.size());
				common::Write(rManifestStream, iCoordCount);

				// Sort by coord key for deterministic output (mirrors WriteGrid).
				std::vector<uint64_t> keys;
				keys.reserve(mReplayWriters.size());
				for (const auto& [rCoord, rpWriter] : mReplayWriters)
				{
					keys.push_back(rCoord.ToKey());
				}
				std::sort(keys.begin(), keys.end());

				for (uint64_t uiKey : keys)
				{
					engine::GridCoord coord = engine::GridCoord::FromKey(uiKey);
					coord.Write(rManifestStream);
				}
			}));

			for (auto& [rCoord, rpWriter] : mReplayWriters)
			{
				std::filesystem::path coordReplayPath = std::filesystem::path("F7.replay." + std::to_string(rCoord.ToKey()));
				rpWriter->Save({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite, engine::FileFlags::kBackup}, coordReplayPath, mrGameBase.CurrentFrame(rCoord));
			}

			mReplayWriters.clear();

			// Write replay metadata for F8 load
			game::ReplayMeta meta {
				.clientGridCoord = game::gpGame->mClientGridCoord,
				.iClientPlayerIdValue = game::gpGame->ClientPlayerId().iValue,
				.fPreviousClientArmor = game::gpGame->PreviousClientArmor(),
			};
			engine::WriteVersionedFile({engine::FileFlags::kAppDataDirectory, engine::FileFlags::kWrite}, std::filesystem::path("F7.replay.meta"), meta);

			LOG(kDefault, kDebug, "Recording stopped");
			return;
		}

		// Recording tick: update all writers
		if (!mReplayWriters.empty()) [[unlikely]]
		{
			for (auto& [rCoord, rpWriter] : mReplayWriters)
			{
				if (mrGameBase.mCoordFrames.contains(rCoord))
				{
					game::FrameInput& rFrameInput = game::gpGame->mFrameInputs.try_emplace(rCoord).first->second;
					rpWriter->Update(mrGameBase.TickCounter(), rFrameInput, mrGameBase.CurrentFrame(rCoord));
				}
			}
		}

		// Playback tick: load differences for all readers
		if (!mReplayReaders.empty()) [[unlikely]]
		{
			bool bEndReached = false;
			for (auto& [rCoord, rpReader] : mReplayReaders)
			{
				game::FrameInput& rFrameInput = game::gpGame->mFrameInputs.try_emplace(rCoord).first->second;
				if (!rpReader->LoadDifference(mrGameBase.TickCounter(), rFrameInput))
				{
					bEndReached = true;
					break;
				}

				game::gpGame->ApplyTransferStatusChanges(mrGameBase.CurrentFrame(rCoord), rFrameInput);
				rpReader->ValidateChecksum(mrGameBase.TickCounter(), mrGameBase.CurrentFrame(rCoord));
			}

			if (bEndReached)
			{
				LOG(kDefault, kDebug, "End replay {}, looping", mrGameBase.TickCounter());
				mReplayReaders.clear();
				mrGameBase.mGameFlags.Set(engine::GameFlags::kLoadReplay);
			}
		}
	}
}

void GameSaveLoad::WriteGrid(const engine::FileFlags_t& rFlags, const std::filesystem::path& rFilename, engine::GridCoord clientGridCoord)
{
	int64_t iFrameCount = static_cast<int64_t>(mrGameBase.mCoordFrames.size());
	int64_t iVersion = game::Frame::kiVersion;

	bool bWritten = engine::gpFileManager->WriteFileAtomically(rFlags, rFilename, [&](std::fstream& fileStream)
	{
		engine::WriteVersionHeader<game::Frame>(fileStream);

		common::Write(fileStream, iFrameCount);
		clientGridCoord.Write(fileStream);
		common::Write(fileStream, mrGameBase.NextGlobalId());

		game::gpServerSession->WriteFleetData(fileStream);

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
			engine::GridCoord coord = engine::GridCoord::FromKey(uiKey);
			coord.Write(fileStream);
			// NavData is rebuilt lazily on first RunFrameTick — don't persist it (see Frame/CLAUDE.md).
			mrGameBase.mCoordFrames.at(coord).staticData.Write(fileStream, /*bIncludeNavData=*/false);
			fileStream << *mrGameBase.mCoordFrames.at(coord).pCurrent;
		}
	});

	LOG(kDefault, kDebug, "WriteGrid {} iVersion: {} iFrameCount: {} Committed: {}", rFilename, iVersion, iFrameCount, bWritten);
}

bool GameSaveLoad::ReadGrid(const engine::FileFlags_t& rFlags, const std::filesystem::path& rFilename, engine::GridCoord& rClientGridCoord)
{
	std::fstream fileStream = engine::gpFileManager->OpenFile(rFlags, rFilename);

	int64_t iVersion = 0;
	int64_t iSize = 0;
	if (!engine::ReadAndValidateVersionHeader<game::Frame>(fileStream, iVersion, iSize))
	{
		LOG(kDefault, kError, "ReadGrid {} failed: version {} != {}", rFilename, iVersion, game::Frame::kiVersion);
		return false;
	}

	int64_t iFrameCount = 0;
	common::Read(fileStream, iFrameCount);
	// Trust boundary (save file): a corrupt count/capacity anywhere in the grid / fleet / frame
	// deserialization throws CorruptStreamException (or .at()/bad_alloc) — abort the load gracefully
	// (return false) so a hand-crafted or truncated save file can't overrun a buffer or crash the server.
	int64_t iNextGlobalId = 0;
	try
	{
		// Bound the frame count against the stream (each coord frame serializes at least its GridCoord).
		common::ValidateDeserializedCount(iFrameCount, sizeof(engine::GridCoord), fileStream, "ReadGrid frames");
		rClientGridCoord.Read(fileStream);
		common::Read(fileStream, iNextGlobalId);
		// Trust boundary (save file): the global-id counter is a monotonic positive int64 (fresh games start at
		// 1). Validate now but apply only past the stream-good gate below, so a failed or silently-torn load
		// leaves the fresh-fallback game minting from a clean base rather than a garbage/advanced one.
		if (iNextGlobalId <= 0)
		{
			throw common::CorruptStreamException("iNextGlobalId");
		}

		game::gpServerSession->ReadFleetData(fileStream);

		mrGameBase.mCoordFrames.clear();

		for (int64_t i = 0; i < iFrameCount; ++i)
		{
			engine::GridCoord coord;
			coord.Read(fileStream);
			engine::CoordFrames& rSub = mrGameBase.mCoordFrames.try_emplace(coord).first->second;
			rSub.staticData.Read(fileStream, /*bIncludeNavData=*/false);
			rSub.staticData.coord = coord;
			auto pFrame = std::make_unique<game::Frame>();
			fileStream >> *pFrame;
			rSub.pCurrent = std::move(pFrame);
			rSub.pNext = std::make_unique<game::Frame>();
		}

		// Trust boundary (save file): the client grid coord is file-derived and every load entry
		// (ServerLoad/Autoload/Quickload) adopts it as the followed cell — it must name a frame we just read,
		// else the grid is torn. Reject uniformly here (replaces the former Quickload ASSERT on file data).
		if (!mrGameBase.mCoordFrames.contains(rClientGridCoord))
		{
			throw common::CorruptStreamException("client grid coord absent from frames");
		}

		if (!mrGameBase.mCoordFrames.empty())
		{
			mrGameBase.SetTickCounter(mrGameBase.mCoordFrames.begin()->second.pCurrent->interpolate.iTick);
			mrGameBase.SetCurrentTime(mrGameBase.mCoordFrames.begin()->second.pCurrent->interpolate.fCurrentTime);
		}
	}
	catch (const std::exception& rException)
	{
		// Leave a clean empty grid AND fleet manager rather than the partially-filled state a mid-loop throw
		// produced: ReadFleetData (called above, in the try) clears then repopulates its maps, so a throw can
		// leave them torn, and ServerLoad's caller continues on a false return. Callers treat false as "state
		// invalid" (Quickload recreates a fresh frame; Autoload's caller rebuilds via CreateNewFrame).
		mrGameBase.mCoordFrames.clear();
		game::gpServerSession->mpFleetManager->ResetState();
		LOG(kDefault, kError, "ReadGrid {} aborted: corrupt data: {}", rFilename, rException.what());
		return false;
	}

	if (!fileStream.good())
	{
		// A silently-failed istream::read left zeroed defaults that passed the validators while the loop ran on
		// garbage. Give this the same clean-slate failure shape as the catch above rather than committing torn
		// state, so ServerLoad's caller falls back uniformly.
		mrGameBase.mCoordFrames.clear();
		game::gpServerSession->mpFleetManager->ResetState();
		LOG(kDefault, kError, "ReadGrid {} aborted: stream failure after read", rFilename);
		return false;
	}

	// Adopt the validated global-id counter only now that the load fully succeeded — every failure path (a
	// mid-load throw caught above, or a silent stream failure at the good() gate) returns first, so the
	// fresh-fallback game keeps minting from its own clean base rather than a torn/advanced one.
	mrGameBase.SetNextGlobalId(iNextGlobalId);

	LOG(kDefault, kDebug, "ReadGrid {} iVersion: {} iFrameCount: {}", rFilename, iVersion, iFrameCount);
	return true;
}

} // namespace game

#endif // BT_SERVER
