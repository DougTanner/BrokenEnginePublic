#pragma once

#include "File/FileManager.h"

namespace engine
{

// Helper to transfer data between objects using stream operators (for non-copyable types)
template<typename T>
inline void TransferViaStream(const T& rFrom, T& rTo)
{
	std::stringstream buffer;
	buffer << rFrom;
	buffer >> rTo;
}

template<typename SAVED_TYPE, typename DIFFERENCE_TYPE>
class DifferenceStreamWriter
{
public:

	using difference_t = std::tuple<int64_t, DIFFERENCE_TYPE>;

	DifferenceStreamWriter(const SAVED_TYPE& rSavedStart, const DIFFERENCE_TYPE& rInitialDifference)
	{
		mDifferences.reserve(1024);

		// Initialize starting state and frame
		miStartFrame = rSavedStart.iFrame;
		TransferViaStream(rSavedStart, mSavedStart);
		mInitialDifference = rInitialDifference;
		mCurrentDifference = rInitialDifference;
		LOG("DifferenceStreamWriter at frame {}: Saved start: {} Initial difference: {}", miStartFrame, mSavedStart.Checksum(), mInitialDifference.Checksum());

		// Record initial checksum
		mChecksums.reserve(1024);
		mChecksums.push_back(rSavedStart.Checksum());
		LOG("Checksum DifferenceStreamWriter {}: {}", miStartFrame, *std::prev(mChecksums.end()));

#ifdef ENABLE_REPLAY_FULL_FRAMES
		mFullFramesStream << rSavedStart;
#endif
	}

	void Update(int64_t iFrame, const DIFFERENCE_TYPE& rDifference, const SAVED_TYPE& rSavedCurrent)
	{
		// Record checksum for this frame
		mChecksums.push_back(rSavedCurrent.Checksum());
		LOG("Checksum DifferenceStreamWriter Update {}: {}", rSavedCurrent.iFrame, *std::prev(mChecksums.end()));

#ifdef ENABLE_REPLAY_FULL_FRAMES
		mFullFramesStream << rSavedCurrent;
#endif

		// Skip if no state change occurred
		if (rDifference == mCurrentDifference)
		{
			return;
		}

		// Save the changed difference
		mDifferences.emplace_back(iFrame, rDifference);
		mCurrentDifference = rDifference;
	}

	void Save(FileFlags_t fileFlags, const std::filesystem::path& rFilename, const SAVED_TYPE& rSavedEnd)
	{
		int64_t iDifferenceCount = mDifferences.size();

		// Write header with start/end states and metadata
		std::fstream headerStream = gpFileManager->OpenFile(fileFlags, rFilename);
		headerStream << mSavedStart;
		common::Write(headerStream, mInitialDifference);
		common::Write(headerStream, iDifferenceCount);
		headerStream << rSavedEnd;
		LOG("DifferenceStreamWriter save at frame {}: Count {} Checksum {}", rSavedEnd.iFrame, iDifferenceCount, rSavedEnd.Checksum());

		// Write difference records
		std::fstream fileStream = gpFileManager->OpenFile(fileFlags, std::filesystem::path(rFilename).concat(".frames"));
		if (!mDifferences.empty())
		{
			common::Write(fileStream, mDifferences);
		}

		// Write checksums for validation
		mChecksums.push_back(rSavedEnd.Checksum());
		LOG("Checksum DifferenceStreamWriter Save {}: {}", rSavedEnd.iFrame, *std::prev(mChecksums.end()));
		std::fstream checksumStream = gpFileManager->OpenFile(fileFlags, std::filesystem::path(rFilename).concat(".checksums"));
		if (!mChecksums.empty())
		{
			common::Write(checksumStream, mChecksums);
		}

#ifdef ENABLE_REPLAY_FULL_FRAMES
		// Write complete frame snapshots for debugging
		mFullFramesStream << rSavedEnd;
		std::fstream fullFramesStream = gpFileManager->OpenFile(fileFlags, std::filesystem::path(rFilename).concat(".fullframes"));
		fullFramesStream << mFullFramesStream.str();
#endif
	}

private:

	SAVED_TYPE mSavedStart {};
	DIFFERENCE_TYPE mInitialDifference {};

	std::vector<difference_t> mDifferences;
	DIFFERENCE_TYPE mCurrentDifference {};

	std::vector<common::crc_t> mChecksums;
	int64_t miStartFrame = 0;

#ifdef ENABLE_REPLAY_FULL_FRAMES
	std::stringstream mFullFramesStream;
#endif
};

template<typename SAVED_TYPE, typename DIFFERENCE_TYPE>
class DifferenceStreamReader
{
public:

	using difference_t = std::tuple<int64_t, DIFFERENCE_TYPE>;

