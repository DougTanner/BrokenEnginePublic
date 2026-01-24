#include "ExportAudio.h"

#include "DirectXTK/Audio/WAVFileReader.h"

#include "FileManager.h"


using enum common::ChunkFlags;

std::optional<common::ChunkFlags_t> ExportAudio::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	return rDirectoryEntry.path().extension() == ".wav" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kAudio) : std::nullopt;
}

void ExportAudio::Export()
{
	// Load and parse WAV file using DirectXTK
	std::unique_ptr<uint8_t[]> waveData;
	const WAVEFORMATEX* pWaveformatex = nullptr;
	const uint8_t* pAudioData = nullptr;
	uint32_t uiAudioBytes = 0;

	CheckHresult(DirectX::LoadWAVAudioFromFile(mInputPath.c_str(), waveData, &pWaveformatex, &pAudioData, &uiAudioBytes));
	Assert(pWaveformatex->nChannels == 1 || pWaveformatex->nChannels == 2);

	// Convert audio data to int16_t samples
	std::vector<int16_t> pcmSamples;
	if (pWaveformatex->wFormatTag == WAVE_FORMAT_PCM && pWaveformatex->wBitsPerSample == 16)
	{
		// 16-bit PCM - direct copy
		size_t sampleCount = uiAudioBytes / sizeof(int16_t);
		pcmSamples.resize(sampleCount);
		std::memcpy(pcmSamples.data(), pAudioData, uiAudioBytes);
	}
	else if (pWaveformatex->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && pWaveformatex->wBitsPerSample == 32)
	{
		// 32-bit IEEE float - convert to int16
		size_t sampleCount = uiAudioBytes / sizeof(float);
		pcmSamples.resize(sampleCount);
		const float* floatSamples = reinterpret_cast<const float*>(pAudioData);

		for (size_t i = 0; i < sampleCount; ++i)
		{
			float sample = std::clamp(floatSamples[i], -1.0f, 1.0f);
			pcmSamples[i] = static_cast<int16_t>(sample * 32767.0f);
		}
	}
	else
	{
		Assert(false);
	}

	// Allocate header and data for chunk (only store audio data, not WAV headers)
	int64_t iPcmDataSize = pcmSamples.size() * sizeof(int16_t);
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iPcmDataSize);

	// Populate WAVEFORMATEX with PCM audio format information
	pHeader->audioHeader.waveFormat.wFormatTag = WAVE_FORMAT_PCM;
	pHeader->audioHeader.waveFormat.nChannels = pWaveformatex->nChannels;
	pHeader->audioHeader.waveFormat.nSamplesPerSec = pWaveformatex->nSamplesPerSec;
	pHeader->audioHeader.waveFormat.nBlockAlign = pWaveformatex->nChannels * sizeof(int16_t);
	pHeader->audioHeader.waveFormat.nAvgBytesPerSec = pWaveformatex->nSamplesPerSec * pWaveformatex->nChannels * sizeof(int16_t);
	pHeader->audioHeader.waveFormat.wBitsPerSample = 16;
	pHeader->audioHeader.waveFormat.cbSize = 0;

	// Copy PCM data directly
	std::memcpy(dataSpan.data(), pcmSamples.data(), iPcmDataSize);
}
