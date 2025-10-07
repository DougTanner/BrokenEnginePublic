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

	// Read the entire ADPCM file first
	std::vector<uint8_t> adpcmFileData(iAdpcmBytes);
	std::fstream fileStream(adpcmFile, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(adpcmFileData.data()), iAdpcmBytes);

	// Parse ADPCMWAVEFORMAT structure at offset 20 (0x14)
	static_assert(sizeof(WAVEFORMATEX) == 18);
	const ADPCMWAVEFORMAT* pAdpcmFormat = reinterpret_cast<const ADPCMWAVEFORMAT*>(&adpcmFileData[20]);
	
	// Get data chunk size at offset 0x4A
	uint32_t uiDataChunkSize = *reinterpret_cast<const uint32_t*>(&adpcmFileData[0x4A]);
	
	// Allocate header and data for chunk (only store audio data, not WAV headers)
	auto [pHeader, dataSpan] = AllocateHeaderAndData(uiDataChunkSize);
	
	// Copy WAVEFORMATEX structure to AudioHeader
	pHeader->audioHeader.waveFormat = pAdpcmFormat->wfx;
	
	// Copy ADPCM-specific fields
	pHeader->audioHeader.uiSamplesPerBlock = pAdpcmFormat->wSamplesPerBlock;
	pHeader->audioHeader.uiNumCoef = pAdpcmFormat->wNumCoef;
	ASSERT(pHeader->audioHeader.uiNumCoef == 7);
	
	// Copy coefficient array (up to 7 sets)
	for (uint16_t i = 0; i < std::min(pAdpcmFormat->wNumCoef, static_cast<WORD>(7)); ++i)
	{
		pHeader->audioHeader.aCoeff[i].iCoef1 = pAdpcmFormat->aCoef[i].iCoef1;
		pHeader->audioHeader.aCoeff[i].iCoef2 = pAdpcmFormat->aCoef[i].iCoef2;
	}
	
	// Copy only the audio data (starting at offset 0x4E)
	std::memcpy(dataSpan.data(), &adpcmFileData[0x4E], uiDataChunkSize);
}
