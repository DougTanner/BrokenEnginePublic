#include "ExportAudio.h"

#include "DirectXTK/Audio/WAVFileReader.h"

#include "FileManager.h"

#pragma warning(push, 0)
#pragma warning(disable : 4146 4244 4702 4706 6001 6011 6246 6262 6269 6308 6330 6387 26051 26408 26409 26429 26432 26433 26434 26435 26438 26440 26443 26444 26447 26448 26451 26455 26456 26459 26460 26461 26466 26472 26475 26477 26481 26482 26485 26488 26498 26490 26493 26494 26495 26496 26497 26812 26814 26818 26819 28182)
#define __asm__(z) a = a >> (uint8_t)(-s)
#include "codec-adpcm/src/ADPCM.h"
#pragma warning(pop)

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

	CHECK_HRESULT(DirectX::LoadWAVAudioFromFile(mInputPath.c_str(), waveData, &pWaveformatex, &pAudioData, &uiAudioBytes));
	ASSERT(pWaveformatex->nChannels == 1 || pWaveformatex->nChannels == 2);

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
		ASSERT(false);
	}

	// Create MS-ADPCM encoder using codec-adpcm library
	adpcm_ffmpeg::EncoderADPCM_MS encoder;
	common::ScopedLambda encoderCleanup([&encoder]()
	{
		// Encoder cleanup to handle memory leak in codec-adpcm library: codec-adpcm leaks avctx.extradata, so we manually free it
		encoder.end();
		adpcm_ffmpeg::av_free(encoder.ctx().extradata);
		encoder.ctx().extradata = nullptr;
	});

	encoder.setBlockSize(256);

	// Encode PCM samples to ADPCM
	VERIFY_SUCCESS(encoder.begin(pWaveformatex->nSamplesPerSec, pWaveformatex->nChannels));
	adpcm_ffmpeg::AVPacket& packet = encoder.encode(pcmSamples.data(), pcmSamples.size());

	// Allocate header and data for chunk (only store audio data, not WAV headers)
	auto [pHeader, dataSpan] = AllocateHeaderAndData(packet.size);

	// Populate WAVEFORMATEX with audio format information
	pHeader->audioHeader.waveFormat.wFormatTag = WAVE_FORMAT_ADPCM;
	pHeader->audioHeader.waveFormat.nChannels = pWaveformatex->nChannels;
	pHeader->audioHeader.waveFormat.nSamplesPerSec = pWaveformatex->nSamplesPerSec;
	// Mono: 1024
	pHeader->audioHeader.waveFormat.nBlockAlign = 1024;
	// pHeader->audioHeader.waveFormat.nBlockAlign = static_cast<WORD>(encoder.frameSize()); // ((256 * 2) / pWaveformatex->nChannels) - 12;
	// Mono: 16000
	pHeader->audioHeader.waveFormat.nAvgBytesPerSec = 16000;
	// pHeader->audioHeader.waveFormat.nAvgBytesPerSec = (pWaveformatex->nSamplesPerSec * encoder.blockAlign()) / encoder.frameSize();
	// pHeader->audioHeader.waveFormat.nAvgBytesPerSec = (pWaveformatex->nSamplesPerSec * pWaveformatex->nChannels) / 2;
	// pHeader->audioHeader.waveFormat.nAvgBytesPerSec = ((pWaveformatex->nSamplesPerSec / 256) * pHeader->audioHeader.waveFormat.nBlockAlign);
	pHeader->audioHeader.waveFormat.wBitsPerSample = 4; // 4 bits per sample for ADPCM
	pHeader->audioHeader.waveFormat.cbSize = 32; // Extra format bytes for ADPCM

	// Populate ADPCM-specific fields (Copy standard MS-ADPCM coefficients (defined in Microsoft ADPCM specification)
	// pHeader->audioHeader.uiSamplesPerBlock = ((256 * 2) / pWaveformatex->nChannels) - 12;
	pHeader->audioHeader.uiSamplesPerBlock = 256;
	// pHeader->audioHeader.uiSamplesPerBlock = (((pHeader->audioHeader.waveFormat.nBlockAlign - (7 * pWaveformatex->nChannels)) * 8) / (4 * pWaveformatex->nChannels)) + 2;
	ASSERT(pHeader->audioHeader.uiSamplesPerBlock == 256);

	static constexpr int16_t kAdpcmCoeff1[] = {256, 512, 0, 192, 240, 460, 392};
	static constexpr int16_t kAdpcmCoeff2[] = {0, -256, 0, 64, 0, -208, -232};
	pHeader->audioHeader.uiNumCoef = 7; // Standard MS-ADPCM coefficient count
	for (int64_t i = 0; i < 7; ++i)
	{
		pHeader->audioHeader.aCoeff[i].iCoef1 = kAdpcmCoeff1[i];
		pHeader->audioHeader.aCoeff[i].iCoef2 = kAdpcmCoeff2[i];
	}

	// Copy ADPCM compressed data
	std::memcpy(dataSpan.data(), packet.data, packet.size);
}
