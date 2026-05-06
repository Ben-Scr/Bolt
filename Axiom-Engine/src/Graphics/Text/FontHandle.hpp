#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <limits>

namespace Axiom {
    // Stable GUID assigned to the engine-shipped DefaultSans-Regular.ttf so
    // scenes can serialize a reference to it without writing the .ttf into
    // every project. Top-byte 0xAB tags engine-shipped built-ins (see
    // AssetRegistry::RegisterBuiltInAsset). Lives in this lightweight
    // header so component default-initializers can use it without pulling
    // in FontManager.hpp (and transitively Font.hpp + stb_truetype).
    inline constexpr uint64_t k_DefaultFontAssetId = 0xAB00000000000001ULL;

    // Stable handle into FontManager's slot table. Mirrors TextureHandle's
    // shape (16-bit slot index + 16-bit generation) so the same patterns
    // (`IsValid`, equality, `Invalid()`) carry over to font references.
    struct AXIOM_API FontHandle {
        uint16_t index;
        uint16_t generation;

        static constexpr uint16_t k_InvalidIndex = std::numeric_limits<uint16_t>::max();
        static FontHandle Invalid() { return FontHandle(k_InvalidIndex, 0); }

        FontHandle(uint16_t index, uint16_t generation) : index{ index }, generation{ generation } {}
        FontHandle() : index{ k_InvalidIndex }, generation{ 0 } {}

        bool IsValid() const { return index != k_InvalidIndex; }

        bool operator==(const FontHandle& other) const {
            return index == other.index && generation == other.generation;
        }

        bool operator!=(const FontHandle& other) const {
            return !(*this == other);
        }
    };
}
