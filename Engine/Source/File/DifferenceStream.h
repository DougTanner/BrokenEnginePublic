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
	}

	void Update(int64_t iFrame, const DIFFERENCE_TYPE& rDifference)
	{
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
		if (mDifferences.size() == 0)
		{
			LOG("No data to save");
			return;
		}

		mHeader.iDifferenceCount = mDifferences.size();
		memcpy(&mHeader.savedEnd, &rSavedEnd, sizeof(mHeader.savedEnd));
		LOG("Difference count: {} Last frame: {}", mHeader.iDifferenceCount, rSavedEnd.iFrame);
		WriteVersionedFile(fileFlags, rFilename, mHeader);

		std::fstream fileStream = gpFileManager->OpenFile(fileFlags, std::filesystem::path(rFilename).concat(".frames"));
		fileStream.write(reinterpret_cast<char*>(&mDifferences.at(0)), sizeof(difference_t) * mDifferences.size());
	}

private:

	header_t mHeader {};

	std::vector<difference_t> mDifferences;
	DIFFERENCE_TYPE mCurrentDifference {};
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

		std::fstream fileStream = gpFileManager->OpenFile(rFileFlags, std::filesystem::path(rFilename).concat(".frames"));
		mDifferences.resize(mHeader.iDifferenceCount);
		int64_t iBytesRead = fileStream.read(reinterpret_cast<char*>(&mDifferences.at(0)), sizeof(difference_t) * mHeader.iDifferenceCount).gcount();
		if (iBytesRead != static_cast<int64_t>(sizeof(difference_t) * mHeader.iDifferenceCount))
		{
			LOG("Recorded frames file size doesn't match header");
			return;
		}

		memcpy(&rSavedStart, &mHeader.savedStart, sizeof(rSavedStart));
		memcpy(&rInitialDifference, &mHeader.initialDifference, sizeof(rInitialDifference));

		mDifferencesIterator = mDifferences.begin();
	}

	int64_t GetRecordedFrameCount()
	{
		return mDifferences.size();
	}

	bool Update(int64_t iFrame, DIFFERENCE_TYPE& rDifference)
	{
		if (mHeader.savedEnd.iFrame == iFrame)
		{
			return false;
		}

		if (mDifferencesIterator != mDifferences.end() && iFrame == std::get<0>(*mDifferencesIterator))
		{
			//LOG("Loaded: {}", iFrame);

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
		return mDifferences.size() > 0;
	}

private:

	std::vector<difference_t> mDifferences;
	typename std::vector<difference_t>::iterator mDifferencesIterator = mDifferences.end();
	DIFFERENCE_TYPE mCurrentDifference {};
};

} // namespace engine
