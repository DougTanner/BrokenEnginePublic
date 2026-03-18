#include "GameSaveLoad.h"

#if defined(BT_SERVER)

#include "GameBase.h"
#include "Game.h"
#include "Profile/ProfileManager.h"

namespace engine
{

GameSaveLoad::GameSaveLoad(GameBase& rGameBase)
	: mrGameBase(rGameBase)
{
}

void GameSaveLoad::ResetStreams()
{
	mpDifferenceStreamWriter.reset();
	mpDifferenceStreamReader.reset();
}

void GameSaveLoad::Quicksave([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	// Heap: fstream and Frame serialization (stream must stay open across the full write so push/pop
	//   lifecycle doesn't apply, and SOA collection data must persist in the Frame after deserialization)
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbEnableDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuicksave)
		{
			WriteGrid({FileFlags::kAppDataDirectory, FileFlags::kWrite}, mrGameBase.QuicksaveFile(), game::gpGame->mHumanGridCoord);
		}
	}
}

bool GameSaveLoad::Quickload([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	// Heap: fstream and Frame deserialization allocate vectors for variable-size SOA collections.
	//   Stream must stay open across the read, and collection data must persist in the Frame afterward
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbEnableDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuickload || rMenuInput.flags & game::MenuInputFlags::kResetFrame)
		{
			GridCoord loadedHumanGridCoord {};
			bool bQuickloaded = false;

			if (rMenuInput.flags & game::MenuInputFlags::kQuickload)
			{
				if (!ReadGrid({FileFlags::kAppDataDirectory, FileFlags::kRead}, mrGameBase.QuicksaveFile(), loadedHumanGridCoord))
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
				game::gpGame->mHumanGridCoord = loadedHumanGridCoord;
				ASSERT(mrGameBase.mCoordFrames.contains(game::gpGame->mHumanGridCoord));
			}

			return true;
		}
	}

	return false;
}

