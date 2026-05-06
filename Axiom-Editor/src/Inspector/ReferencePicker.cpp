#include "pch.hpp"
#include "Inspector/ReferencePicker.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Components/General/NameComponent.hpp"
#include "Graphics/Text/FontHandle.hpp"
#include "Graphics/Texture2D.hpp"
#include "Gui/AssetType.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Gui/ThumbnailCache.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/Path.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <unordered_set>

namespace Axiom::ReferencePicker {

	namespace {

		struct PickerState {
			// IsOpen drives ImGui::Begin's visibility — it goes false when the
			// user clicks the [X] in the title bar or picks an entry. RequestOpen
			// is the "set me to true on the next frame" pulse from callers.
			bool IsOpen = false;
			bool RequestOpen = false;
			char Search[128] = {};
			std::string Title;
			std::string TargetFieldKey;
			std::vector<Entry> Entries;
			std::string PendingFieldKey;
			std::string PendingValue;
			Style Style = Style::Plain;

			// Eye toggle: when true, engine-shipped (built-in) assets like
			// the default font appear in the picker list. When false, they
			// are hidden so the project's own assets are easier to scan.
			// Persists across opens for the session — the user typically
			// has a stable preference here.
			bool IncludeBuiltIns = true;

			// Thumbnail cache lives on the picker (one cache for any kind of
			// asset preview). The cache LRU-evicts past 256 entries; it's
			// cheap to keep around and lazy-initialised the first time the
			// thumbnail style runs.
			ThumbnailCache Thumbnails;
			bool ThumbnailsInitialized = false;
			std::unordered_set<std::string> LoadedThumbnailPaths;
		};

		PickerState s_State;

		void EnsureThumbnailCacheInitialized() {
			if (!s_State.ThumbnailsInitialized) {
				s_State.Thumbnails.Initialize();
				s_State.ThumbnailsInitialized = true;
			}
		}

		void DiscardThumbnails() {
			if (s_State.ThumbnailsInitialized) {
				s_State.Thumbnails.Clear();
			}
			s_State.LoadedThumbnailPaths.clear();
		}

		std::string ToLowerCopy(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

		std::string GetEntityName(const Scene& scene, EntityHandle handle, uint64_t entityId) {
			if (scene.HasComponent<NameComponent>(handle)) {
				const std::string& name = scene.GetComponent<NameComponent>(handle).Name;
				if (!name.empty()) return name;
			}
			return "Entity " + std::to_string(entityId);
		}

		// AssetRegistry's static state lives in a header-only inline-static
		// class with no AXIOM_API export, so each binary that includes the
		// header gets its own copy of `s_BuiltInById` (Axiom-Engine.dll has
		// one, Axiom-Editor.exe has another). Engine-side built-in
		// registrations (FontManager::Initialize, Application::Initialize's
		// directory scan) populate the DLL's copy; this picker reads the
		// EXE's copy. Without re-registering in editor context the picker
		// sees nothing — explaining why the texture/font lists were empty.
		//
		// This helper mirrors the engine-side built-in registration into
		// the editor binary's copy. Idempotent — guarded by a static flag
		// so the directory walk only runs once per process.
		//
		// TODO: convert AssetRegistry to a properly DLL-exported class
		// (move methods + statics into a .cpp, mark with AXIOM_API) so
		// this duplicate population isn't necessary.
		void EnsureBuiltInsRegisteredInEditor() {
			static bool s_Done = false;
			if (s_Done) return;
			s_Done = true;

			// Default font with hand-picked GUID (matches FontManager's own
			// registration so scenes referencing k_DefaultFontAssetId resolve).
			const std::string fontDir = Path::ResolveAxiomAssets("Fonts");
			if (!fontDir.empty()) {
				const std::string fontPath = Path::Combine(fontDir, "DefaultSans-Regular.ttf");
				if (std::filesystem::exists(fontPath)) {
					std::error_code ec;
					std::string canonical = std::filesystem::weakly_canonical(
						std::filesystem::path(fontPath), ec).make_preferred().string();
					if (ec) canonical = fontPath;
					AssetRegistry::RegisterBuiltInAsset(canonical, k_DefaultFontAssetId, AssetKind::Font);
				}
			}

			// Recursive scan of AxiomAssets/ for icons, audio, shaders, etc.
			const std::string axiomAssetsRoot = Path::ResolveAxiomAssets("");
			if (!axiomAssetsRoot.empty()) {
				AssetRegistry::RegisterBuiltInDirectory(axiomAssetsRoot);
			}
		}

		const ComponentInfo* FindComponentByDisplayName(const std::string& displayName) {
			const ComponentInfo* found = nullptr;
			SceneManager::Get().GetComponentRegistry().ForEachComponentInfo(
				[&](const std::type_index&, const ComponentInfo& info) {
					if (!found && info.category == ComponentCategory::Component && info.displayName == displayName) {
						found = &info;
					}
				});
			return found;
		}

	} // namespace

