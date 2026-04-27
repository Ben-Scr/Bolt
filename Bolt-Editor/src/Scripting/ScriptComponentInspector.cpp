#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Scripting/ScriptComponent.hpp"
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
#include "Components/General/UUIDComponent.hpp"
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
#include <vector>

namespace Bolt {

	namespace {

		struct EditorFieldInfo {
			std::string name;
			std::string displayName;
			std::string type;
			std::string value;
			std::string tooltip;
			bool readOnly = false;
			bool hasClamp = false;
			float clampMin = 0.0f;
			float clampMax = 0.0f;
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

				if (field.displayName.empty()) field.displayName = field.name;
				fields.push_back(std::move(field));
			}
			return fields;
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
			if (!outScene->IsValid(outHandle) || !outScene->HasComponent<UUIDComponent>(outHandle)) {
				return false;
			}

			outEntityId = static_cast<uint64_t>(outScene->GetComponent<UUIDComponent>(outHandle).Id);
			return true;
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

				auto view = scene.GetRegistry().view<UUIDComponent>();
				for (EntityHandle entityHandle : view) {
					const auto& uuidComponent = view.get<UUIDComponent>(entityHandle);
					if (static_cast<uint64_t>(uuidComponent.Id) != entityId) {
						continue;
					}

					resolved = SceneEntityReference{
						entityId,
						GetEntityName(scene, entityHandle, entityId),
						scene.GetName()
					};
					return;
				}
			});

			return resolved;
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
				auto view = scene.GetRegistry().view<UUIDComponent>();
				for (EntityHandle entityHandle : view) {
					const auto& uuidComponent = view.get<UUIDComponent>(entityHandle);
					const uint64_t entityId = static_cast<uint64_t>(uuidComponent.Id);

					ReferencePickerEntry entry;
					entry.Label = GetEntityName(scene, entityHandle, entityId);
					entry.SearchKey = ToLowerCopy(entry.Label);
					entry.Value = std::to_string(entityId);
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

		static std::vector<ReferencePickerEntry> CollectComponentPickerEntries(const std::string& componentTypeName) {
			std::vector<ReferencePickerEntry> entries;
			entries.push_back({ "(None)", "", "(none)", "", "__none__" });
			const ComponentInfo* componentInfo = FindComponentByDisplayName(componentTypeName);
			if (!componentInfo || !componentInfo->has) {
				return entries;
			}

			SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
				auto view = scene.GetRegistry().view<UUIDComponent>();
				for (EntityHandle entityHandle : view) {
					const auto& uuidComponent = view.get<UUIDComponent>(entityHandle);
					const uint64_t entityId = static_cast<uint64_t>(uuidComponent.Id);
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
			constexpr float labelColumnWidth = 160.0f;

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			hoveredAny |= ImGui::IsItemHovered();

			ImGui::SameLine(labelColumnWidth);

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
		
		static float GetEditorLabelColumnWidth()
		{
			return 160.0f;
		}

		static void BeginEditorFieldRow(const char* label)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float labelColumnWidth = GetEditorLabelColumnWidth();

			ImGui::AlignTextToFramePadding();

			const float availableLabelWidth = std::max(1.0f, labelColumnWidth - style.ItemSpacing.x);

			bool truncated = false;
			std::string clippedLabel = ImGuiUtils::Ellipsize(label, availableLabelWidth, &truncated);

			ImGui::TextUnformatted(clippedLabel.c_str());
			if (truncated && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", label);

			ImGui::SameLine(labelColumnWidth);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
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
			else if (type == "entity") {
				const uint64_t entityId = std::strtoull(field.value.c_str(), nullptr, 10);
				bool missing = false;
				std::string secondaryText;
				std::string displayName = "(None)";

				if (entityId != 0) {
					const auto resolved = ResolveEntityReference(entityId);
					if (resolved.has_value()) {
						displayName = resolved->EntityName;
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

	} // namespace

	namespace {
		struct ScriptPickerEntry {
			std::string ClassName;
			std::string RelativePath;
			std::string Extension;
		};

		static bool s_ScriptPickerOpen = false;
		static char s_ScriptPickerSearch[128] = {};
		static std::vector<ScriptPickerEntry> s_ScriptPickerEntries;
		static EntityHandle s_ScriptPickerTargetEntity = entt::null;

		static bool IsScriptExtension(const std::string& ext) {
			return ext == ".cs" || ext == ".cpp";
		}

		static bool IsNativeScriptBootstrapFile(const std::filesystem::path& path) {
			return path.filename().string() == "NativeScriptExports.cpp";
		}

		static void CollectScriptFiles(const std::filesystem::path& dir,
			std::vector<ScriptPickerEntry>& entries) {
			if (!std::filesystem::exists(dir)) return;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
				if (!entry.is_regular_file()) continue;
				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (!IsScriptExtension(ext)) continue;
				if (ext == ".cpp" && IsNativeScriptBootstrapFile(entry.path())) continue;

				ScriptPickerEntry e;
				e.ClassName = entry.path().stem().string();
				e.RelativePath = entry.path().string();
				e.Extension = ext;
				entries.push_back(std::move(e));
			}
			std::sort(entries.begin(), entries.end(),
				[](const ScriptPickerEntry& a, const ScriptPickerEntry& b) {
					if (a.ClassName == b.ClassName) {
						return a.RelativePath < b.RelativePath;
					}
					return a.ClassName < b.ClassName;
				});
		}

		static void OpenScriptPicker(EntityHandle entity) {
			s_ScriptPickerOpen = true;
			s_ScriptPickerSearch[0] = '\0';
			s_ScriptPickerTargetEntity = entity;
			s_ScriptPickerEntries.clear();

			BoltProject* project = ProjectManager::GetCurrentProject();
			if (project) {
				CollectScriptFiles(std::filesystem::path(project->ScriptsDirectory), s_ScriptPickerEntries);
				CollectScriptFiles(std::filesystem::path(project->NativeSourceDir), s_ScriptPickerEntries);
			}
			else {
				std::string base = Path::ExecutableDir();
				CollectScriptFiles(std::filesystem::path(base) / ".." / ".." / ".." / "Bolt-Sandbox" / "Source", s_ScriptPickerEntries);
				CollectScriptFiles(std::filesystem::path(base) / ".." / ".." / ".." / "Bolt-NativeScripts" / "Source", s_ScriptPickerEntries);
			}
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
					std::string lowerPath = ToLowerCopy(entry.RelativePath);
					if (lowerName.find(filter) == std::string::npos && lowerPath.find(filter) == std::string::npos) continue;
				}

				bool truncated = false;
				const std::string displayLabel = ImGuiUtils::Ellipsize(
					entry.ClassName + "  " + entry.Extension,
					ImGui::GetContentRegionAvail().x,
					&truncated);
				std::string label = displayLabel + "##" + entry.RelativePath;
				if (ImGui::Selectable(label.c_str(), false)) {
					Scene* scene = ScriptEngine::GetScene();
					if (!scene) scene = SceneManager::Get().GetActiveScene();
					if (scene && scene->IsValid(s_ScriptPickerTargetEntity)
						&& scene->HasComponent<ScriptComponent>(s_ScriptPickerTargetEntity)) {
						auto& sc = scene->GetComponent<ScriptComponent>(s_ScriptPickerTargetEntity);
						if (!sc.HasScript(entry.ClassName)) {
							sc.AddScript(entry.ClassName);
						}
					}
					s_ScriptPickerOpen = false;
				}

				if (ImGui::IsItemHovered() && (truncated || !entry.RelativePath.empty())) {
					ImGui::SetTooltip("%s", entry.RelativePath.c_str());
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

		for (size_t i = 0; i < scriptComp.Scripts.size(); i++) {
			auto& instance = scriptComp.Scripts[i];
			std::string label = instance.GetClassName().empty() ? "Script" : instance.GetClassName();

			ImGui::PushID(static_cast<int>(i));

			bool removeRequested = false;
			bool open = ImGuiUtils::BeginComponentSection(label.c_str(), removeRequested);

			if (removeRequested) {
				removeIndex = i;
			}

			if (open) {
				ImGui::AlignTextToFramePadding();
				ImGui::BeginDisabled();
				BeginEditorFieldRow("Script");
				ImGui::EndDisabled();
				ImGui::SameLine();

				ImGui::BeginDisabled();
				//TODO(Ben-Scr): The Button should be aligned the same way as the other editor fields.
				ImGui::Button((label + "##ScriptClass").c_str());
				ImGui::EndDisabled();

				if (ScriptEngine::IsInitialized()) {
					RenderScriptFieldsForInstance(scriptComp, instance, i);
				}

				ImGuiUtils::EndComponentSection();
			}

			ImGui::PopID();
		}

		if (removeIndex != SIZE_MAX) {
			ScriptSystem::RemoveScript(entity, removeIndex);
		}

		RenderScriptPicker();
		RenderReferencePickerPopup();
	}

} // namespace Bolt
