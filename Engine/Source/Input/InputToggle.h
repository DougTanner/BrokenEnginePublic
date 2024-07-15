#pragma once

namespace engine
{

class InputToggle
{
public:

	bool UpdateToggle(bool bIsDown)
	{
		bool bWasDown = IsDown();

		mFlags.Set(ToggleFlags::kKeyDown, bIsDown);
		mFlags.Set(ToggleFlags::kKeyPressed, !bWasDown && bIsDown);
		mFlags.Set(ToggleFlags::kKeyReleased, bWasDown && !bIsDown);

		return bWasDown != bIsDown;
	}

	bool IsDown() const
	{
		return mFlags & ToggleFlags::kKeyDown;
	}

	bool WasPressed() const
	{
		return mFlags & ToggleFlags::kKeyPressed;
	}

	bool WasReleased() const
	{
		return mFlags & ToggleFlags::kKeyReleased;
	}

private:

	enum class ToggleFlags : uint64_t
	{
		kKeyDown     = 0x1,
		kKeyPressed  = 0x2,
		kKeyReleased = 0x4,
	};
	using ToggleFlags_t = common::Flags<ToggleFlags>;

	ToggleFlags_t mFlags {};
};

} // namespace engine
