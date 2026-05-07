#pragma once

namespace engine
{

// Helper to transfer data between objects using stream operators (for non-copyable types)
template <typename T>
inline void TransferViaStream(const T& rFrom, T& rTo)
{
	std::stringstream buffer;
	buffer << rFrom;
	buffer >> rTo;
}

template <typename SAVED_TYPE, typename DIFFERENCE_TYPE>
class DifferenceStreamWriter
{
public:

	using difference_t = std::tuple<int64_t, DIFFERENCE_TYPE>;

	DifferenceStreamWriter(const SAVED_TYPE& rSavedStart, const DIFFERENCE_TYPE& rInitialDifference)
	{
		mDifferences.reserve(1024);

		// Initialize starting state and frame
		miStartTick = rSavedStart.interpolate.iTick;
		TransferViaStream(rSavedStart, mSavedStart);
		mInitialDifference = rInitialDifference;
		mCurrentDifference = rInitialDifference;
		LOG(kDefault, kVerbose, "DifferenceStreamWriter at frame {}: Saved start: {} Initial difference: {}", miStartTick, mSavedStart.Crc(), mInitialDifference.Crc());

		// Record initial checksum
		mChecksums.reserve(1024);
		mChecksums.push_back(mSavedStart.Crc());
		LOG(kDefault, kVerbose, "Checksum DifferenceStreamWriter {}: {}", miStartTick, *std::prev(mChecksums.end()));

		if constexpr (kbReplayFullFrames)
		{
			mFullFramesStream << mSavedStart;
		}
	}

	void Update(int64_t iTick, const DIFFERENCE_TYPE& rDifference, const SAVED_TYPE& rSavedCurrent)
	{
		// Record checksum for this frame
		mChecksums.push_back(rSavedCurrent.Crc());
		LOG(kDefault, kVerbose, "Checksum DifferenceStreamWriter Update {}: {}", rSavedCurrent.interpolate.iTick, *std::prev(mChecksums.end()));

		if constexpr (kbReplayFullFrames)
		{
			mFullFramesStream << rSavedCurrent;
		}

		// Skip if no state change occurred
		if (rDifference.Crc() == mCurrentDifference.Crc())
		{
			return;
		}

		// Save the changed difference
		mDifferences.emplace_back(iTick, rDifference);
		mCurrentDifference = rDifference;
	}

	void Save(FileFlags_t fileFlags, const std::filesystem::path& rFilename, const SAVED_TYPE& rSavedEnd)
	{
		int64_t iDifferenceCount = mDifferences.size();

		// Write header with version info, start/end states and metadata
		std::fstream headerStream = gpFileManager->OpenFile(fileFlags, rFilename);

		// Write version headers (matches WriteVersionedFile pattern)
		common::Write(headerStream, static_cast<int64_t>(SAVED_TYPE::kiVersion));
		common::Write(headerStream, std::is_trivially_copyable_v<SAVED_TYPE> ? static_cast<int64_t>(sizeof(SAVED_TYPE)) : int64_t{0});
		common::Write(headerStream, static_cast<int64_t>(DIFFERENCE_TYPE::kiVersion));
		common::Write(headerStream, std::is_trivially_copyable_v<DIFFERENCE_TYPE> ? static_cast<int64_t>(sizeof(DIFFERENCE_TYPE)) : int64_t{0});

		headerStream << mSavedStart;
		if constexpr (std::is_trivially_copyable_v<DIFFERENCE_TYPE>)
			common::Write(headerStream, mInitialDifference);
		else
			headerStream << mInitialDifference;
		common::Write(headerStream, iDifferenceCount);
		headerStream << rSavedEnd;
		LOG(kDefault, kVerbose, "DifferenceStreamWriter save at frame {}: Count {} Checksum {}", rSavedEnd.interpolate.iTick, iDifferenceCount, rSavedEnd.Crc());

		// Write difference records
		std::fstream fileStream = gpFileManager->OpenFile(fileFlags, std::filesystem::path(rFilename).concat(".frames"));
		if (!mDifferences.empty())
		{
			if constexpr (std::is_trivially_copyable_v<DIFFERENCE_TYPE>)
			{
				common::Write(fileStream, mDifferences);
			}
			else
			{
				for (const auto& [iTick, difference] : mDifferences)
				{
					common::Write(fileStream, iTick);
					fileStream << difference;
				}
			}
		}

		// Write checksums for validation
		mChecksums.push_back(rSavedEnd.Crc());
		LOG(kDefault, kVerbose, "Checksum DifferenceStreamWriter Save {}: {}", rSavedEnd.interpolate.iTick, *std::prev(mChecksums.end()));
		std::fstream checksumStream = gpFileManager->OpenFile(fileFlags, std::filesystem::path(rFilename).concat(".checksums"));
		if (!mChecksums.empty())
		{
			common::Write(checksumStream, mChecksums);
		}

		if constexpr (kbReplayFullFrames)
		{
			// Write complete frame snapshots for debugging
			mFullFramesStream << rSavedEnd;
			std::fstream fullFramesStream = gpFileManager->OpenFile(fileFlags, std::filesystem::path(rFilename).concat(".fullframes"));
			fullFramesStream << mFullFramesStream.str();
		}
	}

private:

