#pragma once

#include "File/FileManager.h"

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
		int64_t iStartTick = rSavedStart.interpolate.iTick;
		TransferViaStream(rSavedStart, mSavedStart);
		mInitialDifference = rInitialDifference;
		mCurrentDifference = rInitialDifference;
		LOG(kDefault, kVerbose, "DifferenceStreamWriter at frame {}: Saved start: {} Initial difference: {}", iStartTick, mSavedStart.Crc(), mInitialDifference.Crc());

		// Record initial checksum
		mChecksums.reserve(1024);
		mChecksums.push_back(mSavedStart.Crc());
		LOG(kDefault, kVerbose, "Checksum DifferenceStreamWriter {}: {}", iStartTick, *std::prev(mChecksums.end()));

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

	bool Save(FileFlags_t fileFlags, const std::filesystem::path& rFilename, const SAVED_TYPE& rSavedEnd)
	{
		int64_t iDifferenceCount = mDifferences.size();

		// Write header with version info, start/end states and metadata
		const bool bHeaderWritten = gpFileManager->WriteFileAtomically(fileFlags, rFilename, [&](std::fstream& rHeaderStream)
		{
			// Write version headers (matches WriteVersionedFile pattern)
			WriteVersionHeader<SAVED_TYPE>(rHeaderStream);
			WriteVersionHeader<DIFFERENCE_TYPE>(rHeaderStream);

			rHeaderStream << mSavedStart;
			if constexpr (std::is_trivially_copyable_v<DIFFERENCE_TYPE>)
			{
				common::Write(rHeaderStream, mInitialDifference);
			}
			else
			{
				rHeaderStream << mInitialDifference;
			}
			common::Write(rHeaderStream, iDifferenceCount);
			rHeaderStream << rSavedEnd;
		});
		LOG(kDefault, kVerbose, "DifferenceStreamWriter save at frame {}: Count {} Checksum {}", rSavedEnd.interpolate.iTick, iDifferenceCount, rSavedEnd.Crc());

		// Write difference records
		const std::filesystem::path framesFilename = std::filesystem::path(rFilename).concat(".frames");
		const bool bFramesWritten = gpFileManager->WriteFileAtomically(fileFlags, framesFilename, [&](std::fstream& rFramesStream)
		{
			for (const auto& [iTick, difference] : mDifferences)
			{
				common::Write(rFramesStream, iTick);
				if constexpr (std::is_trivially_copyable_v<DIFFERENCE_TYPE>)
				{
					common::Write(rFramesStream, difference);
				}
				else
				{
					rFramesStream << difference;
				}
			}
		});

		// Write checksums for validation
		mChecksums.push_back(rSavedEnd.Crc());
		LOG(kDefault, kVerbose, "Checksum DifferenceStreamWriter Save {}: {}", rSavedEnd.interpolate.iTick, *std::prev(mChecksums.end()));
		const std::filesystem::path checksumsFilename = std::filesystem::path(rFilename).concat(".checksums");
		const bool bChecksumsWritten = gpFileManager->WriteFileAtomically(fileFlags, checksumsFilename, [&](std::fstream& rChecksumStream)
		{
			if (!mChecksums.empty())
			{
				common::Write(rChecksumStream, mChecksums);
			}
		});

		// Track the first sibling that failed so a torn set can be reported and cleaned up as a whole.
		std::filesystem::path failedFilename;
		if (!bHeaderWritten)
		{
			failedFilename = rFilename;
		}
		else if (!bFramesWritten)
		{
			failedFilename = framesFilename;
		}
		else if (!bChecksumsWritten)
		{
			failedFilename = checksumsFilename;
		}

		if constexpr (kbReplayFullFrames)
		{
			// Write complete frame snapshots for debugging
			mFullFramesStream << rSavedEnd;
			const std::filesystem::path fullFramesFilename = std::filesystem::path(rFilename).concat(".fullframes");
			const bool bFullFramesWritten = gpFileManager->WriteFileAtomically(fileFlags, fullFramesFilename, [&](std::fstream& rFullFramesStream)
			{
				rFullFramesStream << mFullFramesStream.str();
			});
			if (!bFullFramesWritten && failedFilename.empty())
			{
				failedFilename = fullFramesFilename;
			}
		}

		// Any in-process write failure leaves a torn recording; delete the whole sibling set so a partial replay isn't loaded.
		if (!failedFilename.empty())
		{
			LOG(kDefault, kError, "DifferenceStreamWriter save failed writing \"{}\"; deleting partial replay set", failedFilename.string());
			CleanupFiles(fileFlags, rFilename);
		}

		return failedFilename.empty();
	}

	void CleanupFiles(const FileFlags_t& rFileFlags, const std::filesystem::path& rFilename) const
	{
		const auto RemovePartialFile = [&](const std::filesystem::path& rPartialFilename)
		{
			try
			{
				gpFileManager->RemoveFile(rFileFlags, rPartialFilename);
			}
			catch (const std::filesystem::filesystem_error& rException)
			{
				LOG(kDefault, kError, "DifferenceStreamWriter cleanup failed removing \"{}\": {}", rPartialFilename.string(), rException.what());
			}
		};

		RemovePartialFile(rFilename);
		RemovePartialFile(std::filesystem::path(rFilename).concat(".frames"));
		RemovePartialFile(std::filesystem::path(rFilename).concat(".checksums"));
		if constexpr (kbReplayFullFrames)
		{
			RemovePartialFile(std::filesystem::path(rFilename).concat(".fullframes"));
		}
	}

