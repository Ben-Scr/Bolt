#pragma once

#include "Assets/AssetKind.hpp"
#include "Inspector/PropertyType.hpp"

#include <optional>
#include <string>
#include <vector>

// =============================================================================
// ReferencePicker
// -----------------------------------------------------------------------------
// One modal popup that handles every kind of reference field in the inspector
// — textures, audio, scenes, prefabs, generic assets, entities, and component
// references on entities. Replaces the per-type s_TexturePicker /
// s_AudioPicker / s_ReferencePicker statics that used to live next to each
// inspector.
//
// Caller flow (per frame, per field):
//   1. Determine the field's current value as a PropertyValue.
//   2. Render the field row with DrawReferenceField(...) — that function
//      handles the button / mixed-state / drag-drop logic and calls into
//      OpenForFieldKey() if the user clicks it.
//   3. After all property rows have been drawn, call RenderPopup() once to
//      render the modal if any field requested it this frame.
//   4. Each property row checks ConsumeSelection(fieldKey) at the top of its
//      next render to apply a selection that arrived from the popup.
//
// Field keys
// ----------
// A "field key" is an opaque string the caller invents to uniquely identify a
// field across frames. The picker only uses it to route a selection back to
// the right caller — keys never appear in the UI. Keys must be stable across
// frames for as long as the popup is open. Suggested format:
//   "<componentTypeName>.<fieldName>"  (native components)
//   "<scriptClassName>#<scriptIndex>.<fieldName>"  (script fields)
// =============================================================================

namespace Axiom {

	struct PropertyValue;

	namespace ReferencePicker {

		// One row in the picker's list. Label + secondary appear in the popup;
		// SearchKey is the lower-cased text matched against the search field;
		// Value is the string that comes back via ConsumeSelection — the
		// caller decodes it back into a PropertyValue using PropertyValue::FromString.
		// IsBuiltIn marks engine-shipped assets (default font, built-in
		// shaders) so the picker's eye toggle can hide them from search.
		struct Entry {
			std::string Label;
			std::string Secondary;
			std::string SearchKey;
			std::string Value;
			std::string UniqueId;
			bool IsBuiltIn = false;
		};

		// Build entry lists for the standard reference kinds. Each list is
		// sorted alphabetically and prepended with a "(None)" entry.
		std::vector<Entry> CollectAssetsByKind(AssetKind kind);
		std::vector<Entry> CollectEntities();          // every loaded scene + Prefab assets
		std::vector<Entry> CollectComponentTargets(const std::string& componentDisplayName);

		// Visual style for the picker window. Plain = simple selectable list
		// (entities, components, scenes). Thumbnails = 48px image grid that
		// loads each entry's preview from `Entry.Secondary` (the asset's
		// full path). Used for texture / audio asset pickers so they look
		// the same as SpriteRenderer's existing texture browser.
		enum class Style { Plain, Thumbnails };

		// Request that the picker window open this frame for `fieldKey`.
		// `entries` are owned by the picker until the window closes.
		void OpenForFieldKey(const std::string& fieldKey, const std::string& title,
			std::vector<Entry> entries, Style style = Style::Plain);

		// Render the modal popup if any caller requested it this frame.
		// Idempotent — safe to call exactly once per frame regardless of how
		// many fields might have requested an open.
		void RenderPopup();

		// Pick up a selection that was made on a previous frame for this
		// field. Returns std::nullopt unless the user picked something for
		// this exact key. The returned string is the picker entry's Value
		// (an asset UUID, "prefab:<id>", "<entityId>:<typeName>", etc.).
		std::optional<std::string> ConsumeSelection(const std::string& fieldKey);

		// Draw a labelled reference button for the inspector field row.
		// `displayValue` is the visible text on the button ("(None)", an
		// asset name, "—" when mixed, etc.). Returns true if the user clicked
		// the button, in which case the caller should call OpenForFieldKey.
		// `outHovered` is set to true if the cursor is over the button so
		// caller-supplied tooltips (e.g. clamp ranges) work.
		bool DrawReferenceField(const char* label, const std::string& displayValue,
			const std::string& secondary, bool missing, bool mixed, bool& outHovered);

		// Display name to show on the reference button for the unified
		// PropertyDrawer. Empty UUID renders as "(None)"; missing asset
		// renders in red as "(Missing)". `secondary` is set to the asset path
		// or scene name for the hover tooltip.
		std::string ResolveAssetDisplay(uint64_t assetId, AssetKind expectedKind,
			bool& outMissing, std::string* outSecondary);
		std::string ResolvePrefabDisplay(uint64_t prefabId, bool& outMissing,
			std::string* outSecondary);
		std::string ResolveEntityDisplay(uint64_t entityId, bool& outMissing,
			std::string* outSecondary);
		std::string ResolveComponentRefDisplay(uint64_t entityId,
			const std::string& componentTypeName, bool& outMissing,
			std::string* outSecondary);

	} // namespace ReferencePicker

} // namespace Axiom
