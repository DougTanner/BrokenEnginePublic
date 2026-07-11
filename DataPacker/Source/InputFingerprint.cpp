#include "InputFingerprint.h"

namespace
{

class Sha1Hasher
{
public:

	void Update(const std::byte* pBytes, size_t uiBytes)
	{
		muiTotalBytes += uiBytes;
		for (size_t i = 0; i < uiBytes; ++i)
		{
			mpBlock[muiBlockBytes++] = static_cast<uint8_t>(pBytes[i]);
			if (muiBlockBytes == std::size(mpBlock))
			{
				ProcessBlock();
				muiBlockBytes = 0;
			}
		}
	}

	void Update(std::string_view text)
	{
		Update(reinterpret_cast<const std::byte*>(text.data()), text.size());
	}

	std::string Finish()
	{
		uint64_t uiBitCount = muiTotalBytes * 8;
		mpBlock[muiBlockBytes++] = 0x80;
		if (muiBlockBytes > 56)
		{
			std::fill(mpBlock + muiBlockBytes, mpBlock + std::size(mpBlock), static_cast<uint8_t>(0));
			ProcessBlock();
			muiBlockBytes = 0;
		}
		std::fill(mpBlock + muiBlockBytes, mpBlock + 56, static_cast<uint8_t>(0));
		for (int64_t iByte = 0; iByte < 8; ++iByte)
		{
			mpBlock[63 - iByte] = static_cast<uint8_t>(uiBitCount >> (iByte * 8));
		}
		ProcessBlock();

		std::string result;
		result.reserve(40);
		for (uint32_t uiWord : mpState)
		{
			result += std::format("{:08x}", uiWord);
		}
		return result;
	}

private:

	static uint32_t RotateLeft(uint32_t uiValue, uint32_t uiBits)
	{
		return (uiValue << uiBits) | (uiValue >> (32 - uiBits));
	}

	void ProcessBlock()
	{
		uint32_t puiWords[80] {};
		for (int64_t iWord = 0; iWord < 16; ++iWord)
		{
			int64_t iByte = iWord * 4;
			puiWords[iWord] = (static_cast<uint32_t>(mpBlock[iByte]) << 24) |
				(static_cast<uint32_t>(mpBlock[iByte + 1]) << 16) |
				(static_cast<uint32_t>(mpBlock[iByte + 2]) << 8) |
				static_cast<uint32_t>(mpBlock[iByte + 3]);
		}
		for (int64_t iWord = 16; iWord < 80; ++iWord)
		{
			puiWords[iWord] = RotateLeft(puiWords[iWord - 3] ^ puiWords[iWord - 8] ^ puiWords[iWord - 14] ^ puiWords[iWord - 16], 1);
		}

		uint32_t uiA = mpState[0];
		uint32_t uiB = mpState[1];
		uint32_t uiC = mpState[2];
		uint32_t uiD = mpState[3];
		uint32_t uiE = mpState[4];
		for (int64_t iRound = 0; iRound < 80; ++iRound)
		{
			uint32_t uiFunction = 0;
			uint32_t uiConstant = 0;
			if (iRound < 20)
			{
				uiFunction = (uiB & uiC) | ((~uiB) & uiD);
				uiConstant = 0x5A827999;
			}
			else if (iRound < 40)
			{
				uiFunction = uiB ^ uiC ^ uiD;
				uiConstant = 0x6ED9EBA1;
			}
			else if (iRound < 60)
			{
				uiFunction = (uiB & uiC) | (uiB & uiD) | (uiC & uiD);
				uiConstant = 0x8F1BBCDC;
			}
			else
			{
				uiFunction = uiB ^ uiC ^ uiD;
				uiConstant = 0xCA62C1D6;
			}

			uint32_t uiNext = RotateLeft(uiA, 5) + uiFunction + uiE + uiConstant + puiWords[iRound];
			uiE = uiD;
			uiD = uiC;
			uiC = RotateLeft(uiB, 30);
			uiB = uiA;
			uiA = uiNext;
		}

		mpState[0] += uiA;
		mpState[1] += uiB;
		mpState[2] += uiC;
		mpState[3] += uiD;
		mpState[4] += uiE;
	}