	std::vector<Entry> CollectAssetsByKind(AssetKind kind) {
		EnsureBuiltInsRegisteredInEditor();

		// Force a re-scan: AssetRegistry only auto-rebuilds when something
		// has marked it dirty. Without this, dropping a new asset into
		// Assets/ won't show up in the picker until a save / reload.
		AssetRegistry::MarkDirty();
		AssetRegistry::Sync();

		std::vector<Entry> entries;
		entries.push_back({ "(None)", "", "(none)", "", "__none__", false });
		for (const AssetRegistry::Record& record : AssetRegistry::GetAssetsByKind(kind)) {
			Entry entry;
			entry.Label = std::filesystem::path(record.Path).filename().string();
			entry.Secondary = record.Path;
			entry.SearchKey = ToLowerCopy(entry.Label + " " + entry.Secondary);
			entry.Value = std::to_string(record.Id);
			entry.UniqueId = entry.Value;
			entry.IsBuiltIn = AssetRegistry::IsBuiltIn(record.Id);
			entries.push_back(std::move(entry));
		}
		std::sort(entries.begin() + 1, entries.end(), [](const Entry& a, const Entry& b) {
			if (a.Label == b.Label) return a.Secondary < b.Secondary;
			return a.Label < b.Label;
		});
		return entries;
	}

	std::vector<Entry> CollectEntities() {
		std::vector<Entry> entries;
		entries.push_back({ "(None)", "", "(none)", "0", "__none__" });

		SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
			auto view = scene.GetRegistry().view<entt::entity>();
			for (EntityHandle handle : view) {
				if (!scene.IsValid(handle)) continue;
				const uint64_t entityId = scene.GetRuntimeID(handle);
				if (entityId == 0) continue;

				Entry entry;
				entry.Label = GetEntityName(scene, handle, entityId);
				entry.Secondary = scene.GetName();
				entry.SearchKey = ToLowerCopy(entry.Label);
				entry.Value = std::to_string(entityId);
				entry.UniqueId = entry.Value;
				entries.push_back(std::move(entry));
			}
		});

		AssetRegistry::MarkDirty();
		AssetRegistry::Sync();
		for (const AssetRegistry::Record& record : AssetRegistry::GetAssetsByKind(AssetKind::Prefab)) {
			Entry entry;
			entry.Label = std::filesystem::path(record.Path).filename().string();
			entry.Secondary = record.Path;
			entry.SearchKey = ToLowerCopy(entry.Label + " prefab " + entry.Secondary);
			entry.Value = "prefab:" + std::to_string(record.Id);
			entry.UniqueId = entry.Value;
			entries.push_back(std::move(entry));
		}

