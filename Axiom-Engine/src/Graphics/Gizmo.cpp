#include "pch.hpp"
#include "Gizmo.hpp"

namespace Axiom {
	std::vector<Square> Gizmo::s_Squares;
	std::vector<Circle> Gizmo::s_Circles;
	std::vector<Line> Gizmo::s_Lines;
	size_t Gizmo::s_MaxVertices = 100000;
	size_t Gizmo::s_RegisteredVertices = 0;
	float Gizmo::s_LineWidth = 1.0f;
	AABB Gizmo::s_CamViewportAABB{};
	bool Gizmo::s_HasViewportOverride = false;

	bool Gizmo::s_IsEnabled = true;
	bool Gizmo::s_ShowInRuntime = true;
	GizmoLayer Gizmo::s_Layer = GizmoLayer::Shared;
	Color Gizmo::s_Color = { 0.f, 1.f, 0.f, 1.f };

	void Gizmo::DrawCircle(const Vec2& center, float radius, int segments) {
		if (!s_IsEnabled || s_RegisteredVertices + segments >= s_MaxVertices)
			return;

		s_RegisteredVertices += segments;
		s_Circles.emplace_back(Circle{ center, radius, segments, s_Color, s_Layer });
	}

	void Gizmo::DrawSquare(const Vec2& center, const Vec2& scale, float degrees) {
		if (!s_IsEnabled || s_RegisteredVertices + k_BoxVertices >= s_MaxVertices)
			return;

		s_RegisteredVertices += k_BoxVertices;
		s_Squares.emplace_back(Square{ center, scale / 2.f, Radians<float>(degrees), s_Color, s_Layer });
	}

	void Gizmo::DrawLine(const Vec2& start, const Vec2& end) {
		if (!s_IsEnabled || s_RegisteredVertices + k_LineVertices >= s_MaxVertices)
			return;

		s_RegisteredVertices += k_LineVertices;
		s_Lines.emplace_back(Line{ start, end, s_Color, s_Layer });
	}

	void Gizmo::Clear() {
		s_Squares.clear();
		s_Lines.clear(); 
		s_Circles.clear();
		s_RegisteredVertices = 0; 
	}
}
