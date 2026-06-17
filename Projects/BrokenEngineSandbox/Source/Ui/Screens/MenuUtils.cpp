#include "MenuUtils.h"

namespace game
{

common::ScopedWorkbufferAllocation<char*> AppendUtf8(common::Workbuffer& rWorkbuffer, std::u32string_view u32String)
{
	// Reserve the worst case (4 UTF-8 bytes per code point + the null terminator), encode in a single pass, then
	// shrink to the actual length. The returned move-only handle owns the frame, so the bytes stay valid through
	// the caller's full-expression (the implicit const char* hands straight to the consuming ImGui call).
	common::ScopedWorkbufferAllocation<char*> scopedAllocation = rWorkbuffer.PushBuffer<char*>(static_cast<int64_t>(u32String.size()) * 4 + 1);
	char* const pcBase = scopedAllocation;
	char* pcWrite = pcBase;
	for (char32_t cCodePoint : u32String)
	{
		if (cCodePoint < 0x80)
		{
			*pcWrite++ = static_cast<char>(cCodePoint);
		}
		else if (cCodePoint < 0x800)
		{
			*pcWrite++ = static_cast<char>(0xC0 | (cCodePoint >> 6));
			*pcWrite++ = static_cast<char>(0x80 | (cCodePoint & 0x3F));
		}
		else if (cCodePoint < 0x10000)
		{
			*pcWrite++ = static_cast<char>(0xE0 | (cCodePoint >> 12));
			*pcWrite++ = static_cast<char>(0x80 | ((cCodePoint >> 6) & 0x3F));
			*pcWrite++ = static_cast<char>(0x80 | (cCodePoint & 0x3F));
		}
		else
		{
			*pcWrite++ = static_cast<char>(0xF0 | (cCodePoint >> 18));
			*pcWrite++ = static_cast<char>(0x80 | ((cCodePoint >> 12) & 0x3F));
			*pcWrite++ = static_cast<char>(0x80 | ((cCodePoint >> 6) & 0x3F));
			*pcWrite++ = static_cast<char>(0x80 | (cCodePoint & 0x3F));
		}
	}
	*pcWrite++ = '\0';
	rWorkbuffer.ShrinkLastPushBuffer(static_cast<int64_t>(pcWrite - pcBase));
	return scopedAllocation;
}

bool WrapperToggle(std::string_view label, engine::Wrapper* pWrapper)
{
	bool bValue = pWrapper->Get<bool>();
	if (ImGui::Checkbox(label.data(), &bValue))
	{
		pWrapper->Set(bValue);
		return true;
	}
	return false;
}

bool WrapperSlider(std::string_view label, engine::Wrapper* pWrapper)
{
	float fValue = pWrapper->Get();
	if (ImGui::SliderFloat(label.data(), &fValue, pWrapper->GetMin(), pWrapper->GetMax(), "%.2f"))
	{
		pWrapper->Set(fValue);
		return true;
	}
	return false;
}

bool WrapperPlusMinus(std::string_view label, engine::Wrapper* pWrapper, float fStep)
{
	bool bChanged = false;
	ImGui::Text("%s", label.data());
	ImGui::SameLine();
	ImGui::PushID(label.data());
	if (ImGui::Button("-"))
	{
		pWrapper->Set(pWrapper->Get() - fStep);
		bChanged = true;
	}
	ImGui::SameLine();
	ImGui::Text("%.0f%%", pWrapper->Get() * 100.0f);
	ImGui::SameLine();
	if (ImGui::Button("+"))
	{
		pWrapper->Set(pWrapper->Get() + fStep);
		bChanged = true;
	}
	ImGui::PopID();
	return bChanged;
}

} // namespace game