	SAVED_TYPE mSavedStart {};
	DIFFERENCE_TYPE mInitialDifference {};

	std::vector<difference_t> mDifferences;
	DIFFERENCE_TYPE mCurrentDifference {};

	std::vector<common::crc_t> mChecksums;
	int64_t miStartTick = 0;

	[[no_unique_address]] std::conditional_t<kbReplayFullFrames, std::stringstream, common::Empty> mFullFramesStream;
};

template <typename SAVED_TYPE, typename DIFFERENCE_TYPE>
class DifferenceStreamReader
{
public:

	using difference_t = std::tuple<int64_t, DIFFERENCE_TYPE>;

	DifferenceStreamReader(const FileFlags_t& rFileFlags, const std::filesystem::path& rFilename, SAVED_TYPE& rSavedStart, DIFFERENCE_TYPE& rInitialDifference)
	{
		// Read header with version info, start/end states and metadata
		std::fstream headerStream = gpFileManager->OpenFile(rFileFlags, rFilename);
		if (!headerStream)
		{
			return;
		}

		// Read and validate version headers (matches ReadVersionedFile pattern)
		int64_t iSavedVersion = 0;
		common::Read(headerStream, iSavedVersion);
		int64_t iSavedSize = 0;
		common::Read(headerStream, iSavedSize);
		int64_t iDifferenceVersion = 0;
		common::Read(headerStream, iDifferenceVersion);
		int64_t iDifferenceSize = 0;
		common::Read(headerStream, iDifferenceSize);

		bool bSavedSizeValid = std::is_trivially_copyable_v<SAVED_TYPE> ? (iSavedSize == static_cast<int64_t>(sizeof(SAVED_TYPE))) : true;
		if (iSavedVersion != SAVED_TYPE::kiVersion || !bSavedSizeValid)
		{
			LOG(kDefault, kWarning, "DifferenceStreamReader SAVED_TYPE version mismatch: file {} {}, expected {} {}", iSavedVersion, iSavedSize, SAVED_TYPE::kiVersion, sizeof(SAVED_TYPE));
			return;
		}

		bool bDifferenceSizeValid = std::is_trivially_copyable_v<DIFFERENCE_TYPE> ? (iDifferenceSize == static_cast<int64_t>(sizeof(DIFFERENCE_TYPE))) : true;
		if (iDifferenceVersion != DIFFERENCE_TYPE::kiVersion || !bDifferenceSizeValid)
		{
			LOG(kDefault, kWarning, "DifferenceStreamReader DIFFERENCE_TYPE version mismatch: file {} {}, expected {} {}", iDifferenceVersion, iDifferenceSize, DIFFERENCE_TYPE::kiVersion, sizeof(DIFFERENCE_TYPE));
			return;
		}

		headerStream >> rSavedStart;
		if constexpr (std::is_trivially_copyable_v<DIFFERENCE_TYPE>)
			common::Read(headerStream, rInitialDifference);
		else
			headerStream >> rInitialDifference;
		mCurrentDifference = rInitialDifference;
		common::Read(headerStream, mDifferenceCount);
		headerStream >> mSavedEnd;

		miStartTick = rSavedStart.interpolate.iTick;
		LOG(kDefault, kVerbose, "DifferenceStreamReader at frame {}: Saved start: {} Initial difference: {}", miStartTick, rSavedStart.Crc(), rInitialDifference.Crc());

		// Load difference records
		if (mDifferenceCount > 0)
		{
			std::fstream fileStream = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".frames"));
			if constexpr (std::is_trivially_copyable_v<DIFFERENCE_TYPE>)
			{
				mDifferences.resize(mDifferenceCount);
				common::Read(fileStream, mDifferences);
				int64_t iBytesRead = fileStream.gcount();
				if (iBytesRead != static_cast<int64_t>(sizeof(difference_t) * mDifferenceCount))
				{
					LOG(kDefault, kWarning, "Recorded frames file size doesn't match header");
					return;
				}
			}
			else
			{
				mDifferences.reserve(mDifferenceCount);
				for (int64_t i = 0; i < mDifferenceCount; ++i)
				{
					int64_t iTick = 0;
					common::Read(fileStream, iTick);
					DIFFERENCE_TYPE difference {};
					fileStream >> difference;
					mDifferences.emplace_back(iTick, std::move(difference));
				}
			}

			mDifferencesIterator = mDifferences.begin();
		}

