#include "pch.hpp"
#include "Inspector/PropertyDrawer.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Gui/HierarchyDragData.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/PropertyType.hpp"
#include "Inspector/PropertyValue.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Axiom::PropertyDrawer {

	namespace {

		constexpr int kMixedValueFlag = 1 << 12; // ImGuiItemFlags_MixedValue (internal)

		// Snapshot the current value across the selection. Returns true iff
		// every entity holds the same PropertyValue. Either way, `outValue`
		// is set to the first entity's value so the widget always has
		// something to render.
		bool SampleUniform(std::span<const Entity> entities, const PropertyDescriptor& d,
			PropertyValue& outValue)
		{
			if (entities.empty()) {
				outValue = PropertyValue{};
				outValue.Type = d.Type;
				return true;
			}
			outValue = d.Get(entities[0]);
			for (std::size_t i = 1; i < entities.size(); ++i) {
				const PropertyValue other = d.Get(entities[i]);
				if (!(other == outValue)) return false;
			}
			return true;
		}

		// For per-channel float vectors: returns the sampled values + a
		// per-channel mixed mask.
		template <std::size_t N>
		void SampleFloatChannels(std::span<const Entity> entities,
			const PropertyDescriptor& d,
			std::array<float, N>& outValues, std::array<bool, N>& mixedMask)
		{
			for (std::size_t c = 0; c < N; ++c) mixedMask[c] = false;
			if (entities.empty()) {
				for (std::size_t c = 0; c < N; ++c) outValues[c] = 0.0f;
				return;
			}
			const PropertyValue first = d.Get(entities[0]);
			for (std::size_t c = 0; c < N; ++c) outValues[c] = first.FloatVec[c];
			for (std::size_t i = 1; i < entities.size(); ++i) {
				const PropertyValue v = d.Get(entities[i]);
				for (std::size_t c = 0; c < N; ++c) {
					if (!mixedMask[c] && v.FloatVec[c] != outValues[c]) {
						mixedMask[c] = true;
					}
				}
			}
		}

		template <std::size_t N>
		void SampleIntChannels(std::span<const Entity> entities,
			const PropertyDescriptor& d,
			std::array<int32_t, N>& outValues, std::array<bool, N>& mixedMask)
		{
			for (std::size_t c = 0; c < N; ++c) mixedMask[c] = false;
			if (entities.empty()) {
				for (std::size_t c = 0; c < N; ++c) outValues[c] = 0;
				return;
			}
			const PropertyValue first = d.Get(entities[0]);
			for (std::size_t c = 0; c < N; ++c) outValues[c] = first.IntVec[c];
			for (std::size_t i = 1; i < entities.size(); ++i) {
				const PropertyValue v = d.Get(entities[i]);
				for (std::size_t c = 0; c < N; ++c) {
					if (!mixedMask[c] && v.IntVec[c] != outValues[c]) {
						mixedMask[c] = true;
					}
				}
			}
		}

		// Centralised post-edit hook so every code path through this
		// translation unit marks the scene dirty, regardless of which
		// component the descriptor belongs to (built-in, script-exposed
		// field, managed component, or package component using
		// Properties::Make). MarkDirty itself is gated on play state.
		inline void MarkSceneDirty(const Entity& entity) {
			if (Scene* scene = const_cast<Entity&>(entity).GetScene()) {
				scene->MarkDirty();
			}
		}

		// Write a value to every entity in the span via the descriptor.
		void WriteAll(std::span<const Entity> entities, const PropertyDescriptor& d,
			const PropertyValue& value)
		{
			for (const Entity& e : entities) {
				d.Set(const_cast<Entity&>(e), value);
				MarkSceneDirty(e);
			}
		}

		// Per-channel write helpers: leave untouched channels alone on each
		// entity. Used by the vector / colour drawers so that editing only
		// the Y of a vec3 doesn't blast every entity's X and Z to the
		// primary entity's values.
		void WriteFloatChannel(std::span<const Entity> entities,
			const PropertyDescriptor& d, std::size_t channel, float newValue)
		{
			for (const Entity& e : entities) {
				PropertyValue current = d.Get(e);
				current.FloatVec[channel] = newValue;
				d.Set(const_cast<Entity&>(e), current);
				MarkSceneDirty(e);
			}
		}
		void WriteIntChannel(std::span<const Entity> entities,
			const PropertyDescriptor& d, std::size_t channel, int32_t newValue)
		{
			for (const Entity& e : entities) {
				PropertyValue current = d.Get(e);
				current.IntVec[channel] = newValue;
				d.Set(const_cast<Entity&>(e), current);
				MarkSceneDirty(e);
			}
		}

		// Right-click "Clear" context menu for reference rows. Returns true
		// if the user picked Clear; the caller then writes a null reference.
		bool DrawReferenceContextMenu(const char* popupId) {
			bool cleared = false;
			if (ImGui::BeginPopupContextItem(popupId)) {
				if (ImGui::MenuItem("Clear")) cleared = true;
				ImGui::EndPopup();
			}
			return cleared;
		}

		uint64_t ResolveDroppedAssetId(const ImGuiPayload* payload) {
			if (!payload || !payload->Data || payload->DataSize <= 0) {
				return 0;
			}

			const char* data = static_cast<const char*>(payload->Data);
			std::string droppedPath(data, static_cast<std::size_t>(payload->DataSize));
			if (!droppedPath.empty() && droppedPath.back() == '\0') {
				droppedPath.pop_back();
			}
			if (droppedPath.empty()) {
				return 0;
			}

			const uint64_t assetId = AssetRegistry::GetOrCreateAssetUUID(droppedPath);
			if (assetId != 0) {
				return assetId;
			}

			AssetRegistry::MarkDirty();
			AssetRegistry::Sync();
			return AssetRegistry::GetOrCreateAssetUUID(droppedPath);
		}

		// ── Per-type drawers ─────────────────────────────────────────

		bool DrawBool(std::span<const Entity> entities, const PropertyDescriptor& d) {
			PropertyValue v;
			const bool uniform = SampleUniform(entities, d, v);
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			if (!uniform) ImGui::PushItemFlag(static_cast<ImGuiItemFlags>(kMixedValueFlag), true);
			bool tmp = v.BoolValue;
			const bool changed = ImGui::Checkbox("##Value", &tmp);
			if (!uniform) ImGui::PopItemFlag();
			ImGui::PopID();
			if (changed) {
				PropertyValue out;
				out.Type = PropertyType::Bool;
				out.BoolValue = tmp;
				WriteAll(entities, d, out);
			}
			return changed;
		}

		bool DrawIntScalar(std::span<const Entity> entities, const PropertyDescriptor& d,
			int valueMin, int valueMax)
		{
			PropertyValue v;
			const bool uniform = SampleUniform(entities, d, v);
			int tmp = static_cast<int>(v.IntValue);
			const int clampMin = d.Metadata.HasClamp
				? std::max(static_cast<int>(d.Metadata.ClampMin), valueMin)
				: valueMin;
			const int clampMax = d.Metadata.HasClamp
				? std::min(static_cast<int>(d.Metadata.ClampMax), valueMax)
				: valueMax;
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			const float speed = d.Metadata.DragSpeed > 0 ? d.Metadata.DragSpeed : 1.0f;
			const bool changed = ImGui::DragInt("##Value", &tmp, speed, clampMin, clampMax,
				uniform ? "%d" : "-");
			ImGui::PopID();
			if (changed) {
				if (tmp < clampMin) tmp = clampMin;
				if (tmp > clampMax) tmp = clampMax;
				PropertyValue out;
				out.Type = d.Type;
				out.IntValue = tmp;
				WriteAll(entities, d, out);
			}
			return changed;
		}

		bool DrawUIntScalar(std::span<const Entity> entities, const PropertyDescriptor& d,
			uint32_t valueMax)
		{
			PropertyValue v;
			const bool uniform = SampleUniform(entities, d, v);
			int tmp = static_cast<int>(std::min<uint64_t>(v.UIntValue, INT_MAX));
			const int clampMin = d.Metadata.HasClamp
				? std::max(static_cast<int>(d.Metadata.ClampMin), 0)
				: 0;
			const int clampMax = d.Metadata.HasClamp
				? std::min(static_cast<int>(d.Metadata.ClampMax), static_cast<int>(std::min<uint32_t>(valueMax, INT_MAX)))
				: static_cast<int>(std::min<uint32_t>(valueMax, INT_MAX));
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			const float speed = d.Metadata.DragSpeed > 0 ? d.Metadata.DragSpeed : 1.0f;
			const bool changed = ImGui::DragInt("##Value", &tmp, speed, clampMin, clampMax,
				uniform ? "%d" : "-");
			ImGui::PopID();
			if (changed) {
				if (tmp < clampMin) tmp = clampMin;
				if (tmp > clampMax) tmp = clampMax;
				PropertyValue out;
				out.Type = d.Type;
				out.UIntValue = static_cast<uint64_t>(tmp);
				WriteAll(entities, d, out);
			}
			return changed;
		}

		bool DrawScalar64(std::span<const Entity> entities, const PropertyDescriptor& d) {
			PropertyValue v;
			const bool uniform = SampleUniform(entities, d, v);
			char buf[64];
			if (d.Type == PropertyType::Int64) {
				if (uniform) std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.IntValue));
				else buf[0] = '\0';
			}
			else { // UInt64
				if (uniform) std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v.UIntValue));
				else buf[0] = '\0';
			}
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			const bool committed = uniform
				? ImGui::InputText("##Value", buf, sizeof(buf), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue)
				: ImGui::InputTextWithHint("##Value", "-", buf, sizeof(buf), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::PopID();
			if (committed && buf[0] != '\0') {
				PropertyValue out;
				out.Type = d.Type;
				if (d.Type == PropertyType::Int64) out.IntValue = std::strtoll(buf, nullptr, 10);
				else out.UIntValue = std::strtoull(buf, nullptr, 10);
				WriteAll(entities, d, out);
				return true;
			}
			return false;
		}

		bool DrawFloat(std::span<const Entity> entities, const PropertyDescriptor& d) {
			PropertyValue v;
			const bool uniform = SampleUniform(entities, d, v);
			float tmp = static_cast<float>(v.FloatValue);
			const float clampMin = d.Metadata.HasClamp ? static_cast<float>(d.Metadata.ClampMin) : 0.0f;
			const float clampMax = d.Metadata.HasClamp ? static_cast<float>(d.Metadata.ClampMax) : 0.0f;
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			const float speed = d.Metadata.DragSpeed > 0 ? d.Metadata.DragSpeed : 0.1f;
			const bool changed = ImGui::DragFloat("##Value", &tmp, speed, clampMin, clampMax,
				uniform ? "%.3f" : "-");
			ImGui::PopID();
			if (changed) {
				PropertyValue out;
				out.Type = d.Type;
				out.FloatValue = static_cast<double>(tmp);
				WriteAll(entities, d, out);
			}
			return changed;
		}

		bool DrawDouble(std::span<const Entity> entities, const PropertyDescriptor& d) {
			PropertyValue v;
			const bool uniform = SampleUniform(entities, d, v);
			double tmp = v.FloatValue;
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			const float speed = d.Metadata.DragSpeed > 0 ? d.Metadata.DragSpeed : 0.1f;
			const bool changed = ImGui::DragScalar("##Value", ImGuiDataType_Double, &tmp,
				speed, nullptr, nullptr, uniform ? "%.6f" : "-");
			ImGui::PopID();
			if (changed) {
				PropertyValue out;
				out.Type = PropertyType::Double;
				out.FloatValue = tmp;
				WriteAll(entities, d, out);
			}
			return changed;
		}

		bool DrawString(std::span<const Entity> entities, const PropertyDescriptor& d) {
			PropertyValue v;
			const bool uniform = SampleUniform(entities, d, v);
			char buf[256]{};
			if (uniform) std::snprintf(buf, sizeof(buf), "%s", v.StringValue.c_str());
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			const bool changed = uniform
				? ImGui::InputText("##Value", buf, sizeof(buf))
				: ImGui::InputTextWithHint("##Value", "-", buf, sizeof(buf));
			ImGui::PopID();
			if (changed) {
				PropertyValue out;
				out.Type = PropertyType::String;
				out.StringValue = buf;
				WriteAll(entities, d, out);
			}
			return changed;
		}

		template <std::size_t N>
		bool DrawFloatVec(std::span<const Entity> entities, const PropertyDescriptor& d) {
			std::array<float, N> values{};
			std::array<bool, N> mixed{};
			SampleFloatChannels<N>(entities, d, values, mixed);
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			const float speed = d.Metadata.DragSpeed > 0 ? d.Metadata.DragSpeed : 0.1f;
			const bool any = ImGuiUtils::MultiEdit::MultiItemRow(static_cast<int>(N), [&](int c) -> bool {
				ImGui::PushID(c);
				float channel = values[c];
				const char* fmt = mixed[c] ? "-" : "%.3f";
				const bool changed = ImGui::DragFloat("##c", &channel, speed, 0.0f, 0.0f, fmt);
				if (changed) WriteFloatChannel(entities, d, static_cast<std::size_t>(c), channel);
				ImGui::PopID();
				return changed;
			});
			ImGui::PopID();
			return any;
		}

		template <std::size_t N>
		bool DrawIntVec(std::span<const Entity> entities, const PropertyDescriptor& d) {
			std::array<int32_t, N> values{};
			std::array<bool, N> mixed{};
			SampleIntChannels<N>(entities, d, values, mixed);
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			const float speed = d.Metadata.DragSpeed > 0 ? d.Metadata.DragSpeed : 1.0f;
			const bool any = ImGuiUtils::MultiEdit::MultiItemRow(static_cast<int>(N), [&](int c) -> bool {
				ImGui::PushID(c);
				int channel = values[c];
				const char* fmt = mixed[c] ? "-" : "%d";
				const bool changed = ImGui::DragInt("##c", &channel, speed, 0, 0, fmt);
				if (changed) WriteIntChannel(entities, d, static_cast<std::size_t>(c), channel);
				ImGui::PopID();
				return changed;
			});
			ImGui::PopID();
			return any;
		}

		bool DrawColor(std::span<const Entity> entities, const PropertyDescriptor& d) {
			std::array<float, 4> values{};
			std::array<bool, 4> mixed{};
			SampleFloatChannels<4>(entities, d, values, mixed);
			const bool anyMixed = mixed[0] || mixed[1] || mixed[2] || mixed[3];
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			bool anyChanged = false;
			if (!anyMixed) {
				float editVals[4] = { values[0], values[1], values[2], values[3] };
				if (ImGui::ColorEdit4("##Value", editVals)) {
					for (int c = 0; c < 4; ++c) {
						if (editVals[c] != values[c]) {
							WriteFloatChannel(entities, d, static_cast<std::size_t>(c), editVals[c]);
							anyChanged = true;
						}
					}
				}
			}
			else {
				anyChanged = ImGuiUtils::MultiEdit::MultiItemRow(4, [&](int c) -> bool {
					ImGui::PushID(c);
					float channel = values[c];
					const char* fmt = mixed[c] ? "-" : "%.3f";
					const bool changed = ImGui::DragFloat("##c", &channel, 0.005f, 0.0f, 1.0f, fmt);
					if (changed) WriteFloatChannel(entities, d, static_cast<std::size_t>(c), channel);
					ImGui::PopID();
					return changed;
				});
			}
			ImGui::PopID();
			return anyChanged;
		}

		bool DrawEnum(std::span<const Entity> entities, const PropertyDescriptor& d) {
			if (!d.Metadata.Enum || d.Metadata.Enum->Options.empty()) return false;
			PropertyValue v;
			const bool uniform = SampleUniform(entities, d, v);

			const char* preview = "-";
			std::string previewBuf;
			if (uniform) {
				for (const auto& opt : d.Metadata.Enum->Options) {
					if (opt.Value == v.IntValue) {
						previewBuf = opt.Name;
						preview = previewBuf.c_str();
						break;
					}
				}
				if (previewBuf.empty()) preview = "Unknown";
			}

			bool changed = false;
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			if (ImGui::BeginCombo("##Value", preview)) {
				for (const auto& opt : d.Metadata.Enum->Options) {
					const bool isSelected = uniform && (v.IntValue == opt.Value);
					if (ImGui::Selectable(opt.Name.c_str(), isSelected)) {
						PropertyValue out;
						out.Type = PropertyType::Enum;
						out.IntValue = opt.Value;
						WriteAll(entities, d, out);
						changed = true;
					}
					if (isSelected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
			return changed;
		}

		bool DrawFlagEnum(std::span<const Entity> entities, const PropertyDescriptor& d) {
			if (!d.Metadata.Enum || d.Metadata.Enum->Options.empty()) return false;

			// Per-flag uniformity. For each option bit, sample whether it's
			// set on entities[0] and whether every other entity matches.
			std::vector<bool> bitOn(d.Metadata.Enum->Options.size(), false);
			std::vector<bool> bitMixed(d.Metadata.Enum->Options.size(), false);
			int64_t sampleAll = 0;
			if (!entities.empty()) {
				const PropertyValue first = d.Get(entities[0]);
				sampleAll = first.IntValue;
				for (std::size_t b = 0; b < d.Metadata.Enum->Options.size(); ++b) {
					const int64_t bit = d.Metadata.Enum->Options[b].Value;
					if (bit == 0) continue;
					bitOn[b] = (first.IntValue & bit) == bit;
				}
				for (std::size_t i = 1; i < entities.size(); ++i) {
					const PropertyValue v = d.Get(entities[i]);
					for (std::size_t b = 0; b < d.Metadata.Enum->Options.size(); ++b) {
						if (bitMixed[b]) continue;
						const int64_t bit = d.Metadata.Enum->Options[b].Value;
						if (bit == 0) continue;
						const bool on = (v.IntValue & bit) == bit;
						if (on != bitOn[b]) bitMixed[b] = true;
					}
				}
			}

			// Build the preview string (combo button label).
			std::string preview;
			bool anyMixed = false;
			for (std::size_t b = 0; b < d.Metadata.Enum->Options.size(); ++b) {
				if (bitMixed[b]) { anyMixed = true; break; }
			}
			if (anyMixed) {
				preview = "-";
			}
			else {
				for (std::size_t b = 0; b < d.Metadata.Enum->Options.size(); ++b) {
					if (d.Metadata.Enum->Options[b].Value == 0) continue;
					if (bitOn[b]) {
						if (!preview.empty()) preview += ", ";
						preview += d.Metadata.Enum->Options[b].Name;
					}
				}
				if (preview.empty()) preview = "(None)";
			}

			bool changed = false;
			ImGui::PushID(d.Name.c_str());
			ImGuiUtils::BeginInspectorFieldRow(d.DisplayName.c_str());
			if (ImGui::BeginCombo("##Value", preview.c_str())) {
				for (std::size_t b = 0; b < d.Metadata.Enum->Options.size(); ++b) {
					const auto& opt = d.Metadata.Enum->Options[b];
					if (opt.Value == 0) continue;
					ImGui::PushID(static_cast<int>(b));
					if (bitMixed[b]) ImGui::PushItemFlag(static_cast<ImGuiItemFlags>(kMixedValueFlag), true);
					bool tmp = bitOn[b];
					if (ImGui::Checkbox(opt.Name.c_str(), &tmp)) {
						// Per-entity bit toggle so individual entities keep
						// the bits we didn't touch this frame.
						for (const Entity& e : entities) {
							PropertyValue current = d.Get(e);
							if (tmp) current.IntValue |= opt.Value;
							else     current.IntValue &= ~opt.Value;
							d.Set(const_cast<Entity&>(e), current);
							MarkSceneDirty(e);
						}
						changed = true;
					}
					if (bitMixed[b]) ImGui::PopItemFlag();
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
			(void)sampleAll;
			return changed;
		}

		// Generic reference-type drawer. `displayResolver` produces the
		// button text and tooltip; `pickerOpener` builds the picker entries
		// and opens the popup; `payloadAccept` handles drag-drop targets.
		template <typename DisplayResolver, typename PickerOpener, typename PayloadAccept>
		bool DrawReference(std::span<const Entity> entities, const PropertyDescriptor& d,
			const std::string& fieldKey, PropertyType valueType,
			DisplayResolver&& displayResolver, PickerOpener&& pickerOpener,
			PayloadAccept&& payloadAccept)
		{
			PropertyValue v;
			const bool uniform = SampleUniform(entities, d, v);
			bool missing = false;
			std::string secondary;
			std::string display = uniform ? displayResolver(v, missing, secondary)
				: std::string("\xe2\x80\x94");

			bool hovered = false;
			const bool clicked = ReferencePicker::DrawReferenceField(
				d.DisplayName.c_str(), display, secondary, missing, !uniform, hovered);

			bool changed = false;
			if (clicked) {
				pickerOpener(fieldKey);
			}
			if (DrawReferenceContextMenu("##RefCtx")) {
				PropertyValue cleared;
				cleared.Type = valueType;
				WriteAll(entities, d, cleared);
				changed = true;
			}
			if (ImGui::BeginDragDropTarget()) {
				PropertyValue dropped;
				dropped.Type = valueType;
				if (payloadAccept(dropped)) {
					WriteAll(entities, d, dropped);
					changed = true;
				}
				ImGui::EndDragDropTarget();
			}
			if (auto pending = ReferencePicker::ConsumeSelection(fieldKey); pending) {
				PropertyValue out = PropertyValue::FromString(valueType, *pending);
				WriteAll(entities, d, out);
				changed = true;
			}
			(void)hovered;
			return changed;
		}

		bool DrawAssetRefByKind(std::span<const Entity> entities, const PropertyDescriptor& d,
			const std::string& fieldKey, AssetKind kind, PropertyType valueType,
			const char* pickerTitle)
		{
			// Texture pickers get the thumbnail-grid layout (matching the
			// SpriteRenderer's existing browser); other asset kinds keep the
			// plain selectable list. The list contents are identical — only
			// the visual layout differs.
			const ReferencePicker::Style style = (kind == AssetKind::Texture)
				? ReferencePicker::Style::Thumbnails
				: ReferencePicker::Style::Plain;

			return DrawReference(entities, d, fieldKey, valueType,
				[kind](const PropertyValue& v, bool& missing, std::string& secondary) {
					return ReferencePicker::ResolveAssetDisplay(v.UIntValue, kind, missing, &secondary);
				},
				[kind, pickerTitle, style](const std::string& key) {
					ReferencePicker::OpenForFieldKey(key, pickerTitle,
						ReferencePicker::CollectAssetsByKind(kind), style);
				},
				[kind](PropertyValue& outValue) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
						const uint64_t assetId = ResolveDroppedAssetId(payload);
						if (assetId != 0 && AssetRegistry::GetKind(assetId) == kind) {
							outValue.UIntValue = assetId;
							return true;
						}
					}
					return false;
				});
		}

		bool DrawEntityRef(std::span<const Entity> entities, const PropertyDescriptor& d,
			const std::string& fieldKey)
		{
			return DrawReference(entities, d, fieldKey, PropertyType::EntityRef,
				[](const PropertyValue& v, bool& missing, std::string& secondary) {
					if (v.StringValue == "prefab") {
						return ReferencePicker::ResolvePrefabDisplay(v.UIntValue, missing, &secondary);
					}
					return ReferencePicker::ResolveEntityDisplay(v.UIntValue, missing, &secondary);
				},
				[](const std::string& key) {
					ReferencePicker::OpenForFieldKey(key, "Select Entity",
						ReferencePicker::CollectEntities());
				},
				[](PropertyValue& outValue) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
						if (payload->DataSize == sizeof(HierarchyDragData)) {
							const auto* data = static_cast<const HierarchyDragData*>(payload->Data);
							const EntityHandle handle = static_cast<EntityHandle>(data->EntityHandle);
							const Scene* scene = SceneManager::Get().GetActiveScene();
							if (scene && scene->IsValid(handle)) {
								outValue.UIntValue = scene->GetRuntimeID(handle);
								outValue.StringValue.clear();
								return outValue.UIntValue != 0;
							}
						}
					}
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
						const uint64_t assetId = ResolveDroppedAssetId(payload);
						if (assetId != 0 && AssetRegistry::GetKind(assetId) == AssetKind::Prefab) {
							outValue.UIntValue = assetId;
							outValue.StringValue = "prefab";
							return true;
						}
					}
					return false;
				});
		}

		bool DrawComponentRef(std::span<const Entity> entities, const PropertyDescriptor& d,
			const std::string& fieldKey)
		{
			const std::string& componentTypeName = d.Metadata.ComponentTypeName;
			return DrawReference(entities, d, fieldKey, PropertyType::ComponentRef,
				[&componentTypeName](const PropertyValue& v, bool& missing, std::string& secondary) {
					return ReferencePicker::ResolveComponentRefDisplay(v.UIntValue,
						componentTypeName, missing, &secondary);
				},
				[&componentTypeName](const std::string& key) {
					ReferencePicker::OpenForFieldKey(key, "Select " + componentTypeName,
						ReferencePicker::CollectComponentTargets(componentTypeName));
				},
				[&componentTypeName](PropertyValue& outValue) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMPONENT_REF")) {
						std::string refStr(static_cast<const char*>(payload->Data));
						const std::size_t sep = refStr.find(':');
						if (sep != std::string::npos) {
							const std::string typeName = refStr.substr(sep + 1);
							if (typeName == componentTypeName) {
								outValue.UIntValue = std::strtoull(refStr.substr(0, sep).c_str(), nullptr, 10);
								outValue.StringValue = typeName;
								return outValue.UIntValue != 0;
							}
						}
					}
					return false;
				});
		}

	} // namespace

	bool Draw(std::span<const Entity> entities, const PropertyDescriptor& d,
		const std::string& fieldKey)
	{
		// Header / spacer / read-only style come from metadata.
		if (!d.Metadata.HeaderContent.empty()) {
			ImGui::Spacing();
			ImGui::TextUnformatted(d.Metadata.HeaderContent.c_str());
			ImGui::Separator();
			ImGui::Spacing();
		}
		if (d.Metadata.HasSpace && d.Metadata.SpaceHeight > 0.0f) {
			ImGui::Dummy(ImVec2(0.0f, d.Metadata.SpaceHeight));
		}

		const bool readOnly = d.Metadata.ReadOnly;
		if (readOnly) ImGui::BeginDisabled();

		bool changed = false;
		switch (d.Type) {
		case PropertyType::None:
			break;
		case PropertyType::Bool:    changed = DrawBool(entities, d); break;
		case PropertyType::Int8:    changed = DrawIntScalar(entities, d, -128, 127); break;
		case PropertyType::Int16:   changed = DrawIntScalar(entities, d, -32768, 32767); break;
		case PropertyType::Int32:   changed = DrawIntScalar(entities, d, INT_MIN, INT_MAX); break;
		case PropertyType::UInt8:   changed = DrawUIntScalar(entities, d, 255); break;
		case PropertyType::UInt16:  changed = DrawUIntScalar(entities, d, 65535); break;
		case PropertyType::UInt32:  changed = DrawUIntScalar(entities, d, INT_MAX); break;
		case PropertyType::Int64:
		case PropertyType::UInt64:  changed = DrawScalar64(entities, d); break;
		case PropertyType::Float:   changed = DrawFloat(entities, d); break;
		case PropertyType::Double:  changed = DrawDouble(entities, d); break;
		case PropertyType::String:  changed = DrawString(entities, d); break;
		case PropertyType::Vec2:    changed = DrawFloatVec<2>(entities, d); break;
		case PropertyType::Vec3:    changed = DrawFloatVec<3>(entities, d); break;
		case PropertyType::Vec4:    changed = DrawFloatVec<4>(entities, d); break;
		case PropertyType::IntVec2: changed = DrawIntVec<2>(entities, d); break;
		case PropertyType::IntVec3: changed = DrawIntVec<3>(entities, d); break;
		case PropertyType::IntVec4: changed = DrawIntVec<4>(entities, d); break;
		case PropertyType::Color:   changed = DrawColor(entities, d); break;
		case PropertyType::Enum:    changed = DrawEnum(entities, d); break;
		case PropertyType::FlagEnum:changed = DrawFlagEnum(entities, d); break;
		case PropertyType::TextureRef:
			changed = DrawAssetRefByKind(entities, d, fieldKey, AssetKind::Texture,
				PropertyType::TextureRef, "Select Texture"); break;
		case PropertyType::AudioRef:
			changed = DrawAssetRefByKind(entities, d, fieldKey, AssetKind::Audio,
				PropertyType::AudioRef, "Select Audio"); break;
		case PropertyType::SceneRef:
			changed = DrawAssetRefByKind(entities, d, fieldKey, AssetKind::Scene,
				PropertyType::SceneRef, "Select Scene"); break;
		case PropertyType::PrefabRef:
			changed = DrawAssetRefByKind(entities, d, fieldKey, AssetKind::Prefab,
				PropertyType::PrefabRef, "Select Prefab"); break;
		case PropertyType::AssetRef:
			changed = DrawAssetRefByKind(entities, d, fieldKey, AssetKind::Unknown,
				PropertyType::AssetRef, "Select Asset"); break;
		case PropertyType::EntityRef:    changed = DrawEntityRef(entities, d, fieldKey); break;
		case PropertyType::ComponentRef: changed = DrawComponentRef(entities, d, fieldKey); break;
		}

		if (readOnly) ImGui::EndDisabled();

		// Tooltip (rendered after the row so the rect is captured).
		if (!d.Metadata.Tooltip.empty() && ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", d.Metadata.Tooltip.c_str());
		}

		return changed;
	}

	bool DrawWithPrefix(std::span<const Entity> entities, const PropertyDescriptor& d,
		const std::string& fieldKeyPrefix)
	{
		return Draw(entities, d, fieldKeyPrefix + "." + d.Name);
	}

	void DrawAll(std::span<const Entity> entities,
		std::span<const PropertyDescriptor> descriptors,
		const std::string& fieldKeyPrefix)
	{
		for (const PropertyDescriptor& desc : descriptors) {
			DrawWithPrefix(entities, desc, fieldKeyPrefix);
		}
	}

} // namespace Axiom::PropertyDrawer
