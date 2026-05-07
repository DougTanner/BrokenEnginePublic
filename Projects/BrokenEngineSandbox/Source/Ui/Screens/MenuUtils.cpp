#include "MenuUtils.h"

namespace game
{

const char* AppendUtf8(common::Workbuffer& rWorkbuffer, std::u32string_view u32str)
{
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	for (char32_t c : u32str)
	{
		if (c < 0x80)
		{
			rWorkbuffer.PushBack<char>(static_cast<char>(c));
		}
		else if (c < 0x800)
		{
			rWorkbuffer.PushBack<char>(static_cast<char>(0xC0 | (c >> 6)));
			rWorkbuffer.PushBack<char>(static_cast<char>(0x80 | (c & 0x3F)));
		}
		else if (c < 0x10000)
		{
			rWorkbuffer.PushBack<char>(static_cast<char>(0xE0 | (c >> 12)));
			rWorkbuffer.PushBack<char>(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			rWorkbuffer.PushBack<char>(static_cast<char>(0x80 | (c & 0x3F)));
		}
		else
		{
			rWorkbuffer.PushBack<char>(static_cast<char>(0xF0 | (c >> 18)));
			rWorkbuffer.PushBack<char>(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
			rWorkbuffer.PushBack<char>(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			rWorkbuffer.PushBack<char>(static_cast<char>(0x80 | (c & 0x3F)));
		}
	}
	rWorkbuffer.PushBack<char>('\0');
	const char* pcResult = rWorkbuffer.View().data();
	return pcResult;
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
