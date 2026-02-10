#include "RawInputManager.h"

#include "Graphics/Graphics.h"

#include "Game.h"

namespace engine
{

RawInputManager::RawInputManager()
{
	gpRawInputManager = this;

	try
	{
		mpGamePad = std::make_unique<GamePad>();
	}
	catch ([[maybe_unused]] const std::exception& rException)
	{
		Log("Failed GamePad: {}", rException.what());
	}
	catch (...)
	{
		Log("Failed GamePad");
	}
}

RawInputManager::~RawInputManager()
{
	gpRawInputManager = nullptr;
}

void RawInputManager::UpdateFocus(bool bHasFocus, HWND hwnd)
{
	mbHasFocus = bHasFocus;

	RAWINPUTDEVICE pRawinputdevices[2] {};
	if (bHasFocus)
	{
		pRawinputdevices[0].usUsagePage = 0x01;
		pRawinputdevices[0].usUsage = 0x02;
		pRawinputdevices[0].dwFlags = RIDEV_INPUTSINK; // adds HID mouse and also ignores legacy mouse messages
		pRawinputdevices[0].hwndTarget = hwnd;

		pRawinputdevices[1].usUsagePage = 0x01;
		pRawinputdevices[1].usUsage = 0x06;
		pRawinputdevices[1].dwFlags = RIDEV_NOLEGACY; // adds HID keyboard and also ignores legacy keyboard messages
		pRawinputdevices[1].hwndTarget = nullptr;

		Log("RegisterRawInputDevices");
		if (RegisterRawInputDevices(pRawinputdevices, 2, sizeof(RAWINPUTDEVICE)) == FALSE)
		{
			Log("Failed to register raw input: {}", common::LastErrorString().data());
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

		Log("UnregisterRawInputDevices");
		if (RegisterRawInputDevices(pRawinputdevices, 2, sizeof(RAWINPUTDEVICE)) == FALSE)
		{
			Log("Failed to unregister raw input: {}", common::LastErrorString().data());
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
		// Keep cursor within window bounds when focused and while not in main menu
		// DT: GAMELOGIC
		TrapCursor(!game::gpGame->InMainMenu() && game::gpGame->ShouldUpdateFrame());
	}

	if (!mbHasFocus)
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
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonLeft] = mouseState.leftButton;
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonMiddle] = mouseState.middleButton;
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonRight] = mouseState.rightButton;
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonExtraOne] = mouseState.xButton1;
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonExtraTwo] = mouseState.xButton2;
	mRawInput.iScrollWheelValue = mouseState.scrollWheelValue;

	// Game pad (only first game pad supported)
	if (mpGamePad != nullptr)
	{
		GamePad::State gamepadState = mpGamePad->GetState(0);
		if (gamepadState.IsConnected())
		{
			if (!mbGamePadConnected)
			{
				GamePad::Capabilities gamepadCapabilities = mpGamePad->GetCapabilities(0);
				mbGamePadConnected = true;
				Log("Game pad connected: {}", static_cast<int64_t>(gamepadCapabilities.gamepadType));
			}

			mRawInput.f2LeftThumbstick.x = gamepadState.thumbSticks.leftX;
			mRawInput.f2LeftThumbstick.y = gamepadState.thumbSticks.leftY;
			mRawInput.f2RightThumbstick.x = gamepadState.thumbSticks.rightX;
			mRawInput.f2RightThumbstick.y = gamepadState.thumbSticks.rightY;

			mRawInput.f2Dpad.x = gamepadState.dpad.left ? -1.0f : (gamepadState.dpad.right ? 1.0f : 0.0f);
			mRawInput.f2Dpad.y = gamepadState.dpad.up ? 1.0f : (gamepadState.dpad.down ? -1.0f : 0.0f);

			mRawInput.f2Triggers.x = gamepadState.triggers.left;
			mRawInput.f2Triggers.y = gamepadState.triggers.right;

			mRawInput.pGamepadButtons[kGamepadButtonA] = gamepadState.IsAPressed();
			mRawInput.pGamepadButtons[kGamepadButtonB] = gamepadState.IsBPressed();
			mRawInput.pGamepadButtons[kGamepadButtonX] = gamepadState.IsXPressed();
			mRawInput.pGamepadButtons[kGamepadButtonY] = gamepadState.IsYPressed();
			mRawInput.pGamepadButtons[kGamepadLeftShoulder] = gamepadState.IsLeftShoulderPressed();
			mRawInput.pGamepadButtons[kGamepadRightShoulder] = gamepadState.IsRightShoulderPressed();
			mRawInput.pGamepadButtons[kGamepadStart] = gamepadState.IsStartPressed();
			mRawInput.pGamepadButtons[kGamepadMenu] = gamepadState.IsMenuPressed();
		}
		else
		{
			if (mbGamePadConnected)
			{
				mbGamePadConnected = false;
				Log("Game pad disconnected");
			}

			mRawInput.f2LeftThumbstick.x = 0.0f;
			mRawInput.f2LeftThumbstick.y = 0.0f;
			mRawInput.f2RightThumbstick.x = 0.0f;
			mRawInput.f2RightThumbstick.y = 0.0f;

			mRawInput.pGamepadButtons[kGamepadButtonA] = false;
			mRawInput.pGamepadButtons[kGamepadButtonB] = false;
			mRawInput.pGamepadButtons[kGamepadButtonX] = false;
			mRawInput.pGamepadButtons[kGamepadButtonY] = false;
			mRawInput.pGamepadButtons[kGamepadLeftShoulder] = false;
			mRawInput.pGamepadButtons[kGamepadRightShoulder] = false;
			mRawInput.pGamepadButtons[kGamepadStart] = false;
			mRawInput.pGamepadButtons[kGamepadMenu] = false;
		}
	}
}

void RawInputManager::HandleRawInput(LPARAM lparam)
{
	auto hrawinput = reinterpret_cast<HRAWINPUT>(lparam);

	UINT uiRawInputBytes = 0;
	GetRawInputData(hrawinput, RID_INPUT, nullptr, &uiRawInputBytes, sizeof(RAWINPUTHEADER));

	auto pRawinput = common::gpThreadLocal->mWorkbuffer.GetBuffer<RAWINPUT*>(uiRawInputBytes);
	if (GetRawInputData(hrawinput, RID_INPUT, pRawinput, &uiRawInputBytes, sizeof(RAWINPUTHEADER)) != uiRawInputBytes)
	{
		Log("GetRawInputData did not return correct size!");
		common::DebugBreak();
		common::gpThreadLocal->mWorkbuffer.Release();
		return;
	}

	if (pRawinput->header.dwType == RIM_TYPEKEYBOARD)
	{
		USHORT key = pRawinput->data.keyboard.VKey;
		if (key < kiKeyboardKeyCount)
		{
			mpbKeyboardKeysDown[key] = (pRawinput->data.keyboard.Flags & RI_KEY_BREAK) == 0;
		}
	}

	common::gpThreadLocal->mWorkbuffer.Release();
}

} // namespace engine
