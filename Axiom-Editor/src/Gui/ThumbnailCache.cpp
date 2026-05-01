#include <pch.hpp>
#include "Gui/ThumbnailCache.hpp"
#include <imgui.h>
#include <algorithm>

namespace Axiom {

	void ThumbnailCache::Initialize() {
		m_Cache.clear();
	}

	void ThumbnailCache::Shutdown() {
		Clear();
	}

	unsigned int ThumbnailCache::GetThumbnail(const std::string& absolutePath) {
		auto it = m_Cache.find(absolutePath);
		if (it != m_Cache.end()) {
			return it->second.GlHandle;
		}

		// Only attempt to load image files
		std::string ext = std::filesystem::path(absolutePath).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (GetAssetType(ext) != AssetType::Image) {
			return 0;
		}

		auto tex = std::make_unique<Texture2D>(
			absolutePath.c_str(),
			Filter::Bilinear,
			Wrap::Clamp,
			Wrap::Clamp,
			false,   // no mipmaps for thumbnails
			false,
			true
		);

		if (!tex->IsValid()) {
			// Cache a null entry so we don't retry
			m_Cache[absolutePath] = { nullptr, 0 };
			return 0;
		}

		unsigned int handle = tex->GetHandle();
		m_Cache[absolutePath] = { std::move(tex), handle };
		return handle;
	}

	Texture2D* ThumbnailCache::GetCacheEntry(const std::string& absolutePath) {
		auto it = m_Cache.find(absolutePath);
		if (it != m_Cache.end() && it->second.Texture) {
			return it->second.Texture.get();
		}
		return nullptr;
	}

	void ThumbnailCache::Invalidate(const std::string& absolutePath) {
		auto it = m_Cache.find(absolutePath);
		if (it != m_Cache.end()) {
			m_Cache.erase(it);
		}
	}

	void ThumbnailCache::Clear() {
		m_Cache.clear();
	}

	AssetType ThumbnailCache::GetAssetType(const std::string& extension) {
		if (extension.empty())
			return AssetType::Unknown;

		std::string ext = extension;
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
			return AssetType::Image;
		if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".hlsl" || ext == ".shader")
			return AssetType::Shader;
		if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
			return AssetType::Audio;
		if (ext == ".ttf" || ext == ".otf")
			return AssetType::Font;
		if (ext == ".lua" || ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" || ext == ".cs")
			return AssetType::Script;
		if (ext == ".scene" || ext == ".json" || ext == ".axiom")
			return AssetType::Scene;
		if (ext == ".prefab")
			return AssetType::Prefab;
		if (ext == ".cfg" || ext == ".ini" || ext == ".yaml" || ext == ".toml" || ext == ".xml")
			return AssetType::Config;

		return AssetType::Unknown;
	}

	const char* ThumbnailCache::GetAssetTypeLabel(AssetType type) {
		switch (type) {
			case AssetType::Folder:  return "Folder";
			case AssetType::Image:   return "Image";
			case AssetType::Shader:  return "Shader";
			case AssetType::Audio:   return "Audio";
			case AssetType::Font:    return "Font";
			case AssetType::Script:  return "Script";
			case AssetType::Scene:   return "Scene";
			case AssetType::Config:  return "Config";
			case AssetType::Prefab:  return "Prefab";
			default:                 return "File";
		}
	}

	void ThumbnailCache::DrawAssetIcon(AssetType type, ImVec2 pos, float size) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float pad = size * 0.15f;
		const ImVec2 min(pos.x + pad, pos.y + pad);
		const ImVec2 max(pos.x + size - pad, pos.y + size - pad);

