#pragma once

#include "Frame/Pools/ObjectPool.h"

namespace game
{

struct Frame;

}

namespace engine
{

struct SoundInfo
{
	common::crc_t uiCrc = 0;
	float fVolume = 0.0f;
	float fPitch = 1.0f;
	float fFadeOutTime = 0.0f;
	DirectX::XMVECTOR vecPosition {};
	DirectX::XMVECTOR vecVelocity {};

	bool operator==(const SoundInfo& rOther) const = default;
};
struct Sound
{
	int64_t iId = 0;

	bool operator==(const Sound& rOther) const = default;
};
struct Sounds : public ObjectPool<SoundInfo, Sound, sound_t, kuiMaxSounds>
{
	int64_t iNextSoundId = 1;

	static void Copy(Sounds& __restrict rCurrent, const Sounds& __restrict rPrevious)
	{
		rCurrent.iNextSoundId = rPrevious.iNextSoundId;

		ObjectPool::Copy(rCurrent, rPrevious);
	}

	void Add(sound_t& ruiIndex, const SoundInfo& rSoundInfo)
	{
		bool bNew = ruiIndex == 0;

		ObjectPool::Add(ruiIndex, rSoundInfo);

		if (bNew && ruiIndex != 0) [[unlikely]]
		{
			Sound& rSound = Get(ruiIndex);
			rSound.iId = iNextSoundId++;
		}
	}
};
static_assert(std::is_trivially_copyable_v<Sounds>);

inline constexpr int64_t kiSoundsVersion = 1 + sizeof(Sounds);

}
