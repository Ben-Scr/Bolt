#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <limits>

namespace Axiom {
    struct AXIOM_API TextureHandle {
        uint16_t index;
        uint16_t generation;

        static constexpr uint16_t k_InvalidIndex = std::numeric_limits<uint16_t>::max();
        static TextureHandle Invalid() { return TextureHandle(k_InvalidIndex, 0); }

        TextureHandle(uint16_t index, uint16_t generation) : index{ index }, generation{ generation } {}
        TextureHandle() : index{ k_InvalidIndex }, generation{ 0 } {}

        bool IsValid() const { return index != k_InvalidIndex; }

        bool operator==(const TextureHandle& other) const {
            return index == other.index && generation == other.generation;
        }

        bool operator!=(const TextureHandle& other) const {
            return !(*this == other);
        }
    };
}
