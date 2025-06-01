#include "ExportAudio.h"

#include "FileManager.h"

using enum common::ChunkFlags;

std::optional<common::ChunkFlags_t> ExportAudio::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	return rDirectoryEntry.path().extension() == ".wav" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kAudio) : std::nullopt;
}

void ExportAudio::Export()
{
	std::filesystem::path adpcmencode3Executable(gpFileManager->mWindowsSdkBinariesDirectory);
	adpcmencode3Executable.append("adpcmencode3.exe");

	std::filesystem::path adpcmFile(gpFileManager->mTempDirectory);
	adpcmFile /= mRelativeDirectory;
	adpcmFile /= mInputPath.filename();
	std::filesystem::remove(adpcmFile);

	std::wstring commandLineParameters(L"");
	commandLineParameters += L" \"" + mInputPath.native() + L"\"";
	commandLineParameters += L" \"" + adpcmFile.native() + L"\"";

	auto log = std::to_wstring(common::gpThreadLocal->miThreadId.value());
	log += L": ";
	log += adpcmencode3Executable.native();
	log += commandLineParameters;
	log += L"\n";
	OutputDebugStringW(log.c_str());

	std::string output = common::RunExecutable(adpcmencode3Executable, commandLineParameters);
	if (output.find("ERROR") != std::string::npos)
	{
		throw std::runtime_error(std::format("adpcmencode3.exe error: {}", output));
	}

	VERIFY_SUCCESS(std::filesystem::exists(adpcmFile));
	int64_t iAdpcmBytes = std::filesystem::file_size(adpcmFile);
	VERIFY_SUCCESS(iAdpcmBytes < static_cast<int64_t>(std::filesystem::file_size(mInputPath)));

	auto [pHeader, dataSpan] = AllocateHeaderAndData(iAdpcmBytes);
	std::fstream fileStream(adpcmFile, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(dataSpan.data()), iAdpcmBytes);
}
