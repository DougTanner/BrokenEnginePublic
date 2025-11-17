#pragma once

#include "File/FileManager.h"

namespace engine
{

template<typename SAVED_TYPE, typename DIFFERENCE_TYPE>
struct DifferenceStreamHeader
{
	static constexpr int64_t kiVersion = 9 + SAVED_TYPE::kiVersion + DIFFERENCE_TYPE::kiVersion;

	SAVED_TYPE savedStart {};

	DIFFERENCE_TYPE initialDifference {};
	int64_t iDifferenceCount = 0;

	SAVED_TYPE savedEnd {};
};

template<typename SAVED_TYPE, typename DIFFERENCE_TYPE>
class DifferenceStreamWriter
{
public:

	using header_t = DifferenceStreamHeader<SAVED_TYPE, DIFFERENCE_TYPE>;
	using difference_t = std::tuple<int64_t, DIFFERENCE_TYPE>;

	DifferenceStreamWriter(const SAVED_TYPE& rSavedStart, const DIFFERENCE_TYPE& rInitialDifference)
	{
		mDifferences.reserve(1024);

		memcpy(&mHeader.savedStart, &rSavedStart, sizeof(mHeader.savedStart));
		memcpy(&mHeader.initialDifference, &rInitialDifference, sizeof(mHeader.initialDifference));
		memcpy(&mCurrentDifference, &rInitialDifference, sizeof(mCurrentDifference));

		miStartFrame = rSavedStart.iFrame;
		mChecksums.reserve(1024);
		mChecksums.push_back(rSavedStart.Checksum());
	}

	void Update(int64_t iFrame, const DIFFERENCE_TYPE& rDifference, const SAVED_TYPE& rSavedCurrent)
	{
		mChecksums.push_back(rSavedCurrent.Checksum());
		LOG("Checksum DifferenceStreamWriter {}: {}", rSavedCurrent.iFrame, rSavedCurrent.Checksum());

		if (rDifference == mCurrentDifference)
		{
			return;
		}

		LOG("Saved: {}", iFrame);
		mDifferences.emplace_back(iFrame, rDifference);

		mCurrentDifference = rDifference;
	}

	void Save(FileFlags_t fileFlags, const std::filesystem::path& rFilename, const SAVED_TYPE& rSavedEnd)
	{
		mHeader.iDifferenceCount = mDifferences.size();
		memcpy(&mHeader.savedEnd, &rSavedEnd, sizeof(mHeader.savedEnd));
		LOG("Difference count: {} Last frame: {}", mHeader.iDifferenceCount, rSavedEnd.iFrame);
		WriteVersionedFile(fileFlags, rFilename, mHeader);

		std::fstream fileStream = gpFileManager->OpenFile(fileFlags, std::filesystem::path(rFilename).concat(".frames"));
		if (!mDifferences.empty())
		{
			common::Write(fileStream, mDifferences);
		}

		mChecksums.push_back(rSavedEnd.Checksum());
		std::fstream checksumStream = gpFileManager->OpenFile(fileFlags, std::filesystem::path(rFilename).concat(".checksums"));
		if (!mChecksums.empty())
		{
			common::Write(checksumStream, mChecksums);
		}
		LOG("Checksum count: {}", mChecksums.size());
	}

private:

	header_t mHeader {};

	std::vector<difference_t> mDifferences;
	DIFFERENCE_TYPE mCurrentDifference {};

	std::vector<common::crc_t> mChecksums;
	int64_t miStartFrame = 0;
};

template<typename SAVED_TYPE, typename DIFFERENCE_TYPE>
class DifferenceStreamReader
{
public:

	using header_t = DifferenceStreamHeader<SAVED_TYPE, DIFFERENCE_TYPE>;
	using difference_t = std::tuple<int64_t, DIFFERENCE_TYPE>;

	header_t mHeader {};

	DifferenceStreamReader(const FileFlags_t& rFileFlags, const std::filesystem::path& rFilename, SAVED_TYPE& rSavedStart, DIFFERENCE_TYPE& rInitialDifference)
	{
		if (!ReadVersionedFile(rFileFlags, rFilename, mHeader))
		{
			return;
		}

		LOG("Start/End: {} -> {} Difference count: {}", mHeader.savedStart.iFrame, mHeader.savedEnd.iFrame, mHeader.iDifferenceCount);

		if (mHeader.iDifferenceCount > 0)
		{
			std::fstream fileStream = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".frames"));
			mDifferences.resize(mHeader.iDifferenceCount);
			common::Read(fileStream, mDifferences);
			int64_t iBytesRead = fileStream.gcount();
			if (iBytesRead != static_cast<int64_t>(sizeof(difference_t) * mHeader.iDifferenceCount))
			{
				LOG("Recorded frames file size doesn't match header");
				return;
			}

			mDifferencesIterator = mDifferences.begin();
		}

		miStartFrame = mHeader.savedStart.iFrame;
		int64_t iChecksumCount = mHeader.savedEnd.iFrame - mHeader.savedStart.iFrame + 1;
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
			else
			{
				LOG("Loaded checksum count: {}", mChecksums.size());
			}
		}

		memcpy(&rSavedStart, &mHeader.savedStart, sizeof(rSavedStart));
		memcpy(&rInitialDifference, &mHeader.initialDifference, sizeof(rInitialDifference));
	}

	int64_t GetRecordedFrameCount()
	{
		return mDifferences.size();
	}

	bool Update(int64_t iFrame, DIFFERENCE_TYPE& rDifference, const SAVED_TYPE& rSavedCurrent)
	{
		if (!mChecksums.empty())
		{
			int64_t iChecksumIndex = iFrame - miStartFrame;
			if (iChecksumIndex >= 0 && iChecksumIndex < static_cast<int64_t>(mChecksums.size()))
			{
				LOG("Checksum DifferenceStreamReader {}: {}", rSavedCurrent.iFrame, rSavedCurrent.Checksum());
				common::BreakOnNotEqual(rSavedCurrent.Checksum(), mChecksums.at(iChecksumIndex));
			}
		}

		if (mHeader.savedEnd.iFrame == iFrame)
		{
			return false;
		}

		if (mDifferencesIterator != mDifferences.end() && iFrame == std::get<0>(*mDifferencesIterator))
		{
			LOG("Loaded: {}", iFrame);

			rDifference = std::get<1>(*mDifferencesIterator);
			++mDifferencesIterator;
			mCurrentDifference = rDifference;
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

	std::vector<difference_t> mDifferences;
	typename std::vector<difference_t>::iterator mDifferencesIterator = mDifferences.end();
	DIFFERENCE_TYPE mCurrentDifference {};

	std::vector<common::crc_t> mChecksums;
	int64_t miStartFrame = 0;
};

} // namespace engine