		std::sort(entries.begin() + 1, entries.end(), [](const Entry& a, const Entry& b) {
			if (a.Label == b.Label) return a.Secondary < b.Secondary;
			return a.Label < b.Label;
		});
		return entries;
	}

	std::vector<Entry> CollectComponentTargets(const std::string& componentDisplayName) {
		std::vector<Entry> entries;
		entries.push_back({ "(None)", "", "(none)", "", "__none__" });
		const ComponentInfo* info = FindComponentByDisplayName(componentDisplayName);
		if (!info || !info->has) return entries;

		SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
			auto view = scene.GetRegistry().view<entt::entity>();
			for (EntityHandle handle : view) {
				if (!scene.IsValid(handle)) continue;
				const uint64_t entityId = scene.GetRuntimeID(handle);
				if (entityId == 0) continue;

				Entity entity = scene.GetEntity(handle);
				if (!info->has(entity)) continue;

				const std::string entityName = GetEntityName(scene, handle, entityId);
				Entry entry;
				entry.Label = entityName + " (" + componentDisplayName + ")";
				entry.SearchKey = ToLowerCopy(entityName + " " + componentDisplayName);
				entry.Value = std::to_string(entityId) + ":" + componentDisplayName;
				entry.UniqueId = entry.Value;
				entries.push_back(std::move(entry));
			}
		});

		std::sort(entries.begin() + 1, entries.end(), [](const Entry& a, const Entry& b) {
			if (a.Label == b.Label) return a.Secondary < b.Secondary;
			return a.Label < b.Label;
		});
		return entries;
	}

	void OpenForFieldKey(const std::string& fieldKey, const std::string& title,
		std::vector<Entry> entries, Style style)
	{
		s_State.RequestOpen = true;
		s_State.IsOpen = true;
		s_State.Title = title;
		s_State.TargetFieldKey = fieldKey;
		s_State.Entries = std::move(entries);
		s_State.Search[0] = '\0';
		s_State.Style = style;
		// Thumbnail style: drop any stale entries from the previous open so
		// thumbnails refresh against the new entry list. The cache itself
		// stays initialised so we don't pay the GL setup cost again.
		if (style == Style::Thumbnails) {
			EnsureThumbnailCacheInitialized();
			DiscardThumbnails();
		}
	}

	std::optional<std::string> ConsumeSelection(const std::string& fieldKey) {
		if (s_State.PendingFieldKey != fieldKey) return std::nullopt;
		std::string value = s_State.PendingValue;
		s_State.PendingFieldKey.clear();
		s_State.PendingValue.clear();
		return value;
	}

	void RenderPopup() {
		// Mirror the SpriteRenderer texture picker's UX: a regular window
		// (not a modal) with its own [X] close button. RequestOpen is the
		// "appear this frame" pulse from OpenForFieldKey; IsOpen is the
		// living visibility state ImGui::Begin reads + writes via the
		// title-bar X. Once the window closes (user picked or clicked X),
		// drop the thumbnail cache so we don't keep GPU memory tied up
		// for textures the user is no longer browsing.
		if (!s_State.IsOpen) {
			if (s_State.ThumbnailsInitialized) DiscardThumbnails();
			return;
		}

		// Use a fresh window the first time the picker opens; ImGui will
		// reposition it on top. Reset position so the picker doesn't end
		// up off-screen if the editor docking layout changed since last open.
		const ImVec2 size = (s_State.Style == Style::Thumbnails)
			? ImVec2(360.0f, 460.0f)
			: ImVec2(440.0f, 430.0f);
		if (s_State.RequestOpen) {
			ImGui::SetNextWindowSize(size, ImGuiCond_Always);
			ImGui::SetNextWindowFocus();
			s_State.RequestOpen = false;
		}
		else {
			ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
		}

		const std::string windowTitle = s_State.Title + "##ReferencePickerWindow";
		if (!ImGui::Begin(windowTitle.c_str(), &s_State.IsOpen,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			if (!s_State.IsOpen) DiscardThumbnails();
			return;
		}

		// Eye toggle on the right; search bar fills the remainder. Uses
		// the engine-shipped visibility_eye icon (AxiomAssets/Textures/
		// Editor/visibility_eye/) at 16 px. Tint mirrors the log panel's
		// filter pattern: full-white when showing, dim grey when hiding.
		// If the icon fails to load (icon dir missing on disk), fall
		// back to an ASCII-labelled button so the toggle stays usable.
		const ImGuiStyle& style = ImGui::GetStyle();
		const unsigned int eyeIcon = EditorIcons::Get("visibility_eye", 16);
		const ImVec2 eyeIconSize(16.0f, 16.0f);
		const float toggleWidth = (eyeIcon != 0)
			? eyeIconSize.x + style.FramePadding.x * 2.0f
			: ImGui::CalcTextSize("(o)").x + style.FramePadding.x * 2.0f;
		const float searchWidth = std::max(60.0f,
			ImGui::GetContentRegionAvail().x - toggleWidth - style.ItemSpacing.x);
		ImGui::SetNextItemWidth(searchWidth);
		ImGui::InputTextWithHint("##ReferenceSearch", "Search...", s_State.Search, sizeof(s_State.Search));
		ImGui::SameLine();
		const bool showingBuiltIns = s_State.IncludeBuiltIns;
		bool toggleClicked = false;
		if (eyeIcon != 0) {
			const ImVec4 tint = showingBuiltIns
				? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
				: ImVec4(0.4f, 0.4f, 0.4f, 0.5f);
			toggleClicked = ImGui::ImageButton("##BuiltInToggle",
				static_cast<ImTextureID>(static_cast<intptr_t>(eyeIcon)),
				eyeIconSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f),
				ImVec4(0.0f, 0.0f, 0.0f, 0.0f), tint);
		}
		else {
			if (showingBuiltIns) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			toggleClicked = ImGui::Button(showingBuiltIns ? "(o)##BuiltInToggle" : "(-)##BuiltInToggle");
			if (showingBuiltIns) {
				ImGui::PopStyleColor();
			}
		}
		if (toggleClicked) {
			s_State.IncludeBuiltIns = !s_State.IncludeBuiltIns;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(showingBuiltIns
				? "Built-in (engine-shipped) assets are visible.\nClick to hide them."
				: "Built-in (engine-shipped) assets are hidden.\nClick to show them.");
		}
		ImGui::Separator();

		const std::string filter = ToLowerCopy(std::string(s_State.Search));

		// Pre-filter entries once so both layouts share the same visible
		// set + the empty-state message can be displayed correctly. The
		// eye toggle hides built-ins; the (None) entry has IsBuiltIn=false
		// so it always survives the filter.
		std::vector<const Entry*> visible;
		visible.reserve(s_State.Entries.size());
		for (const Entry& entry : s_State.Entries) {
			if (!s_State.IncludeBuiltIns && entry.IsBuiltIn) continue;
			if (!filter.empty() && entry.SearchKey.find(filter) == std::string::npos) continue;
			visible.push_back(&entry);
		}

		auto applySelection = [&](const Entry* entry) {
			s_State.PendingFieldKey = s_State.TargetFieldKey;
			s_State.PendingValue = entry ? entry->Value : std::string();
			s_State.IsOpen = false;
		};

		ImGui::BeginChild("##ReferencePickerList");

		if (s_State.Style == Style::Thumbnails) {
			// Thumbnail-row layout, copied from the SpriteRenderer texture
			// picker. Each entry shows a 48px image preview + filename +
			// relative path. Entries with empty Secondary (e.g. "(None)")
			// fall back to a label-only row so the (None) entry still
			// renders sanely at the top.
			const float thumbnailSize = 48.0f;
			const float rowPadding = 6.0f;
			const float lineHeight = ImGui::GetTextLineHeight();
			const float rowHeight = std::max(thumbnailSize + rowPadding * 2.0f,
				lineHeight * 2.0f + rowPadding * 2.0f + 2.0f);
			std::unordered_set<std::string> visiblePaths;
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(visible.size()), rowHeight);
			while (clipper.Step()) {
				for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
					const Entry& entry = *visible[static_cast<std::size_t>(index)];
					const bool hasThumbnail = !entry.Secondary.empty();
					if (hasThumbnail) {
						visiblePaths.insert(entry.Secondary);
						s_State.LoadedThumbnailPaths.insert(entry.Secondary);
					}

					ImGui::PushID(entry.UniqueId.c_str());
					const float rowWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
					const ImVec2 rowMin = ImGui::GetCursorScreenPos();
					const ImVec2 rowMax(rowMin.x + rowWidth, rowMin.y + rowHeight);
					ImGui::InvisibleButton("##Row", ImVec2(rowWidth, rowHeight));
					const bool hovered = ImGui::IsItemHovered();
					if (hovered) {
						drawList->AddRectFilled(rowMin, rowMax, IM_COL32(70, 78, 92, 120), 4.0f);
					}
					if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
						applySelection(&entry);
					}

					const ImVec2 thumbMin(rowMin.x + rowPadding, rowMin.y + rowPadding);
					if (hasThumbnail) {
						const ImVec2 thumbMax(thumbMin.x + thumbnailSize, thumbMin.y + thumbnailSize);
						drawList->AddRectFilled(thumbMin, thumbMax, IM_COL32(35, 35, 35, 255), 4.0f);

						const unsigned int thumbnail = s_State.Thumbnails.GetThumbnail(entry.Secondary);
						Texture2D* texture = s_State.Thumbnails.GetCacheEntry(entry.Secondary);
						if (thumbnail != 0 && texture && texture->IsValid()) {
							float drawWidth = thumbnailSize;
							float drawHeight = thumbnailSize;
							const float texW = static_cast<float>(texture->GetWidth());
							const float texH = static_cast<float>(texture->GetHeight());
							if (texW > 0.0f && texH > 0.0f) {
								const float aspect = texW / texH;
								if (aspect > 1.0f) drawHeight = thumbnailSize / aspect;
								else               drawWidth  = thumbnailSize * aspect;
							}
							const ImVec2 imageMin(
								thumbMin.x + (thumbnailSize - drawWidth) * 0.5f,
								thumbMin.y + (thumbnailSize - drawHeight) * 0.5f);
							const ImVec2 imageMax(imageMin.x + drawWidth, imageMin.y + drawHeight);
							drawList->AddImage(
								static_cast<ImTextureID>(static_cast<intptr_t>(thumbnail)),
								imageMin, imageMax, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
						}
						else {
							ThumbnailCache::DrawAssetIcon(AssetType::Image, thumbMin, thumbnailSize);
						}
					}

					const float textX = hasThumbnail
						? thumbMin.x + thumbnailSize + rowPadding * 1.5f
						: rowMin.x + rowPadding;
					const float textWidth = std::max(rowMax.x - textX - rowPadding, 1.0f);
					bool nameTruncated = false;
					bool pathTruncated = false;
					const std::string displayName = ImGuiUtils::Ellipsize(entry.Label, textWidth, &nameTruncated);
					drawList->AddText(ImVec2(textX, rowMin.y + rowPadding),
						ImGui::GetColorU32(ImGuiCol_Text), displayName.c_str());
					if (!entry.Secondary.empty()) {
						const std::string displayPath = ImGuiUtils::Ellipsize(entry.Secondary, textWidth, &pathTruncated);
						drawList->AddText(ImVec2(textX, rowMin.y + rowPadding + lineHeight),
							ImGui::GetColorU32(ImGuiCol_TextDisabled), displayPath.c_str());
					}
					if (hovered && (nameTruncated || pathTruncated)) {
						ImGui::SetTooltip("%s", entry.Secondary.empty()
							? entry.Label.c_str()
							: entry.Secondary.c_str());
					}
					ImGui::PopID();
				}
			}

			// LRU eviction for off-screen thumbnails — match the existing
			// texture-picker behaviour so memory stays bounded as the user
			// scrolls a large texture project.
			for (auto it = s_State.LoadedThumbnailPaths.begin(); it != s_State.LoadedThumbnailPaths.end();) {
				if (visiblePaths.find(*it) == visiblePaths.end()) {
					s_State.Thumbnails.Invalidate(*it);
					it = s_State.LoadedThumbnailPaths.erase(it);
				}
				else {
					++it;
				}
			}
		}
		else {
			// Plain layout for non-asset reference types (entities, prefabs,
			// component refs, scenes). PushID(UniqueId) keeps Selectable IDs
			// distinct without having to bake them into the visible label.
			for (const Entry* entryPtr : visible) {
				const Entry& entry = *entryPtr;
				ImGui::PushID(entry.UniqueId.c_str());
				bool truncated = false;
				const std::string label = ImGuiUtils::Ellipsize(entry.Label, ImGui::GetContentRegionAvail().x, &truncated);
				if (ImGui::Selectable(label.c_str(), false)) {
					applySelection(&entry);
				}
				if (ImGui::IsItemHovered() && (truncated || !entry.Secondary.empty())) {
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
				ImGui::PopID();
			}
		}

		if (visible.empty()) ImGui::TextDisabled("No matching items");
		ImGui::EndChild();
		ImGui::End();

		if (!s_State.IsOpen) DiscardThumbnails();
	}

	bool DrawReferenceField(const char* label, const std::string& displayValue,
		const std::string& secondary, bool missing, bool mixed, bool& outHovered)
	{
		ImGui::PushID(label);
		ImGuiUtils::BeginInspectorFieldRow(label);
		const float buttonWidth = std::max(ImGui::GetContentRegionAvail().x, 120.0f);
		const ImGuiStyle& style = ImGui::GetStyle();

		if (missing) {
			ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.30f, 0.12f, 0.12f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.16f, 0.16f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.26f, 0.10f, 0.10f, 1.0f));
		}
		else if (mixed) {
			ImGui::PushStyleColor(ImGuiCol_Button,        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
		}
		else {
			ImGui::PushStyleColor(ImGuiCol_Button,        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
		}

		const std::string buttonLabel = mixed ? std::string("\xe2\x80\x94") : displayValue;  // em-dash
		bool truncated = false;
		const std::string buttonText = ImGuiUtils::Ellipsize(buttonLabel, buttonWidth - style.FramePadding.x * 2.0f, &truncated);
		const bool clicked = ImGui::Button((buttonText + "##ReferenceValue").c_str(), ImVec2(buttonWidth, 0.0f));
		outHovered = ImGui::IsItemHovered();
		if (outHovered && !mixed && (truncated || !secondary.empty())) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(displayValue.c_str());
			if (!secondary.empty()) {
				ImGui::Separator();
				ImGui::TextDisabled("%s", secondary.c_str());
			}
			ImGui::EndTooltip();
		}
		ImGui::PopStyleColor(3);
		ImGui::PopID();
		return clicked;
	}

	std::string ResolveAssetDisplay(uint64_t assetId, AssetKind expectedKind,
		bool& outMissing, std::string* outSecondary)
	{
		outMissing = false;
		if (outSecondary) outSecondary->clear();
		if (assetId == 0) return "(None)";

		const AssetKind kind = AssetRegistry::GetKind(assetId);
		if (kind != expectedKind) {
			outMissing = true;
			return "(Missing Asset)";
		}
		if (outSecondary) *outSecondary = AssetRegistry::ResolvePath(assetId);
		const std::string name = AssetRegistry::GetDisplayName(assetId);
		if (name.empty()) {
			outMissing = true;
			return "(Missing Asset)";
		}
		return name;
	}

	std::string ResolvePrefabDisplay(uint64_t prefabId, bool& outMissing,
		std::string* outSecondary)
	{
		outMissing = false;
		if (outSecondary) outSecondary->clear();
		if (prefabId == 0) return "(None)";
		if (AssetRegistry::GetKind(prefabId) != AssetKind::Prefab) {
			outMissing = true;
			return "(Missing Prefab)";
		}
		if (outSecondary) *outSecondary = AssetRegistry::ResolvePath(prefabId);
		return AssetRegistry::GetDisplayName(prefabId);
	}

	std::string ResolveEntityDisplay(uint64_t entityId, bool& outMissing,
		std::string* outSecondary)
	{
		outMissing = false;
		if (outSecondary) outSecondary->clear();
		if (entityId == 0) return "(None)";

		std::string display;
		std::string secondary;
		bool resolved = false;
		SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
			if (resolved) return;
			EntityHandle handle = entt::null;
			if (scene.TryResolveRuntimeID(entityId, handle)) {
				display = GetEntityName(scene, handle, entityId);
				secondary = scene.GetName();
				resolved = true;
			}
		});
		if (!resolved) {
			outMissing = true;
			return "(Missing Entity)";
		}
		if (outSecondary) *outSecondary = secondary;
		return display;
	}

	std::string ResolveComponentRefDisplay(uint64_t entityId,
		const std::string& componentTypeName, bool& outMissing,
		std::string* outSecondary)
	{
		outMissing = false;
		if (outSecondary) outSecondary->clear();
		if (entityId == 0) return "(None)";

		std::string entityName;
		bool resolved = false;
		SceneManager::Get().ForeachLoadedScene([&](const Scene& scene) {
			if (resolved) return;
			EntityHandle handle = entt::null;
			if (scene.TryResolveRuntimeID(entityId, handle)) {
				entityName = GetEntityName(scene, handle, entityId);
				resolved = true;
			}
		});
		if (!resolved) {
			outMissing = true;
			return "(Missing)." + componentTypeName;
		}
		return entityName + " (" + componentTypeName + ")";
	}

} // namespace Axiom::ReferencePicker
