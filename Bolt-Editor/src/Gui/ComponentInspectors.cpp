#include <pch.hpp>
#include "Assets/AssetRegistry.hpp"
#include "Gui/ComponentInspectors.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Gui/ThumbnailCache.hpp"

#include "Components/Components.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/Texture2D.hpp"
#include "Audio/AudioManager.hpp"
#include "Physics/PhysicsTypes.hpp"
#include <Project/ProjectManager.hpp>
#include <Project/BoltProject.hpp>
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"

#include <imgui.h>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <unordered_set>

namespace Bolt {

	// ── Texture Picker State (for SpriteRenderer inspector) ─────────

	namespace {
		static TextureHandle LoadTextureFromAssetPath(const std::string& path, UUID* outAssetId = nullptr) {
			const uint64_t assetId = AssetRegistry::GetOrCreateAssetUUID(path);
			if (outAssetId) {
				*outAssetId = UUID(assetId);
			}

			if (assetId != 0 && AssetRegistry::IsTexture(assetId)) {
				TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
				if (handle.IsValid()) {
					return handle;
				}
			}

			return TextureManager::LoadTexture(path);
		}

		static void AssignSpriteTexture(SpriteRendererComponent& sprite, const std::string& path) {
			UUID assetId = UUID(0);
			sprite.TextureHandle = LoadTextureFromAssetPath(path, &assetId);
			sprite.TextureAssetId = assetId;
		}

		static void AssignAudioClip(AudioSourceComponent& audioSource, const std::string& path) {
			const uint64_t assetId = AssetRegistry::GetOrCreateAssetUUID(path);
			if (assetId != 0 && AssetRegistry::IsAudio(assetId)) {
				audioSource.SetAudioHandle(AudioManager::LoadAudioByUUID(assetId), UUID(assetId));
				return;
			}

			audioSource.SetAudioHandle(AudioManager::LoadAudio(path), UUID(0));
		}

		static void DrawPickerHeaderControls(const char* searchId, char* searchBuffer, std::size_t searchBufferSize, bool& isOpen) {
			const float closeButtonSize = ImGui::GetFrameHeight();
			const float inputWidth = std::max(ImGui::GetContentRegionAvail().x - closeButtonSize - ImGui::GetStyle().ItemSpacing.x, 1.0f);

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputTextWithHint(searchId, "Search...", searchBuffer, searchBufferSize);
			ImGui::SameLine();
			if (ImGui::Button("X", ImVec2(closeButtonSize, closeButtonSize))) {
				isOpen = false;
			}
			ImGui::Separator();
		}

		static bool DrawAssetSelectionField(const char* label, const std::string& displayName, const std::string& tooltip = {}) {
			ImGui::PushID(label);
			ImGuiUtils::BeginInspectorFieldRow(label);
			const std::string resolvedDisplayName = displayName.empty() ? std::string("(None)") : displayName;
			const float fieldWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
			bool truncated = false;
			const std::string buttonText = ImGuiUtils::Ellipsize(
				resolvedDisplayName,
				fieldWidth - ImGui::GetStyle().FramePadding.x * 2.0f,
				&truncated);

			const bool clicked = ImGui::Button((buttonText + "##Value").c_str(), ImVec2(fieldWidth, 0.0f));
			if (ImGui::IsItemHovered() && (truncated || !tooltip.empty())) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(resolvedDisplayName.c_str());
				if (!tooltip.empty()) {
					ImGui::TextDisabled("%s", tooltip.c_str());
				}
				ImGui::EndTooltip();
			}

			ImGui::PopID();
			return clicked;
		}

		struct TexturePickerEntry {
			std::string RelativePath;
			std::string DisplayName;
			std::string FullPath;
			uint64_t AssetId = 0;
		};

		static bool s_TexturePickerOpen = false;
		static char s_TexturePickerSearch[128] = {};
		static std::vector<TexturePickerEntry> s_TexturePickerEntries;
		static ThumbnailCache s_TexturePickerThumbnails;
		static bool s_TexturePickerThumbnailCacheInitialized = false;
		static std::unordered_set<std::string> s_TexturePickerLoadedPaths;
		// Store entity handle instead of raw component pointer to avoid
		// dangling pointer if the entity is destroyed or components reallocate.
		static EntityHandle s_TexturePickerTargetEntity = entt::null;

		static bool IsImageExtension(const std::string& ext) {
			return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
				|| ext == ".bmp" || ext == ".tga";
		}

