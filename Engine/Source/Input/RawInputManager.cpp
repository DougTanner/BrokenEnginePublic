#include "RawInputManager.h"

#if defined(BT_CLIENT)

#include "Game.h"

namespace engine
{

RawInputManager::RawInputManager()
{
	ASSERT(gpRawInputManager == nullptr);

	gpRawInputManager = this;

	try
	{
		mpGamePad = std::make_unique<GamePad>();
	}
	catch ([[maybe_unused]] const std::exception& rException)
	{
		LOG(kInput, kError, "Failed GamePad: {}", rException.what());
	}
	catch (...)
	{
		LOG(kInput, kError, "Failed GamePad");
	}
}

RawInputManager::~RawInputManager()
{
	if (gpRawInputManager == this)
	{
		gpRawInputManager = nullptr;
	}
}

void RawInputManager::UpdateFocus(bool bHasFocus, HWND hwnd)
{
	mStateFlags.Set(RawInputStateFlags::kHasFocus, bHasFocus);
	mHwnd = hwnd;

	RAWINPUTDEVICE pRawinputdevices[2] {};
	if (bHasFocus)
	{
		pRawinputdevices[0].usUsagePage = 0x01;
		pRawinputdevices[0].usUsage = 0x02;
		pRawinputdevices[0].dwFlags = RIDEV_INPUTSINK; // adds HID mouse; legacy mouse messages still arrive (they feed DirectXTK Mouse)
		pRawinputdevices[0].hwndTarget = hwnd;

		pRawinputdevices[1].usUsagePage = 0x01;
		pRawinputdevices[1].usUsage = 0x06;
		pRawinputdevices[1].dwFlags = RIDEV_NOLEGACY; // adds HID keyboard and also ignores legacy keyboard messages
		pRawinputdevices[1].hwndTarget = nullptr;

		LOG(kInput, kInfo, "RegisterRawInputDevices");
		if (RegisterRawInputDevices(pRawinputdevices, 2, sizeof(RAWINPUTDEVICE)) == FALSE)
		{
			LOG(kInput, kError, "Failed to register raw input: {}", common::LastErrorString().data());
		}

		if (mpGamePad != nullptr)
		{
			mpGamePad->Resume();
		}

		std::fill(std::begin(mpbKeyboardKeysDown), std::end(mpbKeyboardKeysDown), false);
	}
	else
	{
		TrapCursor(false);

		pRawinputdevices[0].usUsagePage = 0x01;
		pRawinputdevices[0].usUsage = 0x02;
		pRawinputdevices[0].dwFlags = RIDEV_REMOVE;
		pRawinputdevices[0].hwndTarget = nullptr;

		pRawinputdevices[1].usUsagePage = 0x01;
		pRawinputdevices[1].usUsage = 0x06;
		pRawinputdevices[1].dwFlags = RIDEV_REMOVE;
		pRawinputdevices[1].hwndTarget = nullptr;

		LOG(kInput, kInfo, "UnregisterRawInputDevices");
		if (RegisterRawInputDevices(pRawinputdevices, 2, sizeof(RAWINPUTDEVICE)) == FALSE)
		{
			LOG(kInput, kError, "Failed to unregister raw input: {}", common::LastErrorString().data());
		}

		if (mpGamePad != nullptr)
		{
			mpGamePad->Suspend();
		}
	}
}

bool RawInputManager::SetVibration(int64_t iPlayer, float fLeftMotor, float fRightMotor, float fLeftTrigger, float fRightTrigger)
{
	if (mpGamePad != nullptr)
	{
		return mpGamePad->SetVibration(static_cast<int>(iPlayer), fLeftMotor, fRightMotor, fLeftTrigger, fRightTrigger);
	}

	return false;
}

void RawInputManager::TrapCursor(bool bTrap)
{
	if (bTrap)
	{
		RECT rect {};
		GetClientRect(mHwnd, &rect);
		POINT pointUpperLeft {};
		pointUpperLeft.x = rect.left;
		pointUpperLeft.y = rect.top;
		POINT pointLowerRight {};
		pointLowerRight.x = rect.right;
		pointLowerRight.y = rect.bottom;
		MapWindowPoints(mHwnd, nullptr, &pointUpperLeft, 1);
		MapWindowPoints(mHwnd, nullptr, &pointLowerRight, 1);
		rect.left = pointUpperLeft.x;
		rect.top = pointUpperLeft.y;
		rect.right = pointLowerRight.x;
		rect.bottom = pointLowerRight.y;
		ClipCursor(&rect);
	}
	else
	{
		ClipCursor(nullptr);
	}
}

void RawInputManager::Update(bool bLostFocus)
{
	if (bLostFocus) [[unlikely]]
	{
		TrapCursor(false);
	}
	else
	{
		TrapCursor(game::gpGame->ShouldTrapCursor());
	}

	if (!(mStateFlags & RawInputStateFlags::kHasFocus))
	{
		return;
	}

	// Keyboard
	for (int64_t i = 0; i < kiKeyboardKeyCount; ++i)
	{
		mRawInput.pKeyboardKeys[i] = mpbKeyboardKeysDown[i];
	}

	// Mouse
	Mouse::State mouseState = mMouse.GetState();
	mRawInput.f2MousePosition.x = static_cast<float>(mouseState.x) / static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	mRawInput.f2MousePosition.y = static_cast<float>(mouseState.y) / static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonLeft, mouseState.leftButton);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonMiddle, mouseState.middleButton);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonRight, mouseState.rightButton);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonExtraOne, mouseState.xButton1);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonExtraTwo, mouseState.xButton2);
	mRawInput.iScrollWheelValue = mouseState.scrollWheelValue;

	// Game pad (only first game pad supported)
	if (mpGamePad != nullptr)
	{
		GamePad::State gamepadState = mpGamePad->GetState(0);
		if (gamepadState.IsConnected())
		{
			if (!(mStateFlags & RawInputStateFlags::kGamePadConnected))
			{
				mStateFlags.Set(RawInputStateFlags::kGamePadConnected);
				LOG(kInput, kInfo, "Game pad connected: {}", static_cast<int64_t>(mpGamePad->GetCapabilities(0).gamepadType));
			}

			mRawInput.f2LeftThumbstick.x = gamepadState.thumbSticks.leftX;
			mRawInput.f2LeftThumbstick.y = gamepadState.thumbSticks.leftY;
			mRawInput.f2RightThumbstick.x = gamepadState.thumbSticks.rightX;
			mRawInput.f2RightThumbstick.y = gamepadState.thumbSticks.rightY;

			mRawInput.f2Dpad.x = gamepadState.dpad.left ? -1.0f : (gamepadState.dpad.right ? 1.0f : 0.0f);
			mRawInput.f2Dpad.y = gamepadState.dpad.up ? 1.0f : (gamepadState.dpad.down ? -1.0f : 0.0f);

			mRawInput.gamepadButtons.Set(kGamepadButtonA, gamepadState.IsAPressed());
			mRawInput.gamepadButtons.Set(kGamepadButtonB, gamepadState.IsBPressed());
			mRawInput.gamepadButtons.Set(kGamepadButtonX, gamepadState.IsXPressed());
			mRawInput.gamepadButtons.Set(kGamepadButtonY, gamepadState.IsYPressed());
			mRawInput.gamepadButtons.Set(kGamepadLeftShoulder, gamepadState.IsLeftShoulderPressed());
			mRawInput.gamepadButtons.Set(kGamepadRightShoulder, gamepadState.IsRightShoulderPressed());
			mRawInput.gamepadButtons.Set(kGamepadStart, gamepadState.IsStartPressed());
			mRawInput.gamepadButtons.Set(kGamepadMenu, gamepadState.IsMenuPressed());
		}
		else
		{
			if (mStateFlags & RawInputStateFlags::kGamePadConnected)
			{
				mStateFlags.Clear(RawInputStateFlags::kGamePadConnected);
				LOG(kInput, kInfo, "Game pad disconnected");
			}

			mRawInput.f2LeftThumbstick.x = 0.0f;
			mRawInput.f2LeftThumbstick.y = 0.0f;
			mRawInput.f2RightThumbstick.x = 0.0f;
			mRawInput.f2RightThumbstick.y = 0.0f;

			mRawInput.f2Dpad.x = 0.0f;
			mRawInput.f2Dpad.y = 0.0f;

			mRawInput.gamepadButtons = {};
		}
	}
}

void RawInputManager::HandleRawInput(LPARAM lparam)
{
	HRAWINPUT hrawinput = reinterpret_cast<HRAWINPUT>(lparam);

	// Only fixed-size mouse/keyboard usages are registered (UpdateFocus), never variable-length RAWHID, so sizeof(RAWINPUT) bounds every packet
	RAWINPUT rawinput {};
	UINT uiRawInputBytes = sizeof(rawinput);
	if (GetRawInputData(hrawinput, RID_INPUT, &rawinput, &uiRawInputBytes, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
	{
		LOG(kInput, kWarning, "GetRawInputData failed: {}", common::LastErrorString().data());
		DEBUG_BREAK();
		return;
	}

	if (rawinput.header.dwType == RIM_TYPEKEYBOARD)
	{
		USHORT uiKey = rawinput.data.keyboard.VKey;
		if (uiKey < kiKeyboardKeyCount)
		{
			mpbKeyboardKeysDown[uiKey] = (rawinput.data.keyboard.Flags & RI_KEY_BREAK) == 0;
		}
	}
}

} // namespace engine

#endif
