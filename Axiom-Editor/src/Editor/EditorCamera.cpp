#include "pch.hpp"
#include "EditorCamera.hpp"
#include <Math/Math.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Axiom {

	void EditorCamera::SetViewportSize(int width, int height) {
		if (width <= 0 || height <= 0) return;
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		UpdateProjection();
	}

	void EditorCamera::Update(float deltaTime, bool isHovered, const Vec2& mouseDelta, float scroll) {
		if (!isHovered) return;

		if (scroll != 0.0f) {
			float zoomDelta = -scroll * ZoomSpeed * 0.1f;
			OrthographicSize = Max(0.5f, OrthographicSize + zoomDelta * OrthographicSize);
			UpdateProjection();
		}

		if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
			float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
			float worldHeight = 2.0f * OrthographicSize * Zoom;
			float worldWidth = worldHeight * aspect;

			float pixelsToWorldX = worldWidth / static_cast<float>(m_ViewportWidth);
			float pixelsToWorldY = worldHeight / static_cast<float>(m_ViewportHeight);

			Position.x -= mouseDelta.x * pixelsToWorldX;
			Position.y += mouseDelta.y * pixelsToWorldY;
			UpdateView();
		}
	}

	glm::mat4 EditorCamera::GetViewProjectionMatrix() const {
		return m_ProjMat * m_ViewMat;
	}

	AABB EditorCamera::GetViewportAABB() const {
		float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
		float halfH = OrthographicSize * Zoom;
		float halfW = halfH * aspect;
		return AABB::Create(Position, Vec2(halfW, halfH));
	}

	void EditorCamera::UpdateProjection() {
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0) return;
		float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
		float halfH = OrthographicSize * Zoom;
		float halfW = halfH * aspect;
		m_ProjMat = glm::ortho(-halfW, halfW, -halfH, halfH, 0.0f, 100.0f);
	}

	void EditorCamera::UpdateView() {
		glm::mat4 camModel(1.0f);
		camModel = glm::translate(camModel, glm::vec3(Position.x, Position.y, 0.0f));
		m_ViewMat = glm::inverse(camModel);
	}

}
