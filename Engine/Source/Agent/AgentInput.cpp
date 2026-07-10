#include "Pch.h"

#if defined(BT_CLIENT)

#include "Agent/AgentInput.h"

#include "Agent/AgentUiRegistry.h"
#include "Input/RawInputManager.h"

namespace engine
{

namespace
{

constexpr int32_t kiWheelDelta = 120; // Win32 WHEEL_DELTA — one notch of the DirectXTK lifetime scroll accumulator

// Rect stability threshold: max per-corner pixel delta below which two consecutive frames count as settled.
constexpr float kfRectStablePixels = 0.5f;

float MaxCornerDelta(const XMFLOAT4& rRectA, const XMFLOAT4& rRectB)
{
	float fDeltaMinX = std::fabs(rRectA.x - rRectB.x);
	float fDeltaMinY = std::fabs(rRectA.y - rRectB.y);
	float fDeltaMaxX = std::fabs(rRectA.z - rRectB.z);
	float fDeltaMaxY = std::fabs(rRectA.w - rRectB.w);
	return std::max(std::max(fDeltaMinX, fDeltaMinY), std::max(fDeltaMaxX, fDeltaMaxY));
}

} // namespace

AgentInput::AgentInput()
{
	ASSERT(gpAgentInput == nullptr);
	gpAgentInput = this;
}

AgentInput::~AgentInput()
{
	if (gpAgentInput == this)
	{
		gpAgentInput = nullptr;
	}
}

bool AgentInput::BeginScript(const AgentScript& rScript)
{
	if (mbScriptActive)
	{
		return false;
	}

	mScript = rScript;
	mbScriptActive = true;
	meStatus = AgentScriptStatus::kPending;

	miPhase = 0;
	miPhaseFrame = 0;
	miElapsedFrames = 0;
	miStableCount = 0;
	mbHaveLastRect = false;
	mbResolvedDisabled = false;

	// Clear held synthetic key/button/pos state so nothing leaks from a prior script; the scroll accumulator is a
	// lifetime accumulator by design (consumers diff it), so it persists. The ImGui-pos pin is cleared here (not in
	// Finish) so it persists across a script's completion: describe_ui runs after Finish, so it must survive until the
	// next script re-seeds or clears it.
	std::fill(std::begin(mpbSyntheticKeys), std::end(mpbSyntheticKeys), false);
	muiSyntheticMouseButtons = 0;
	mbSyntheticMousePosValid = false;
	mbImGuiMousePosPinned = false;
	return true;
}

void AgentInput::Finish(AgentScriptStatus eStatus)
{
	meStatus = eStatus;
	mbScriptActive = false;

	// Release held synthetic keys/buttons so nothing sticks after the script ends (the overlay stops running once
	// inactive anyway, but keep members clean for the next script). Scroll accumulator persists by design.
	// Down/up sink asymmetry: this clears the overlay (game-side) synthetic mouse buttons, and the game overlay auto-
	// releases a bare `down` ~2 frames later, but the ImGui side stays logically held until a matching `up` event.
	// `click` is the composing primitive that pairs down+up on both sinks; a bare `down` without a later `up` leaves
	// ImGui's button state held.
	// Pos-sink asymmetry: the overlay (game-world) synthetic mouse pos clears here, but the ImGui-pos pin does NOT — it
	// deliberately persists until the next script (see BeginScript). Between scripts the game-world mouse reverts to the
	// physical cursor while the UI mouse stays pinned; the accepted asymmetry.
	std::fill(std::begin(mpbSyntheticKeys), std::end(mpbSyntheticKeys), false);
	muiSyntheticMouseButtons = 0;
	mbSyntheticMousePosValid = false;
}

void AgentInput::IssueImGuiMousePos(float fX, float fY)
{
	// Remember the pos + pin it so ImGuiManager::Prepare can re-issue it after the Win32 backend (last-writer-wins).
	mbImGuiMousePosPinned = true;
	mf2ImGuiPinnedPixels[0] = fX;
	mf2ImGuiPinnedPixels[1] = fY;
	if (ImGui::GetCurrentContext() != nullptr)
	{
		ImGui::GetIO().AddMousePosEvent(fX, fY);
	}
}

void AgentInput::ReissueImGuiMousePos()
{
	if (mbImGuiMousePosPinned && ImGui::GetCurrentContext() != nullptr)
	{
		ImGui::GetIO().AddMousePosEvent(mf2ImGuiPinnedPixels[0], mf2ImGuiPinnedPixels[1]);
	}
}

bool AgentInput::StabilizeTarget()
{
	if (gpAgentUiRegistry == nullptr)
	{
		Finish(AgentScriptStatus::kNotFound);
		return false;
	}

	int64_t iIndex = gpAgentUiRegistry->ResolveLabel(mScript.pcLabel, mScript.bHasWindow ? mScript.pcWindow : nullptr);
	if (iIndex == AgentUiRegistry::kNotFound)
	{
		Finish(AgentScriptStatus::kNotFound);
		return false;
	}
	if (iIndex == AgentUiRegistry::kAmbiguous)
	{
		Finish(AgentScriptStatus::kAmbiguous);
		return false;
	}

	const AgentUiItem& rItem = gpAgentUiRegistry->Item(iIndex);
	XMFLOAT4 f4Rect = rItem.f4Rect;
	mf2TargetCenter[0] = 0.5f * (f4Rect.x + f4Rect.z);
	mf2TargetCenter[1] = 0.5f * (f4Rect.y + f4Rect.w);
	IssueImGuiMousePos(mf2TargetCenter[0], mf2TargetCenter[1]);

	if (mbHaveLastRect && MaxCornerDelta(f4Rect, mf4LastRect) < kfRectStablePixels)
	{
		++miStableCount;
	}
	else
	{
		miStableCount = 0;
	}
	mf4LastRect = f4Rect;
	mbHaveLastRect = true;

	if (miElapsedFrames >= mScript.iTimeoutFrames)
	{
		Finish(AgentScriptStatus::kTimeout);
		return false;
	}

	if (miStableCount >= 2)
	{
		mbResolvedDisabled = rItem.bDisabled;
		miResolvedStatusFlags = rItem.iStatusFlags;
		// ImGuiItemStatusFlags_Visible is set only inside ItemAdd when the rect overlaps the clip rect (imgui.cpp:11292).
		// Checkbox/menu-item-class widgets emit ITEM_INFO on their clipped early-return too (e.g. Checkbox imgui_widgets.cpp:1247)
		// with StatusFlags lacking Visible — that is how a clipped item is label-resolvable yet reads not-visible. Widgets that
		// emit no clipped-path ITEM_INFO (sliders/drags/buttons/InvisibleButton) never gain a registry label when clipped and
		// resolve as kNotFound instead — except a nav-focused/active clipped widget, which bypasses ItemAdd's clip early-return
		// (imgui.cpp:11251) and still lands here without the Visible bit. This one site covers kClick, kHover, kSetSlider.
		if ((miResolvedStatusFlags & ImGuiItemStatusFlags_Visible) == 0)
		{
			Finish(AgentScriptStatus::kClipped);
			return false;
		}
		return true;
	}
	return false;
}

void AgentInput::AdvanceFrame()
{
	if (!mbScriptActive)
	{
		return;
	}

	++miElapsedFrames;

	ImGuiIO* pIo = (ImGui::GetCurrentContext() != nullptr) ? &ImGui::GetIO() : nullptr;

	switch (mScript.eKind)
	{
		case AgentScriptKind::kClick:
		{
			if (miPhase == 0)
			{
				if (!StabilizeTarget())
				{
					return;
				}
				miPhase = 1;
				miPhaseFrame = 0;
				return;
			}
			IssueImGuiMousePos(mf2TargetCenter[0], mf2TargetCenter[1]);
			if (miPhase == 1)
			{
				if (pIo != nullptr)
				{
					pIo->AddMouseButtonEvent(mScript.iImGuiMouseButton, true);
				}
				miPhase = 2;
				return;
			}
			if (miPhase == 2)
			{
				if (pIo != nullptr)
				{
					pIo->AddMouseButtonEvent(mScript.iImGuiMouseButton, false);
				}
				miPhase = 3;
				miPhaseFrame = 0;
				return;
			}
			if (++miPhaseFrame >= 2) // settle
			{
				Finish(AgentScriptStatus::kDone);
			}
			return;
		}

		case AgentScriptKind::kHover:
		{
			if (miPhase == 0)
			{
				if (!StabilizeTarget())
				{
					return;
				}
				miPhase = 1;
				miPhaseFrame = 0;
				return;
			}
			IssueImGuiMousePos(mf2TargetCenter[0], mf2TargetCenter[1]);
			if (++miPhaseFrame >= mScript.iHoldFrames)
			{
				Finish(AgentScriptStatus::kDone);
			}
			return;
		}

		case AgentScriptKind::kSetSlider:
		{
			if (miPhase == 0)
			{
				if (!StabilizeTarget())
				{
					return;
				}
				// Only inputable widgets (sliders / drags) open ImGui's Ctrl+click temp-input. Ctrl+clicking a non-
				// inputable target (e.g. a Button) would instead fire its real activation — pre-validate and bail.
				if ((miResolvedStatusFlags & ImGuiItemStatusFlags_Inputable) == 0)
				{
					Finish(AgentScriptStatus::kNotInputable);
					return;
				}
				miPhase = 1;
				miPhaseFrame = 0;
				return;
			}
			IssueImGuiMousePos(mf2TargetCenter[0], mf2TargetCenter[1]);
			// Ctrl+Click opens ImGui's temp text-input on the slider (value pre-selected); type the value; Enter commits.
			if (miPhase == 1)
			{
				if (pIo != nullptr)
				{
					pIo->AddKeyEvent(ImGuiMod_Ctrl, true);
					pIo->AddMouseButtonEvent(0, true);
				}
				miPhase = 2;
				return;
			}
			if (miPhase == 2)
			{
				if (pIo != nullptr)
				{
					pIo->AddMouseButtonEvent(0, false);
					// Release Ctrl with the mouse-up: held into phase 3 it trips InputText's ignore_char_inputs, dropping typed chars.
					pIo->AddKeyEvent(ImGuiMod_Ctrl, false);
				}
				miPhase = 3;
				return;
			}
			if (miPhase == 3)
			{
				if (pIo != nullptr)
				{
					for (int64_t i = 0; mScript.pcValueText[i] != '\0'; ++i)
					{
						pIo->AddInputCharacter(static_cast<unsigned int>(static_cast<unsigned char>(mScript.pcValueText[i])));
					}
				}
				miPhase = 4;
				return;
			}
			if (miPhase == 4)
			{
				if (pIo != nullptr)
				{
					pIo->AddKeyEvent(ImGuiKey_Enter, true);
				}
				miPhase = 5;
				return;
			}
			if (miPhase == 5)
			{
				if (pIo != nullptr)
				{
					pIo->AddKeyEvent(ImGuiKey_Enter, false);
				}
				miPhase = 6;
				miPhaseFrame = 0;
				return;
			}
			if (++miPhaseFrame >= 2) // settle
			{
				Finish(AgentScriptStatus::kDone);
			}
			return;
		}

		case AgentScriptKind::kKey:
		{
			// Overlay sink only: hold the synthetic VK down for iHoldFrames, then release, driving KeyboardPressed edges.
			if (miPhase == 0)
			{
				if (mScript.iKeyVk > 0 && mScript.iKeyVk < static_cast<int32_t>(std::size(mpbSyntheticKeys)))
				{
					mpbSyntheticKeys[mScript.iKeyVk] = true;
				}
				miPhase = 1;
				miPhaseFrame = 0;
				return;
			}
			if (miPhase == 1)
			{
				if (++miPhaseFrame >= mScript.iHoldFrames)
				{
					if (mScript.iKeyVk > 0 && mScript.iKeyVk < static_cast<int32_t>(std::size(mpbSyntheticKeys)))
					{
						mpbSyntheticKeys[mScript.iKeyVk] = false;
					}
					miPhase = 2;
				}
				return;
			}
			Finish(AgentScriptStatus::kDone); // release edge published in phase 1's final frame while still active
			return;
		}

		case AgentScriptKind::kMouse:
		{
			// Raw pixel coords feed both sinks (overlay for game world clicks, ImGui IO for UI). Pos re-pinned each frame.
			if (mScript.bHasCoord)
			{
				mbSyntheticMousePosValid = true;
				mf2SyntheticMousePixels[0] = mScript.f2CoordPixels[0];
				mf2SyntheticMousePixels[1] = mScript.f2CoordPixels[1];
				IssueImGuiMousePos(mScript.f2CoordPixels[0], mScript.f2CoordPixels[1]);
			}

			switch (mScript.eMouseAction)
			{
				case AgentMouseAction::kMove:
				{
					if (++miPhaseFrame >= 2)
					{
						Finish(AgentScriptStatus::kDone);
					}
					return;
				}
				case AgentMouseAction::kWheel:
				{
					if (miPhase == 0)
					{
						miSyntheticScrollAccumulator += mScript.iWheelNotches * kiWheelDelta;
						if (pIo != nullptr)
						{
							pIo->AddMouseWheelEvent(0.0f, static_cast<float>(mScript.iWheelNotches));
						}
						miPhase = 1;
					}
					if (++miPhaseFrame >= 2)
					{
						Finish(AgentScriptStatus::kDone);
					}
					return;
				}
				case AgentMouseAction::kDown:
				{
					if (miPhase == 0)
					{
						muiSyntheticMouseButtons |= mScript.uiOverlayMouseButtonBit;
						if (pIo != nullptr)
						{
							pIo->AddMouseButtonEvent(mScript.iImGuiMouseButton, true);
						}
						miPhase = 1;
					}
					if (++miPhaseFrame >= 2)
					{
						Finish(AgentScriptStatus::kDone);
					}
					return;
				}
				case AgentMouseAction::kUp:
				{
					if (miPhase == 0)
					{
						muiSyntheticMouseButtons &= ~mScript.uiOverlayMouseButtonBit;
						if (pIo != nullptr)
						{
							pIo->AddMouseButtonEvent(mScript.iImGuiMouseButton, false);
						}
						miPhase = 1;
					}
					if (++miPhaseFrame >= 2)
					{
						Finish(AgentScriptStatus::kDone);
					}
					return;
				}
				case AgentMouseAction::kClick:
				{
					if (miPhase == 0)
					{
						muiSyntheticMouseButtons |= mScript.uiOverlayMouseButtonBit;
						if (pIo != nullptr)
						{
							pIo->AddMouseButtonEvent(mScript.iImGuiMouseButton, true);
						}
						miPhase = 1;
						miPhaseFrame = 0;
						return;
					}
					if (miPhase == 1)
					{
						muiSyntheticMouseButtons &= ~mScript.uiOverlayMouseButtonBit;
						if (pIo != nullptr)
						{
							pIo->AddMouseButtonEvent(mScript.iImGuiMouseButton, false);
						}
						miPhase = 2;
						miPhaseFrame = 0;
						return;
					}
					if (++miPhaseFrame >= 2)
					{
						Finish(AgentScriptStatus::kDone);
					}
					return;
				}
			}
			return;
		}
	}
}

void AgentInput::Overlay(RawInput& rRawInput)
{
	// OR synthetic keyboard bits onto the published snapshot (never clears real bits).
	for (int64_t i = 0; i < kiKeyboardKeyCount; ++i)
	{
		if (mpbSyntheticKeys[i])
		{
			rRawInput.pKeyboardKeys[i] = true;
		}
	}

	// OR synthetic mouse buttons.
	if (muiSyntheticMouseButtons & kMouseButtonLeft)
	{
		rRawInput.mouseButtons.Set(kMouseButtonLeft, true);
	}
	if (muiSyntheticMouseButtons & kMouseButtonMiddle)
	{
		rRawInput.mouseButtons.Set(kMouseButtonMiddle, true);
	}
	if (muiSyntheticMouseButtons & kMouseButtonRight)
	{
		rRawInput.mouseButtons.Set(kMouseButtonRight, true);
	}
	if (muiSyntheticMouseButtons & kMouseButtonExtraOne)
	{
		rRawInput.mouseButtons.Set(kMouseButtonExtraOne, true);
	}
	if (muiSyntheticMouseButtons & kMouseButtonExtraTwo)
	{
		rRawInput.mouseButtons.Set(kMouseButtonExtraTwo, true);
	}

	// Overwrite mouse position with the synthetic pixel pos (normalized as the hardware path does).
	if (mbSyntheticMousePosValid)
	{
		rRawInput.f2MousePosition.x = mf2SyntheticMousePixels[0] / static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
		rRawInput.f2MousePosition.y = mf2SyntheticMousePixels[1] / static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	}

	// NOTE: the synthetic scroll accumulator is deliberately NOT added here. iScrollWheelValue is a lifetime
	// accumulator that consumers diff, so its synthetic offset is folded in on every publish in RawInputManager::
	// Update (script active or not) — adding it here too would double-count it while a script runs.
}

} // namespace engine

#endif // defined(BT_CLIENT)
