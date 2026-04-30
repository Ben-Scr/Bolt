#pragma once
#include <imgui.h>
#include "Collections/Color.hpp"
#include "Collections/Vec2.hpp"
#include <magic_enum/magic_enum.hpp>
#include <algorithm>
#include <functional>
#include <string>
#include <type_traits>

namespace Bolt::ImGuiUtils {
	float GetInspectorLabelColumnWidth();
	void BeginInspectorFieldRow(const char* label);
	void DrawInspectorLabel(const char* label);

	template<typename Draw>
	bool DrawInspectorControl(const char* label, Draw&& draw)
	{
		ImGui::PushID(label);
		BeginInspectorFieldRow(label);
		const bool changed = draw("##Value");
		ImGui::PopID();
		return changed;
	}

	template<typename Draw>
	void DrawInspectorValue(const char* label, Draw&& draw)
	{
		ImGui::PushID(label);
		BeginInspectorFieldRow(label);
		draw("##Value");
		ImGui::PopID();
	}

	inline Color DrawColorPick4(const char* label, const Color& color)
	{
		float values[4] = { color.r, color.g, color.b, color.a };
		DrawInspectorValue(label, [&values](const char* id) {
			ImGui::ColorEdit4(id, values);
		});
		return Color(values[0], values[1], values[2], values[3]);
	}
	template<typename Draw>
	void DrawEnabled(bool enabled, Draw&& draw) {
				if (!enabled) {
			ImGui::BeginDisabled();
		}

		draw();

		if (!enabled) {
			ImGui::EndDisabled();
		}
	}

	template<typename TEnum, typename Setter>
	bool DrawEnumCombo(const char* label, TEnum currentValue, Setter&& setter)
	{
		static_assert(std::is_enum_v<TEnum>, "DrawEnumCombo requires an enum type.");

		auto previewView = magic_enum::enum_name(currentValue);
		const char* preview = previewView.empty() ? "Unknown" : previewView.data();

		bool changed = false;

		ImGui::PushID(label);
		BeginInspectorFieldRow(label);
		if (ImGui::BeginCombo("##Value", preview)) {
			for (TEnum value : magic_enum::enum_values<TEnum>()) {
				const bool isSelected = (currentValue == value);

				auto name = magic_enum::enum_name(value);
				if (name.empty()) {
					continue;
				}

				if (ImGui::Selectable(name.data(), isSelected)) {
					setter(value);
					changed = true;
				}

				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}
		ImGui::PopID();

		return changed;
	}

	template<typename TVec2>
	void DrawVec2ReadOnly(const char* id, const TVec2& vec2)
	{
		float values[2] = { static_cast<float>(vec2.x), static_cast<float>(vec2.y) };
		DrawInspectorValue(id, [&values](const char*) {
			const float componentWidth = std::max(45.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
			ImGui::BeginDisabled();
			ImGui::SetNextItemWidth(componentWidth);
			ImGui::InputFloat("##X", &values[0], 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(componentWidth);
			ImGui::InputFloat("##Y", &values[1], 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::EndDisabled();
		});
	}

	template<typename TVec2>
	bool DrawVec2(const char* id, TVec2& vec2)
	{
		float values[2] = { vec2.x, vec2.y };
		bool changed = false;
		DrawInspectorValue(id, [&values, &changed](const char*) {
			const float componentWidth = std::max(45.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
			ImGui::SetNextItemWidth(componentWidth);
			changed |= ImGui::InputFloat("##X", &values[0], 0.0f, 0.0f, "%.3f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(componentWidth);
			changed |= ImGui::InputFloat("##Y", &values[1], 0.0f, 0.0f, "%.3f");
		});

		if (changed) {
			vec2.x = values[0];
			vec2.y = values[1];
		}

		return changed;
	}

	void DrawTexturePreview(unsigned int rendererId, float texWidth, float texHeight, float previewSize = 96.0f);
	std::string Ellipsize(const std::string& text, float maxWidth, bool* outTruncated = nullptr);
	void TextEllipsis(const std::string& text, float maxWidth = -1.0f);
	void TextDisabledEllipsis(const std::string& text, float maxWidth = -1.0f);
	bool SelectableEllipsis(const std::string& text, const char* id, bool selected = false,
		ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0.0f, 0.0f), float maxWidth = -1.0f);
	bool MenuItemEllipsis(const std::string& text, const char* id,
		const char* shortcut = nullptr, bool selected = false, bool enabled = true, float maxWidth = -1.0f);

	bool BeginComponentSection(const char* label, bool& removeRequested, const std::function<void()>& contextMenu = {});

	void EndComponentSection();

}