		static void CollectTextureFiles(const std::filesystem::path& dir, const std::filesystem::path& root,
			std::vector<TexturePickerEntry>& entries) {
			if (!std::filesystem::exists(dir)) return;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
				if (!entry.is_regular_file()) continue;
				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (!IsImageExtension(ext)) continue;

				std::string rel = std::filesystem::relative(entry.path(), root).string();
				std::string name = entry.path().filename().string();
				entries.push_back({ rel, name, entry.path().string(), AssetRegistry::GetOrCreateAssetUUID(entry.path().string()) });
			}
			std::sort(entries.begin(), entries.end(),
				[](const TexturePickerEntry& a, const TexturePickerEntry& b) {
					if (a.DisplayName == b.DisplayName) {
						return a.RelativePath < b.RelativePath;
					}
					return a.DisplayName < b.DisplayName;
				});
		}

		static void EnsureTexturePickerThumbnailCache() {
			if (!s_TexturePickerThumbnailCacheInitialized) {
				s_TexturePickerThumbnails.Initialize();
				s_TexturePickerThumbnailCacheInitialized = true;
			}
		}

		static void ClearTexturePickerThumbnailCache() {
			if (s_TexturePickerThumbnailCacheInitialized) {
				s_TexturePickerThumbnails.Clear();
			}
			s_TexturePickerLoadedPaths.clear();
		}

		static void ApplyTexturePickerSelection(const TexturePickerEntry* entry) {
			Scene* scene = SceneManager::Get().GetActiveScene();
			if (scene && scene->IsValid(s_TexturePickerTargetEntity)
				&& scene->HasComponent<SpriteRendererComponent>(s_TexturePickerTargetEntity)) {
				auto& sprite = scene->GetComponent<SpriteRendererComponent>(s_TexturePickerTargetEntity);
				if (!entry) {
					sprite.TextureHandle = TextureHandle();
					sprite.TextureAssetId = UUID(0);
				}
				else if (entry->AssetId != 0 && AssetRegistry::IsTexture(entry->AssetId)) {
					sprite.TextureHandle = TextureManager::LoadTextureByUUID(entry->AssetId);
					sprite.TextureAssetId = UUID(entry->AssetId);
				}
				else {
					sprite.TextureHandle = TextureManager::LoadTexture(entry->FullPath);
					sprite.TextureAssetId = UUID(0);
				}
			}

			s_TexturePickerOpen = false;
		}

		static void OpenTexturePicker(EntityHandle entity) {
			s_TexturePickerOpen = true;
			s_TexturePickerSearch[0] = '\0';
			s_TexturePickerTargetEntity = entity;
			s_TexturePickerEntries.clear();
			EnsureTexturePickerThumbnailCache();
			ClearTexturePickerThumbnailCache();

			// Collect from BoltAssets/Textures (engine) and Assets/Textures (user project)
			std::string boltTexDir = Path::ResolveBoltAssets("Textures");
			if (!boltTexDir.empty()) {
				auto boltTexPath = std::filesystem::path(boltTexDir);
				CollectTextureFiles(boltTexPath, boltTexPath, s_TexturePickerEntries);
			}
			if (BoltProject* project = ProjectManager::GetCurrentProject()) {
				CollectTextureFiles(project->AssetsDirectory, project->AssetsDirectory, s_TexturePickerEntries);
			}
			else {
				auto userAssetsDir = std::filesystem::path(Path::ExecutableDir()) / "Assets";
				CollectTextureFiles(userAssetsDir, userAssetsDir, s_TexturePickerEntries);
			}
		}

		static void RenderTexturePicker() {
			if (!s_TexturePickerOpen) {
				ClearTexturePickerThumbnailCache();
				return;
			}

			EnsureTexturePickerThumbnailCache();

			ImGui::SetNextWindowSize(ImVec2(340, 420), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Select Texture", &s_TexturePickerOpen)) {
				ImGui::End();
				if (!s_TexturePickerOpen) {
					ClearTexturePickerThumbnailCache();
				}
				return;
			}

			DrawPickerHeaderControls("##TexSearch", s_TexturePickerSearch, sizeof(s_TexturePickerSearch), s_TexturePickerOpen);

			ImGui::BeginChild("##TexList");

			if (ImGui::Selectable("(None)", false)) {
				ApplyTexturePickerSelection(nullptr);
			}

			std::string filter(s_TexturePickerSearch);
			std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

			std::vector<const TexturePickerEntry*> filteredEntries;
			filteredEntries.reserve(s_TexturePickerEntries.size());
			for (const auto& entry : s_TexturePickerEntries) {
				if (!filter.empty()) {
					std::string lowerName = entry.DisplayName;
					std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
					std::string lowerPath = entry.RelativePath;
					std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
					if (lowerName.find(filter) == std::string::npos && lowerPath.find(filter) == std::string::npos) continue;
				}
				filteredEntries.push_back(&entry);
			}

			if (filteredEntries.empty()) {
				ImGui::TextDisabled("No matching textures");
			}
			else {
				const float thumbnailSize = 48.0f;
				const float rowPadding = 6.0f;
				const float lineHeight = ImGui::GetTextLineHeight();
				const float rowHeight = std::max(thumbnailSize + rowPadding * 2.0f, lineHeight * 2.0f + rowPadding * 2.0f + 2.0f);
				std::unordered_set<std::string> visiblePaths;
				ImDrawList* drawList = ImGui::GetWindowDrawList();

				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(filteredEntries.size()), rowHeight);
				while (clipper.Step()) {
					for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
						const TexturePickerEntry& entry = *filteredEntries[static_cast<std::size_t>(index)];
						visiblePaths.insert(entry.FullPath);
						s_TexturePickerLoadedPaths.insert(entry.FullPath);

						ImGui::PushID(entry.RelativePath.c_str());

						const float rowWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
						const ImVec2 rowMin = ImGui::GetCursorScreenPos();
						const ImVec2 rowMax(rowMin.x + rowWidth, rowMin.y + rowHeight);

						ImGui::InvisibleButton("##TextureRow", ImVec2(rowWidth, rowHeight));
						const bool hovered = ImGui::IsItemHovered();
						if (hovered) {
							drawList->AddRectFilled(rowMin, rowMax, IM_COL32(70, 78, 92, 120), 4.0f);
						}
						if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
							ApplyTexturePickerSelection(&entry);
						}

						const ImVec2 thumbMin(rowMin.x + rowPadding, rowMin.y + rowPadding);
						const ImVec2 thumbMax(thumbMin.x + thumbnailSize, thumbMin.y + thumbnailSize);
						drawList->AddRectFilled(thumbMin, thumbMax, IM_COL32(35, 35, 35, 255), 4.0f);

						const unsigned int thumbnail = s_TexturePickerThumbnails.GetThumbnail(entry.FullPath);
						Texture2D* thumbnailTexture = s_TexturePickerThumbnails.GetCacheEntry(entry.FullPath);
						if (thumbnail != 0 && thumbnailTexture && thumbnailTexture->IsValid()) {
							float drawWidth = thumbnailSize;
							float drawHeight = thumbnailSize;
							const float texWidth = thumbnailTexture->GetWidth();
							const float texHeight = thumbnailTexture->GetHeight();
							if (texWidth > 0.0f && texHeight > 0.0f) {
								const float aspect = texWidth / texHeight;
								if (aspect > 1.0f) {
									drawHeight = thumbnailSize / aspect;
								}
								else {
									drawWidth = thumbnailSize * aspect;
								}
							}

							const ImVec2 imageMin(
								thumbMin.x + (thumbnailSize - drawWidth) * 0.5f,
								thumbMin.y + (thumbnailSize - drawHeight) * 0.5f);
							const ImVec2 imageMax(imageMin.x + drawWidth, imageMin.y + drawHeight);
							drawList->AddImage(
								static_cast<ImTextureID>(static_cast<intptr_t>(thumbnail)),
								imageMin,
								imageMax,
								ImVec2(0.0f, 1.0f),
								ImVec2(1.0f, 0.0f));
						}
						else {
							ThumbnailCache::DrawAssetIcon(AssetType::Image, thumbMin, thumbnailSize);
						}

						const float textX = thumbMax.x + rowPadding * 1.5f;
						const float textWidth = std::max(rowMax.x - textX - rowPadding, 1.0f);
						bool nameTruncated = false;
						bool pathTruncated = false;
						const std::string displayName = ImGuiUtils::Ellipsize(entry.DisplayName, textWidth, &nameTruncated);
						const std::string displayPath = ImGuiUtils::Ellipsize(entry.RelativePath, textWidth, &pathTruncated);
						drawList->AddText(ImVec2(textX, rowMin.y + rowPadding), ImGui::GetColorU32(ImGuiCol_Text), displayName.c_str());
						drawList->AddText(ImVec2(textX, rowMin.y + rowPadding + lineHeight), ImGui::GetColorU32(ImGuiCol_TextDisabled), displayPath.c_str());

						if (hovered && (nameTruncated || pathTruncated)) {
							ImGui::SetTooltip("%s", entry.RelativePath.c_str());
						}

						ImGui::PopID();
					}
				}

				for (auto it = s_TexturePickerLoadedPaths.begin(); it != s_TexturePickerLoadedPaths.end();) {
					if (visiblePaths.find(*it) == visiblePaths.end()) {
						s_TexturePickerThumbnails.Invalidate(*it);
						it = s_TexturePickerLoadedPaths.erase(it);
					}
					else {
						++it;
					}
				}
			}

			ImGui::EndChild();
			ImGui::End();
			if (!s_TexturePickerOpen) {
				ClearTexturePickerThumbnailCache();
			}
		}
	}

	void DrawNameComponentInspector(Entity entity)
	{
		auto& name = entity.GetComponent<NameComponent>();
		char buffer[256]{};
		std::snprintf(buffer, sizeof(buffer), "%s", name.Name.c_str());
		if (ImGui::InputText("##NameValue", buffer, sizeof(buffer))) {
			name.Name = buffer;
		}
	}

	void DrawTransform2DInspector(Entity entity)
	{
		auto& transform = entity.GetComponent<Transform2DComponent>();
		ImGuiUtils::DrawInspectorControl("Position", [&transform](const char* id) {
			return ImGui::DragFloat2(id, &transform.Position.x, 0.05f);
		});
		ImGuiUtils::DrawInspectorControl("Scale", [&transform](const char* id) {
			return ImGui::DragFloat2(id, &transform.Scale.x, 0.05f, 0.001f);
		});
		ImGuiUtils::DrawInspectorControl("Rotation", [&transform](const char* id) {
			return ImGui::DragFloat(id, &transform.Rotation, 0.01f);
		});
	}

	void DrawRigidbody2DInspector(Entity entity)
	{
		auto& rb2D = entity.GetComponent<Rigidbody2DComponent>();

		Vec2 position = rb2D.GetPosition();
		Vec2 velocity = rb2D.GetVelocity();
		float gravityScale = rb2D.GetGravityScale();
		float rotation = rb2D.GetRotation();

		if (ImGuiUtils::DrawInspectorControl("Position", [&position](const char* id) {
			return ImGui::DragFloat2(id, &position.x, 0.05f);
		}))
			rb2D.SetPosition(position);

		if (ImGuiUtils::DrawInspectorControl("Velocity", [&velocity](const char* id) {
			return ImGui::DragFloat2(id, &velocity.x, 0.05f);
		}))
			rb2D.SetVelocity(velocity);

		if (ImGuiUtils::DrawInspectorControl("Rotation", [&rotation](const char* id) {
			return ImGui::DragFloat(id, &rotation, 0.01f);
		}))
			rb2D.SetRotation(rotation);

		if (ImGuiUtils::DrawInspectorControl("Gravity Scale", [&gravityScale](const char* id) {
			return ImGui::SliderFloat(id, &gravityScale, 0.0f, 1.0f);
		}))
			rb2D.SetGravityScale(gravityScale);

		ImGuiUtils::DrawEnumCombo<BodyType>("Body Type", rb2D.GetBodyType(), [&rb2D](BodyType newType) {
			rb2D.SetBodyType(newType);
			});
	}

	void DrawSpriteRendererInspector(Entity entity)
	{
		auto& sprite = entity.GetComponent<SpriteRendererComponent>();
		ImGuiUtils::DrawInspectorControl("Color", [&sprite](const char* id) {
			return ImGui::ColorEdit4(id, &sprite.Color.r, ImGuiColorEditFlags_NoInputs);
		});
		ImGuiUtils::DrawInspectorControl("Sorting Order", [&sprite](const char* id) {
			return ImGui::DragScalar(id, ImGuiDataType_S16, &sprite.SortingOrder, 1.0f);
		});
		ImGuiUtils::DrawInspectorControl("Sorting Layer", [&sprite](const char* id) {
			return ImGui::DragScalar(id, ImGuiDataType_U8, &sprite.SortingLayer, 1.0f);
		});

		std::string textureDisplayName = "(None)";
		std::string textureTooltip;
		if (sprite.TextureHandle.IsValid()) {
			textureTooltip = TextureManager::GetTextureName(sprite.TextureHandle);
			if (!textureTooltip.empty()) {
				textureDisplayName = std::filesystem::path(textureTooltip).filename().string();
			}
		}

		if (DrawAssetSelectionField("Texture", textureDisplayName, textureTooltip)) {
			OpenTexturePicker(entity.GetHandle());
		}

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				std::string droppedPath(static_cast<const char*>(payload->Data));
				std::string ext = std::filesystem::path(droppedPath).extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
					AssignSpriteTexture(sprite, droppedPath);
				}
			}
			ImGui::EndDragDropTarget();
		}

		RenderTexturePicker();

		if (sprite.TextureHandle.IsValid()) {
			if (Texture2D* texture = TextureManager::GetTexture(sprite.TextureHandle)) {
				const float texWidth = texture->GetWidth();
				const float texHeight = texture->GetHeight();

				ImGuiUtils::DrawTexturePreview(texture->GetHandle(), texWidth, texHeight);
				ImGui::Text("%.0f x %.0f", texWidth, texHeight);

				ImGui::PushID("TextureSettings");
				ImGuiUtils::DrawEnumCombo<Filter>("Filter", texture->GetFilter(), [&texture](Filter newFilter) {
					texture->SetFilter(newFilter);
					});
				ImGuiUtils::DrawEnumCombo<Wrap>("Wrap U", texture->GetWrapU(), [&texture](Wrap wrapU) {
					texture->SetWrapU(wrapU);
					});
				ImGuiUtils::DrawEnumCombo<Wrap>("Wrap V", texture->GetWrapV(), [&texture](Wrap wrapV) {
					texture->SetWrapV(wrapV);
					});
				ImGui::PopID();
			}
		}
	}

	void DrawCamera2DInspector(Entity entity)
	{
		auto& camera = entity.GetComponent<Camera2DComponent>();
		float zoom = camera.GetZoom();
		if (ImGuiUtils::DrawInspectorControl("Zoom", [&zoom](const char* id) {
			return ImGui::DragFloat(id, &zoom, 0.01f, 0.01f, 100.0f);
		})) {
			camera.SetZoom(zoom);
		}
		float ortho = camera.GetOrthographicSize();
		if (ImGuiUtils::DrawInspectorControl("Orthographic Size", [&ortho](const char* id) {
			return ImGui::DragFloat(id, &ortho, 0.05f, 0.05f, 1000.0f);
		})) {
			camera.SetOrthographicSize(ortho);
		}

		Color clearColor = camera.GetClearColor();
		Color newClearColor = ImGuiUtils::DrawColorPick4("Clear Color", clearColor);
		if (newClearColor != clearColor) {
			camera.SetClearColor(newClearColor);
		}

		ImGuiUtils::DrawVec2ReadOnly("Viewport Size", camera.GetViewport()->GetSize());
		ImGuiUtils::DrawVec2ReadOnly("World Viewport", camera.WorldViewPort());
	}

	void DrawParticleSystem2DInspector(Entity entity)
	{
		auto& ps = entity.GetComponent<ParticleSystem2DComponent>();

		const std::string playbackLabel = std::string(ps.IsPlaying() ? "Pause" : "Play") + "##Value";
		if (ImGuiUtils::DrawInspectorControl("Playback", [&playbackLabel](const char*) {
			return ImGui::Button(playbackLabel.c_str());
		})) {
			if (ps.IsPlaying()) {
				ps.Pause();
			}
			else {
				ps.Play();
			}
		}

		ImGuiUtils::DrawInspectorControl("Play On Awake", [&ps](const char* id) {
			return ImGui::Checkbox(id, &ps.PlayOnAwake);
		});

		ImGuiUtils::DrawInspectorControl("LifeTime", [&ps](const char* id) {
			return ImGui::InputFloat(id, &ps.ParticleSettings.LifeTime);
		});
		ImGuiUtils::DrawInspectorControl("Scale", [&ps](const char* id) {
			return ImGui::InputFloat(id, &ps.ParticleSettings.Scale);
		});
		ImGuiUtils::DrawInspectorControl("Speed", [&ps](const char* id) {
			return ImGui::InputFloat(id, &ps.ParticleSettings.Speed);
		});

		if (ImGuiUtils::DrawInspectorControl("Gravity", [&ps](const char* id) {
			return ImGui::RadioButton(id, ps.ParticleSettings.UseGravity);
		}))
			ps.ParticleSettings.UseGravity = !ps.ParticleSettings.UseGravity;

		ImGuiUtils::DrawEnabled(ps.ParticleSettings.UseGravity, [&ps]() {
			ImGuiUtils::DrawInspectorControl("Gravity Value", [&ps](const char* id) {
				return ImGui::InputFloat2(id, &ps.ParticleSettings.Gravity.x);
			});
		});

		if (ImGuiUtils::DrawInspectorControl("Random Colors", [&ps](const char* id) {
			return ImGui::RadioButton(id, ps.ParticleSettings.UseRandomColors);
		}))
			ps.ParticleSettings.UseRandomColors = !ps.ParticleSettings.UseRandomColors;

		if (ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::PushID("Emission");
			ImGuiUtils::DrawInspectorControl("Emit Over Time", [&ps](const char* id) {
				return ImGui::InputInt(id, (int*)&ps.EmissionSettings.EmitOverTime);
			});

			ImGuiUtils::DrawEnumCombo<ParticleSystem2DComponent::ShapeType>("Shape Type", std::visit([](auto&& shape) -> ParticleSystem2DComponent::ShapeType {
				using T = std::decay_t<decltype(shape)>;
				if constexpr (std::is_same_v<T, ParticleSystem2DComponent::CircleParams>) {
					return ParticleSystem2DComponent::ShapeType::Circle;
				}
				else if constexpr (std::is_same_v<T, ParticleSystem2DComponent::SquareParams>) {
					return ParticleSystem2DComponent::ShapeType::Square;
				}
				}, ps.Shape), [&ps](ParticleSystem2DComponent::ShapeType newType) {
					if (newType == ParticleSystem2DComponent::ShapeType::Circle) {
						ps.Shape = ParticleSystem2DComponent::CircleParams{ 1.f, false };
					}
					else if (newType == ParticleSystem2DComponent::ShapeType::Square) {
						ps.Shape = ParticleSystem2DComponent::SquareParams{ Vec2{ 1.f, 1.f } };
					}
					});

				std::visit([&](auto& s) {
					using T = std::decay_t<decltype(s)>;
					if constexpr (std::is_same_v<T, ParticleSystem2DComponent::CircleParams>) {
						ImGuiUtils::DrawInspectorControl("Radius", [&s](const char* id) {
							return ImGui::InputFloat(id, &s.Radius);
						});
						ImGuiUtils::DrawInspectorControl("On Circle Edge", [&s](const char* id) {
							return ImGui::Checkbox(id, &s.IsOnCircle);
						});
					}
					else if constexpr (std::is_same_v<T, ParticleSystem2DComponent::SquareParams>) {
						ImGuiUtils::DrawVec2("Half Extents", s.HalfExtends);
					}
					}, ps.Shape);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("Rendering")) {
			ImGui::PushID("Rendering");
			ps.RenderingSettings.Color = ImGuiUtils::DrawColorPick4("Color", ps.RenderingSettings.Color);
			ImGuiUtils::DrawInspectorControl("Max Particles", [&ps](const char* id) {
				return ImGui::DragInt(id, (int*)&ps.RenderingSettings.MaxParticles, 1.0f, 1, 10000);
			});
			ImGuiUtils::DrawInspectorControl("Sorting Order", [&ps](const char* id) {
				return ImGui::InputInt(id, (int*)&ps.RenderingSettings.SortingOrder);
			});
			ImGuiUtils::DrawInspectorControl("Sorting Layer", [&ps](const char* id) {
				return ImGui::InputInt(id, (int*)&ps.RenderingSettings.SortingLayer);
			});

			if (Texture2D* texture = TextureManager::GetTexture(ps.GetTextureHandle())) {
				ImGuiUtils::DrawTexturePreview(texture->GetHandle(), texture->GetWidth(), texture->GetHeight());
			}
			ImGui::PopID();
		}
	}

	void DrawBoxCollider2DInspector(Entity entity)
	{
		auto& collider = entity.GetComponent<BoxCollider2DComponent>();
		Scene* scene = SceneManager::Get().GetActiveScene();
		if (!scene || !scene->IsValid(entity.GetHandle()) || !scene->HasComponent<Transform2DComponent>(entity.GetHandle())) {
			ImGui::TextDisabled("Box collider requires a valid Transform 2D");
			return;
		}

		const auto& transform = scene->GetComponent<Transform2DComponent>(entity.GetHandle());
		Vec2 localSize = collider.GetLocalScale(*scene);
		Vec2 size = collider.GetScale();
		Vec2 offset = collider.GetCenter();

		if (ImGuiUtils::DrawInspectorControl("Offset", [&offset](const char* id) {
			return ImGui::DragFloat2(id, &offset.x, 0.05f);
		})) {
			collider.SetCenter(offset, *scene);
		}

		if (ImGuiUtils::DrawInspectorControl("Size", [&size](const char* id) {
			return ImGui::DragFloat2(id, &size.x, 0.05f, 0.001f, 1000.0f);
		})) {
			size.x = std::max(size.x, 0.001f);
			size.y = std::max(size.y, 0.001f);

			if (std::fabs(transform.Scale.x) > 0.0001f) {
				localSize.x = size.x / transform.Scale.x;
			}
			if (std::fabs(transform.Scale.y) > 0.0001f) {
				localSize.y = size.y / transform.Scale.y;
			}

			collider.SetScale(localSize, *scene);
		}

		bool sensor = collider.IsSensor();
		if (ImGuiUtils::DrawInspectorControl("Sensor", [&sensor](const char* id) {
			return ImGui::Checkbox(id, &sensor);
		})) {
			collider.SetSensor(sensor, *scene);
		}

		bool contactEvents = collider.CanRegisterContacts();
		if (ImGuiUtils::DrawInspectorControl("Contact Events", [&contactEvents](const char* id) {
			return ImGui::Checkbox(id, &contactEvents);
		})) {
			collider.SetRegisterContacts(contactEvents);
		}

		bool enabled = collider.IsEnabled();
		if (ImGuiUtils::DrawInspectorControl("Collider Enabled", [&enabled](const char* id) {
			return ImGui::Checkbox(id, &enabled);
		})) {
			collider.SetEnabled(enabled);
		}

		float friction = collider.GetFriction();
		if (ImGuiUtils::DrawInspectorControl("Friction", [&friction](const char* id) {
			return ImGui::DragFloat(id, &friction, 0.01f, 0.0f, 10.0f);
		})) {
			collider.SetFriction(friction);
		}

		float bounciness = collider.GetBounciness();
		if (ImGuiUtils::DrawInspectorControl("Bounciness", [&bounciness](const char* id) {
			return ImGui::DragFloat(id, &bounciness, 0.01f, 0.0f, 1.0f);
		})) {
			collider.SetBounciness(bounciness);
		}

		uint64_t layerMask = collider.GetLayer();
		if (ImGuiUtils::DrawInspectorControl("Layer Mask", [&layerMask](const char* id) {
			return ImGui::InputScalar(id, ImGuiDataType_U64, &layerMask);
		})) {
			collider.SetLayer(layerMask);
		}

		ImGuiUtils::DrawVec2ReadOnly("Local Size", localSize);
	}

	// ── Audio Picker (same pattern as Texture Picker) ──────────────

	namespace {
		struct AudioPickerEntry {
			std::string DisplayName;
			std::string RelativePath;
			std::string FullPath;
			uint64_t AssetId = 0;
		};

		static bool s_AudioPickerOpen = false;
		static char s_AudioPickerSearch[128] = {};
		static std::vector<AudioPickerEntry> s_AudioPickerEntries;
		static EntityHandle s_AudioPickerTargetEntity = entt::null;

		static void ApplyAudioPickerSelection(const AudioPickerEntry* entry) {
			Scene* scene = SceneManager::Get().GetActiveScene();
			if (scene && scene->IsValid(s_AudioPickerTargetEntity)
				&& scene->HasComponent<AudioSourceComponent>(s_AudioPickerTargetEntity)) {
				auto& audioSource = scene->GetComponent<AudioSourceComponent>(s_AudioPickerTargetEntity);
				if (!entry) {
					audioSource.SetAudioHandle(AudioHandle(), UUID(0));
				}
				else if (entry->AssetId != 0 && AssetRegistry::IsAudio(entry->AssetId)) {
					audioSource.SetAudioHandle(AudioManager::LoadAudioByUUID(entry->AssetId), UUID(entry->AssetId));
				}
				else {
					audioSource.SetAudioHandle(AudioManager::LoadAudio(entry->FullPath), UUID(0));
				}
			}

			s_AudioPickerOpen = false;
		}

		static void CollectAudioFiles(const std::string& dir, const std::string& rootDir,
			std::vector<AudioPickerEntry>& out)
		{
			if (!std::filesystem::exists(dir)) return;
			for (auto& entry : std::filesystem::recursive_directory_iterator(dir,
				std::filesystem::directory_options::skip_permission_denied))
			{
				if (!entry.is_regular_file()) continue;
				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
					AudioPickerEntry e;
					e.DisplayName = entry.path().stem().string();
					e.FullPath = entry.path().string();
					e.RelativePath = std::filesystem::relative(entry.path(), rootDir).string();
					e.AssetId = AssetRegistry::GetOrCreateAssetUUID(entry.path().string());
					out.push_back(e);
				}
			}
			std::sort(out.begin(), out.end(), [](const AudioPickerEntry& a, const AudioPickerEntry& b) {
				if (a.DisplayName == b.DisplayName) {
					return a.RelativePath < b.RelativePath;
				}
				return a.DisplayName < b.DisplayName;
			});
		}

		static void OpenAudioPicker(EntityHandle entity) {
			s_AudioPickerOpen = true;
			s_AudioPickerSearch[0] = '\0';
			s_AudioPickerTargetEntity = entity;
			s_AudioPickerEntries.clear();

			BoltProject* project = ProjectManager::GetCurrentProject();
			if (project) {
				CollectAudioFiles(project->AssetsDirectory, project->AssetsDirectory, s_AudioPickerEntries);
			}
		}

		static void RenderAudioPicker() {
			if (!s_AudioPickerOpen) return;

			ImGui::SetNextWindowSize(ImVec2(320, 380), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Select Audio", &s_AudioPickerOpen)) {
				ImGui::End();
				return;
			}

			DrawPickerHeaderControls("##AudioSearch", s_AudioPickerSearch, sizeof(s_AudioPickerSearch), s_AudioPickerOpen);

			std::string filter(s_AudioPickerSearch);
			std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

			ImGui::BeginChild("AudioList");
			if (ImGui::Selectable("(None)", false)) {
				ApplyAudioPickerSelection(nullptr);
			}

			for (const auto& entry : s_AudioPickerEntries) {
				if (!filter.empty()) {
					std::string lower = entry.DisplayName;
					std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
					std::string lowerPath = entry.RelativePath;
					std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
					if (lower.find(filter) == std::string::npos && lowerPath.find(filter) == std::string::npos) continue;
				}

				bool truncated = false;
				const std::string label = ImGuiUtils::Ellipsize(entry.DisplayName, ImGui::GetContentRegionAvail().x, &truncated) + "##" + entry.RelativePath;
				if (ImGui::Selectable(label.c_str(), false)) {
					ApplyAudioPickerSelection(&entry);
				}
				if (ImGui::IsItemHovered() && (truncated || !entry.RelativePath.empty())) {
					ImGui::SetTooltip("%s", entry.RelativePath.c_str());
				}
			}
			ImGui::EndChild();
			ImGui::End();
		}
	}

	void DrawAudioSourceInspector(Entity entity)
	{
		auto& audio = entity.GetComponent<AudioSourceComponent>();

		bool playOnAwake = audio.GetPlayOnAwake();
		if (ImGuiUtils::DrawInspectorControl("Play On Awake", [&playOnAwake](const char* id) {
			return ImGui::Checkbox(id, &playOnAwake);
		})) {
			audio.SetPlayOnAwake(playOnAwake);
		}

		float volume = audio.GetVolume();
		if (ImGuiUtils::DrawInspectorControl("Volume", [&volume](const char* id) {
			return ImGui::SliderFloat(id, &volume, 0.0f, 1.0f);
		})) {
			audio.SetVolume(volume);
		}

		float pitch = audio.GetPitch();
		if (ImGuiUtils::DrawInspectorControl("Pitch", [&pitch](const char* id) {
			return ImGui::DragFloat(id, &pitch, 0.01f, 0.1f, 3.0f);
		})) {
			audio.SetPitch(pitch);
		}

		bool loop = audio.IsLooping();
		if (ImGuiUtils::DrawInspectorControl("Loop", [&loop](const char* id) {
			return ImGui::Checkbox(id, &loop);
		})) {
			audio.SetLoop(loop);
		}

		ImGui::Spacing();
		bool hasClip = audio.GetAudioHandle().IsValid();
		const std::string audioPath = hasClip ? AudioManager::GetAudioName(audio.GetAudioHandle()) : std::string();
		const std::string audioDisplayName = hasClip && !audioPath.empty()
			? std::filesystem::path(audioPath).filename().string()
			: std::string("(None)");

		if (DrawAssetSelectionField("Clip", audioDisplayName, audioPath)) {
			OpenAudioPicker(entity.GetHandle());
		}

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				std::string droppedPath(static_cast<const char*>(payload->Data));
				std::string ext = std::filesystem::path(droppedPath).extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
					AssignAudioClip(audio, droppedPath);
				}
			}
			ImGui::EndDragDropTarget();
		}

		RenderAudioPicker();
	}

	// ── Bolt-Physics Inspectors ─────────────────────────────────────

	void DrawBoltBody2DInspector(Entity entity)
	{
		auto& body = entity.GetComponent<BoltBody2DComponent>();

		const char* bodyTypeNames[] = { "Static", "Dynamic", "Kinematic" };
		int currentType = static_cast<int>(body.Type);
		if (ImGuiUtils::DrawInspectorControl("Body Type", [&currentType, &bodyTypeNames](const char* id) {
			return ImGui::Combo(id, &currentType, bodyTypeNames, 3);
		})) {
			body.Type = static_cast<BoltPhys::BodyType>(currentType);
			if (body.m_Body) body.m_Body->SetBodyType(body.Type);
		}

		if (ImGuiUtils::DrawInspectorControl("Mass", [&body](const char* id) {
			return ImGui::DragFloat(id, &body.Mass, 0.1f, 0.001f, 10000.0f);
		})) {
			if (body.m_Body) body.m_Body->SetMass(body.Mass);
		}

		if (ImGuiUtils::DrawInspectorControl("Use Gravity", [&body](const char* id) {
			return ImGui::Checkbox(id, &body.UseGravity);
		})) {
			if (body.m_Body) body.m_Body->SetGravityEnabled(body.UseGravity);
		}

		if (ImGuiUtils::DrawInspectorControl("Boundary Check", [&body](const char* id) {
			return ImGui::Checkbox(id, &body.BoundaryCheck);
		})) {
			if (body.m_Body) body.m_Body->SetBoundaryCheckEnabled(body.BoundaryCheck);
		}

		if (body.IsValid()) {
			ImGui::Separator();
			ImGui::TextDisabled("Runtime");
			Vec2 vel = body.GetVelocity();
			ImGuiUtils::DrawVec2ReadOnly("Velocity", vel);
			Vec2 pos = body.GetPosition();
			ImGuiUtils::DrawVec2ReadOnly("Position", pos);
		}
	}

	void DrawBoltBoxCollider2DInspector(Entity entity)
	{
		auto& collider = entity.GetComponent<BoltBoxCollider2DComponent>();

		if (ImGuiUtils::DrawInspectorControl("Half Extents", [&collider](const char* id) {
			return ImGui::DragFloat2(id, &collider.HalfExtents.x, 0.05f, 0.01f, 100.0f);
		})) {
			if (collider.m_Collider) {
				collider.m_Collider->SetHalfExtents({ collider.HalfExtents.x, collider.HalfExtents.y });
			}
		}
	}

	void DrawBoltCircleCollider2DInspector(Entity entity)
	{
		auto& collider = entity.GetComponent<BoltCircleCollider2DComponent>();

		if (ImGuiUtils::DrawInspectorControl("Radius", [&collider](const char* id) {
			return ImGui::DragFloat(id, &collider.Radius, 0.05f, 0.01f, 100.0f);
		})) {
			if (collider.m_Collider) {
				collider.m_Collider->SetRadius(collider.Radius);
			}
		}
	}

}
