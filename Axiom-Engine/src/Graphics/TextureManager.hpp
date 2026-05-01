#pragma once
#include "Core/Export.hpp"
#include "Graphics/DefaultTexture.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureEntry.hpp"
#include "TextureHandle.hpp"
#include "Serialization/Path.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace Axiom {
    class Application;
}

namespace Axiom {
        class AXIOM_API TextureManager {
        public:
            static void Initialize();
            static void Shutdown();

            static TextureHandle LoadTexture(const std::string_view& path, Filter filter = Filter::Point, Wrap u = Wrap::Clamp, Wrap v = Wrap::Clamp);
            static TextureHandle LoadTextureByUUID(uint64_t assetId, Filter filter = Filter::Point, Wrap u = Wrap::Clamp, Wrap v = Wrap::Clamp);
            static TextureHandle GetDefaultTexture(DefaultTexture type);
            static void UnloadTexture(TextureHandle handle);
            static TextureHandle GetTextureHandle(const std::string& name);
            static Texture2D* GetTexture(TextureHandle handle);
            static std::vector<TextureHandle> GetLoadedHandles();
            static void UnloadAll(bool defaultTextures = false);
            static uint64_t GetTextureAssetUUID(TextureHandle handle);

            /// Returns the texture path relative to a texture root directory.
            /// This is the same format accepted by LoadTexture().
            static std::string GetTextureName(TextureHandle handle) {
                if (handle.index >= s_Textures.size() || !s_Textures[handle.index].IsValid
                    || s_Textures[handle.index].Generation != handle.generation)
                    return "";

                const std::string& fullName = s_Textures[handle.index].Name;

                // Try stripping any known texture root prefix
                auto tryStrip = [&](const std::string& root) -> std::string {
                    if (root.empty() || fullName.size() <= root.size()) return "";
                    if (fullName.compare(0, root.size(), root) != 0) return "";
                    size_t start = root.size();
                    if (start < fullName.size() && (fullName[start] == '/' || fullName[start] == '\\'))
                        start++;
                    return fullName.substr(start);
                };

                // Try primary root first
                std::string rel = tryStrip(s_RootPath);
                if (!rel.empty()) return rel;

                // Try user Assets/Textures as fallback
                std::string base = Path::ExecutableDir();
                rel = tryStrip(Path::Combine(base, "Assets", "Textures"));
                if (!rel.empty()) return rel;

                return fullName;
            }

            static bool IsValid(TextureHandle handle) {
                return handle.index < s_Textures.size() &&
                    s_Textures[handle.index].IsValid &&
                    s_Textures[handle.index].Generation == handle.generation;
            }

		private:
			static TextureHandle FindTextureByPath(const std::string& path, Filter filter, Wrap u, Wrap v);
			static TextureHandle FindTextureByPath(const std::string& path);
			static void LoadDefaultTextures();

            static std::array<std::string, 9> s_DefaultTextures;
            static std::vector<TextureEntry> s_Textures;
            static std::queue<uint16_t> s_FreeIndices;
            static bool s_IsInitialized;

            static std::string s_RootPath;

            friend class Renderer2D;
        };
}

namespace std {
    template<>
    struct hash<Axiom::TextureHandle> {
        size_t operator()(const Axiom::TextureHandle& h) const noexcept {
            return (static_cast<size_t>(h.index) << 16) ^ static_cast<size_t>(h.generation);
        }
    };
}
