#pragma once
#include "Collections/AABB.hpp"
#include "Collections/Color.hpp"
#include <Math/Math.hpp>
#include <cstdint>
#include <vector>

namespace Axiom {
	enum class GizmoLayer : uint8_t {
		EditorOnly = 1 << 0,
		Shared = 1 << 1
	};

	enum class GizmoLayerMask : uint8_t {
		None = 0,
		EditorOnly = static_cast<uint8_t>(GizmoLayer::EditorOnly),
		Shared = static_cast<uint8_t>(GizmoLayer::Shared),
		All = static_cast<uint8_t>(GizmoLayer::EditorOnly) | static_cast<uint8_t>(GizmoLayer::Shared)
	};

	constexpr GizmoLayerMask operator|(GizmoLayerMask lhs, GizmoLayerMask rhs) {
		return static_cast<GizmoLayerMask>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
	}

	constexpr bool HasAnyLayer(GizmoLayer layer, GizmoLayerMask mask) {
		return (static_cast<uint8_t>(layer) & static_cast<uint8_t>(mask)) != 0;
	}

	struct AXIOM_API Square { Vec2 Center, HalfExtents; float Radiant; Color Color; GizmoLayer Layer = GizmoLayer::Shared; };
	struct AXIOM_API Circle { Vec2 Center; float Radius; int Segments; Color Color; GizmoLayer Layer = GizmoLayer::Shared; };
	struct AXIOM_API Line { Vec2 Start; Vec2 End; Color Color; GizmoLayer Layer = GizmoLayer::Shared; };

	struct AXIOM_API Box { Vec3 Center, HalfExtents; float Radiant; Color Color; };
	struct AXIOM_API Sphere { Vec3 Center; float Radius; int Segments; Color Color; };
	struct AXIOM_API Line3D { Vec3 Start; Vec3 End; Color Color; };

	class AXIOM_API Gizmo {
	public:
		// 2D
		static void DrawSquare(const Vec2& center, const Vec2& scale, float degrees);
		static void DrawLine(const Vec2& start, const Vec2& end);
		static void DrawCircle(const Vec2& center, float radius, int segments = 32);

		// 3D
		/*
		static void DrawBox(const Vec3& center, const Vec3& scale, const Vec3& rotation);
		static void DrawSphere(const Vec3& center, const float radius);
		static void DrawLine3D(const Vec3& start, const Vec3& end);
		*/

		static void SetLineWidth(float width) { s_LineWidth = Max(0.001f, width); }
		static float GetLineWidth() { return s_LineWidth; }

		static void SetEnabled(bool enabled) { s_IsEnabled = enabled; }
		static bool IsEnabled() { return s_IsEnabled; }

		static void SetShowInRuntime(bool show) { s_ShowInRuntime = show; }
		static bool GetShowInRuntime() { return s_ShowInRuntime; }

		static void SetColor(const Color& color) { s_Color = color; }
		static Color GetColor() { return s_Color; }
		static void SetLayer(GizmoLayer layer) { s_Layer = layer; }
		static GizmoLayer GetLayer() { return s_Layer; }

		static void SetMaxVertices(size_t maxVertices) { s_MaxVertices = maxVertices; };
		static size_t GetMaxVertices() { return s_MaxVertices; }

		static size_t GetRegisteredVertices() { return s_RegisteredVertices; }
		static void SetViewportAABBOverride(const AABB& viewportAABB) { s_CamViewportAABB = viewportAABB; s_HasViewportOverride = true; }
		static void ClearViewportAABBOverride() { s_HasViewportOverride = false; }
		static bool HasViewportAABBOverride() { return s_HasViewportOverride; }
		static const AABB& GetViewportAABBOverride() { return s_CamViewportAABB; }

		static void Clear();
		static AABB s_CamViewportAABB;
	private:
		static bool s_HasViewportOverride;
		static bool s_IsEnabled;
		static bool s_ShowInRuntime;
		static GizmoLayer s_Layer;

		static  Color s_Color;;

		static const size_t k_BoxVertices = 4;
		static const size_t k_LineVertices = 1;
		static float s_LineWidth;

		static std::vector<Square> s_Squares;
		static std::vector<Circle> s_Circles;
		static std::vector<Line> s_Lines;

		static size_t s_MaxVertices;
		static size_t s_RegisteredVertices;

		friend class GizmoRenderer2D;
		friend class Renderer2D;
	};
}
