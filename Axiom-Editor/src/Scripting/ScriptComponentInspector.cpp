#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/NameComponent.hpp"
#include "Core/Application.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/PropertyDescriptor.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Inspector/PropertyType.hpp"
#include "Inspector/PropertyValue.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Axiom {

	namespace {

		// JSON field record produced by C# Axiom-ScriptCore (one per
		// [ShowInEditor] field). The C++ side parses this into a
		// PropertyDescriptor and drives the unified PropertyDrawer.
		struct EditorFieldRecord {
			std::string Name;
			std::string DisplayName;
			std::string TypeTag;             // "float", "enum", "component:Foo", ...
			std::string ComponentTypeName;   // populated when TypeTag starts with "component:"
			std::string Value;                // current value as a string (PropertyValue::FromString accepts this)
			std::string Tooltip;
			std::string HeaderContent;
			int HeaderSize = 0;
			bool ReadOnly = false;
			bool HasClamp = false;
			float ClampMin = 0.0f;
			float ClampMax = 0.0f;
			bool HasSpace = false;
			float SpaceHeight = 0.0f;
			std::shared_ptr<EnumDescriptor> Enum;
		};

		std::string ToLowerCopy(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

		PropertyType ResolveScriptFieldType(const EditorFieldRecord& rec) {
			if (rec.TypeTag.rfind("component:", 0) == 0) return PropertyType::ComponentRef;
			if (rec.TypeTag == "entity") return PropertyType::EntityRef;
			return PropertyTypeFromString(rec.TypeTag);
		}

		std::vector<EditorFieldRecord> ParseEditorFields(const char* json) {
			std::vector<EditorFieldRecord> fields;
			if (!json || !*json) return fields;

			Json::Value root;
			std::string parseError;
			if (!Json::TryParse(json, root, &parseError) || !root.IsArray()) {
				AIM_CORE_WARN_TAG("ScriptInspector", "Failed to parse editor field metadata: {}", parseError);
				return fields;
			}

			for (const Json::Value& item : root.GetArray()) {
				if (!item.IsObject()) continue;
				EditorFieldRecord rec;

				if (const Json::Value* v = item.FindMember("name"))           rec.Name = v->AsStringOr();
				if (const Json::Value* v = item.FindMember("displayName"))    rec.DisplayName = v->AsStringOr();
				if (const Json::Value* v = item.FindMember("type"))           rec.TypeTag = v->AsStringOr();
				if (const Json::Value* v = item.FindMember("value"))          rec.Value = v->AsStringOr();
				if (const Json::Value* v = item.FindMember("readOnly"))       rec.ReadOnly = v->AsBoolOr(false);
				if (const Json::Value* v = item.FindMember("hasClamp"))       rec.HasClamp = v->AsBoolOr(false);
				if (const Json::Value* v = item.FindMember("clampMin"))       rec.ClampMin = static_cast<float>(v->AsDoubleOr(0.0));
				if (const Json::Value* v = item.FindMember("clampMax"))       rec.ClampMax = static_cast<float>(v->AsDoubleOr(0.0));
				if (const Json::Value* v = item.FindMember("tooltip"))        rec.Tooltip = v->AsStringOr();
				if (const Json::Value* v = item.FindMember("headerContent"))  rec.HeaderContent = v->AsStringOr();
				if (const Json::Value* v = item.FindMember("headerSize"))     rec.HeaderSize = static_cast<int>(v->AsDoubleOr(0.0));
				if (const Json::Value* v = item.FindMember("hasSpace"))       rec.HasSpace = v->AsBoolOr(false);
				if (const Json::Value* v = item.FindMember("spaceHeight"))    rec.SpaceHeight = static_cast<float>(v->AsDoubleOr(0.0));

				if (rec.TypeTag.rfind("component:", 0) == 0) {
					rec.ComponentTypeName = rec.TypeTag.substr(10);
				}

				if (rec.TypeTag == "enum" || rec.TypeTag == "flagenum") {
					rec.Enum = std::make_shared<EnumDescriptor>();
					rec.Enum->IsFlags = (rec.TypeTag == "flagenum");
					if (const Json::Value* flagsValue = item.FindMember("enumIsFlags")) {
						rec.Enum->IsFlags = flagsValue->AsBoolOr(rec.Enum->IsFlags);
					}
					if (const Json::Value* opts = item.FindMember("enumOptions"); opts && opts->IsArray()) {
						for (const Json::Value& opt : opts->GetArray()) {
							if (!opt.IsObject()) continue;
							EnumOption option;
							if (const Json::Value* name = opt.FindMember("name"))   option.Name = name->AsStringOr();
							if (const Json::Value* value = opt.FindMember("value")) option.Value = static_cast<int64_t>(value->AsDoubleOr(0.0));
							rec.Enum->Options.push_back(std::move(option));
						}
					}
				}

				if (rec.DisplayName.empty()) rec.DisplayName = rec.Name;
				fields.push_back(std::move(rec));
			}
			return fields;
		}

		// One-shot: apply the new value of a script field to one entity. Used
		// inside the descriptor's Set lambda; runs once per entity in the
		// PropertyDrawer's selection span.
		void ApplyScriptFieldEdit(const Entity& entity, std::size_t scriptIndex,
			const std::string& className, const std::string& fieldName,
			const std::string& newValueStr, bool isPlaying)
		{
			if (!entity.HasComponent<ScriptComponent>()) return;
			auto& sc = const_cast<Entity&>(entity).GetComponent<ScriptComponent>();
			if (scriptIndex >= sc.Scripts.size()) return;
			const auto& inst = sc.Scripts[scriptIndex];
			if (inst.GetClassName() != className) return;

			auto& callbacks = ScriptEngine::GetCallbacks();
			if (inst.HasManagedInstance() && callbacks.SetScriptField) {
				callbacks.SetScriptField(static_cast<int32_t>(inst.GetGCHandle()),
					fieldName.c_str(), newValueStr.c_str());
			}
			if (!isPlaying) {
				sc.PendingFieldValues[className + "." + fieldName] = newValueStr;
			}
		}

		void ApplyManagedComponentFieldEdit(const Entity& entity,
			const std::string& className, const std::string& fieldName,
			const std::string& newValueStr)
		{
			if (!entity.HasComponent<ScriptComponent>()) return;
			auto& sc = const_cast<Entity&>(entity).GetComponent<ScriptComponent>();
			sc.PendingFieldValues[className + "." + fieldName] = newValueStr;
		}

		// Per-entity, per-field value cache. Built once per render call and
		// shared via shared_ptr by every descriptor's Get lambda so multi-
		// selection mixed-value detection sees real per-entity values
		// instead of the primary entity's value broadcast to everyone.
		// Key: uint32_t cast of EntityHandle (matches the codebase pattern
		// in AxiomPhysicsWorld2D for keying maps on entt::entity).
		using ScriptFieldValueMap = std::unordered_map<uint32_t, std::unordered_map<std::string, std::string>>;

		// Look up a field's value on a specific entity, falling back to the
		// class-default string if the entity didn't contribute its own
		// snapshot (e.g. the entity's ScriptComponent doesn't carry the
		// expected script in this slot).
		PropertyValue LookupPerEntityValue(const std::shared_ptr<ScriptFieldValueMap>& perEntity,
			const Entity& entity, const std::string& fieldName,
			const std::string& defaultValue, PropertyType type)
		{
			if (perEntity) {
				auto eIt = perEntity->find(static_cast<uint32_t>(entity.GetHandle()));
				if (eIt != perEntity->end()) {
					auto fIt = eIt->second.find(fieldName);
					if (fIt != eIt->second.end()) {
						return PropertyValue::FromString(type, fIt->second);
					}
				}
			}
			return PropertyValue::FromString(type, defaultValue);
		}

		// Build a PropertyDescriptor that bridges one [ShowInEditor] field to
		// the unified PropertyDrawer. The Get lambda reads the per-entity
		// value from the shared snapshot map (so multi-select sees mixed
		// values correctly). The Set lambda propagates the new value through
		// SetScriptField + PendingFieldValues for whichever entity the
		// drawer is iterating.
		PropertyDescriptor BuildScriptFieldDescriptor(EditorFieldRecord rec,
			const std::string& className, std::size_t scriptIndex,
			bool multiEditEnabled, bool isPlaying,
			std::shared_ptr<ScriptFieldValueMap> perEntityValues)
		{
			PropertyDescriptor d;
			d.Name = rec.Name;
			d.DisplayName = rec.DisplayName;
			d.Type = ResolveScriptFieldType(rec);
			d.Metadata.ReadOnly = rec.ReadOnly;
			d.Metadata.Tooltip = rec.Tooltip;
			d.Metadata.HeaderContent = rec.HeaderContent;
			d.Metadata.HeaderSize = rec.HeaderSize;
			d.Metadata.HasSpace = rec.HasSpace;
			d.Metadata.SpaceHeight = rec.SpaceHeight;
			d.Metadata.HasClamp = rec.HasClamp;
			d.Metadata.ClampMin = rec.ClampMin;
			d.Metadata.ClampMax = rec.ClampMax;
			d.Metadata.Enum = rec.Enum;
			d.Metadata.ComponentTypeName = rec.ComponentTypeName;

			d.Get = [perEntityValues, fieldName = rec.Name, defaultValue = rec.Value, type = d.Type]
				(const Entity& e) -> PropertyValue {
					return LookupPerEntityValue(perEntityValues, e, fieldName, defaultValue, type);
				};

			d.Set = [className, fieldName = rec.Name, scriptIndex, multiEditEnabled, isPlaying]
				(Entity& entity, const PropertyValue& v) {
					const std::string newValueStr = v.ToString();
					ApplyScriptFieldEdit(entity, scriptIndex, className, fieldName, newValueStr, isPlaying);
					(void)multiEditEnabled; // PropertyDrawer already iterates the span
				};
			return d;
		}

		PropertyDescriptor BuildManagedComponentFieldDescriptor(EditorFieldRecord rec,
			const std::string& className,
			std::shared_ptr<ScriptFieldValueMap> perEntityValues)
		{
			PropertyDescriptor d;
			d.Name = rec.Name;
			d.DisplayName = rec.DisplayName;
			d.Type = ResolveScriptFieldType(rec);
			d.Metadata.ReadOnly = rec.ReadOnly;
			d.Metadata.Tooltip = rec.Tooltip;
			d.Metadata.HeaderContent = rec.HeaderContent;
			d.Metadata.HeaderSize = rec.HeaderSize;
			d.Metadata.HasSpace = rec.HasSpace;
			d.Metadata.SpaceHeight = rec.SpaceHeight;
			d.Metadata.HasClamp = rec.HasClamp;
			d.Metadata.ClampMin = rec.ClampMin;
			d.Metadata.ClampMax = rec.ClampMax;
			d.Metadata.Enum = rec.Enum;
			d.Metadata.ComponentTypeName = rec.ComponentTypeName;

			d.Get = [perEntityValues, fieldName = rec.Name, defaultValue = rec.Value, type = d.Type]
				(const Entity& e) -> PropertyValue {
					return LookupPerEntityValue(perEntityValues, e, fieldName, defaultValue, type);
				};

			d.Set = [className, fieldName = rec.Name](Entity& entity, const PropertyValue& v) {
				ApplyManagedComponentFieldEdit(entity, className, fieldName, v.ToString());
			};
			return d;
		}

		std::string MakeFieldKey(std::size_t scriptIndex, const std::string& className,
			const std::string& fieldName)
		{
			return className + "#" + std::to_string(scriptIndex) + "." + fieldName;
		}

		void RenderScriptFieldsForInstance(ScriptComponent& sc,
			const ScriptInstance& instance, std::size_t scriptIndex,
			std::span<const Entity> entities, bool multiEditEnabled)
		{
			auto& callbacks = ScriptEngine::GetCallbacks();
			const bool isPlaying = Application::GetIsPlaying();
			const bool hasLiveInstance = instance.HasManagedInstance();
			const std::string& className = instance.GetClassName();

			// Reference records: provide field metadata (display name, type,
			// clamp, enum options, ...) and the class-default values used as
			// fallback when an entity hasn't contributed its own snapshot.
			// Sourced from the primary instance's live JSON if possible,
			// otherwise from the class's field defs.
			const char* json = nullptr;
			if (hasLiveInstance && callbacks.GetScriptFields) {
				json = callbacks.GetScriptFields(static_cast<int32_t>(instance.GetGCHandle()));
			}
			else if (callbacks.GetClassFieldDefs) {
				json = callbacks.GetClassFieldDefs(className.c_str());
			}
			if (!json || !*json || (json[0] == '[' && json[1] == ']')) return;

			auto records = ParseEditorFields(json);
			if (records.empty()) return;

			// Patch primary's reference records with primary's PendingFieldValues
			// so the class-default fallback used for entities-without-snapshots
			// matches what the user authored on the primary in editor mode.
			if (!hasLiveInstance) {
				for (auto& rec : records) {
					const std::string key = className + "." + rec.Name;
					auto it = sc.PendingFieldValues.find(key);
					if (it != sc.PendingFieldValues.end()) rec.Value = it->second;
				}
			}

			// Per-entity snapshot. Walk every selected entity and capture
			// THIS entity's value for every field, so PropertyDrawer's
			// SampleUniform sees true per-entity values across the
			// selection. GetScriptFields returns a static buffer that may
			// be reused on the next call, so each entity is fully parsed
			// before moving on.
			auto perEntityValues = std::make_shared<ScriptFieldValueMap>();
			for (const Entity& e : entities) {
				if (!e.HasComponent<ScriptComponent>()) continue;
				const auto& eSc = e.GetComponent<ScriptComponent>();
				if (scriptIndex >= eSc.Scripts.size()) continue;
				const auto& eInst = eSc.Scripts[scriptIndex];
				if (eInst.GetClassName() != className) continue;

				auto& bucket = (*perEntityValues)[static_cast<uint32_t>(e.GetHandle())];
				if (eInst.HasManagedInstance() && callbacks.GetScriptFields) {
					const char* eJson = callbacks.GetScriptFields(static_cast<int32_t>(eInst.GetGCHandle()));
					if (eJson && *eJson && !(eJson[0] == '[' && eJson[1] == ']')) {
						auto eRecords = ParseEditorFields(eJson);
						for (auto& r : eRecords) {
							bucket[r.Name] = std::move(r.Value);
						}
					}
				}
				else {
					// Pre-play: pull this entity's per-field overrides from
					// its own PendingFieldValues, with the class default as
					// fallback (rec.Value here is already class-default
					// because hasLiveInstance was false above).
					for (const auto& r : records) {
						const std::string key = className + "." + r.Name;
						auto it = eSc.PendingFieldValues.find(key);
						bucket[r.Name] = (it != eSc.PendingFieldValues.end()) ? it->second : r.Value;
					}
				}
			}

			ImGui::Indent(8.0f);
			for (auto& rec : records) {
				const std::string fieldKey = MakeFieldKey(scriptIndex, className, rec.Name);
				PropertyDescriptor descriptor = BuildScriptFieldDescriptor(
					rec, className, scriptIndex, multiEditEnabled, isPlaying, perEntityValues);
				PropertyDrawer::Draw(entities, descriptor, fieldKey);
			}
			ImGui::Unindent(8.0f);
		}

		void RenderFieldsForManagedComponent(ScriptComponent& sc,
			const std::string& className, std::size_t componentIndex,
			std::span<const Entity> entities, bool /*multiEditEnabled*/)
		{
			auto& callbacks = ScriptEngine::GetCallbacks();
			if (!callbacks.GetClassFieldDefs) return;

			const char* json = callbacks.GetClassFieldDefs(className.c_str());
			if (!json || !*json || (json[0] == '[' && json[1] == ']')) return;

			auto records = ParseEditorFields(json);
			if (records.empty()) return;

			// Patch primary's reference records with primary's PendingFieldValues
			// so the descriptor's class-default fallback matches the primary
			// entity's authored value.
			for (auto& rec : records) {
				const std::string key = className + "." + rec.Name;
				auto it = sc.PendingFieldValues.find(key);
				if (it != sc.PendingFieldValues.end()) rec.Value = it->second;
			}

			// Managed components have no per-entity managed instance the
			// editor can read from in edit mode — values live in each
			// entity's own PendingFieldValues. Build the per-entity
			// snapshot from there so multi-select sees true mixed state.
			auto perEntityValues = std::make_shared<ScriptFieldValueMap>();
			for (const Entity& e : entities) {
				if (!e.HasComponent<ScriptComponent>()) continue;
				const auto& eSc = e.GetComponent<ScriptComponent>();
				if (!eSc.HasManagedComponent(className)) continue;

				auto& bucket = (*perEntityValues)[static_cast<uint32_t>(e.GetHandle())];
				for (const auto& r : records) {
					const std::string key = className + "." + r.Name;
					auto it = eSc.PendingFieldValues.find(key);
					bucket[r.Name] = (it != eSc.PendingFieldValues.end()) ? it->second : r.Value;
				}
			}

			ImGui::Indent(8.0f);
			for (auto& rec : records) {
				const std::string fieldKey = className + "#component" + std::to_string(componentIndex) + "." + rec.Name;
				PropertyDescriptor descriptor = BuildManagedComponentFieldDescriptor(rec, className, perEntityValues);
				PropertyDrawer::Draw(entities, descriptor, fieldKey);
			}
			ImGui::Unindent(8.0f);
		}

	} // namespace

	namespace {
		using ScriptPickerEntry = EditorScriptDiscovery::ScriptEntry;

		bool s_ScriptPickerOpen = false;
		char s_ScriptPickerSearch[128] = {};
		std::vector<ScriptPickerEntry> s_ScriptPickerEntries;
		EntityHandle s_ScriptPickerTargetEntity = entt::null;

		void OpenScriptPicker(EntityHandle entity) {
			s_ScriptPickerOpen = true;
			s_ScriptPickerSearch[0] = '\0';
			s_ScriptPickerTargetEntity = entity;
			s_ScriptPickerEntries.clear();
			EditorScriptDiscovery::CollectProjectScriptEntries(s_ScriptPickerEntries);
		}

		void RenderScriptPicker() {
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

		// Two ScriptComponents have the same "shape" if they list the same
		// managed components and the same script class names in the same order.
		// Multi-edit script-field propagation is only safe when the shape
		// matches across the entire selection — otherwise the script index
		// `i` doesn't refer to the same script on every entity.
		bool ScriptComponentShapeMatches(const ScriptComponent& a, const ScriptComponent& b) {
			if (a.ManagedComponents.size() != b.ManagedComponents.size()) return false;
			if (a.Scripts.size() != b.Scripts.size()) return false;
			for (std::size_t i = 0; i < a.ManagedComponents.size(); ++i) {
				if (a.ManagedComponents[i] != b.ManagedComponents[i]) return false;
			}
			for (std::size_t i = 0; i < a.Scripts.size(); ++i) {
				if (a.Scripts[i].GetClassName() != b.Scripts[i].GetClassName()) return false;
				if (a.Scripts[i].GetType() != b.Scripts[i].GetType()) return false;
			}
			return true;
		}
	}

	void DrawScriptComponentInspector(std::span<const Entity> entities)
	{
		if (entities.empty()) return;

		// The inspector dispatcher can hand us a selection that doesn't
		// uniformly carry ScriptComponent (e.g. mid-edit add/remove,
		// stale selection after undo). Bail / skip rather than blowing
		// up inside EnTT's view assertion.
		const Entity& primary = entities[0];
		if (!primary.HasComponent<ScriptComponent>()) {
			return;
		}
		auto& scriptComp = const_cast<Entity&>(primary).GetComponent<ScriptComponent>();
		bool shapeUniform = true;
		for (std::size_t i = 1; i < entities.size(); ++i) {
			if (!entities[i].HasComponent<ScriptComponent>()) {
				shapeUniform = false;
				continue;
			}
			const auto& other = entities[i].GetComponent<ScriptComponent>();
			if (!ScriptComponentShapeMatches(scriptComp, other)) {
				shapeUniform = false;
				break;
			}
		}
		if (entities.size() > 1 && !shapeUniform) {
			ImGui::TextDisabled("Multi-edit limited - script set differs across selection");
		}
		const bool multiEditEnabled = shapeUniform;
		const std::span<const Entity> propagationSpan = multiEditEnabled
			? entities
			: std::span<const Entity>(&primary, 1);

		size_t removeIndex = SIZE_MAX;
		size_t removeManagedComponentIndex = SIZE_MAX;

		for (size_t i = 0; i < scriptComp.ManagedComponents.size(); i++) {
			const std::string& className = scriptComp.ManagedComponents[i];
			std::string label = className.empty() ? "C# Component" : className + " (C# Component)";

			ImGui::PushID(("managed-component-" + std::to_string(i)).c_str());

			bool removeRequested = false;
			bool open = ImGuiUtils::BeginComponentSection(label.c_str(), removeRequested);
			if (removeRequested) removeManagedComponentIndex = i;

			if (open) {
				if (ScriptEngine::IsInitialized()) {
					RenderFieldsForManagedComponent(scriptComp, className, i, propagationSpan, multiEditEnabled);
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
			if (instance.GetType() == ScriptType::Native) label += " (Native)";

			ImGui::PushID(static_cast<int>(i));

			bool removeRequested = false;
			bool open = ImGuiUtils::BeginComponentSection(label.c_str(), removeRequested);
			if (removeRequested) removeIndex = i;

			if (open) {
				ImGuiUtils::DrawInspectorValue("Script", [&label, &instance](const char* id) {
					ImGui::Button((label + id).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Double-click to open script");
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							std::vector<EditorScriptDiscovery::ScriptEntry> entries;
							EditorScriptDiscovery::CollectProjectScriptEntries(entries);
							for (const auto& entry : entries) {
								if (entry.ClassName == instance.GetClassName() && entry.Type == instance.GetType()) {
									ExternalEditor::OpenFile(entry.Path.string());
									break;
								}
							}
						}
					}
				});

				if (instance.GetType() != ScriptType::Native && ScriptEngine::IsInitialized()) {
					RenderScriptFieldsForInstance(scriptComp, instance, i, propagationSpan, multiEditEnabled);
				}
				ImGuiUtils::EndComponentSection();
			}
			ImGui::PopID();
		}

		if (removeIndex != SIZE_MAX) {
			if (multiEditEnabled) {
				for (const Entity& e : entities) {
					ScriptSystem::RemoveScript(const_cast<Entity&>(e), removeIndex);
				}
			}
			else {
				ScriptSystem::RemoveScript(const_cast<Entity&>(primary), removeIndex);
			}
		}
		if (removeManagedComponentIndex != SIZE_MAX && removeManagedComponentIndex < scriptComp.ManagedComponents.size()) {
			const std::string className = scriptComp.ManagedComponents[removeManagedComponentIndex];
			if (multiEditEnabled) {
				for (const Entity& e : entities) {
					auto& sc = const_cast<Entity&>(e).GetComponent<ScriptComponent>();
					if (sc.RemoveManagedComponent(className)) {
						if (Scene* scene = const_cast<Entity&>(e).GetScene()) scene->MarkDirty();
					}
				}
			}
			else {
				if (scriptComp.RemoveManagedComponent(className)) {
					if (Scene* scene = const_cast<Entity&>(primary).GetScene()) scene->MarkDirty();
				}
			}
		}

		RenderScriptPicker();
		// NOTE: ReferencePicker::RenderPopup() is intentionally NOT called
		// here. The script inspector always runs inside one of the outer
		// inspector loops (ImGuiEditorLayer / PrefabInspector), and those
		// loops render the popup once after iterating every component.
		// Calling RenderPopup here too would render the same popup twice
		// per frame — duplicating Selectable IDs and triggering ImGui's
		// "conflicting ID" assertion.
	}

} // namespace Axiom
