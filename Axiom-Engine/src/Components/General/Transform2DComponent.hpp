#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"
#include "Math/Trigonometry.hpp"
#include <box2d/types.h>
#include <glm/glm.hpp>

namespace Axiom {
    static inline Vec2 Hadamard(const Vec2& a, const Vec2& b) {
        return { a.x * b.x, a.y * b.y };
    }

    static inline Vec2 Rotate(const Vec2& v, float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return { c * v.x - s * v.y, s * v.x + c * v.y };
    }

	class AXIOM_API Transform2DComponent {
    public:
		Vec2 Position{ 0.0f, 0.0f };
		Vec2 Scale{ 1.0f, 1.0f };
        float Rotation{ 0.0f };   // Info: Z-Rotation angle in radians


		Transform2DComponent() = default;
        Transform2DComponent(const Vec2& position) : Position{ position } {};
        Transform2DComponent(const Vec2& position, const Vec2& scale) : Position{ position }, Scale{ scale } {};
        Transform2DComponent(const Vec2& position, const Vec2& scale, float rotation) : Position{ position }, Scale{scale}, Rotation{ rotation } {};

        static Transform2DComponent FromPosition(const Vec2& pos);
        static Transform2DComponent FromScale(const Vec2& scale);

        bool IsDirty() const { return m_Dirty; }
        void MarkDirty() { m_Dirty = true; }
        void ClearDirty() { m_Dirty = false; }
        void SetPosition(const Vec2& position) { Position = position; MarkDirty(); }
        void SetRotation(float rotation) { Rotation = rotation; MarkDirty(); }
        void SetScale(const Vec2& scale) { Scale = scale; MarkDirty(); }

        float GetRotationDegrees() const;
        glm::mat3 GetModelMatrix() const;
        Vec2 GetForwardDirection() const;

        Vec2 TransformPoint(const Vec2& localPoint) const {
            Vec2 p = Hadamard(localPoint, Scale); // S
            p = Rotate(p, Rotation);              // R
            p = { p.x + Position.x, p.y + Position.y }; // T
            return p;
        }
        Vec2 TransformVector(const Vec2& localVec) const {
            Vec2 v = Hadamard(localVec, Scale);
            return Rotate(v, Rotation);
        }

        // Info: Used internally for Box2D
        b2Rot GetB2Rotation() const;
        bool operator==(const Transform2DComponent& other) const {
            return Position == other.Position
                && Rotation == other.Rotation
                && Scale == other.Scale;
        }
        bool operator!=(const Transform2DComponent& other) const {
            return !(*this == other);
        }

        // No arithmetic operators — composing whole transforms has no well-defined semantics.
    private:
        bool m_Dirty = true;
	};

    static inline float LookAt2D(const Transform2DComponent& from, const Vec2& to) {
        Vec2 lookDir = to - from.Position;
        float lookAtZ = atan2(lookDir.x, lookDir.y);
        return std::remainder(lookAtZ - from.Rotation, TwoPi<float>());
    }
}