	DifferenceStreamReader(const FileFlags_t& rFileFlags, const std::filesystem::path& rFilename, SAVED_TYPE& rSavedStart, DIFFERENCE_TYPE& rInitialDifference)
	{
		// Read header with start/end states and metadata
		std::fstream headerStream = gpFileManager->OpenFile(rFileFlags, rFilename);
		if (!headerStream)
		{
			return;
		}

		headerStream >> rSavedStart;
		common::Read(headerStream, rInitialDifference);
		common::Read(headerStream, mDifferenceCount);
		headerStream >> mSavedEnd;

		miStartFrame = rSavedStart.iFrame;
		LOG("DifferenceStreamReader at frame {}: Saved start: {} Initial difference: {}", miStartFrame, rSavedStart.Checksum(), rInitialDifference.Checksum());

		// Load difference records
		if (mDifferenceCount > 0)
		{
			std::fstream fileStream = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".frames"));
			mDifferences.resize(mDifferenceCount);
			common::Read(fileStream, mDifferences);
			int64_t iBytesRead = fileStream.gcount();
			if (iBytesRead != static_cast<int64_t>(sizeof(difference_t) * mDifferenceCount))
			{
				LOG("Recorded frames file size doesn't match header");
				return;
			}

			mDifferencesIterator = mDifferences.begin();
		}

		// Load checksums for validation
		int64_t iChecksumCount = mSavedEnd.iFrame - rSavedStart.iFrame + 1;
		if (iChecksumCount > 0)
		{
			std::fstream checksumStream = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".checksums"));
			mChecksums.resize(iChecksumCount);
			common::Read(checksumStream, mChecksums);
			int64_t iBytesRead = checksumStream.gcount();
			if (iBytesRead != static_cast<int64_t>(sizeof(common::crc_t) * iChecksumCount))
			{
				LOG("Checksum file size doesn't match expected count (expected {}, got {})", iChecksumCount, iBytesRead / sizeof(common::crc_t));
				DEBUG_BREAK();
				mChecksums.clear();
			}
		}

#ifdef ENABLE_REPLAY_FULL_FRAMES
		// Load complete frame snapshots for debugging
		std::fstream fullFramesFile = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".fullframes"));
		if (fullFramesFile)
		{
			mFullFramesStream << fullFramesFile.rdbuf();
			SAVED_TYPE firstFrame;
			mFullFramesStream >> firstFrame;
			ASSERT(firstFrame == rSavedStart);
			++miFullFramesIndex;
		}
#endif
	}

	int64_t GetRecordedFrameCount()
	{
		return mDifferenceCount;
	}

	const SAVED_TYPE& GetSavedEnd() const
	{
		return mSavedEnd;
	}

	bool Update(int64_t iFrame, DIFFERENCE_TYPE& rDifference, const SAVED_TYPE& rSavedCurrent)
	{
		// Validate checksum if available
		if (!mChecksums.empty())
		{
			int64_t iChecksumIndex = iFrame - miStartFrame;
			if (iChecksumIndex >= 0 && iChecksumIndex < static_cast<int64_t>(mChecksums.size()))
			{
				LOG("Checksum DifferenceStreamReader {}: {}", rSavedCurrent.iFrame, rSavedCurrent.Checksum());

#ifdef ENABLE_REPLAY_FULL_FRAMES
				// Compare against full frame snapshot if available
				if (iChecksumIndex == miFullFramesIndex && mFullFramesStream.rdbuf()->in_avail() > 0)
				{
					SAVED_TYPE savedFrame {};
					mFullFramesStream >> savedFrame;
					common::BreakOnNotEqual(savedFrame, rSavedCurrent);
					++miFullFramesIndex;
				}
#endif

				common::crc_t currentChecksum = rSavedCurrent.Checksum();
				common::crc_t savedChecksum = mChecksums.at(iChecksumIndex);

				// On checksum mismatch, provide detailed diagnostics
				if (currentChecksum != savedChecksum)
				{
#ifdef ENABLE_REPLAY_FULL_FRAMES
					if (miFullFramesIndex > 0)
					{
						SAVED_TYPE savedFrame {};
						mFullFramesStream >> savedFrame;
						ASSERT(savedFrame.iFrame == rSavedCurrent.iFrame);
						++miFullFramesIndex;
						common::BreakOnNotEqual(rSavedCurrent, savedFrame);
					}
#endif
					common::BreakOnNotEqual(currentChecksum, savedChecksum);
				}
			}
		}

		// Check if reached end of recording
		if (mSavedEnd.iFrame == iFrame)
		{
			return false;
		}

		// Load difference if available for this frame, otherwise use current
		if (mDifferencesIterator != mDifferences.end() && iFrame == std::get<0>(*mDifferencesIterator))
		{
			rDifference = std::get<1>(*mDifferencesIterator);
			++mDifferencesIterator;
			mCurrentDifference = rDifference;

			LOG("Loaded difference {}: {}", iFrame, mCurrentDifference.Checksum());
		}
		else
		{
			rDifference = mCurrentDifference;
		}

		return true;
	}

	bool Loaded()
	{
		return !mDifferences.empty();
	}

private:

	SAVED_TYPE mSavedEnd {};
	int64_t mDifferenceCount = 0;

	std::vector<difference_t> mDifferences;
	typename std::vector<difference_t>::iterator mDifferencesIterator = mDifferences.end();
	DIFFERENCE_TYPE mCurrentDifference {};

	std::vector<common::crc_t> mChecksums;
	int64_t miStartFrame = 0;

#ifdef ENABLE_REPLAY_FULL_FRAMES
	std::stringstream mFullFramesStream;
	int64_t miFullFramesIndex = 0;
#endif
};

} // namespace engine
