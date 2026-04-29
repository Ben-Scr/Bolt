#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Core/Application.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Components/General/NameComponent.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Bolt {

	namespace {

		struct EditorFieldInfo {
			std::string name;
			std::string displayName;
			std::string type;
			std::string value;
			std::string tooltip;
			std::string headerContent;
			int headerSize = 0;
			bool readOnly = false;
			bool hasClamp = false;
			float clampMin = 0.0f;
			float clampMax = 0.0f;
			bool hasSpace = false;
			float spaceHeight = 0.0f;
		};

		struct SceneEntityReference {
			uint64_t EntityId = 0;
			std::string EntityName;
			std::string SceneName;
		};

		struct ReferencePickerEntry {
			std::string Label;
			std::string Secondary;
			std::string SearchKey;
			std::string Value;
			std::string UniqueId;
		};

		struct ReferencePickerState {
			bool RequestOpen = false;
			char Search[128] = {};
			std::string Title;
			std::string TargetFieldKey;
			std::vector<ReferencePickerEntry> Entries;
			std::string PendingFieldKey;
			std::string PendingValue;
		};

		static ReferencePickerState s_ReferencePicker;

		static std::vector<EditorFieldInfo> ParseEditorFields(const char* json) {
			std::vector<EditorFieldInfo> fields;
			if (!json || !*json) return fields;

			Json::Value root;
			std::string parseError;
			if (!Json::TryParse(json, root, &parseError) || !root.IsArray()) {
				BT_CORE_WARN_TAG("ScriptInspector", "Failed to parse editor field metadata: {}", parseError);
				return fields;
			}

			for (const Json::Value& item : root.GetArray()) {
				if (!item.IsObject()) {
					continue;
				}

				EditorFieldInfo field;
				if (const Json::Value* nameValue = item.FindMember("name")) field.name = nameValue->AsStringOr();
				if (const Json::Value* displayNameValue = item.FindMember("displayName")) field.displayName = displayNameValue->AsStringOr();
				if (const Json::Value* typeValue = item.FindMember("type")) field.type = typeValue->AsStringOr();
				if (const Json::Value* valueValue = item.FindMember("value")) field.value = valueValue->AsStringOr();
				if (const Json::Value* readOnlyValue = item.FindMember("readOnly")) field.readOnly = readOnlyValue->AsBoolOr(false);
				if (const Json::Value* hasClampValue = item.FindMember("hasClamp")) field.hasClamp = hasClampValue->AsBoolOr(false);
				if (const Json::Value* clampMinValue = item.FindMember("clampMin")) field.clampMin = static_cast<float>(clampMinValue->AsDoubleOr(0.0));
				if (const Json::Value* clampMaxValue = item.FindMember("clampMax")) field.clampMax = static_cast<float>(clampMaxValue->AsDoubleOr(0.0));
				if (const Json::Value* tooltipValue = item.FindMember("tooltip")) field.tooltip = tooltipValue->AsStringOr();
				if (const Json::Value* headerContentValue = item.FindMember("headerContent")) field.headerContent = headerContentValue->AsStringOr();
				if (const Json::Value* headerSizeValue = item.FindMember("headerSize")) field.headerSize = static_cast<int>(headerSizeValue->AsDoubleOr(0.0));
				if (const Json::Value* hasSpaceValue = item.FindMember("hasSpace")) field.hasSpace = hasSpaceValue->AsBoolOr(false);
				if (const Json::Value* spaceHeightValue = item.FindMember("spaceHeight")) field.spaceHeight = static_cast<float>(spaceHeightValue->AsDoubleOr(0.0));

				if (field.displayName.empty()) field.displayName = field.name;
				fields.push_back(std::move(field));
			}
			return fields;
		}

		static void RenderFieldHeader(const EditorFieldInfo& field) {
			if (field.headerContent.empty()) {
				return;
			}

			ImGui::Spacing();
			const float baseFontSize = std::max(ImGui::GetFontSize(), 1.0f);
			const float requestedSize = field.headerSize > 0
				? baseFontSize + static_cast<float>(field.headerSize)
				: baseFontSize;
			ImGui::PushFont(nullptr, std::max(1.0f, requestedSize));
			ImGui::TextUnformatted(field.headerContent.c_str());
			ImGui::PopFont();
			ImGui::Separator();
			ImGui::Spacing();
		}

		static std::string ToLowerCopy(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

		static bool IsUnsignedIntegerString(const std::string& value) {
			if (value.empty()) {
				return false;
			}

			return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
				return std::isdigit(ch) != 0;
			});
		}

		static const ComponentInfo* FindComponentByDisplayName(const std::string& displayName) {
			const ComponentInfo* found = nullptr;
			SceneManager::Get().GetComponentRegistry().ForEachComponentInfo(
				[&](const std::type_index&, const ComponentInfo& info) {
					if (!found && info.category == ComponentCategory::Component && info.displayName == displayName) {
						found = &info;
					}
				});
			return found;
		}

		static std::string GetEntityName(const Scene& scene, EntityHandle entityHandle, uint64_t entityId) {
			if (scene.HasComponent<NameComponent>(entityHandle)) {
				const std::string& name = scene.GetComponent<NameComponent>(entityHandle).Name;
				if (!name.empty()) {
					return name;
				}
			}

			return "Entity " + std::to_string(entityId);
		}

		static bool TryGetHierarchyPayloadEntity(Scene*& outScene, EntityHandle& outHandle, uint64_t& outEntityId) {
			struct HierarchyDragData { int Index; uint32_t EntityHandle; };

			outScene = ScriptEngine::GetScene();
			if (!outScene) {
				outScene = SceneManager::Get().GetActiveScene();
			}
			if (!outScene) {
				return false;
			}

			const ImGuiPayload* payload = ImGui::GetDragDropPayload();
			if (!payload || payload->DataSize != sizeof(HierarchyDragData)) {
				return false;
			}

			const auto* data = static_cast<const HierarchyDragData*>(payload->Data);
			outHandle = static_cast<EntityHandle>(data->EntityHandle);
			if (!outScene->IsValid(outHandle)) {
				return false;
			}

			outEntityId = outScene->GetRuntimeID(outHandle);
			return outEntityId != 0;
		}

		static std::optional<SceneEntityReference> ResolveEntityReference(uint64_t entityId) {
			if (entityId == 0) {
				return std::nullopt;
			}

			std::optional<SceneEntityReference> resolved;
			SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
				if (resolved.has_value()) {
					return;
				}

				EntityHandle entityHandle = entt::null;
				if (scene.TryResolveRuntimeID(entityId, entityHandle)) {
					resolved = SceneEntityReference{
						entityId,
						GetEntityName(scene, entityHandle, entityId),
						scene.GetName()
					};
				}
			});

			return resolved;
		}

		static bool TryParsePrefabFieldValue(const std::string& value, uint64_t& outPrefabGuid) {
			outPrefabGuid = 0;
			static constexpr std::string_view prefix = "prefab:";
			if (value.rfind(prefix, 0) != 0) {
				return false;
			}

			outPrefabGuid = std::strtoull(value.c_str() + prefix.size(), nullptr, 10);
			return outPrefabGuid != 0;
		}

		static std::string GetAssetDisplayName(const std::string& value, AssetKind expectedKind, bool& missing, std::string* secondaryText = nullptr) {
			missing = false;
			if (secondaryText) {
				secondaryText->clear();
			}

			if (value.empty()) {
				return "(None)";
			}

			if (IsUnsignedIntegerString(value)) {
				const uint64_t assetId = std::strtoull(value.c_str(), nullptr, 10);
				if (AssetRegistry::GetKind(assetId) == expectedKind) {
					if (secondaryText) {
						*secondaryText = AssetRegistry::ResolvePath(assetId);
					}

					const std::string name = AssetRegistry::GetDisplayName(assetId);
					if (!name.empty()) {
						return name;
					}
				}

				missing = true;
				return "(Missing Asset)";
			}

			const std::filesystem::path path(value);
			if (std::filesystem::exists(path)) {
				if (secondaryText) {
					*secondaryText = value;
				}
				return path.filename().string();
			}

			missing = true;
			return "(Missing Asset)";
		}

		static std::string GetComponentDisplayName(const std::string& value, const std::string& expectedType, bool& missing, std::string* secondaryText = nullptr) {
			missing = false;
			if (secondaryText) {
				secondaryText->clear();
			}

			if (value.empty()) {
				return "(None)";
			}

			const std::size_t separator = value.find(':');
			if (separator == std::string::npos) {
				missing = true;
				return "(Invalid Component)";
			}

			const uint64_t entityId = std::strtoull(value.substr(0, separator).c_str(), nullptr, 10);
			const std::string componentName = value.substr(separator + 1);
			const auto resolved = ResolveEntityReference(entityId);
			if (!resolved.has_value()) {
				missing = true;
				return "(Missing)." + componentName;
			}

			if (secondaryText) {
				secondaryText->clear();
			}

			if (!componentName.empty() && componentName != expectedType) {
				missing = true;
			}

			return resolved->EntityName + " (" + (componentName.empty() ? expectedType : componentName) + ")";
		}

		static void NormalizeAssetFieldValue(std::string& value, AssetKind expectedKind) {
			if (value.empty() || IsUnsignedIntegerString(value)) {
				return;
			}

			const uint64_t assetId = AssetRegistry::GetOrCreateAssetUUID(value);
			if (assetId != 0 && AssetRegistry::GetKind(assetId) == expectedKind) {
				value = std::to_string(assetId);
			}
		}

		static std::vector<ReferencePickerEntry> CollectEntityPickerEntries() {
			std::vector<ReferencePickerEntry> entries;
			entries.push_back({ "(None)", "", "(none)", "0", "__none__" });

			SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
				auto view = scene.GetRegistry().view<entt::entity>();
				for (EntityHandle entityHandle : view) {
					if (!scene.IsValid(entityHandle)) {
						continue;
					}

					const uint64_t entityId = scene.GetRuntimeID(entityHandle);
					if (entityId == 0) {
						continue;
					}

					ReferencePickerEntry entry;
					entry.Label = GetEntityName(scene, entityHandle, entityId);
					entry.Secondary = scene.GetName();
					entry.SearchKey = ToLowerCopy(entry.Label);
					entry.Value = std::to_string(entityId);
					entry.UniqueId = entry.Value;
					entries.push_back(std::move(entry));
				}
			});

			for (const AssetRegistry::Record& record : AssetRegistry::GetAssetsByKind(AssetKind::Prefab)) {
				ReferencePickerEntry entry;
				entry.Label = std::filesystem::path(record.Path).filename().string();
				entry.Secondary = record.Path;
				entry.SearchKey = ToLowerCopy(entry.Label + " prefab " + entry.Secondary);
				entry.Value = "prefab:" + std::to_string(record.Id);
				entry.UniqueId = entry.Value;
				entries.push_back(std::move(entry));
			}

			std::sort(entries.begin(), entries.end(), [](const ReferencePickerEntry& a, const ReferencePickerEntry& b) {
				if (a.Label == b.Label) {
					return a.Secondary < b.Secondary;
				}
				return a.Label < b.Label;
			});

			return entries;
		}

		static std::vector<ReferencePickerEntry> CollectComponentPickerEntries(const std::string& componentTypeName) {
			std::vector<ReferencePickerEntry> entries;
			entries.push_back({ "(None)", "", "(none)", "", "__none__" });
			const ComponentInfo* componentInfo = FindComponentByDisplayName(componentTypeName);
			if (!componentInfo || !componentInfo->has) {
				return entries;
			}

			SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
				auto view = scene.GetRegistry().view<entt::entity>();
				for (EntityHandle entityHandle : view) {
					if (!scene.IsValid(entityHandle)) {
						continue;
					}

					const uint64_t entityId = scene.GetRuntimeID(entityHandle);
					if (entityId == 0) {
						continue;
					}

					Entity entity = scene.GetEntity(entityHandle);
					if (!componentInfo->has(entity)) {
						continue;
					}

					const std::string entityName = GetEntityName(scene, entityHandle, entityId);

					ReferencePickerEntry entry;
					entry.Label = entityName + " (" + componentTypeName + ")";
					entry.SearchKey = ToLowerCopy(entityName + " " + componentTypeName);
					entry.Value = std::to_string(entityId) + ":" + componentTypeName;
					entry.UniqueId = entry.Value;
					entries.push_back(std::move(entry));
				}
			});

			std::sort(entries.begin(), entries.end(), [](const ReferencePickerEntry& a, const ReferencePickerEntry& b) {
				if (a.Label == b.Label) {
					return a.Secondary < b.Secondary;
				}
				return a.Label < b.Label;
			});

			return entries;
		}

		static std::vector<ReferencePickerEntry> CollectAssetPickerEntries(AssetKind kind) {
			std::vector<ReferencePickerEntry> entries;
			for (const AssetRegistry::Record& record : AssetRegistry::GetAssetsByKind(kind)) {
				ReferencePickerEntry entry;
				entry.Label = std::filesystem::path(record.Path).filename().string();
				entry.Secondary = record.Path;
				entry.SearchKey = ToLowerCopy(entry.Label + " " + entry.Secondary);
				entry.Value = std::to_string(record.Id);
				entry.UniqueId = entry.Value;
				entries.push_back(std::move(entry));
			}
			std::sort(entries.begin(), entries.end(), [](const ReferencePickerEntry& a, const ReferencePickerEntry& b) {
				if (a.Label == b.Label) {
					return a.Secondary < b.Secondary;
				}
				return a.Label < b.Label;
			});
			entries.insert(entries.begin(), { "(None)", "", "(none)", "", "__none__" });
			return entries;
		}

		static void OpenReferencePicker(const std::string& fieldKey, const std::string& title, std::vector<ReferencePickerEntry> entries) {
			s_ReferencePicker.RequestOpen = true;
			s_ReferencePicker.Title = title;
			s_ReferencePicker.TargetFieldKey = fieldKey;
			s_ReferencePicker.Entries = std::move(entries);
			s_ReferencePicker.Search[0] = '\0';
		}

		static std::optional<std::string> ConsumeReferencePickerSelection(const std::string& fieldKey) {
			if (s_ReferencePicker.PendingFieldKey != fieldKey) {
				return std::nullopt;
			}

			std::string value = s_ReferencePicker.PendingValue;
			s_ReferencePicker.PendingFieldKey.clear();
			s_ReferencePicker.PendingValue.clear();
			return value;
		}

		static bool DrawReferenceFieldControls(
			const char* label,
			const std::string& displayValue,
			const std::string& secondaryText,
			bool missing,
			bool& hoveredAny)
		{
			ImGui::PushID(label);
			ImGuiUtils::BeginInspectorFieldRow(label);
			float buttonWidth = std::max(ImGui::GetContentRegionAvail().x, 120.0f);
			const ImGuiStyle& style = ImGui::GetStyle();

			if (missing) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.12f, 0.12f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.16f, 0.16f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.10f, 0.10f, 1.0f));
			}
			else {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
			}

			bool truncated = false;
			const std::string buttonText = ImGuiUtils::Ellipsize(displayValue, buttonWidth - style.FramePadding.x * 2.0f, &truncated);
			bool openPicker = ImGui::Button((buttonText + "##ReferenceValue").c_str(), ImVec2(buttonWidth, 0.0f));
			hoveredAny |= ImGui::IsItemHovered();
			if (ImGui::IsItemHovered() && (truncated || !secondaryText.empty())) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(displayValue.c_str());
				if (!secondaryText.empty()) {
					ImGui::Separator();
					ImGui::TextDisabled("%s", secondaryText.c_str());
				}
				ImGui::EndTooltip();
			}
			ImGui::PopStyleColor(3);
			ImGui::PopID();

			return openPicker;
		}

		static void RenderReferencePickerPopup() {
			if (s_ReferencePicker.RequestOpen) {
				ImGui::OpenPopup("Reference Picker##ScriptInspector");
				s_ReferencePicker.RequestOpen = false;
			}

			ImGui::SetNextWindowSize(ImVec2(440.0f, 430.0f), ImGuiCond_Appearing);
			if (!ImGui::BeginPopupModal("Reference Picker##ScriptInspector", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
				return;
			}

			ImGui::TextUnformatted(s_ReferencePicker.Title.c_str());
			const float closeButtonSize = ImGui::GetFrameHeight();
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeButtonSize);
			if (ImGui::Button("X##ReferencePickerClose", ImVec2(closeButtonSize, closeButtonSize))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::Separator();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##ReferenceSearch", "Search...", s_ReferencePicker.Search, sizeof(s_ReferencePicker.Search));
			ImGui::Separator();

			ImGui::BeginChild("##ReferencePickerList");

			const std::string filter = ToLowerCopy(std::string(s_ReferencePicker.Search));
			bool hasVisibleEntries = false;

			for (const ReferencePickerEntry& entry : s_ReferencePicker.Entries) {
				if (!filter.empty() && entry.SearchKey.find(filter) == std::string::npos) {
					continue;
				}

				hasVisibleEntries = true;

				bool labelTruncated = false;
				const std::string label = ImGuiUtils::Ellipsize(entry.Label, ImGui::GetContentRegionAvail().x, &labelTruncated);
				if (ImGui::Selectable((label + "##" + entry.UniqueId).c_str(), false)) {
					s_ReferencePicker.PendingFieldKey = s_ReferencePicker.TargetFieldKey;
					s_ReferencePicker.PendingValue = entry.Value;
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::IsItemHovered() && (labelTruncated || !entry.Secondary.empty())) {
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(entry.Label.c_str());
					if (!entry.Secondary.empty()) {
						ImGui::Separator();
						ImGui::TextDisabled("%s", entry.Secondary.c_str());
					}
					ImGui::EndTooltip();
				}

				if (!entry.Secondary.empty()) {
					ImGui::Indent(14.0f);
					ImGuiUtils::TextDisabledEllipsis(entry.Secondary);
					ImGui::Unindent(14.0f);
				}
			}

			if (!hasVisibleEntries) {
				ImGui::TextDisabled("No matching items");
			}

			ImGui::EndChild();
			ImGui::EndPopup();
		}
		
		static void BeginEditorFieldRow(const char* label)
		{
			ImGuiUtils::BeginInspectorFieldRow(label);
		}

		static bool ParseFloatList(const std::string& value, float* outValues, int count)
		{
			if (!outValues || count <= 0) return false;
			std::string remaining = value;
			for (int i = 0; i < count; ++i) {
				const std::size_t separator = remaining.find(',');
				const std::string token = separator == std::string::npos
					? remaining
					: remaining.substr(0, separator);
				outValues[i] = static_cast<float>(std::atof(token.c_str()));
				if (separator == std::string::npos) {
					return i + 1 == count;
				}
				remaining = remaining.substr(separator + 1);
			}
			return true;
		}

		static bool ParseIntList(const std::string& value, int* outValues, int count)
		{
			if (!outValues || count <= 0) return false;
			std::string remaining = value;
			for (int i = 0; i < count; ++i) {
				const std::size_t separator = remaining.find(',');
				const std::string token = separator == std::string::npos
					? remaining
					: remaining.substr(0, separator);
				outValues[i] = std::atoi(token.c_str());
				if (separator == std::string::npos) {
					return i + 1 == count;
				}
				remaining = remaining.substr(separator + 1);
			}
			return true;
		}

		static std::string FormatFloatList(const float* values, int count)
		{
			char buf[160];
			if (count == 2) {
				std::snprintf(buf, sizeof(buf), "%g,%g", values[0], values[1]);
			}
			else if (count == 3) {
				std::snprintf(buf, sizeof(buf), "%g,%g,%g", values[0], values[1], values[2]);
			}
			else {
				std::snprintf(buf, sizeof(buf), "%g,%g,%g,%g", values[0], values[1], values[2], values[3]);
			}
			return buf;
		}

		static std::string FormatIntList(const int* values, int count)
		{
			char buf[160];
			if (count == 2) {
				std::snprintf(buf, sizeof(buf), "%d,%d", values[0], values[1]);
			}
			else if (count == 3) {
				std::snprintf(buf, sizeof(buf), "%d,%d,%d", values[0], values[1], values[2]);
			}
			else {
				std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d", values[0], values[1], values[2], values[3]);
			}
			return buf;
		}

		static void RenderEditorField(const std::string& fieldKey, int32_t gcHandle, EditorFieldInfo& field) {
			(void)gcHandle;
			ImGui::PushID(field.name.c_str());

			if (const auto pickerValue = ConsumeReferencePickerSelection(fieldKey)) {
				field.value = *pickerValue;
			}

			if (field.readOnly)
				ImGui::BeginDisabled();

			bool changed = false;
			std::string newValue;
			bool hoveredAny = false;

			const std::string& type = field.type;
			const char* label = field.displayName.c_str();

			if (type == "float" || type == "double") {
				float val = static_cast<float>(std::atof(field.value.c_str()));
				float mn = field.hasClamp ? field.clampMin : 0.0f;
				float mx = field.hasClamp ? field.clampMax : 0.0f;

				BeginEditorFieldRow(label);
				if (ImGui::DragFloat("##Value", &val, 0.1f, mn, mx)) {
					changed = true;
					char buf[64];
					std::snprintf(buf, sizeof(buf), "%g", val);
					newValue = buf;
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "int") {
				int val = std::atoi(field.value.c_str());
				int mn = field.hasClamp ? static_cast<int>(field.clampMin) : 0;
				int mx = field.hasClamp ? static_cast<int>(field.clampMax) : 0;

				BeginEditorFieldRow(label);
				if (ImGui::DragInt("##Value", &val, 1.0f, mn, mx)) {
					changed = true;
					newValue = std::to_string(val);
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "short") {
				int val = std::atoi(field.value.c_str());
				int mn = field.hasClamp ? std::max(static_cast<int>(field.clampMin), -32768) : -32768;
				int mx = field.hasClamp ? std::min(static_cast<int>(field.clampMax), 32767) : 32767;
				
				BeginEditorFieldRow(label);
				if (ImGui::DragInt("##Value", &val, 1.0f, mn, mx)) {
					if (val < -32768) val = -32768;
					if (val > 32767) val = 32767;
					changed = true;
					newValue = std::to_string(val);
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "byte") {
				int val = std::atoi(field.value.c_str());
				int mn = field.hasClamp ? std::max(static_cast<int>(field.clampMin), 0) : 0;
				int mx = field.hasClamp ? std::min(static_cast<int>(field.clampMax), 255) : 255;
				
				BeginEditorFieldRow(label);
				if (ImGui::DragInt("##Value", &val, 1.0f, mn, mx)) {
					if (val < 0) val = 0;
					if (val > 255) val = 255;
					changed = true;
					newValue = std::to_string(val);
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "sbyte") {
				int val = std::atoi(field.value.c_str());
				int mn = field.hasClamp ? std::max(static_cast<int>(field.clampMin), -128) : -128;
				int mx = field.hasClamp ? std::min(static_cast<int>(field.clampMax), 127) : 127;
				
				BeginEditorFieldRow(label);
				if (ImGui::DragInt("##Value", &val, 1.0f, mn, mx)) {
					if (val < -128) val = -128;
					if (val > 127) val = 127;
					changed = true;
					newValue = std::to_string(val);
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "uint") {
				int val = std::atoi(field.value.c_str());
				int mn = field.hasClamp ? std::max(static_cast<int>(field.clampMin), 0) : 0;
				int mx = field.hasClamp ? std::min(static_cast<int>(field.clampMax), INT_MAX) : INT_MAX;
				
				BeginEditorFieldRow(label);
				if (ImGui::DragInt("##Value", &val, 1.0f, mn, mx)) {
					if (val < 0) val = 0;
					changed = true;
					newValue = std::to_string(val);
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "ushort") {
				int val = std::atoi(field.value.c_str());
				int mn = field.hasClamp ? std::max(static_cast<int>(field.clampMin), 0) : 0;
				int mx = field.hasClamp ? std::min(static_cast<int>(field.clampMax), 65535) : 65535;
				
				BeginEditorFieldRow(label);
				if (ImGui::DragInt("##Value", &val, 1.0f, mn, mx)) {
					if (val < 0) val = 0;
					if (val > 65535) val = 65535;
					changed = true;
					newValue = std::to_string(val);
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "long" || type == "ulong") {
				char buf[64];
				std::strncpy(buf, field.value.c_str(), sizeof(buf) - 1);
				buf[sizeof(buf) - 1] = '\0';

				BeginEditorFieldRow(label);
				if (ImGui::InputText("##Value", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
					changed = true;
					newValue = buf;
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "bool") {
				bool val = (field.value == "true" || field.value == "True" || field.value == "1");
				
				BeginEditorFieldRow(label);
				if (ImGui::Checkbox("##Value", &val)) {
					changed = true;
					newValue = val ? "true" : "false";
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "string") {
				char buf[256];
				std::strncpy(buf, field.value.c_str(), sizeof(buf) - 1);
				buf[sizeof(buf) - 1] = '\0';

				BeginEditorFieldRow(label);
				if (ImGui::InputText("##Value", buf, sizeof(buf))) {
					changed = true;
					newValue = buf;
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "color") {
				float col[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				if (!field.value.empty()) {
					std::sscanf(field.value.c_str(), "%f,%f,%f,%f",
						&col[0], &col[1], &col[2], &col[3]);
				}

				BeginEditorFieldRow(label);
				if (ImGui::ColorEdit4("##Value", col)) {
					changed = true;
					char buf[128];
					std::snprintf(buf, sizeof(buf), "%g,%g,%g,%g",
						col[0], col[1], col[2], col[3]);
					newValue = buf;
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "vector2" || type == "vector3" || type == "vector4") {
				const int componentCount = type == "vector2" ? 2 : (type == "vector3" ? 3 : 4);
				float values[4] = {};
				ParseFloatList(field.value, values, componentCount);

				BeginEditorFieldRow(label);
				if ((componentCount == 2 && ImGui::DragFloat2("##Value", values, 0.1f))
					|| (componentCount == 3 && ImGui::DragFloat3("##Value", values, 0.1f))
					|| (componentCount == 4 && ImGui::DragFloat4("##Value", values, 0.1f))) {
					changed = true;
					newValue = FormatFloatList(values, componentCount);
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "vector2Int" || type == "vector3Int" || type == "vector4Int") {
				const int componentCount = type == "vector2Int" ? 2 : (type == "vector3Int" ? 3 : 4);
				int values[4] = {};
				ParseIntList(field.value, values, componentCount);

				BeginEditorFieldRow(label);
				if ((componentCount == 2 && ImGui::DragInt2("##Value", values, 1.0f))
					|| (componentCount == 3 && ImGui::DragInt3("##Value", values, 1.0f))
					|| (componentCount == 4 && ImGui::DragInt4("##Value", values, 1.0f))) {
					changed = true;
					newValue = FormatIntList(values, componentCount);
				}
				hoveredAny |= ImGui::IsItemHovered();
			}
			else if (type == "entity") {
				bool missing = false;
				std::string secondaryText;
				std::string displayName = "(None)";

				uint64_t prefabGuid = 0;
				if (TryParsePrefabFieldValue(field.value, prefabGuid)) {
					if (AssetRegistry::GetKind(prefabGuid) == AssetKind::Prefab) {
						displayName = AssetRegistry::GetDisplayName(prefabGuid);
						secondaryText = AssetRegistry::ResolvePath(prefabGuid);
					}
					else {
						missing = true;
						displayName = "(Missing Prefab)";
					}
				}
				else {
					const uint64_t entityId = std::strtoull(field.value.c_str(), nullptr, 10);
					const auto resolved = ResolveEntityReference(entityId);
					if (entityId == 0) {
						displayName = "(None)";
					}
					else if (resolved.has_value()) {
						displayName = resolved->EntityName;
						secondaryText = resolved->SceneName;
					}
					else {
						missing = true;
						displayName = "(Missing Entity)";
					}
				}

				if (DrawReferenceFieldControls(label, displayName, secondaryText, missing, hoveredAny)) {
					OpenReferencePicker(fieldKey, "Select Entity", CollectEntityPickerEntries());
				}

				if (ImGui::BeginDragDropTarget()) {
					if (ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
						Scene* scene = nullptr;
						EntityHandle handle = entt::null;
						uint64_t droppedEntityId = 0;
						if (TryGetHierarchyPayloadEntity(scene, handle, droppedEntityId)) {
							newValue = std::to_string(droppedEntityId);
							changed = true;
						}
					}
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
						std::string droppedPath(static_cast<const char*>(payload->Data));
						const uint64_t assetId = AssetRegistry::GetOrCreateAssetUUID(droppedPath);
						if (assetId != 0 && AssetRegistry::GetKind(assetId) == AssetKind::Prefab) {
							newValue = "prefab:" + std::to_string(assetId);
							changed = true;
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
			else if (type == "texture") {
				NormalizeAssetFieldValue(field.value, AssetKind::Texture);

				bool missing = false;
				std::string secondaryText;
				const std::string displayName = GetAssetDisplayName(field.value, AssetKind::Texture, missing, &secondaryText);

				if (DrawReferenceFieldControls(label, displayName, secondaryText, missing, hoveredAny)) {
					OpenReferencePicker(fieldKey, "Select Texture", CollectAssetPickerEntries(AssetKind::Texture));
				}

				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
						std::string droppedPath(static_cast<const char*>(payload->Data));
						const uint64_t assetId = AssetRegistry::GetOrCreateAssetUUID(droppedPath);
						if (assetId != 0 && AssetRegistry::GetKind(assetId) == AssetKind::Texture) {
							newValue = std::to_string(assetId);
							changed = true;
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
			else if (type == "audio") {
				NormalizeAssetFieldValue(field.value, AssetKind::Audio);

				bool missing = false;
				std::string secondaryText;
				const std::string displayName = GetAssetDisplayName(field.value, AssetKind::Audio, missing, &secondaryText);

				if (DrawReferenceFieldControls(label, displayName, secondaryText, missing, hoveredAny)) {
					OpenReferencePicker(fieldKey, "Select Audio", CollectAssetPickerEntries(AssetKind::Audio));
				}

				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
						std::string droppedPath(static_cast<const char*>(payload->Data));
						const uint64_t assetId = AssetRegistry::GetOrCreateAssetUUID(droppedPath);
						if (assetId != 0 && AssetRegistry::GetKind(assetId) == AssetKind::Audio) {
							newValue = std::to_string(assetId);
							changed = true;
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
			else if (type.rfind("component:", 0) == 0) {
				const std::string componentTypeName = type.substr(10);
				bool missing = false;
				std::string secondaryText;
				const std::string displayName = GetComponentDisplayName(field.value, componentTypeName, missing, &secondaryText);

				if (DrawReferenceFieldControls(label, displayName, secondaryText, missing, hoveredAny)) {
					OpenReferencePicker(fieldKey, "Select " + componentTypeName, CollectComponentPickerEntries(componentTypeName));
				}

				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMPONENT_REF")) {
						std::string refStr(static_cast<const char*>(payload->Data));
						const std::size_t separator = refStr.find(':');
						if (separator != std::string::npos) {
							const std::string droppedTypeName = refStr.substr(separator + 1);
							if (droppedTypeName == componentTypeName) {
								newValue = refStr;
								changed = true;
							}
						}
					}

					if (ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
						Scene* scene = nullptr;
						EntityHandle handle = entt::null;
						uint64_t droppedEntityId = 0;
						const ComponentInfo* componentInfo = FindComponentByDisplayName(componentTypeName);
						if (componentInfo && componentInfo->has
							&& TryGetHierarchyPayloadEntity(scene, handle, droppedEntityId)
							&& componentInfo->has(scene->GetEntity(handle))) {
							newValue = std::to_string(droppedEntityId) + ":" + componentTypeName;
							changed = true;
						}
					}

					ImGui::EndDragDropTarget();
				}
			}
			else {
				ImGui::TextDisabled("%s: %s (%s)", label, field.value.c_str(), type.c_str());
				hoveredAny |= ImGui::IsItemHovered();
			}

			if (field.readOnly)
				ImGui::EndDisabled();

			if (!field.tooltip.empty() && hoveredAny) {
				ImGui::SetTooltip("%s", field.tooltip.c_str());
			}

			if (changed) {
				field.value = newValue;
			}

			ImGui::PopID();
		}

		static std::string MakeFieldKey(std::size_t scriptIndex, const std::string& className, const std::string& fieldName) {
			return className + "#" + std::to_string(scriptIndex) + "." + fieldName;
		}

		static void RenderScriptFieldsForInstance(ScriptComponent& sc, const ScriptInstance& instance, std::size_t scriptIndex) {
			auto& callbacks = ScriptEngine::GetCallbacks();
			bool isPlaying = Application::GetIsPlaying();
			bool hasLiveInstance = instance.HasManagedInstance();

			const char* json = nullptr;

			if (hasLiveInstance && callbacks.GetScriptFields) {
				json = callbacks.GetScriptFields(static_cast<int32_t>(instance.GetGCHandle()));
			}
			else if (callbacks.GetClassFieldDefs) {
				json = callbacks.GetClassFieldDefs(instance.GetClassName().c_str());
			}

			if (!json || !*json || (json[0] == '[' && json[1] == ']')) return;

			auto fields = ParseEditorFields(json);
			if (fields.empty()) return;

			if (!hasLiveInstance) {
				for (auto& field : fields) {
					std::string key = instance.GetClassName() + "." + field.name;
					auto it = sc.PendingFieldValues.find(key);
					if (it != sc.PendingFieldValues.end())
						field.value = it->second;
				}
			}

			ImGui::Indent(8.0f);
			for (auto& field : fields) {
				std::string oldValue = field.value;
				const std::string fieldKey = MakeFieldKey(scriptIndex, instance.GetClassName(), field.name);

				RenderFieldHeader(field);
				if (field.hasSpace && field.spaceHeight > 0.0f) {
					ImGui::Dummy(ImVec2(0.0f, field.spaceHeight));
				}
				RenderEditorField(fieldKey, hasLiveInstance ? static_cast<int32_t>(instance.GetGCHandle()) : 0, field);

				if (field.value != oldValue) {
					if (hasLiveInstance && callbacks.SetScriptField) {
						callbacks.SetScriptField(
							static_cast<int32_t>(instance.GetGCHandle()),
							field.name.c_str(), field.value.c_str());
					}

					if (!isPlaying) {
						std::string key = instance.GetClassName() + "." + field.name;
						sc.PendingFieldValues[key] = field.value;
					}
				}
			}
			ImGui::Unindent(8.0f);
		}

		static void RenderFieldsForManagedComponent(ScriptComponent& sc, const std::string& className, std::size_t componentIndex) {
			auto& callbacks = ScriptEngine::GetCallbacks();
			if (!callbacks.GetClassFieldDefs) {
				return;
			}

			const char* json = callbacks.GetClassFieldDefs(className.c_str());
			if (!json || !*json || (json[0] == '[' && json[1] == ']')) return;

			auto fields = ParseEditorFields(json);
			if (fields.empty()) return;

			for (auto& field : fields) {
				const std::string key = className + "." + field.name;
				auto it = sc.PendingFieldValues.find(key);
				if (it != sc.PendingFieldValues.end()) {
					field.value = it->second;
				}
			}

			ImGui::Indent(8.0f);
			for (auto& field : fields) {
				const std::string oldValue = field.value;
				const std::string fieldKey = className + "#component" + std::to_string(componentIndex) + "." + field.name;

				RenderFieldHeader(field);
				if (field.hasSpace && field.spaceHeight > 0.0f) {
					ImGui::Dummy(ImVec2(0.0f, field.spaceHeight));
				}
				RenderEditorField(fieldKey, 0, field);

				if (field.value != oldValue) {
					sc.PendingFieldValues[className + "." + field.name] = field.value;
				}
			}
			ImGui::Unindent(8.0f);
		}

	} // namespace

	namespace {
		using ScriptPickerEntry = EditorScriptDiscovery::ScriptEntry;

		static bool s_ScriptPickerOpen = false;
		static char s_ScriptPickerSearch[128] = {};
		static std::vector<ScriptPickerEntry> s_ScriptPickerEntries;
		static EntityHandle s_ScriptPickerTargetEntity = entt::null;

		static void OpenScriptPicker(EntityHandle entity) {
			s_ScriptPickerOpen = true;
			s_ScriptPickerSearch[0] = '\0';
			s_ScriptPickerTargetEntity = entity;
			s_ScriptPickerEntries.clear();
			EditorScriptDiscovery::CollectProjectScriptEntries(s_ScriptPickerEntries);
		}

		static void RenderScriptPicker() {
			if (!s_ScriptPickerOpen) return;

			ImGui::SetNextWindowSize(ImVec2(320, 380), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Select Script", &s_ScriptPickerOpen)) {
				ImGui::End();
				return;
			}

			const float closeButtonSize = ImGui::GetFrameHeight();
			const float searchWidth = std::max(ImGui::GetContentRegionAvail().x - closeButtonSize - ImGui::GetStyle().ItemSpacing.x, 1.0f);
			ImGui::SetNextItemWidth(searchWidth);
			ImGui::InputTextWithHint("##ScriptSearch", "Search...", s_ScriptPickerSearch, sizeof(s_ScriptPickerSearch));
			ImGui::SameLine();
			if (ImGui::Button("X##ScriptPickerClose", ImVec2(closeButtonSize, closeButtonSize))) {
				s_ScriptPickerOpen = false;
			}
			ImGui::Separator();

			ImGui::BeginChild("##ScriptList");

			std::string filter = ToLowerCopy(std::string(s_ScriptPickerSearch));

			for (const auto& entry : s_ScriptPickerEntries) {
				if (!filter.empty()) {
					std::string lowerName = ToLowerCopy(entry.ClassName);
					std::string lowerPath = ToLowerCopy(entry.Path.string());
					if (lowerName.find(filter) == std::string::npos && lowerPath.find(filter) == std::string::npos) continue;
				}

				bool truncated = false;
				const std::string displayLabel = ImGuiUtils::Ellipsize(
					entry.ClassName + "  " + entry.Extension,
					ImGui::GetContentRegionAvail().x,
					&truncated);
				std::string label = displayLabel + "##" + entry.Path.string();
				if (ImGui::Selectable(label.c_str(), false)) {
					Scene* scene = ScriptEngine::GetScene();
					if (!scene) scene = SceneManager::Get().GetActiveScene();
					if (scene && scene->IsValid(s_ScriptPickerTargetEntity)
						&& scene->HasComponent<ScriptComponent>(s_ScriptPickerTargetEntity)) {
						auto& sc = scene->GetComponent<ScriptComponent>(s_ScriptPickerTargetEntity);
						if (!sc.HasScript(entry.ClassName, entry.Type)) {
							sc.AddScript(entry.ClassName, entry.Type);
							scene->MarkDirty();
						}
					}
					s_ScriptPickerOpen = false;
				}

				if (ImGui::IsItemHovered() && (truncated || !entry.Path.empty())) {
					const std::string path = entry.Path.string();
					ImGui::SetTooltip("%s", path.c_str());
				}
			}

			ImGui::EndChild();
			ImGui::End();
		}
	}

	void DrawScriptComponentInspector(Entity entity)
	{
		auto& scriptComp = entity.GetComponent<ScriptComponent>();

		size_t removeIndex = SIZE_MAX;
		size_t removeManagedComponentIndex = SIZE_MAX;

		for (size_t i = 0; i < scriptComp.ManagedComponents.size(); i++) {
			const std::string& className = scriptComp.ManagedComponents[i];
			std::string label = className.empty() ? "C# Component" : className + " (C# Component)";

			ImGui::PushID(("managed-component-" + std::to_string(i)).c_str());

			bool removeRequested = false;
			bool open = ImGuiUtils::BeginComponentSection(label.c_str(), removeRequested);

			if (removeRequested) {
				removeManagedComponentIndex = i;
			}

			if (open) {
				if (ScriptEngine::IsInitialized()) {
					RenderFieldsForManagedComponent(scriptComp, className, i);
				}
				else {
					ImGui::TextDisabled("Script engine not initialized");
				}

				ImGuiUtils::EndComponentSection();
			}

			ImGui::PopID();
		}

		for (size_t i = 0; i < scriptComp.Scripts.size(); i++) {
			auto& instance = scriptComp.Scripts[i];
			std::string label = instance.GetClassName().empty() ? "Script" : instance.GetClassName();
			if (instance.GetType() == ScriptType::Native) {
				label += " (Native)";
			}

			ImGui::PushID(static_cast<int>(i));

			bool removeRequested = false;
			bool open = ImGuiUtils::BeginComponentSection(label.c_str(), removeRequested);

			if (removeRequested) {
				removeIndex = i;
			}

			if (open) {
				ImGuiUtils::DrawInspectorValue("Script", [&label](const char* id) {
					ImGui::BeginDisabled();
					ImGui::Button((label + id).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
					ImGui::EndDisabled();
				});

				if (instance.GetType() != ScriptType::Native && ScriptEngine::IsInitialized()) {
					RenderScriptFieldsForInstance(scriptComp, instance, i);
				}

				ImGuiUtils::EndComponentSection();
			}

			ImGui::PopID();
		}

		if (removeIndex != SIZE_MAX) {
			ScriptSystem::RemoveScript(entity, removeIndex);
		}
		if (removeManagedComponentIndex != SIZE_MAX && removeManagedComponentIndex < scriptComp.ManagedComponents.size()) {
			if (scriptComp.RemoveManagedComponent(scriptComp.ManagedComponents[removeManagedComponentIndex])) {
				if (Scene* scene = entity.GetScene()) {
					scene->MarkDirty();
				}
			}
		}

		RenderScriptPicker();
		RenderReferencePickerPopup();
	}

} // namespace Bolt
