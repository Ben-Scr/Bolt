#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Scene/SceneDefinition.hpp"
#include "Scene/ComponentRegistry.hpp"

namespace Bolt {
	class PhysicsSystem2D;
}

namespace Bolt {
	class Scene;

	class BOLT_API SceneManager {
		friend class Window;

	public:
		SceneManager() = default;
		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

		template<typename T>
		void RegisterComponentType(ComponentInfo componentInfo) {
			m_ComponentRegistry.Register<T>(componentInfo);
		}
		ComponentRegistry& GetComponentRegistry() {
			return m_ComponentRegistry;
		}
		const ComponentRegistry& GetComponentRegistry() const {
			return m_ComponentRegistry;
		}
		SceneDefinition& RegisterScene(const std::string& name);
		std::weak_ptr<Scene> LoadScene(const std::string& name);

		std::weak_ptr<Scene> LoadSceneAdditive(const std::string& name);
		std::weak_ptr<Scene> ReloadScene(const std::string& name);

		void UnloadScene(const std::string& name);
		void UnloadAllScenes(bool includePersistent = false);

		std::vector<std::weak_ptr<Scene>> GetLoadedScenes();
		std::weak_ptr<Scene> GetLoadedScene(const std::string& name);
		Scene* GetActiveScene();
		const Scene* GetActiveScene() const;

		bool SetActiveScene(const std::string& name);

		bool HasSceneDefinition(const std::string& name) const;
		bool IsSceneLoaded(const std::string& name) const;

		bool IsInitialized() const { return m_IsInitialized; }

		std::vector<std::string> GetRegisteredSceneNames() const;
		std::vector<std::string> GetLoadedSceneNames() const;

		void ForeachLoadedScene(const std::function<void(const Scene&)>& func) const {
			for (const std::shared_ptr<Scene>& scenePointer : m_LoadedScenes) {
				if (scenePointer) {
					func(*scenePointer);
				}
			}
		}

		static SceneManager& Get();

	private:
		using SceneDefinitionMap = std::unordered_map<std::string, std::unique_ptr<SceneDefinition>>;
		using LoadedSceneList = std::vector<std::shared_ptr<Scene>>;
		using SceneSetupCallback = std::function<void(Scene&)>;

		void Initialize();
		void RegisterCoreComponents();
		void Shutdown();

		void UpdateScenes();
		void OnGuiScenes();
		void FixedUpdateScenes();
		void InitializeStartupScenes();
		std::shared_ptr<Scene> LoadSceneInternal(const std::string& name,
			bool additive,
			SceneSetupCallback setupCallback = {},
			bool makeActive = false,
			std::optional<size_t> insertIndex = std::nullopt);
		SceneDefinition* GetSceneDefinition(const std::string& name);
		const SceneDefinition* GetSceneDefinition(const std::string& name) const;
		LoadedSceneList::iterator FindLoadedSceneIterator(const std::string& name);
		LoadedSceneList::const_iterator FindLoadedSceneIterator(const std::string& name) const;
		void RollbackSceneLoad(const std::shared_ptr<Scene>& scene, size_t awakenedSystemCount);
		void ReleaseScene(LoadedSceneList::iterator it);
		void RefreshActiveScene();

		SceneDefinitionMap m_SceneDefinitions;
		std::vector<std::string> m_SceneDefinitionOrder;
		LoadedSceneList m_LoadedScenes;
		ComponentRegistry m_ComponentRegistry;
		Scene* m_ActiveScene = nullptr;
		bool m_IsInitialized = false;

		friend class Application;
		friend class PhysicsSystem2D;
	};
}