private:

	SAVED_TYPE mSavedStart {};
	DIFFERENCE_TYPE mInitialDifference {};

	std::vector<difference_t> mDifferences;
	DIFFERENCE_TYPE mCurrentDifference {};

	std::vector<common::crc_t> mChecksums;

	std::stringstream mFullFramesStream;
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
		int64_t iSavedSize = 0;
		if (!ReadAndValidateVersionHeader<SAVED_TYPE>(headerStream, iSavedVersion, iSavedSize))
		{
			LOG(kDefault, kWarning, "DifferenceStreamReader SAVED_TYPE version mismatch: file {} {}, expected {} {}", iSavedVersion, iSavedSize, SAVED_TYPE::kiVersion, sizeof(SAVED_TYPE));
			return;
		}

		int64_t iDifferenceVersion = 0;
		int64_t iDifferenceSize = 0;
		if (!ReadAndValidateVersionHeader<DIFFERENCE_TYPE>(headerStream, iDifferenceVersion, iDifferenceSize))
		{
			LOG(kDefault, kWarning, "DifferenceStreamReader DIFFERENCE_TYPE version mismatch: file {} {}, expected {} {}", iDifferenceVersion, iDifferenceSize, DIFFERENCE_TYPE::kiVersion, sizeof(DIFFERENCE_TYPE));
			return;
		}

		headerStream >> rSavedStart;
		if constexpr (std::is_trivially_copyable_v<DIFFERENCE_TYPE>)
		{
			common::Read(headerStream, rInitialDifference);
		}
		else
		{
			headerStream >> rInitialDifference;
		}
		mCurrentDifference = rInitialDifference;
		common::Read(headerStream, mDifferenceCount);
		headerStream >> mSavedEnd;

		miStartTick = rSavedStart.interpolate.iTick;
		LOG(kDefault, kVerbose, "DifferenceStreamReader at frame {}: Saved start: {} Initial difference: {}", miStartTick, rSavedStart.Crc(), rInitialDifference.Crc());

		// Load difference records
		if (mDifferenceCount > 0)
		{
			std::fstream fileStream = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".frames"));
			// Trust boundary (replay .frames file): bound the difference count against the stream before
			// allocating; each record serializes at least an int64 tick plus one byte of difference.
			common::ValidateDeserializedCount(mDifferenceCount, sizeof(int64_t) + 1, fileStream, "DifferenceStreamReader differences");
			mDifferences.reserve(mDifferenceCount);
			for (int64_t i = 0; i < mDifferenceCount; ++i)
			{
				int64_t iTick = 0;
				common::Read(fileStream, iTick);
				if (fileStream.gcount() != static_cast<std::streamsize>(sizeof(iTick)))
				{
					LOG(kDefault, kWarning, "Recorded frames file size doesn't match header");
					return;
				}
				DIFFERENCE_TYPE difference {};
				if constexpr (std::is_trivially_copyable_v<DIFFERENCE_TYPE>)
				{
					common::Read(fileStream, difference);
					if (fileStream.gcount() != static_cast<std::streamsize>(sizeof(DIFFERENCE_TYPE)))
					{
						LOG(kDefault, kWarning, "Recorded frames file size doesn't match header");
						return;
					}
				}
				else
				{
					fileStream >> difference;
					if (!fileStream)
					{
						LOG(kDefault, kWarning, "Recorded frames file size doesn't match header");
						return;
					}
				}
				mDifferences.emplace_back(iTick, std::move(difference));
			}

			mDifferencesIterator = mDifferences.begin();
		}

		// Load checksums for validation
		int64_t iChecksumCount = mSavedEnd.interpolate.iTick - rSavedStart.interpolate.iTick + 1;
		if (iChecksumCount > 0)
		{
			std::fstream checksumStream = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".checksums"));
			// Trust boundary (replay .checksums file): bound the checksum count against the stream before resize.
			common::ValidateDeserializedCount(iChecksumCount, sizeof(common::crc_t), checksumStream, "DifferenceStreamReader checksums");
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
				if (firstFrame.Crc() != rSavedStart.Crc())
				{
					// Stale/mismatched debug file: discard it so frame comparisons don't reference the wrong baseline
					LOG(kDefault, kWarning, "Full frames file doesn't match saved start frame, discarding");
					DEBUG_BREAK();
					mFullFramesStream.str({});
				}
				else
				{
					++miFullFramesIndex;
				}
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
			LOG(kNetwork, kError, "LogDifferences CRC Client: {} Server: {}", savedChecksum, currentChecksum);
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

	std::stringstream mFullFramesStream;
	int64_t miFullFramesIndex = 0;
};

} // namespace engine
