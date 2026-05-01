#pragma once
#include "Graphics/Texture2D.hpp"
#include "Gui/AssetType.hpp"

#include <imgui.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace Axiom {

	class ThumbnailCache {
	public:
		void Initialize();
		void Shutdown();

		// Returns the OpenGL texture ID for the given asset, or 0 if none.
		// For image files, loads and caches the actual image as a thumbnail.
		// For other types, returns 0 (caller should use DrawAssetIcon instead).
		unsigned int GetThumbnail(const std::string& absolutePath);

		// Returns the cached Texture2D for an already-loaded thumbnail, or nullptr.
		Texture2D* GetCacheEntry(const std::string& absolutePath);

		// Removes a cached thumbnail (e.g. after file delete/rename).
		void Invalidate(const std::string& absolutePath);

		// Clears the entire cache.
		void Clear();

		// Draws a type-appropriate icon using ImGui draw primitives.
		// Used for folders and non-image assets.
		static void DrawAssetIcon(AssetType type, ImVec2 pos, float size);

		// Determines the asset type from a file extension.
		static AssetType GetAssetType(const std::string& extension);

		// Returns a display label for an asset type.
		static const char* GetAssetTypeLabel(AssetType type);

	private:
		struct CachedThumbnail {
			std::unique_ptr<Texture2D> Texture;
			unsigned int GlHandle = 0;
		};

		std::unordered_map<std::string, CachedThumbnail> m_Cache;
	};

} // namespace Axiom
