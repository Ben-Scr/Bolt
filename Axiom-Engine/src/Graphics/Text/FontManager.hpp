#pragma once
#include "Core/Export.hpp"
#include "Graphics/Text/Font.hpp"
#include "Graphics/Text/FontHandle.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Axiom {

    // Engine-wide font cache. Mirrors the lifecycle and lookup-by-UUID
    // contract of TextureManager / AudioManager: handles stay valid until
    // explicitly Unload'd or the engine shuts down.
    //
    // Each (path, pixelSize) pair maps to its own slot — a 16px and 32px
    // bake of the same .ttf are different fonts at the GL level today.
    // A future SDF atlas would collapse the per-size axis, but not in this
    // MVP. Components reference fonts by UUID (the .ttf's GUID), so the
    // pixelSize sits on the consumer (TextRendererComponent::FontSize) and is
    // resolved at render time.
    class AXIOM_API FontManager {
    public:
        static bool Initialize();
        static void Shutdown();
        static bool IsInitialized() { return s_IsInitialized; }

        static FontHandle LoadFont(std::string_view path, float pixelSize);
        static FontHandle LoadFontByUUID(uint64_t assetId, float pixelSize);
        static void UnloadFont(const FontHandle& handle);
        static void UnloadAll();

        static bool IsValid(const FontHandle& handle);
        static Font* GetFont(const FontHandle& handle);
        static uint64_t GetFontAssetUUID(const FontHandle& handle);

        // Returns a shared default font (the engine's bundled DefaultSans
        // bake at 32 px). Loaded lazily on first request. Used when a
        // TextRendererComponent has no font assigned so editor/runtime never
        // shows zero-size empty text.
        static FontHandle GetDefaultFont();

    private:
        struct Slot {
            std::unique_ptr<Font> Font;
            uint64_t AssetUUID = 0;
            float PixelSize = 0.0f;
            uint16_t Generation = 0;
            bool InUse = false;
        };

        static FontHandle CreateSlot(std::unique_ptr<Font> font, uint64_t assetUUID, float pixelSize);
        static FontHandle FindExisting(uint64_t assetUUID, float pixelSize);

        // Each (uuid, pixelSize) pair gets its own slot — the Font object
        // owns a GL atlas which is sized for one specific px height.
        struct LookupKey {
            uint64_t Uuid = 0;
            int PixelSizeQuantized = 0;
            bool operator==(const LookupKey& o) const noexcept {
                return Uuid == o.Uuid && PixelSizeQuantized == o.PixelSizeQuantized;
            }
        };
        struct LookupKeyHash {
            size_t operator()(const LookupKey& k) const noexcept {
                return std::hash<uint64_t>{}(k.Uuid) ^ (std::hash<int>{}(k.PixelSizeQuantized) << 1);
            }
        };

        inline static bool s_IsInitialized = false;
        inline static std::vector<Slot> s_Slots;
        inline static std::unordered_map<LookupKey, uint16_t, LookupKeyHash> s_Lookup;
        inline static FontHandle s_DefaultFont;
    };

}
