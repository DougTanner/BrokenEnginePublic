#include "FileManager.h"

namespace
{

using ScopedHandle = std::unique_ptr<void, decltype(&CloseHandle)>;

struct SymbolicLinkReparseDataBuffer
{
	ULONG uiReparseTag;
	USHORT uiReparseDataLength;
	USHORT uiReserved;
	USHORT uiSubstituteNameOffset;
	USHORT uiSubstituteNameLength;
	USHORT uiPrintNameOffset;
	USHORT uiPrintNameLength;
	ULONG uiFlags;
	WCHAR cPathBuffer[1];
};

std::filesystem::path PathFromUtf8(const std::string& rValue)
{
	const std::u8string value(reinterpret_cast<const char8_t*>(rValue.data()), rValue.size());
	return std::filesystem::path(value);
}

std::optional<std::filesystem::path> FindExecutableOnPath(const wchar_t* pcExecutable)
{
	DWORD uiCharacters = SearchPathW(nullptr, pcExecutable, nullptr, 0, nullptr, nullptr);
	if (uiCharacters == 0)
	{
		return std::nullopt;
	}
	std::vector<wchar_t> path(uiCharacters);
	DWORD uiWritten = SearchPathW(nullptr, pcExecutable, nullptr, static_cast<DWORD>(path.size()), path.data(), nullptr);
	if (uiWritten == 0 || uiWritten >= path.size())
	{
		return std::nullopt;
	}
	return std::filesystem::path(std::wstring(path.data(), uiWritten));
}

std::string TrimLine(std::string value)
{
	while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == '\0'))
	{
		value.pop_back();
	}
	return value;
}

std::optional<std::filesystem::path> RunGit(const std::filesystem::path& rGit, const std::filesystem::path& rRoot, const wchar_t* pcArguments)
{
	std::wstring parameters = L" -C \"" + rRoot.native() + L"\" " + pcArguments;
	common::ExecutableResult result;
	try
	{
		result = common::RunExecutable(rGit, parameters);
	}
	catch (const std::exception&)
	{
		return std::nullopt;
	}
	if (result.miExitCode != 0)
	{
		return std::nullopt;
	}
	std::string output = TrimLine(std::move(result.mOutput));
	if (output.empty())
	{
		return std::nullopt;
	}
	return std::filesystem::absolute(PathFromUtf8(output)).lexically_normal();
}

