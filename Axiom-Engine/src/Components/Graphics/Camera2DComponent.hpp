#pragma once
#include "Components/General/Transform2DComponent.hpp"
#include "Collections/AABB.hpp"
#include "Collections/Color.hpp"
#include "Collections/Viewport.hpp"
#include "Scene/EntityHandle.hpp"

#include <glm/glm.hpp>
#include <memory>

namespace Axiom {
	class Scene;

	class AXIOM_API Camera2DComponent {
	public:
		Camera2DComponent() = default;

		static Camera2DComponent* Main();

		void UpdateViewport();

		void SetOrthographicSize(float size) { m_OrthographicSize = (size > 0 ? size : 0.001f); UpdateProj(); }
		void AddOrthographicSize(float ds) { SetOrthographicSize(m_OrthographicSize + ds); }
		float GetOrthographicSize() const { return m_OrthographicSize; }

		void SetPosition(Vec2 p);
		void AddPosition(Vec2 p);
		Vec2 GetPosition() const;

		void SetRotation(float rad);
		float GetRotation() const;

		void SetZoom(float z) { m_Zoom = z; UpdateProj(); }
		float GetZoom() const { return m_Zoom; }

		void SetClearColor(const Color& color) { m_ClearColor = color; }
		const Color& GetClearColor() const { return m_ClearColor; }


		AABB GetViewportAABB() const { return m_WorldViewportAABB; }
		Viewport* GetViewport() const { return m_Viewport; }
		Vec2 WorldViewPort() const;
		Vec2 ScreenToWorld(Vec2 pos) const;

		glm::mat4 GetViewProjectionMatrix() const;
		const glm::mat4 GetViewMatrix() const { return m_ViewMat; }
		const glm::mat4 GetProjectionMatrix() const { return m_ProjMat; }

		float ViewportWidth() const { return static_cast<float>(m_Viewport->GetWidth()); }
		float ViewportHeight() const { return static_cast<float>(m_Viewport->GetHeight()); }

		bool IsValid() const { return m_OwnerScene != nullptr; }
	private:
		void SetViewport(Viewport* viewport) { m_Viewport = viewport; }
		void UpdateProj();
		void UpdateView();
		void UpdateViewportAABB();

		// Re-lookup each call; EnTT's packed storage relocates instances on emplace/destroy.
		Transform2DComponent* TryGetTransform();
		const Transform2DComponent* TryGetTransform() const;

		void Initialize(Scene& scene, EntityHandle entity);
		void Destroy();

		EntityHandle m_OwnerEntity{ entt::null };
		Scene* m_OwnerScene = nullptr;
		float m_Zoom{ 1.0f };
		float m_OrthographicSize{ 5.0f };
		Color m_ClearColor{ 0.1f, 0.1f, 0.1f, 1.0f };
		Viewport* m_Viewport = nullptr;

		glm::mat4 m_ViewMat{};
		glm::mat4 m_ProjMat{};
		AABB m_WorldViewportAABB;

		friend class Scene;
	};
}
