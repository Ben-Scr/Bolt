#include "pch.hpp"
#include "Camera2DComponent.hpp"
#include "Collections/Viewport.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Axiom {
	Camera2DComponent* Camera2DComponent::Main() {
		Application* app = Application::GetInstance();
		if (!app || !app->GetSceneManager()) {
			return nullptr;
		}

		Scene* activeScene = app->GetSceneManager()->GetActiveScene();
		return activeScene ? activeScene->GetMainCamera() : nullptr;
	}

	Transform2DComponent* Camera2DComponent::TryGetTransform() {
		if (!m_OwnerScene || m_OwnerEntity == entt::null) return nullptr;
		if (!m_OwnerScene->IsValid(m_OwnerEntity)) return nullptr;
		if (!m_OwnerScene->HasComponent<Transform2DComponent>(m_OwnerEntity)) return nullptr;
		return &m_OwnerScene->GetComponent<Transform2DComponent>(m_OwnerEntity);
	}

	const Transform2DComponent* Camera2DComponent::TryGetTransform() const {
		if (!m_OwnerScene || m_OwnerEntity == entt::null) return nullptr;
		if (!m_OwnerScene->IsValid(m_OwnerEntity)) return nullptr;
		if (!m_OwnerScene->HasComponent<Transform2DComponent>(m_OwnerEntity)) return nullptr;
		return &m_OwnerScene->GetComponent<Transform2DComponent>(m_OwnerEntity);
	}

	void Camera2DComponent::UpdateViewport() {
		UpdateProj();
		UpdateView();
	}

	void Camera2DComponent::SetPosition(Vec2 p) {
		if (auto* transform = TryGetTransform()) {
			// Route through Transform2DComponent::SetPosition so we update the
			// authored Local* value. Writing transform->Position directly here
			// would be silently overwritten by the next TransformHierarchySystem
			// propagation pass (which derives Position from LocalPosition) and
			// the camera would snap back.
			transform->SetPosition(p);
			// Cache the world value too so UpdateView (and any same-frame
			// reader) sees the new placement before propagation runs.
			transform->Position = p;
			UpdateView();
		}
	}

	void Camera2DComponent::AddPosition(Vec2 p) {
		if (auto* transform = TryGetTransform()) {
			SetPosition(p + transform->Position);
		}
	}

	Vec2 Camera2DComponent::GetPosition() const {
		if (const auto* transform = TryGetTransform()) {
			return transform->Position;
		}
		return Vec2{ 0.0f, 0.0f };
	}

	void Camera2DComponent::SetRotation(float rad) {
		if (auto* transform = TryGetTransform()) {
			transform->SetRotation(rad);
			transform->Rotation = rad;
			UpdateView();
		}
	}

	float Camera2DComponent::GetRotation() const {
		if (const auto* transform = TryGetTransform()) {
			return transform->Rotation;
		}
		return 0.0f;
	}

	glm::mat4 Camera2DComponent::GetViewProjectionMatrix() const {
		return m_ProjMat * m_ViewMat;
	}

	void Camera2DComponent::UpdateProj() {
		if (!m_Viewport || m_Viewport->GetWidth() == 0 || m_Viewport->GetHeight() == 0) return;
		const float aspect = m_Viewport->GetAspect();
		const float halfH = m_OrthographicSize * m_Zoom;
		const float halfW = halfH * aspect;

		const float zNear = 0.0f;
		const float zFar = 100.0f;

		m_ProjMat = glm::ortho(-halfW, +halfW, -halfH, +halfH, zNear, zFar);
		if (TryGetTransform()) {
			UpdateViewportAABB();
		}
	}

	void Camera2DComponent::UpdateView() {
		// Closed-form view = Rz(-theta) * T(-p) — replaces per-frame glm::inverse(M).
		const Transform2DComponent* transform = TryGetTransform();
		if (!transform) return;

		const float rotZ = transform->Rotation;
		const float c = std::cos(rotZ);
		const float s = std::sin(rotZ);
		const float px = transform->Position.x;
		const float py = transform->Position.y;

		// Column-major: rotation block is Rz(-theta), translation column is Rz(-theta) * (-p).
		glm::mat4 view(1.0f);
		view[0][0] =  c; view[0][1] = -s; view[0][2] = 0.0f; view[0][3] = 0.0f;
		view[1][0] =  s; view[1][1] =  c; view[1][2] = 0.0f; view[1][3] = 0.0f;
		view[2][0] = 0.0f; view[2][1] = 0.0f; view[2][2] = 1.0f; view[2][3] = 0.0f;
		view[3][0] = -c * px - s * py;
		view[3][1] =  s * px - c * py;
		view[3][2] = 0.0f;
		view[3][3] = 1.0f;
		m_ViewMat = view;

		UpdateViewportAABB();
	}

	void Camera2DComponent::UpdateViewportAABB() {
		if (!m_Viewport) return;

		const Transform2DComponent* transform = TryGetTransform();
		if (!transform) return;
		Vec2 worldViewport = WorldViewPort();
		m_WorldViewportAABB = AABB::Create(transform->Position, worldViewport / 2.f);
	}

	Vec2 Camera2DComponent::WorldViewPort() const {
		if (!m_Viewport) return { 0.0f, 0.0f };

		float aspect = m_Viewport->GetAspect();
		float worldHeight = 2.0f * (m_OrthographicSize * m_Zoom);
		float worldWidth = worldHeight * aspect;
		return { worldWidth, worldHeight };
	}

	Vec2 Camera2DComponent::ScreenToWorld(Vec2 pos) const
	{
		if (!m_Viewport) return { 0.0f, 0.0f };
		if (m_Viewport->GetWidth() == 0 || m_Viewport->GetHeight() == 0) return { 0.0f, 0.0f };
		const float xNdc = (2.0f * pos.x / float(m_Viewport->GetWidth())) - 1.0f;
		const float yNdc = 1.0f - (2.0f * pos.y / float(m_Viewport->GetHeight()));

		const float zNear = 0.0f, zFar = 100.0f;
		const float zNdc = -(zFar + zNear) / (zFar - zNear);

		const glm::vec4 clip(xNdc, yNdc, zNdc, 1.0f);

		const glm::mat4 vp = m_ProjMat * m_ViewMat;
		const glm::mat4 invVp = glm::inverse(vp);

		glm::vec4 world = invVp * clip;
		if (world.w != 0.0f) world /= world.w;

		return { world.x, world.y };
	}

	void Camera2DComponent::Initialize(Scene& scene, EntityHandle entity) {
		m_Viewport = Window::GetMainViewport();
		m_OwnerScene = &scene;
		m_OwnerEntity = entity;
		m_ViewMat = glm::mat4(1.0f);
		m_ProjMat = glm::mat4(1.0f);
		UpdateProj();
		UpdateView();
	}

	void Camera2DComponent::Destroy() {
		m_OwnerScene = nullptr;
		m_OwnerEntity = entt::null;
		m_Viewport = nullptr;
	}
}
