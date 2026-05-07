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
        // World-space cached values. These are READ by renderer, physics, camera,
        // gizmos, scripts, etc. — keeping the public field name unchanged keeps
        // every reader compatible. They are WRITTEN by TransformHierarchySystem
        // each frame from the Local* values composed with the parent's world
        // transform (or by the physics engine for rigidbody entities).
		Vec2 Position{ 0.0f, 0.0f };
		Vec2 Scale{ 1.0f, 1.0f };
        float Rotation{ 0.0f };   // Info: Z-Rotation angle in radians

        // Authored local-space values. For root entities these match Position
        // /Scale/Rotation. For child entities they describe the offset from
        // the parent's world transform. Edits in the inspector and JSON
        // (de)serialization act on these — Position is a derived snapshot.
        Vec2 LocalPosition{ 0.0f, 0.0f };
        Vec2 LocalScale{ 1.0f, 1.0f };
        float LocalRotation{ 0.0f };


		Transform2DComponent() = default;
        // Constructors mirror the world value into Local* so freshly-created
        // root entities behave the same as before the hierarchy split.
        Transform2DComponent(const Vec2& position)
            : Position{ position }, LocalPosition{ position } {};
        Transform2DComponent(const Vec2& position, const Vec2& scale)
            : Position{ position }, Scale{ scale }, LocalPosition{ position }, LocalScale{ scale } {};
        Transform2DComponent(const Vec2& position, const Vec2& scale, float rotation)
            : Position{ position }, Scale{scale}, Rotation{ rotation },
              LocalPosition{ position }, LocalScale{ scale }, LocalRotation{ rotation } {};

        static Transform2DComponent FromPosition(const Vec2& pos);
        static Transform2DComponent FromScale(const Vec2& scale);

        bool IsDirty() const { return m_Dirty; }
        void MarkDirty() { m_Dirty = true; }
        void ClearDirty() { m_Dirty = false; }
        // Setters write only the authored Local value. World (Position/Scale
        // /Rotation) is recomputed by TransformHierarchySystem; in particular
        // the editor calls TransformHierarchySystem::Propagate immediately
        // before drawing the viewport FBO so the slider edit is reflected the
        // same frame. Writing Position here would briefly stamp the local
        // value into the world cache and the renderer would draw the entity
        // at "local interpreted as world" for one frame before propagation
        // corrected it — that was the source of the one-frame jump on child
        // transform edits.
        void SetPosition(const Vec2& position) { LocalPosition = position; MarkDirty(); }
        void SetRotation(float rotation) { LocalRotation = rotation; MarkDirty(); }
        void SetScale(const Vec2& scale) { LocalScale = scale; MarkDirty(); }

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