	uint32_t mpState[5] { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	uint8_t mpBlock[64] {};
	uint64_t muiTotalBytes = 0;
	size_t muiBlockBytes = 0;
};

std::string HashFileAsGitBlob(const std::filesystem::path& rPath, uintmax_t uiSize)
{
	Sha1Hasher hasher;
	hasher.Update(std::format("blob {}", uiSize));
	hasher.Update(std::string_view("\0", 1));

	std::fstream stream(rPath, std::ios::in | std::ios::binary);
	std::vector<std::byte> buffer(64 * 1024);
	while (stream)
	{
		stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
		hasher.Update(buffer.data(), static_cast<size_t>(stream.gcount()));
	}
	if (!stream.eof())
	{
		throw std::runtime_error(std::format("Failed to fingerprint \"{}\"", rPath.string()));
	}
	return hasher.Finish();
}

std::optional<std::filesystem::path> FindExecutableOnPath(const wchar_t* pcExecutable)
{
	DWORD uiPathCharacters = SearchPathW(nullptr, pcExecutable, nullptr, 0, nullptr, nullptr);
	while (uiPathCharacters != 0)
	{
		std::vector<wchar_t> path(uiPathCharacters);
		DWORD uiWrittenCharacters = SearchPathW(nullptr, pcExecutable, nullptr, static_cast<DWORD>(path.size()), path.data(), nullptr);
		if (uiWrittenCharacters == 0)
		{
			return std::nullopt;
		}
		if (uiWrittenCharacters < path.size())
		{
			return std::filesystem::path(std::wstring(path.data(), uiWrittenCharacters));
		}
		uiPathCharacters = uiWrittenCharacters;
	}
	return std::nullopt;
}

}

InputFingerprintCache::InputFingerprintCache(const std::filesystem::path& rRepositoryRoot)
{
	LoadGitIndex(rRepositoryRoot);
}

std::string InputFingerprintCache::Get(const std::filesystem::path& rPath)
{
	std::scoped_lock lock(mMutex);
	return GetUnlocked(rPath);
}

std::string InputFingerprintCache::GetUnlocked(const std::filesystem::path& rPath)
{
	if (std::filesystem::is_directory(rPath))
	{
		return GetDirectory(rPath);
	}
	return GetFile(rPath);
}

std::string InputFingerprintCache::GetFile(const std::filesystem::path& rPath)
{
	std::string key = PathKey(rPath);
	for (;;)
	{
		FileSnapshot snapshot = Snapshot(rPath);
		auto cachedIterator = mCachedFingerprints.find(key);
		if (cachedIterator != mCachedFingerprints.end() && cachedIterator->second.snapshot == snapshot)
		{
			return cachedIterator->second.fingerprint;
		}

		std::string fingerprint;
		auto gitIterator = mGitIndex.find(key);
		if (gitIterator != mGitIndex.end() && gitIterator->second.snapshot == snapshot)
		{
			fingerprint = gitIterator->second.blobId;
		}
		else
		{
			fingerprint = HashFileAsGitBlob(rPath, snapshot.uiSize);
		}
		if (Snapshot(rPath) == snapshot)
		{
			mCachedFingerprints.insert_or_assign(key, CachedFingerprint {.snapshot = snapshot, .fingerprint = fingerprint});
			return fingerprint;
		}
	}
}

std::string InputFingerprintCache::GetDirectory(const std::filesystem::path& rPath)
{
	std::vector<std::filesystem::path> files;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(rPath))
	{
		if (rEntry.is_regular_file())
		{
			files.push_back(rEntry.path());
		}
	}
	std::sort(files.begin(), files.end(), [&rPath](const std::filesystem::path& rLeft, const std::filesystem::path& rRight)
	{
		return std::filesystem::relative(rLeft, rPath).generic_string() < std::filesystem::relative(rRight, rPath).generic_string();
	});

	Sha1Hasher hasher;
	for (const std::filesystem::path& rFile : files)
	{
		std::string relativePath = std::filesystem::relative(rFile, rPath).generic_string();
		hasher.Update(relativePath);
		hasher.Update(std::string_view("\0", 1));
		hasher.Update(GetFile(rFile));
		hasher.Update(std::string_view("\0", 1));
	}
	return hasher.Finish();
}

