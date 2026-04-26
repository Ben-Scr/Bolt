#pragma once

#include "Bolt.hpp"

#include "Components/General/Transform2DComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/NativeEngineAPI.hpp"
#include "Scripting/NativeScriptRegistry.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace Bolt {

	class NativeScript {
	public:
		virtual ~NativeScript() = default;

		virtual void Start() {}
		virtual void Update(float deltaTime) {}
		virtual void OnDestroy() {}

		uint32_t GetEntityID() const { return m_EntityID; }
		EntityHandle GetEntityHandle() const { return m_Entity; }
		Scene* GetScene() const { return m_Scene; }
		entt::registry* GetRegistry() const { return m_Scene ? &m_Scene->GetRegistry() : nullptr; }

		template<typename TComponent, typename... Args>
			requires (!std::is_empty_v<TComponent>)
		TComponent& AddComponent(Args&&... args) {
			entt::registry* registry = GetRegistry();
			if (registry->all_of<TComponent>(m_Entity)) {
				return registry->get<TComponent>(m_Entity);
			}

			if constexpr (std::is_constructible_v<TComponent, EntityHandle, Args...>) {
				return registry->emplace<TComponent>(m_Entity, m_Entity, std::forward<Args>(args)...);
			}
			else if constexpr (sizeof...(Args) == 0 && std::is_default_constructible_v<TComponent>) {
				return registry->emplace<TComponent>(m_Entity);
			}
			else {
				return registry->emplace<TComponent>(m_Entity, std::forward<Args>(args)...);
			}
		}

		template<typename TTag>
			requires std::is_empty_v<TTag>
		void AddComponent() {
			entt::registry* registry = GetRegistry();
			if (registry && !registry->all_of<TTag>(m_Entity)) {
				registry->emplace<TTag>(m_Entity);
			}
		}

		template<typename TComponent>
		bool HasComponent() const {
			entt::registry* registry = GetRegistry();
			return registry && registry->valid(m_Entity) && registry->all_of<TComponent>(m_Entity);
		}

		template<typename TComponent>
		TComponent& GetComponent() {
			return GetRegistry()->get<TComponent>(m_Entity);
		}

		template<typename TComponent>
		const TComponent& GetComponent() const {
			return GetRegistry()->get<TComponent>(m_Entity);
		}

		template<typename TComponent>
		bool TryGetComponent(TComponent*& out) {
			entt::registry* registry = GetRegistry();
			if (!registry || m_Entity == entt::null) {
				out = nullptr;
				return false;
			}

			out = registry->try_get<TComponent>(m_Entity);
			return out != nullptr;
		}

		template<typename TComponent>
		void RemoveComponent() {
			entt::registry* registry = GetRegistry();
			if (registry && registry->all_of<TComponent>(m_Entity)) {
				registry->remove<TComponent>(m_Entity);
			}
		}

		// Convenience: position/rotation access via the bound scene, with API fallback.
		void GetPosition(float& x, float& y) const {
			if (HasComponent<Transform2DComponent>()) {
				const auto& transform = GetComponent<Transform2DComponent>();
				x = transform.Position.x;
				y = transform.Position.y;
				return;
			}

			if (g_EngineAPI) g_EngineAPI->GetPosition(m_EntityID, &x, &y);
		}
		void SetPosition(float x, float y) {
			if (HasComponent<Transform2DComponent>()) {
				GetComponent<Transform2DComponent>().Position = { x, y };
				return;
			}

			if (g_EngineAPI) g_EngineAPI->SetPosition(m_EntityID, x, y);
		}
		float GetRotation() const {
			if (HasComponent<Transform2DComponent>()) {
				return GetComponent<Transform2DComponent>().Rotation;
			}

			return g_EngineAPI ? g_EngineAPI->GetRotation(m_EntityID) : 0.0f;
		}
		void SetRotation(float rot) {
			if (HasComponent<Transform2DComponent>()) {
				GetComponent<Transform2DComponent>().Rotation = rot;
				return;
			}

			if (g_EngineAPI) g_EngineAPI->SetRotation(m_EntityID, rot);
		}

	private:
		friend class NativeScriptHost;
		uint32_t m_EntityID = 0;
		EntityHandle m_Entity = entt::null;
		Scene* m_Scene = nullptr;
	};

} // namespace Bolt

#define BT_NATIVE_LOG_INFO(msg)  do { if (Bolt::g_EngineAPI && Bolt::g_EngineAPI->LogInfo) Bolt::g_EngineAPI->LogInfo(msg); } while(0)
#define BT_NATIVE_LOG_WARN(msg)  do { if (Bolt::g_EngineAPI && Bolt::g_EngineAPI->LogWarn) Bolt::g_EngineAPI->LogWarn(msg); } while(0)
#define BT_NATIVE_LOG_ERROR(msg) do { if (Bolt::g_EngineAPI && Bolt::g_EngineAPI->LogError) Bolt::g_EngineAPI->LogError(msg); } while(0)

#define REGISTER_SCRIPT(ClassName) \
	static struct ClassName##_AutoReg { \
		ClassName##_AutoReg() { Bolt::NativeScriptRegistry::Register(#ClassName, []() -> Bolt::NativeScript* { return new ClassName(); }); } \
	} s_##ClassName##_autoreg;