void GameSaveLoad::SaveLoadReplay([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	if constexpr (kbEnableDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kSaveReplay)
		{
			mrGameBase.mGameFlags.Set(GameFlags::kSaveReplay);
		}
		else if (rMenuInput.flags & game::MenuInputFlags::kLoadReplay)
		{
			mrGameBase.mGameFlags.Set(GameFlags::kLoadReplay);
		}

		if (mrGameBase.mGameFlags & GameFlags::kLoadReplay)
		{
			// Heap: DifferenceStream reader + Frame deserialization + ReplayMeta file I/O
			ScopedSuppressAllocationTracking suppressAllocationTracking;

			mrGameBase.mGameFlags.Clear(GameFlags::kLoadReplay);

			if (mpDifferenceStreamReader != nullptr)
			{
				mpDifferenceStreamReader.reset();
				return;
			}

			game::ReplayMeta meta {};
			if (!ReadVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay.meta"), meta))
			{
				Log(kLogError, "Failed to read replay metadata");
				return;
			}

			mrGameBase.Reset();

			// Clear all frames and create fresh at recorded coordinate
			mrGameBase.mCoordFrames.clear();
			CoordFrames& rSub = mrGameBase.mCoordFrames.try_emplace(meta.humanGridCoord).first->second;
			rSub.pCurrent = std::make_unique<game::Frame>();
			rSub.pNext = std::make_unique<game::Frame>();

			game::gpGame->mHumanGridCoord = meta.humanGridCoord;

			// DifferenceStreamReader deserializes initial frame and initial FrameInput
			game::FrameInput initialFrameInput {};
			mpDifferenceStreamReader = std::make_unique<DifferenceStreamReader<game::Frame, game::FrameInput>>(FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay"), mrGameBase.CurrentFrame(meta.humanGridCoord), initialFrameInput);

			if (!mpDifferenceStreamReader->Loaded())
			{
				mpDifferenceStreamReader.reset();
				return;
			}

			mrGameBase.miTickCounter = mrGameBase.CurrentFrame(meta.humanGridCoord).interpolate.iTick;
			mrGameBase.mfCurrentTime = mrGameBase.CurrentFrame(meta.humanGridCoord).interpolate.fCurrentTime;
			game::gpGame->RestoreReplayMeta(meta);
		}
	}
}

void GameSaveLoad::SyncReplay([[maybe_unused]] game::Frame& rFrame, [[maybe_unused]] game::FrameInput& rFrameInput)
{
	// Heap: DifferenceStream reader/writer persist across frames, growing vectors for diffs and checksums.
	//   Workbuffer is popped each frame so can't hold cross-frame state; size depends on recording length
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbEnableDebugInput)
	{
		if ((mrGameBase.mGameFlags & GameFlags::kSaveReplay) && mpDifferenceStreamWriter == nullptr)
		{
			mrGameBase.mGameFlags.Clear(GameFlags::kSaveReplay);
			mpDifferenceStreamReader.reset();
			mpDifferenceStreamWriter = std::make_unique<DifferenceStreamWriter<game::Frame, game::FrameInput>>(rFrame, rFrameInput);
			return;
		}
		else if ((mrGameBase.mGameFlags & GameFlags::kSaveReplay) && mpDifferenceStreamWriter != nullptr)
		{
			mrGameBase.mGameFlags.Clear(GameFlags::kSaveReplay);
			mpDifferenceStreamWriter->Save({FileFlags::kAppDataDirectory, FileFlags::kWrite, FileFlags::kBackup}, std::filesystem::path("F7.replay"), rFrame);
			mpDifferenceStreamWriter.reset();

			// Write replay metadata for F8 load
			game::ReplayMeta meta {
				.humanGridCoord = game::gpGame->mHumanGridCoord,
				.iHumanPlayerIdValue = game::gpGame->HumanPlayerId().ToUuid().Value(),
				.fPreviousHumanArmor = game::gpGame->PreviousHumanArmor(),
			};
			WriteVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, std::filesystem::path("F7.replay.meta"), meta);

			return;
		}

		if (mpDifferenceStreamWriter != nullptr) [[unlikely]]
		{
			mpDifferenceStreamWriter->Update(mrGameBase.miTickCounter, rFrameInput, rFrame);
		}
		else if (mpDifferenceStreamReader != nullptr) [[unlikely]]
		{
			if (!mpDifferenceStreamReader->LoadDifference(mrGameBase.miTickCounter, rFrameInput))
			{
				Log("End replay {}, looping", mrGameBase.miTickCounter);
				rFrame.LogDifferences(mpDifferenceStreamReader->GetSavedEnd());
				mpDifferenceStreamReader.reset();
				mrGameBase.mGameFlags.Set(GameFlags::kLoadReplay);
			}
			else
			{
				game::gpGame->ApplyTransferStatusChanges(rFrame, rFrameInput);
				mpDifferenceStreamReader->ValidateChecksum(mrGameBase.miTickCounter, rFrame);
			}
		}
	}
}

void GameSaveLoad::WriteGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord humanGridCoord)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);
	int64_t iVersion = game::Frame::kiVersion;
	common::Write(fileStream, iVersion);
	int64_t iSize = 0;
	common::Write(fileStream, iSize);

	int64_t iFrameCount = static_cast<int64_t>(mrGameBase.mCoordFrames.size());
	common::Write(fileStream, iFrameCount);
	humanGridCoord.Write(fileStream);

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
		fileStream << *mrGameBase.mCoordFrames.at(coord).pCurrent;
	}

	Log("WriteGrid {} iVersion: {} iFrameCount: {}", rFilename, iVersion, iFrameCount);
}

bool GameSaveLoad::ReadGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord& rHumanGridCoord)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);

	int64_t iVersion = 0;
	common::Read(fileStream, iVersion);
	int64_t iSize = 0;
	common::Read(fileStream, iSize);

	if (iVersion != game::Frame::kiVersion)
	{
		Log(kLogError, "ReadGrid {} failed: version {} != {}", rFilename, iVersion, game::Frame::kiVersion);
		return false;
	}

	int64_t iFrameCount = 0;
	common::Read(fileStream, iFrameCount);
	rHumanGridCoord.Read(fileStream);

	mrGameBase.mCoordFrames.clear();

	for (int64_t i = 0; i < iFrameCount; ++i)
	{
		GridCoord coord;
		coord.Read(fileStream);
		auto pFrame = std::make_unique<game::Frame>();
		fileStream >> *pFrame;
		CoordFrames& rSub = mrGameBase.mCoordFrames.try_emplace(coord).first->second;
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