		// Load checksums for validation
		int64_t iChecksumCount = mSavedEnd.interpolate.iTick - rSavedStart.interpolate.iTick + 1;
		if (iChecksumCount > 0)
		{
			std::fstream checksumStream = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".checksums"));
			mChecksums.resize(iChecksumCount);
			common::Read(checksumStream, mChecksums);
			int64_t iBytesRead = checksumStream.gcount();
			if (iBytesRead != static_cast<int64_t>(sizeof(common::crc_t) * iChecksumCount))
			{
				LOG(kDefault, kWarning, "Checksum file size doesn't match expected count (expected {}, got {})", iChecksumCount, iBytesRead / sizeof(common::crc_t));
				DEBUG_BREAK();
				mChecksums.clear();
			}
		}

		if constexpr (kbReplayFullFrames)
		{
			// Load complete frame snapshots for debugging
			std::fstream fullFramesFile = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".fullframes"));
			if (fullFramesFile)
			{
				mFullFramesStream << fullFramesFile.rdbuf();
				SAVED_TYPE firstFrame;
				mFullFramesStream >> firstFrame;
				ASSERT(firstFrame.Crc() == rSavedStart.Crc());
				++miFullFramesIndex;
			}
		}

		mbLoaded = true;
	}

	int64_t GetRecordedFrameCount()
	{
		return mDifferenceCount;
	}

	const SAVED_TYPE& GetSavedEnd() const
	{
		return mSavedEnd;
	}

	void ValidateChecksum(int64_t iTick, const SAVED_TYPE& rSavedCurrent)
	{
		if (mChecksums.empty())
		{
			return;
		}

		int64_t iChecksumIndex = iTick - miStartTick - 1;
		if (iChecksumIndex < 0 || iChecksumIndex >= static_cast<int64_t>(mChecksums.size()))
		{
			return;
		}

		LOG(kDefault, kVerbose, "Checksum DifferenceStreamReader {}: {}", rSavedCurrent.interpolate.iTick, rSavedCurrent.Crc());

		[[maybe_unused]] SAVED_TYPE savedFrame {};
		[[maybe_unused]] bool bSavedFrameValid = false;
		if constexpr (kbReplayFullFrames)
		{
			// Read full frame snapshot to maintain stream synchronization
			if (iChecksumIndex == miFullFramesIndex && mFullFramesStream.rdbuf()->in_avail() > 0)
			{
				mFullFramesStream >> savedFrame;
				++miFullFramesIndex;
				bSavedFrameValid = true;
			}
		}

		common::crc_t currentChecksum = rSavedCurrent.Crc();
		common::crc_t savedChecksum = mChecksums.at(iChecksumIndex);

		// On checksum mismatch, provide detailed diagnostics
		if (currentChecksum != savedChecksum)
		{
			if constexpr (kbReplayFullFrames)
			{
				if (bSavedFrameValid)
				{
					savedFrame.LogDifferences(rSavedCurrent);
				}
			}
			LOG(kNetwork, kError, "LogDifferences CRC Client: {} Server: {}", currentChecksum, savedChecksum);
		}
	}

	bool LoadDifference(int64_t iTick, DIFFERENCE_TYPE& rDifference)
	{
		// Check if reached end of recording
		if (mSavedEnd.interpolate.iTick + 1 == iTick)
		{
			return false;
		}

		// Load difference if available for this frame, otherwise use current
		if (mDifferencesIterator != mDifferences.end() && iTick == std::get<0>(*mDifferencesIterator))
		{
			rDifference = std::get<1>(*mDifferencesIterator);
			++mDifferencesIterator;
			mCurrentDifference = rDifference;

			LOG(kDefault, kVerbose, "Loaded difference {}: {}", iTick, mCurrentDifference.Crc());
		}
		else
		{
			rDifference = mCurrentDifference;
		}

		return true;
	}

	bool Update(int64_t iTick, DIFFERENCE_TYPE& rDifference, const SAVED_TYPE& rSavedCurrent)
	{
		ValidateChecksum(iTick, rSavedCurrent);

		return LoadDifference(iTick, rDifference);
	}

	bool Loaded()
	{
		return mbLoaded;
	}

private:

	bool mbLoaded = false;

	SAVED_TYPE mSavedEnd {};
	int64_t mDifferenceCount = 0;

	std::vector<difference_t> mDifferences;
	typename std::vector<difference_t>::iterator mDifferencesIterator = mDifferences.end();
	DIFFERENCE_TYPE mCurrentDifference {};

	std::vector<common::crc_t> mChecksums;
	int64_t miStartTick = 0;

	[[no_unique_address]] std::conditional_t<kbReplayFullFrames, std::stringstream, common::Empty> mFullFramesStream;
	[[no_unique_address]] std::conditional_t<kbReplayFullFrames, int64_t, common::Empty> miFullFramesIndex = {};
};

} // namespace engine