		switch (type) {
			case AssetType::Folder: {
				// Folder: rectangle with a tab on top-left
				const float tabW = (max.x - min.x) * 0.4f;
				const float tabH = (max.y - min.y) * 0.15f;
				const float bodyTop = min.y + tabH + 2.0f;

				drawList->AddRectFilled(
					ImVec2(min.x, min.y),
					ImVec2(min.x + tabW, min.y + tabH),
					IM_COL32(220, 180, 60, 255), 2.0f
				);
				drawList->AddRectFilled(
					ImVec2(min.x, bodyTop),
					max,
					IM_COL32(220, 180, 60, 255), 3.0f
				);
				break;
			}
			case AssetType::Image: {
				// Image icon: rectangle with mountain/sun motif
				drawList->AddRectFilled(min, max, IM_COL32(80, 140, 200, 255), 3.0f);
				const float cx = (min.x + max.x) * 0.5f;
				const float cy = (min.y + max.y) * 0.5f;
				drawList->AddCircleFilled(ImVec2(cx - size * 0.1f, cy - size * 0.08f), size * 0.08f, IM_COL32(255, 220, 80, 255));
				drawList->AddTriangleFilled(
					ImVec2(min.x + pad, max.y - pad * 0.5f),
					ImVec2(cx, cy),
					ImVec2(max.x - pad, max.y - pad * 0.5f),
					IM_COL32(60, 160, 60, 255)
				);
				break;
			}
			case AssetType::Shader: {
				drawList->AddRectFilled(min, max, IM_COL32(160, 80, 200, 255), 3.0f);
				const float cx = (min.x + max.x) * 0.5f;
				const float cy = (min.y + max.y) * 0.5f;
				drawList->AddText(ImVec2(cx - 6.0f, cy - 7.0f), IM_COL32(255, 255, 255, 255), "Sh");
				break;
			}
			case AssetType::Audio: {
				drawList->AddRectFilled(min, max, IM_COL32(60, 180, 140, 255), 3.0f);
				const float cx = (min.x + max.x) * 0.5f;
				const float cy = (min.y + max.y) * 0.5f;
				// Musical note symbol
				drawList->AddCircleFilled(ImVec2(cx - 2.0f, cy + 4.0f), 4.0f, IM_COL32(255, 255, 255, 255));
				drawList->AddLine(ImVec2(cx + 2.0f, cy + 4.0f), ImVec2(cx + 2.0f, cy - 8.0f), IM_COL32(255, 255, 255, 255), 2.0f);
				break;
			}
			case AssetType::Font: {
				drawList->AddRectFilled(min, max, IM_COL32(200, 120, 80, 255), 3.0f);
				const float cx = (min.x + max.x) * 0.5f;
				const float cy = (min.y + max.y) * 0.5f;
				drawList->AddText(ImVec2(cx - 5.0f, cy - 7.0f), IM_COL32(255, 255, 255, 255), "Aa");
				break;
			}
			case AssetType::Script: {
				drawList->AddRectFilled(min, max, IM_COL32(70, 130, 180, 255), 3.0f);
				const float cx = (min.x + max.x) * 0.5f;
				const float cy = (min.y + max.y) * 0.5f;
				drawList->AddText(ImVec2(cx - 7.0f, cy - 7.0f), IM_COL32(255, 255, 255, 255), "</>");
				break;
			}
			case AssetType::Scene: {
				drawList->AddRectFilled(min, max, IM_COL32(100, 180, 100, 255), 3.0f);
				const float cx = (min.x + max.x) * 0.5f;
				const float cy = (min.y + max.y) * 0.5f;
				drawList->AddCircle(ImVec2(cx, cy), size * 0.15f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
				break;
			}
			case AssetType::Prefab: {
				drawList->AddRectFilled(min, max, IM_COL32(70, 160, 180, 255), 3.0f);
				const float cx = (min.x + max.x) * 0.5f;
				const float cy = (min.y + max.y) * 0.5f;
				drawList->AddText(ImVec2(cx - 6.0f, cy - 7.0f), IM_COL32(255, 255, 255, 255), "Pf");
				break;
			}
			default: {
				// Generic file: white document outline
				drawList->AddRectFilled(min, max, IM_COL32(90, 90, 100, 255), 3.0f);
				const float foldSize = (max.x - min.x) * 0.25f;
				drawList->AddTriangleFilled(
					ImVec2(max.x - foldSize, min.y),
					ImVec2(max.x, min.y + foldSize),
					ImVec2(max.x - foldSize, min.y + foldSize),
					IM_COL32(120, 120, 130, 255)
				);
				break;
			}
		}
	}

} // namespace Axiom
