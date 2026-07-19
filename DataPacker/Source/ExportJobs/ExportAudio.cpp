#include "ExportAudio.h"

#include "AudioRepair.h"

std::optional<common::ChunkFlags_t> ExportAudio::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	return rDirectoryEntry.path().extension() == ".wav" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kChunkAudio) : std::nullopt;
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
	// File data — DirectXTK doesn't validate nBlockAlign for PCM/float; zero or inconsistent breaks the trim math below
	ASSERT(pWaveformatex->nBlockAlign != 0 && pWaveformatex->nBlockAlign == pWaveformatex->nChannels * pWaveformatex->wBitsPerSample / 8);

	// Truncated source data: trim the partial frame so conversion below sees whole frames only
	uint32_t uiTrimmedBytes = uiAudioBytes % pWaveformatex->nBlockAlign;
	if (uiTrimmedBytes != 0)
	{
		uiAudioBytes -= uiTrimmedBytes;
		LOG(kDefault, kWarning, "{}: trimmed {} partial-frame bytes", mRelativeFile, uiTrimmedBytes);
	}

	// Decode to one interleaved float working buffer; all analysis/repair runs in float
	std::vector<float> fSamples;
	if (pWaveformatex->wFormatTag == WAVE_FORMAT_PCM && pWaveformatex->wBitsPerSample == 16)
	{
		size_t uiSampleCount = uiAudioBytes / sizeof(int16_t);
		fSamples.resize(uiSampleCount);
		const int16_t* piSamples = reinterpret_cast<const int16_t*>(pAudioData);
		for (size_t i = 0; i < uiSampleCount; ++i)
		{
			// Divisor matches the output multiplier so a defect-free 16-bit file round-trips bit-exactly
			fSamples[i] = static_cast<float>(piSamples[i]) / 32767.0f;
		}
	}
	else if (pWaveformatex->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && pWaveformatex->wBitsPerSample == 32)
	{
		size_t uiSampleCount = uiAudioBytes / sizeof(float);
		fSamples.resize(uiSampleCount);
		std::memcpy(fSamples.data(), pAudioData, uiAudioBytes);
	}
	else
	{
		ASSERT(false);
	}

	// _loop assets loop the whole buffer at runtime (XAUDIO2_LOOP_INFINITE): never edge-fade,
	// validate the seam instead. Music/ assets are loudness-mastered: safe fixes only (see AudioRepair.h)
	bool bLoop = common::ToLower(mInputPath.stem().string()).ends_with("_loop");
	bool bMusic = false;
	for (const std::filesystem::path& rComponent : mRelativeDirectory)
	{
		if (common::ToLower(rComponent.string()) == "music")
		{
			bMusic = true;
			break;
		}
	}
	bool bAllowDeclip = audiorepair::kbDeclipEnabled && (!bMusic || audiorepair::kbDeclipMusic);
	audiorepair::RepairAudio(fSamples, pWaveformatex->nChannels, pWaveformatex->nSamplesPerSec, mRelativeFile, bLoop, bAllowDeclip, !bMusic);

	// Resample to the pack-time rate so source rate == mastering rate at runtime (XAudio2 SRC bypass).
	// Runs after RepairAudio (per plan): repair analysis/fades ran at the source rate and their effect
	// scales naturally through resampling; loop assets resample whole-buffer (seam no longer sample-exact).
	audiorepair::Resample(fSamples, pWaveformatex->nChannels, pWaveformatex->nSamplesPerSec, audiorepair::kiAudioExportSampleRate, mRelativeFile);

	// Single conversion to int16 — the only clamp in the pipeline. lround (not truncation) is
	// required for the bit-exact 16-bit round-trip; clamp floor -32768 so a full-scale negative
	// source sample survives
	std::vector<int16_t> pcmSamples(fSamples.size());
	for (size_t i = 0; i < fSamples.size(); ++i)
	{
		pcmSamples[i] = static_cast<int16_t>(std::clamp(std::lround(fSamples[i] * 32767.0f), -32768L, 32767L));
	}

	// Allocate header and data for chunk (only store audio data, not WAV headers)
	int64_t iPcmDataSize = pcmSamples.size() * sizeof(int16_t);
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iPcmDataSize);

	// Populate WAVEFORMATEX with PCM audio format information
	pHeader->audioHeader.waveFormat.wFormatTag = WAVE_FORMAT_PCM;
	pHeader->audioHeader.waveFormat.nChannels = pWaveformatex->nChannels;
	pHeader->audioHeader.waveFormat.nSamplesPerSec = static_cast<DWORD>(audiorepair::kiAudioExportSampleRate); // resampled above
	pHeader->audioHeader.waveFormat.nBlockAlign = pWaveformatex->nChannels * sizeof(int16_t);
	pHeader->audioHeader.waveFormat.nAvgBytesPerSec = static_cast<DWORD>(audiorepair::kiAudioExportSampleRate) * pWaveformatex->nChannels * sizeof(int16_t);
	pHeader->audioHeader.waveFormat.wBitsPerSample = 16;
	pHeader->audioHeader.waveFormat.cbSize = 0;

	// Copy PCM data directly
	std::memcpy(dataSpan.data(), pcmSamples.data(), iPcmDataSize);
}
