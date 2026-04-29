#include <pch.hpp>
#include "Gui/ImGuiUtils.hpp"
#include <imgui.h>

namespace Bolt::ImGuiUtils {
	float GetInspectorLabelColumnWidth()
	{
		return 160.0f;
	}

	void DrawInspectorLabel(const char* label)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float labelColumnWidth = GetInspectorLabelColumnWidth();
		const float availableLabelWidth = std::max(1.0f, labelColumnWidth - style.ItemSpacing.x);

		ImGui::AlignTextToFramePadding();

		bool truncated = false;
		const std::string clippedLabel = Ellipsize(label ? label : "", availableLabelWidth, &truncated);
		ImGui::TextUnformatted(clippedLabel.c_str());
		if (truncated && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", label);
		}
	}

	void BeginInspectorFieldRow(const char* label)
	{
		DrawInspectorLabel(label);
		ImGui::SameLine(GetInspectorLabelColumnWidth());
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	}

	std::string Ellipsize(const std::string& text, float maxWidth, bool* outTruncated)
	{
		if (outTruncated) {
			*outTruncated = false;
		}

		if (text.empty() || maxWidth <= 0.0f) {
			return text;
		}

		if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
			return text;
		}

		constexpr const char* ellipsis = "...";
		const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
		if (ellipsisWidth >= maxWidth) {
			if (outTruncated) {
				*outTruncated = true;
			}
			return ellipsis;
		}

		const float availableWidth = maxWidth - ellipsisWidth;
		int low = 0;
		int high = static_cast<int>(text.size());
		int bestFit = 0;

		while (low <= high) {
			const int mid = low + ((high - low) / 2);
			const float currentWidth = ImGui::CalcTextSize(text.c_str(), text.c_str() + mid).x;
			if (currentWidth <= availableWidth) {
				bestFit = mid;
				low = mid + 1;
			}
			else {
				high = mid - 1;
			}
		}

		if (outTruncated) {
			*outTruncated = true;
		}

		return text.substr(0, static_cast<std::size_t>(bestFit)) + ellipsis;
	}

	void TextEllipsis(const std::string& text, float maxWidth)
	{
		if (maxWidth < 0.0f) {
			maxWidth = ImGui::GetContentRegionAvail().x;
		}

		bool truncated = false;
		const std::string displayText = Ellipsize(text, maxWidth, &truncated);
		ImGui::TextUnformatted(displayText.c_str());
		if (truncated && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", text.c_str());
		}
	}

	void TextDisabledEllipsis(const std::string& text, float maxWidth)
	{
		if (maxWidth < 0.0f) {
			maxWidth = ImGui::GetContentRegionAvail().x;
		}

		bool truncated = false;
		const std::string displayText = Ellipsize(text, maxWidth, &truncated);
		ImGui::TextDisabled("%s", displayText.c_str());
		if (truncated && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", text.c_str());
		}
	}

	bool SelectableEllipsis(const std::string& text, const char* id, bool selected,
		ImGuiSelectableFlags flags, const ImVec2& size, float maxWidth)
	{
		if (maxWidth < 0.0f) {
			maxWidth = size.x > 0.0f ? size.x : ImGui::GetContentRegionAvail().x;
		}

		bool truncated = false;
		const std::string displayText = Ellipsize(text, maxWidth, &truncated);
		const std::string label = displayText + "##" + (id ? std::string(id) : text);
		const bool activated = ImGui::Selectable(label.c_str(), selected, flags, size);
		if (truncated && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", text.c_str());
		}
		return activated;
	}

	bool MenuItemEllipsis(const std::string& text, const char* id,
		const char* shortcut, bool selected, bool enabled, float maxWidth)
	{
		if (maxWidth < 0.0f) {
			maxWidth = ImGui::GetContentRegionAvail().x;
		}

		bool truncated = false;
		const std::string displayText = Ellipsize(text, maxWidth, &truncated);
		const std::string label = displayText + "##" + (id ? std::string(id) : text);
		const bool activated = ImGui::MenuItem(label.c_str(), shortcut, selected, enabled);
		if (truncated && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", text.c_str());
		}
		return activated;
	}

	void DrawTexturePreview(unsigned int rendererId, float texWidth, float texHeight, float previewSize)
	{
		const ImVec2 previewMin = ImGui::GetCursorScreenPos();
		const ImVec2 previewMax = ImVec2(previewMin.x + previewSize, previewMin.y + previewSize);

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		drawList->AddRectFilled(previewMin, previewMax, IM_COL32(35, 35, 35, 255), 6.0f);

		const float checkerSize = 8.0f;
		for (float y = previewMin.y; y < previewMax.y; y += checkerSize) {
			for (float x = previewMin.x; x < previewMax.x; x += checkerSize) {
				const int ix = static_cast<int>((x - previewMin.x) / checkerSize);
				const int iy = static_cast<int>((y - previewMin.y) / checkerSize);
				const bool even = ((ix + iy) % 2) == 0;

				drawList->AddRectFilled(
					ImVec2(x, y),
					ImVec2(
						(x + checkerSize < previewMax.x) ? x + checkerSize : previewMax.x,
						(y + checkerSize < previewMax.y) ? y + checkerSize : previewMax.y
					),
					even ? IM_COL32(70, 70, 70, 255) : IM_COL32(100, 100, 100, 255)
				);
			}
		}

		float drawWidth = previewSize;
		float drawHeight = previewSize;

		if (texWidth > 0.0f && texHeight > 0.0f) {
			const float aspect = texWidth / texHeight;
			if (aspect > 1.0f) {
				drawHeight = previewSize / aspect;
			}
			else {
				drawWidth = previewSize * aspect;
			}
		}

		const ImVec2 imageMin = ImVec2(
			previewMin.x + (previewSize - drawWidth) * 0.5f,
			previewMin.y + (previewSize - drawHeight) * 0.5f
		);
		const ImVec2 imageMax = ImVec2(imageMin.x + drawWidth, imageMin.y + drawHeight);

		drawList->AddImage(
			(ImTextureID)(intptr_t)rendererId,
			imageMin, imageMax,
			ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f)
		);

		ImGui::Dummy(ImVec2(previewSize, previewSize));
	}

	bool BeginComponentSection(const char* label, bool& removeRequested)
	{
		removeRequested = false;

		ImGui::PushID(label);

		bool truncated = false;
		const float headerWidth = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2.0f;
		const std::string displayLabel = Ellipsize(label, headerWidth, &truncated);
		const std::string headerLabel = displayLabel + "##" + label;
		bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
		if (truncated && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", label);
		}

		if (ImGui::BeginPopupContextItem("ComponentContext")) {
			if (ImGui::MenuItem("Remove Component")) {
				removeRequested = true;
			}
			ImGui::EndPopup();
		}

		if (open) {
			ImGui::Indent(8.0f);
		}
		else {
			ImGui::PopID();
		}

		return open;
	}

	void EndComponentSection()
	{
		ImGui::Unindent(8.0f);
		ImGui::Spacing();
		ImGui::PopID();
	}
}