bool PathEqual(const std::filesystem::path& rLeft, const std::filesystem::path& rRight)
{
	return CompareStringOrdinal(rLeft.native().c_str(), -1, rRight.native().c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool PathLess(const std::filesystem::path& rLeft, const std::filesystem::path& rRight)
{
	int iResult = CompareStringOrdinal(rLeft.native().c_str(), -1, rRight.native().c_str(), -1, TRUE);
	return iResult == CSTR_LESS_THAN || (iResult == CSTR_EQUAL && CompareStringOrdinal(rLeft.native().c_str(), -1, rRight.native().c_str(), -1, FALSE) == CSTR_LESS_THAN);
}

bool IsOrdinaryDirectory(const std::filesystem::path& rPath)
{
	DWORD uiAttributes = GetFileAttributesW(rPath.native().c_str());
	return uiAttributes != INVALID_FILE_ATTRIBUTES && (uiAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (uiAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool IsReparsePoint(const std::filesystem::path& rPath)
{
	DWORD uiAttributes = GetFileAttributesW(rPath.native().c_str());
	return uiAttributes != INVALID_FILE_ATTRIBUTES && (uiAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

struct FileSnapshot
{
	uint64_t uiVolumeSerial = 0;
	FILE_ID_128 mFileId {};
	uint64_t uiSize = 0;
	FILETIME mLastWrite {};
};

FileSnapshot SnapshotFile(const std::filesystem::path& rPath)
{
	ScopedHandle file(CreateFileW(rPath.native().c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr), CloseHandle);
	if (file.get() == nullptr || file.get() == INVALID_HANDLE_VALUE)
	{
		throw std::runtime_error(std::format("Unable to snapshot output file: {}", rPath.string()));
	}
	FILE_ID_INFO idInfo {};
	FILE_BASIC_INFO basicInfo {};
	FILE_STANDARD_INFO standardInfo {};
	if (!GetFileInformationByHandleEx(file.get(), FileIdInfo, &idInfo, sizeof(idInfo)) || !GetFileInformationByHandleEx(file.get(), FileBasicInfo, &basicInfo, sizeof(basicInfo)) || !GetFileInformationByHandleEx(file.get(), FileStandardInfo, &standardInfo, sizeof(standardInfo)))
	{
		throw std::runtime_error(std::format("Unable to query output file identity: {}", rPath.string()));
	}
	return {.uiVolumeSerial = idInfo.VolumeSerialNumber, .mFileId = idInfo.FileId, .uiSize = static_cast<uint64_t>(standardInfo.EndOfFile.QuadPart), .mLastWrite = {basicInfo.LastWriteTime.LowPart, static_cast<DWORD>(basicInfo.LastWriteTime.HighPart)}};
}

bool SnapshotEqual(const FileSnapshot& rLeft, const FileSnapshot& rRight)
{
	return rLeft.uiVolumeSerial == rRight.uiVolumeSerial && std::memcmp(&rLeft.mFileId, &rRight.mFileId, sizeof(FILE_ID_128)) == 0 && rLeft.uiSize == rRight.uiSize && CompareFileTime(&rLeft.mLastWrite, &rRight.mLastWrite) == 0;
}

std::vector<uint8_t> HashFile(const std::filesystem::path& rPath)
{
	BCRYPT_ALG_HANDLE pAlgorithm = nullptr;
	BCRYPT_HASH_HANDLE pHash = nullptr;
	std::vector<uint8_t> result(32);
	if (BCryptOpenAlgorithmProvider(&pAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 || BCryptCreateHash(pAlgorithm, &pHash, nullptr, 0, nullptr, 0, 0) < 0)
	{
		if (pAlgorithm != nullptr)
		{
			BCryptCloseAlgorithmProvider(pAlgorithm, 0);
		}
		throw std::runtime_error("Unable to initialize SHA-256");
	}
	std::ifstream stream(rPath, std::ios::binary);
	std::vector<char> buffer(64 * 1024);
	while (stream)
	{
		stream.read(buffer.data(), buffer.size());
		if (stream.gcount() > 0 && BCryptHashData(pHash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(stream.gcount()), 0) < 0)
		{
			BCryptDestroyHash(pHash);
			BCryptCloseAlgorithmProvider(pAlgorithm, 0);
			throw std::runtime_error("Unable to hash output file");
		}
	}
	if (stream.bad() || BCryptFinishHash(pHash, result.data(), static_cast<ULONG>(result.size()), 0) < 0)
	{
		BCryptDestroyHash(pHash);
		BCryptCloseAlgorithmProvider(pAlgorithm, 0);
		throw std::runtime_error("Unable to finish output hash");
	}
	BCryptDestroyHash(pHash);
	BCryptCloseAlgorithmProvider(pAlgorithm, 0);
	return result;
}

bool IsRecognizedLinkRaw(const std::filesystem::path& rLink, const std::filesystem::path& rExpected)
{
	ScopedHandle link(CreateFileW(rLink.native().c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr), CloseHandle);
	if (link.get() == nullptr || link.get() == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	std::vector<uint8_t> bytes(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
	DWORD uiReturned = 0;
	if (!DeviceIoControl(link.get(), FSCTL_GET_REPARSE_POINT, nullptr, 0, bytes.data(), static_cast<DWORD>(bytes.size()), &uiReturned, nullptr))
	{
		return false;
	}
	const SymbolicLinkReparseDataBuffer* pData = reinterpret_cast<const SymbolicLinkReparseDataBuffer*>(bytes.data());
	if (pData->uiReparseTag != IO_REPARSE_TAG_SYMLINK)
	{
		return false;
	}
	std::wstring target(pData->cPathBuffer + pData->uiSubstituteNameOffset / sizeof(wchar_t), pData->uiSubstituteNameLength / sizeof(wchar_t));
	if (target.rfind(L"\\??\\", 0) == 0)
	{
		target.erase(0, 4);
	}
	std::filesystem::path targetPath(target);
	if (targetPath.is_relative())
	{
		targetPath = rLink.parent_path() / targetPath;
	}
	return PathEqual(std::filesystem::absolute(targetPath).lexically_normal(), std::filesystem::absolute(rExpected).lexically_normal());
}

uint64_t AddChecked(uint64_t uiLeft, uint64_t uiRight)
{
	if (uiLeft > std::numeric_limits<uint64_t>::max() - uiRight)
	{
		throw std::runtime_error("Output materialization size overflow");
	}
	return uiLeft + uiRight;
}

uint64_t MultiplyChecked(uint64_t uiLeft, uint64_t uiRight)
{
	if (uiRight != 0 && uiLeft > std::numeric_limits<uint64_t>::max() / uiRight)
	{
		throw std::runtime_error("Output materialization size overflow");
	}
	return uiLeft * uiRight;
}

}

FileManager::FileManager(std::span<char*> argvSpan)
{
	ASSERT(gpFileManager == nullptr);

	gpFileManager = this;
	wchar_t pcForbidExpensiveExport[2] {};
	DWORD uiForbidExpensiveExportLength = GetEnvironmentVariableW(L"BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT", pcForbidExpensiveExport, static_cast<DWORD>(std::size(pcForbidExpensiveExport)));
	mbForbidExpensiveExport = uiForbidExpensiveExportLength == 1 && pcForbidExpensiveExport[0] == L'1';

	if (argvSpan.size() == 1)
	{
		mpInputDirectories[0] = "../../../Engine/Data";
		mpInputDirectories[1] = "../../../Projects/BrokenEngineSandbox/Data";
		mOutputDirectory = "../../../Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output/Data";
	}
	else
	{
		ASSERT(argvSpan.size() == 4);
		mpInputDirectories[0] = argvSpan[1];
		mpInputDirectories[1] = argvSpan[2];
		mOutputDirectory = argvSpan[3];
	}

	mpInputDirectories[0] = std::filesystem::canonical(mpInputDirectories[0]);
	mpInputDirectories[1] = std::filesystem::canonical(mpInputDirectories[1]);

	// Repo ThirdParty dir, derived from the canonical engine-data dir (<repo>/Engine/Data) rather than a fragile output-relative .. chain
	mThirdPartyDirectory = mpInputDirectories[0].parent_path().parent_path() / "ThirdParty";
	VERIFY_SUCCESS(std::filesystem::exists(mThirdPartyDirectory));

	// Extract project name from project data directory
	mProjectName = mpInputDirectories[1].parent_path().filename().string();
	mOutputDirectory = std::filesystem::absolute(mOutputDirectory).lexically_normal();
	InitializeWorktreeOutputs();
	if (mDataOutput.meState == OutputRootState::kAbsent)
	{
		std::filesystem::create_directories(mOutputDirectory);
		mDataOutput.meState = OutputRootState::kLocal;
	}

	// Input data directories
	LOG(kDefault, kDebug, "Engine data directory: \"{}\"", mpInputDirectories[0].string());
	LOG(kDefault, kDebug, "Game data directory: \"{}\"", mpInputDirectories[1].string());
	LOG(kDefault, kDebug, "Project name: \"{}\"", mProjectName);

	// Temporaries directory
	wchar_t pcDirectory[MAX_PATH] {};
	DWORD uiTempResult = GetTempPathW(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	if (uiTempResult == 0)
	{
		throw std::runtime_error("Failed to get temp directory path");
	}
	mTempDirectory = pcDirectory;
	VERIFY_SUCCESS(std::filesystem::exists(mTempDirectory));
	mTempDirectory.append("DataPacker");
	mTempDirectory /= mProjectName;
	std::filesystem::create_directories(mTempDirectory);
	LOG(kDefault, kDebug, "Temp directory: \"{}\"", mTempDirectory.string());
	mGaeaCacheDirectory = mTempDirectory / "Gaea";
	std::filesystem::create_directories(mGaeaCacheDirectory);

	mpInputFingerprintCache = std::make_unique<InputFingerprintCache>(mpInputDirectories[0].parent_path().parent_path());

	LOG(kDefault, kDebug, "Output directory: \"{}\"", mOutputDirectory.string());
}

void FileManager::InitializeWorktreeOutputs()
{
	mDataOutput.mDestination = mOutputDirectory;
	mAttributionOutput.mDestination = mOutputDirectory.parent_path() / "Attribution";
	std::filesystem::file_status dataStatus = std::filesystem::symlink_status(mDataOutput.mDestination);
	std::filesystem::file_status attributionStatus = std::filesystem::symlink_status(mAttributionOutput.mDestination);
	mDataOutput.meState = dataStatus.type() == std::filesystem::file_type::not_found ? OutputRootState::kAbsent : OutputRootState::kLocal;
	mAttributionOutput.meState = attributionStatus.type() == std::filesystem::file_type::not_found ? OutputRootState::kAbsent : OutputRootState::kLocal;
	for (OutputRootInfo* pRoot : { &mDataOutput, &mAttributionOutput })
	{
		DWORD uiAttributes = GetFileAttributesW(pRoot->mDestination.native().c_str());
		if (uiAttributes != INVALID_FILE_ATTRIBUTES && (uiAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			pRoot->meState = OutputRootState::kUnvalidatedReparse;
		}
	}
	auto RejectUnvalidatedReparse = [this]()
	{
		for (const OutputRootInfo* pRoot : { &mDataOutput, &mAttributionOutput })
		{
			if (pRoot->meState == OutputRootState::kUnvalidatedReparse)
			{
				throw std::runtime_error(std::format("Cannot validate output reparse point: {}", pRoot->mDestination.string()));
			}
		}
	};

	std::optional<std::filesystem::path> git = FindExecutableOnPath(L"git.exe");
	std::filesystem::path repositoryRoot = mpInputDirectories[0].parent_path().parent_path();
	if (!git)
	{
		RejectUnvalidatedReparse();
		LOG(kDefault, kWarning, "Git unavailable; worktree output linking disabled");
		return;
	}
	std::optional<std::filesystem::path> gitDirectory = RunGit(*git, repositoryRoot, L"rev-parse --path-format=absolute --git-dir");
	std::optional<std::filesystem::path> commonDirectory = RunGit(*git, repositoryRoot, L"rev-parse --path-format=absolute --git-common-dir");
	if (!gitDirectory || !commonDirectory || PathEqual(*gitDirectory, *commonDirectory))
	{
		RejectUnvalidatedReparse();
		return;
	}

	std::wstring parameters = L" -C \"" + repositoryRoot.native() + L"\" worktree list --porcelain -z";
	common::ExecutableResult result;
	try
	{
		result = common::RunExecutable(*git, parameters);
	}
	catch (const std::exception&)
	{
		RejectUnvalidatedReparse();
		LOG(kDefault, kWarning, "Git worktree discovery failed; output linking disabled");
		return;
	}
	if (result.miExitCode != 0 || result.mOutput.rfind("worktree ", 0) != 0)
	{
		RejectUnvalidatedReparse();
		LOG(kDefault, kWarning, "Malformed Git worktree metadata; output linking disabled");
		return;
	}
	size_t uiEnd = result.mOutput.find('\0');
	std::filesystem::path primaryRoot = PathFromUtf8(result.mOutput.substr(9, uiEnd - 9));
	std::optional<std::filesystem::path> primaryCommon = RunGit(*git, primaryRoot, L"rev-parse --path-format=absolute --git-common-dir");
	if (!primaryCommon || !PathEqual(*primaryCommon, *commonDirectory))
	{
		RejectUnvalidatedReparse();
		LOG(kDefault, kWarning, "Inconsistent Git worktree metadata; output linking disabled");
		return;
	}

	std::filesystem::path expected = repositoryRoot / "Projects" / mProjectName / "Platforms/VisualStudio2026/Output/Data";
	if (!PathEqual(expected.lexically_normal(), mOutputDirectory))
	{
		RejectUnvalidatedReparse();
		return;
	}
	mDataOutput.mSource = primaryRoot / std::filesystem::relative(mDataOutput.mDestination, repositoryRoot);
	mAttributionOutput.mSource = primaryRoot / std::filesystem::relative(mAttributionOutput.mDestination, repositoryRoot);
	for (OutputRootInfo* pRoot : { &mDataOutput, &mAttributionOutput })
	{
		if (pRoot->meState == OutputRootState::kAbsent && IsReparsePoint(pRoot->mSource))
		{
			throw std::runtime_error(std::format("Primary output source is a reparse point: {}", pRoot->mSource.string()));
		}
		if (pRoot->meState == OutputRootState::kUnvalidatedReparse)
		{
			if (!IsRecognizedLinkRaw(pRoot->mDestination, pRoot->mSource))
			{
				throw std::runtime_error(std::format("Unexpected output reparse point: {}", pRoot->mDestination.string()));
			}
			pRoot->meState = OutputRootState::kRecognizedPrimaryLink;
		}
		if (pRoot->meState == OutputRootState::kAbsent && IsOrdinaryDirectory(pRoot->mSource))
		{
			if (CreateSymbolicLinkW(pRoot->mDestination.native().c_str(), pRoot->mSource.native().c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
			{
				pRoot->meState = OutputRootState::kRecognizedPrimaryLink;
				LOG(kDefault, kDebug, "Linked worktree output \"{}\" to \"{}\"", pRoot->mDestination.string(), pRoot->mSource.string());
			}
			else
			{
				DWORD uiError = GetLastError();
				if (uiError != ERROR_PRIVILEGE_NOT_HELD && uiError != ERROR_INVALID_PARAMETER && uiError != ERROR_NOT_SUPPORTED)
				{
					throw std::runtime_error(std::format("CreateSymbolicLinkW failed with {}", uiError));
				}
				if (MaterializeOutput(*pRoot) == EnsureLocalResult::kCancelled)
				{
					throw std::runtime_error("Output materialization cancelled");
				}
			}
		}
	}
}

FileManager::OutputRootInfo& FileManager::GetOutputRoot(OutputRoot eRoot)
{
	return eRoot == OutputRoot::kData ? mDataOutput : mAttributionOutput;
}

std::filesystem::path FileManager::GetAttributionDirectory() const
{
	return mAttributionOutput.mDestination;
}

FileManager::EnsureLocalResult FileManager::EnsureLocal(OutputRoot eRoot)
{
	OutputRootInfo& rRoot = GetOutputRoot(eRoot);
	if (rRoot.meState == OutputRootState::kLocal)
	{
		return EnsureLocalResult::kAlreadyLocal;
	}
	return MaterializeOutput(rRoot);
}

FileManager::EnsureLocalResult FileManager::MaterializeOutput(OutputRootInfo& rRoot)
{
	if (rRoot.meState == OutputRootState::kLocal)
	{
		return EnsureLocalResult::kAlreadyLocal;
	}
	if (!rRoot.mSource.empty() && IsReparsePoint(rRoot.mSource))
	{
		throw std::runtime_error(std::format("Primary output source is a reparse point: {}", rRoot.mSource.string()));
	}
	if (rRoot.mSource.empty() || !IsOrdinaryDirectory(rRoot.mSource))
	{
		std::filesystem::create_directories(rRoot.mDestination);
		rRoot.meState = OutputRootState::kLocal;
		return EnsureLocalResult::kMaterialized;
	}
	std::filesystem::create_directories(rRoot.mDestination.parent_path());
	std::vector<std::filesystem::path> files;
	std::vector<FileSnapshot> snapshots;
	uint64_t uiAllocation = 0;
	DWORD uiSectorsPerCluster = 0;
	DWORD uiBytesPerSector = 0;
	DWORD uiFreeClusters = 0;
	DWORD uiTotalClusters = 0;
	if (!GetDiskFreeSpaceW(rRoot.mDestination.root_path().native().c_str(), &uiSectorsPerCluster, &uiBytesPerSector, &uiFreeClusters, &uiTotalClusters))
	{
		throw std::runtime_error("GetDiskFreeSpaceW failed");
	}
	uint64_t uiClusterBytes = static_cast<uint64_t>(uiSectorsPerCluster) * uiBytesPerSector;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(rRoot.mSource))
	{
		DWORD uiAttributes = GetFileAttributesW(rEntry.path().native().c_str());
		if (uiAttributes == INVALID_FILE_ATTRIBUTES || (uiAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			throw std::runtime_error(std::format("Nested output reparse point rejected: {}", rEntry.path().string()));
		}
		if (rEntry.is_regular_file())
		{
			files.push_back(rEntry.path());
			snapshots.push_back(SnapshotFile(rEntry.path()));
			uint64_t uiSize = rEntry.file_size();
			uiAllocation = AddChecked(uiAllocation, AddChecked(uiSize, uiClusterBytes - 1) / uiClusterBytes * uiClusterBytes);
		}
		else if (!rEntry.is_directory())
		{
			throw std::runtime_error(std::format("Unsupported output entry: {}", rEntry.path().string()));
		}
	}
	std::vector<size_t> order(files.size()); std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&files, &rRoot](size_t uiLeftIndex, size_t uiRightIndex)
	{
		return PathLess(std::filesystem::relative(files.at(uiLeftIndex), rRoot.mSource), std::filesystem::relative(files.at(uiRightIndex), rRoot.mSource));
	});
	ULARGE_INTEGER available {};
	ULARGE_INTEGER total {};
	if (!GetDiskFreeSpaceExW(rRoot.mDestination.root_path().native().c_str(), &available, &total, nullptr))
	{
		throw std::runtime_error("GetDiskFreeSpaceExW failed");
	}
	uint64_t uiReserve = (std::max)(1ull << 30, AddChecked(MultiplyChecked(uiAllocation, 5), 99) / 100);
	if (AddChecked(uiAllocation, uiReserve) > available.QuadPart)
	{
		std::wstring message = std::format(L"Insufficient disk space. Required: {} bytes. Available: {} bytes.", AddChecked(uiAllocation, uiReserve), available.QuadPart);
		MessageBoxW(nullptr, message.c_str(), L"DataPacker - Insufficient Disk Space", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		throw std::runtime_error("Insufficient disk space to materialize worktree output");
	}
	uint64_t uiProjected = available.QuadPart - uiAllocation;
	uint64_t uiWarning = (std::max)(10ull << 30, AddChecked(MultiplyChecked(total.QuadPart, 10), 99) / 100);
	if (uiProjected < uiWarning)
	{
		std::string message = std::format("Copy-on-write needs approximately {} bytes. Available: {} bytes. Projected remaining: {} bytes.", uiAllocation, available.QuadPart, uiProjected);
		if (MessageBoxW(nullptr, std::filesystem::path(message).native().c_str(), L"DataPacker - Low Disk Space", MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL) != IDOK)
		{
			return EnsureLocalResult::kCancelled;
		}
	}
	std::filesystem::path staging;
	for (uint32_t uiAttempt = 0; uiAttempt <= 15; ++uiAttempt)
	{
		staging = rRoot.mDestination;
		staging += std::format(".materializing.{}.{}", GetCurrentProcessId(), uiAttempt);
		std::error_code errorCode;
		if (std::filesystem::create_directory(staging, errorCode))
		{
			break;
		}
		if (uiAttempt == 15)
		{
			throw std::runtime_error("Unable to create unique output staging directory");
		}
	}
	try
	{
		for (size_t uiIndex : order)
		{
			const std::filesystem::path& rSource = files.at(uiIndex);
			std::filesystem::path destination = staging / std::filesystem::relative(rSource, rRoot.mSource);
			std::filesystem::create_directories(destination.parent_path());
			std::filesystem::copy_file(rSource, destination);
			std::filesystem::last_write_time(destination, std::filesystem::last_write_time(rSource));
			if (HashFile(rSource) != HashFile(destination) || !SnapshotEqual(snapshots.at(uiIndex), SnapshotFile(rSource)))
			{
				throw std::runtime_error(std::format("Copied output verification failed: {}", rSource.string()));
			}
		}
		std::vector<std::filesystem::path> finalFiles;
		for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(rRoot.mSource))
		{
			DWORD uiAttributes = GetFileAttributesW(rEntry.path().native().c_str());
			if (uiAttributes == INVALID_FILE_ATTRIBUTES || (uiAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				throw std::runtime_error(std::format("Nested output reparse point rejected: {}", rEntry.path().string()));
			}
			std::filesystem::file_status status = rEntry.symlink_status();
			if (std::filesystem::is_regular_file(status))
			{
				finalFiles.push_back(rEntry.path());
			}
			else if (!std::filesystem::is_directory(status))
			{
				throw std::runtime_error(std::format("Unsupported output entry: {}", rEntry.path().string()));
			}
		}
		std::sort(finalFiles.begin(), finalFiles.end(), [&rRoot](const std::filesystem::path& rLeft, const std::filesystem::path& rRight)
		{
			return PathLess(std::filesystem::relative(rLeft, rRoot.mSource), std::filesystem::relative(rRight, rRoot.mSource));
		});
		if (finalFiles.size() != order.size())
		{
			throw std::runtime_error("Output source inventory changed during materialization");
		}
		for (size_t uiPosition = 0; uiPosition < order.size(); ++uiPosition)
		{
			size_t uiIndex = order.at(uiPosition);
			if (std::filesystem::relative(finalFiles.at(uiPosition), rRoot.mSource) != std::filesystem::relative(files.at(uiIndex), rRoot.mSource) || !SnapshotEqual(SnapshotFile(finalFiles.at(uiPosition)), snapshots.at(uiIndex)))
			{
				throw std::runtime_error("Output source inventory changed during materialization");
			}
		}
		if (rRoot.meState == OutputRootState::kRecognizedPrimaryLink)
		{
			if (!IsRecognizedLinkRaw(rRoot.mDestination, rRoot.mSource))
			{
				throw std::runtime_error("Output link changed during materialization");
			}
			std::filesystem::remove(rRoot.mDestination);
		}
		std::filesystem::rename(staging, rRoot.mDestination);
	}
	catch (...)
	{
		bool bPreserveStaging = false;
		if (!std::filesystem::exists(rRoot.mDestination) && rRoot.meState == OutputRootState::kRecognizedPrimaryLink)
		{
			if (!CreateSymbolicLinkW(rRoot.mDestination.native().c_str(), rRoot.mSource.native().c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
			{
				bPreserveStaging = true;
				LOG(kDefault, kError, "Failed to restore output link \"{}\"; complete recovery copy retained at \"{}\" (Win32 {})", rRoot.mDestination.string(), staging.string(), GetLastError());
			}
		}
		if (!bPreserveStaging && std::filesystem::exists(staging))
		{
			std::filesystem::remove_all(staging);
		}
		throw;
	}
	rRoot.meState = OutputRootState::kLocal;
	LOG(kDefault, kDebug, "Materialized worktree output \"{}\" from \"{}\" ({} bytes)", rRoot.mDestination.string(), rRoot.mSource.string(), uiAllocation);
	return EnsureLocalResult::kMaterialized;
}

std::string FileManager::GetFingerprint(const std::filesystem::path& rPath)
{
	return mpInputFingerprintCache->Get(rPath);
}

FileManager::~FileManager()
{
	if (gpFileManager == this)
	{
		gpFileManager = nullptr;
	}
}