void InputFingerprintCache::LoadGitIndex(const std::filesystem::path& rRepositoryRoot)
{
	std::optional<std::filesystem::path> gitExecutable = FindExecutableOnPath(L"git.exe");
	if (!gitExecutable)
	{
		LOG(kDefault, kDebug, "Git unavailable; hashing DataPacker inputs by content");
		return;
	}

	std::wstring parameters = L" -C \"" + rRepositoryRoot.native() + L"\" ls-files --stage -z";
	common::ExecutableResult result;
	try
	{
		result = common::RunExecutable(*gitExecutable, parameters);
	}
	catch (const std::exception&)
	{
		LOG(kDefault, kDebug, "Git unavailable; hashing DataPacker inputs by content");
		return;
	}
	if (result.miExitCode != 0)
	{
		LOG(kDefault, kDebug, "DataPacker inputs are outside a Git checkout; hashing by content");
		return;
	}

	std::vector<GitCandidate> candidates;
	size_t uiEntryStart = 0;
	while (uiEntryStart < result.mOutput.size())
	{
		size_t uiEntryEnd = result.mOutput.find('\0', uiEntryStart);
		if (uiEntryEnd == std::string::npos)
		{
			return;
		}
		std::string_view entry(result.mOutput.data() + uiEntryStart, uiEntryEnd - uiEntryStart);
		uiEntryStart = uiEntryEnd + 1;
		size_t uiTab = entry.find('\t');
		if (uiTab == std::string_view::npos)
		{
			return;
		}
		std::istringstream headerStream {std::string(entry.substr(0, uiTab))};
		std::string mode;
		std::string blobId;
		int64_t iStage = -1;
		headerStream >> mode >> blobId >> iStage;
		if (iStage != 0 || (mode != "100644" && mode != "100755"))
		{
			continue;
		}
		std::filesystem::path path = rRepositoryRoot / std::filesystem::path(std::string(entry.substr(uiTab + 1)));
		std::error_code error;
		if (!std::filesystem::is_regular_file(path, error))
		{
			continue;
		}
		try
		{
			candidates.emplace_back(GitCandidate {.path = path, .key = PathKey(path), .blobId = std::move(blobId), .snapshot = Snapshot(path)});
		}
		catch (const std::system_error&)
		{
			continue;
		}
	}

	parameters = L" -C \"" + rRepositoryRoot.native() + L"\" diff-files --name-only --ignore-submodules=all -z";
	try
	{
		result = common::RunExecutable(*gitExecutable, parameters);
	}
	catch (const std::exception&)
	{
		return;
	}
	if (result.miExitCode != 0)
	{
		return;
	}
	std::unordered_set<std::string> dirtyPaths;
	uiEntryStart = 0;
	while (uiEntryStart < result.mOutput.size())
	{
		size_t uiEntryEnd = result.mOutput.find('\0', uiEntryStart);
		if (uiEntryEnd == std::string::npos)
		{
			return;
		}
		dirtyPaths.emplace(PathKey(rRepositoryRoot / std::filesystem::path(std::string(result.mOutput.data() + uiEntryStart, uiEntryEnd - uiEntryStart))));
		uiEntryStart = uiEntryEnd + 1;
	}

	for (GitCandidate& rCandidate : candidates)
	{
		if (dirtyPaths.contains(rCandidate.key))
		{
			continue;
		}
		try
		{
			if (Snapshot(rCandidate.path) == rCandidate.snapshot)
			{
				mGitIndex.emplace(std::move(rCandidate.key), GitIndexEntry {.snapshot = rCandidate.snapshot, .blobId = std::move(rCandidate.blobId)});
			}
		}
		catch (const std::system_error&)
		{
			continue;
		}
	}
	LOG(kDefault, kDebug, "Loaded {} Git input fingerprints", mGitIndex.size());
}

InputFingerprintCache::FileSnapshot InputFingerprintCache::Snapshot(const std::filesystem::path& rPath)
{
	HANDLE hFile = CreateFileW(rPath.native().c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), std::format("Failed to open \"{}\" for fingerprinting", rPath.string()));
	}
	std::unique_ptr<void, decltype(&CloseHandle)> fileHandle(hFile, &CloseHandle);
	FILE_BASIC_INFO basicInfo {};
	BY_HANDLE_FILE_INFORMATION fileInfo {};
	if (!GetFileInformationByHandleEx(fileHandle.get(), FileBasicInfo, &basicInfo, sizeof(basicInfo))
		|| !GetFileInformationByHandle(fileHandle.get(), &fileInfo))
	{
		throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), std::format("Failed to inspect \"{}\" for fingerprinting", rPath.string()));
	}
	return
	{
		.uiSize = (static_cast<uintmax_t>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow,
		.iLastWriteTime = basicInfo.LastWriteTime.QuadPart,
		.iChangeTime = basicInfo.ChangeTime.QuadPart,
		.uiFileId = (static_cast<uint64_t>(fileInfo.nFileIndexHigh) << 32) | fileInfo.nFileIndexLow,
		.uiVolumeSerialNumber = fileInfo.dwVolumeSerialNumber,
	};
}

std::string InputFingerprintCache::PathKey(const std::filesystem::path& rPath)
{
	return common::ToLower(std::filesystem::absolute(rPath).lexically_normal().string());
}
