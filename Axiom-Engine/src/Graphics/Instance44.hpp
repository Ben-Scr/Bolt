#pragma once
#include "Collections/Vec2.hpp"
#include "Collections/Color.hpp"
#include "Graphics/TextureHandle.hpp"

namespace Axiom {
    struct Instance44 {
        Vec2 Position{};
        Vec2 Scale{ 1.0f, 1.0f };
        float Rotation{ 0.0f };
        Color Color{};
        TextureHandle TextureHandle{};
        short SortingOrder{ 0 };
        std::uint8_t SortingLayer{ 0 };

        Instance44(Vec2 pos,
            Vec2 scale,
            float rotation,
            Axiom::Color color,
            Axiom::TextureHandle tex,
            short sortingOrder,
            std::uint8_t sortingLayer)
            : Position(pos)
            , Scale(scale)
            , Rotation(rotation)
            , Color(color)
            , TextureHandle(tex)
            , SortingOrder(sortingOrder)
            , SortingLayer(sortingLayer)
        {
        }

        Instance44() = default;

        Instance44(const Instance44&) = default;
        Instance44& operator=(const Instance44&) = default;
        Instance44(Instance44&&) noexcept = default;
        Instance44& operator=(Instance44&&) noexcept = default;
    };
}