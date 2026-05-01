#pragma once
#include "Collections/Vec2.hpp"
#include "Collections/AABB.hpp"
#include <glm/glm.hpp>

namespace Axiom {

	class EditorCamera {
	public:
		void SetViewportSize(int width, int height);
		void Update(float deltaTime, bool isHovered, const Vec2& mouseDelta, float scroll);

		glm::mat4 GetViewProjectionMatrix() const;
		AABB GetViewportAABB() const;

		void SetPosition(const Vec2& pos) {
			Position = pos;
			UpdateView();
		}
		void SetOrthographicSize(const float size) {
			OrthographicSize = size;
			UpdateProjection();
		}
		void SetZoom(const float zoom) {
			Zoom = zoom;
			UpdateProjection();
		}

	private:
		Vec2 Position = { 0.0f, 0.0f };
		float OrthographicSize = 5.0f;
		float Zoom = 1.0f;
		float PanSpeed = 10.0f;
		float ZoomSpeed = 1.0f;

		void UpdateProjection();
		void UpdateView();

		int m_ViewportWidth = 1;
		int m_ViewportHeight = 1;
		glm::mat4 m_ViewMat{ 1.0f };
		glm::mat4 m_ProjMat{ 1.0f };
	};

}
