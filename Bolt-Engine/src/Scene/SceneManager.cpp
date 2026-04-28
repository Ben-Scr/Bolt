#include "pch.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "SceneDefinition.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Systems/AudioUpdateSystem.hpp"
#include "Systems/ParticleUpdateSystem.hpp"
#include "Core/Application.hpp"
#include "Events/SceneEvents.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"


namespace Bolt {
	namespace {
		void AddStandardSceneSystems(SceneDefinition& definition) {
			definition.AddSystem<ParticleUpdateSystem>();
			definition.AddSystem<ScriptSystem>();
			definition.AddSystem<AudioUpdateSystem>();
		}
	}

	SceneManager& SceneManager::Get() {
		auto* app = Application::GetInstance();
		BT_CORE_ASSERT(app && app->GetSceneManager(), "SceneManager is not available before the Application instance exists");
		return *app->GetSceneManager();
	}

	void SceneManager::Initialize() {
		if (m_IsInitialized) {
			BT_CORE_WARN_TAG("SceneManager", "Initialize called more than once");
			return;
		}
		if (m_SceneDefinitions.empty()) {
			BT_CORE_ERROR_TAG("SceneManager", "No scenes were registered before SceneManager initialization");
			return;
		}

		m_IsInitialized = true;
		RegisterCoreComponents();
		InitializeStartupScenes();

		if (m_LoadedScenes.empty() && !m_SceneDefinitionOrder.empty()) {
			const std::string& fallbackScene = m_SceneDefinitionOrder.front();
			if (LoadScene(fallbackScene).expired()) {
				BT_CORE_ERROR_TAG("SceneManager", "Failed to load fallback scene '{}'", fallbackScene);
			}
			else {
				BT_CORE_WARN_TAG("SceneManager", "Loaded fallback scene '{}'", fallbackScene);
			}
		}
	}

	void SceneManager::RegisterCoreComponents() {
		RegisterBuiltInComponents(*this);
	}

	void SceneManager::Shutdown() {
		UnloadAllScenes(true);
		m_SceneDefinitions.clear();
		m_SceneDefinitionOrder.clear();
		m_ActiveScene = nullptr;
		m_IsInitialized = false;
	}

	SceneDefinition& SceneManager::RegisterScene(const std::string& name) {
		auto [it, inserted] = m_SceneDefinitions.emplace(name, std::make_unique<SceneDefinition>(name));
		if (!inserted) {
			BT_CORE_WARN_TAG("SceneManager", "Scene definition '{}' already exists", name);
			return *it->second;
		}

		m_SceneDefinitionOrder.push_back(name);
		SceneDefinition& definition = *it->second;
		AddStandardSceneSystems(definition);
		return definition;
	}


	std::weak_ptr<Scene> SceneManager::LoadScene(const std::string& name) {
		return LoadSceneInternal(name, false);
	}

	std::weak_ptr<Scene> SceneManager::LoadSceneAdditive(const std::string& name) {
		return LoadSceneInternal(name, true);
	}

	std::shared_ptr<Scene> SceneManager::LoadSceneInternal(
		const std::string& name,
		bool additive,
		SceneSetupCallback setupCallback,
		bool makeActive,
		std::optional<size_t> insertIndex) {
		if (!m_IsInitialized) {
			BT_CORE_ERROR_TAG("SceneManager", "LoadScene called before SceneManager initialization");
			return {};
		}
		SceneDefinition* definition = GetSceneDefinition(name);
		if (!definition) {
			return {};
		}
		if (!additive) {
			if (IsSceneLoaded(name)) {
				BT_CORE_WARN_TAG("SceneManager", "Scene '{}' is already loaded. Use LoadSceneAdditive() for multiple instances.", name);
				return GetLoadedScene(name).lock();
			}
			UnloadAllScenes(false);
		}

		std::shared_ptr<Scene> newScene = definition->Instantiate();
		for (const auto& callback : definition->m_LoadCallbacks) {
			callback(*newScene);
		}
		if (setupCallback) {
			setupCallback(*newScene);
		}

		{
			ScenePreStartEvent e(name);
			ScriptEngine::RaiseBeforeSceneLoaded(name);
			Application* app = Application::GetInstance();
			if (app) app->DispatchEvent(e);
		}

		const bool shouldActivate = makeActive || !m_ActiveScene || !additive;
		size_t awakenedSystemCount = 0;
		try {
			newScene->AwakeSystems(&awakenedSystemCount);
			newScene->StartSystems();
		}
		catch (const std::exception& e) {
			BT_CORE_ERROR_TAG("SceneManager", "Failed to start scene '{}': {}", name, e.what());
			try {
				RollbackSceneLoad(newScene, awakenedSystemCount);
			}
			catch (const std::exception& rollbackException) {
				BT_CORE_ERROR_TAG("SceneManager", "Failed to roll back scene '{}': {}", name, rollbackException.what());
			}
			catch (...) {
				BT_CORE_ERROR_TAG("SceneManager", "Failed to roll back scene '{}'", name);
			}
			return {};
		}
		catch (...) {
			BT_CORE_ERROR_TAG("SceneManager", "Failed to start scene '{}'", name);
			try {
				RollbackSceneLoad(newScene, awakenedSystemCount);
			}
			catch (const std::exception& rollbackException) {
				BT_CORE_ERROR_TAG("SceneManager", "Failed to roll back scene '{}': {}", name, rollbackException.what());
			}
			catch (...) {
				BT_CORE_ERROR_TAG("SceneManager", "Failed to roll back scene '{}'", name);
			}
			return {};
		}

		newScene->m_IsLoaded = true;
		const size_t targetIndex = std::min(insertIndex.value_or(m_LoadedScenes.size()), m_LoadedScenes.size());
		m_LoadedScenes.insert(m_LoadedScenes.begin() + static_cast<std::ptrdiff_t>(targetIndex), newScene);
		if (shouldActivate) {
			m_ActiveScene = newScene.get();
		}

		{
			ScenePostStartEvent e(name);
			ScriptEngine::RaiseSceneLoaded(name);
			Application* app = Application::GetInstance();
			if (app) app->DispatchEvent(e);
		}

		BT_CORE_ASSERT(m_ActiveScene, "Active Scene is null after loading");
		return newScene;
	}

	std::weak_ptr<Scene> SceneManager::ReloadScene(const std::string& name) {
		auto it = FindLoadedSceneIterator(name);
		if (it == m_LoadedScenes.end()) {
			BT_CORE_WARN_TAG("SceneManager", "ReloadScene: scene '{}' is not loaded", name);
			return {};
		}

		const size_t reloadIndex = static_cast<size_t>(std::distance(m_LoadedScenes.begin(), it));
		std::shared_ptr<Scene> scene = *it;
		const bool wasActive = m_ActiveScene == scene.get();
		const bool wasDirty = scene->IsDirty();
		Json::Value snapshotRoot = SceneSerializer::SerializeScene(*scene);

		if (!scene->m_Definition) {
			BT_CORE_ERROR_TAG("SceneManager", "ReloadScene: scene '{}' has no definition", name);
			return {};
		}

		std::shared_ptr<Scene> validationScene = scene->m_Definition->Instantiate();
		if (!validationScene || !SceneSerializer::DeserializeScene(*validationScene, snapshotRoot)) {
			BT_CORE_ERROR_TAG("SceneManager", "ReloadScene: refusing to unload '{}' because the snapshot could not be restored", name);
			return scene;
		}

		ReleaseScene(it);
		std::shared_ptr<Scene> reloaded = LoadSceneInternal(name, true, [&snapshotRoot, wasDirty](Scene& restoredScene) {
			if (!SceneSerializer::DeserializeScene(restoredScene, snapshotRoot)) {
				BT_CORE_ERROR_TAG("SceneManager", "ReloadScene: failed to restore snapshot for '{}'", restoredScene.GetName());
				return;
			}

			if (wasDirty) {
				restoredScene.MarkDirty();
			}
			else {
				restoredScene.ClearDirty();
			}
		}, wasActive, reloadIndex);
		return reloaded;
	}

	void SceneManager::RollbackSceneLoad(const std::shared_ptr<Scene>& scene, size_t awakenedSystemCount) {
		if (!scene) {
			return;
		}

		const std::string sceneName = scene->GetName();

		try {
			ScenePreStopEvent e(sceneName);
			Application* app = Application::GetInstance();
			if (app) app->DispatchEvent(e);
		}
		catch (const std::exception& e) {
			BT_CORE_ERROR_TAG("SceneManager", "Rollback pre-stop event failed for '{}': {}", sceneName, e.what());
		}
		catch (...) {
			BT_CORE_ERROR_TAG("SceneManager", "Rollback pre-stop event failed for '{}'", sceneName);
		}

		if (scene->m_Definition) {
			for (const auto& callback : scene->m_Definition->m_UnloadCallbacks) {
				try {
					callback(*scene);
				}
				catch (const std::exception& e) {
					BT_CORE_ERROR_TAG("SceneManager", "Rollback unload callback failed for '{}': {}", sceneName, e.what());
				}
				catch (...) {
					BT_CORE_ERROR_TAG("SceneManager", "Rollback unload callback failed for '{}'", sceneName);
				}
			}
		}

		scene->m_IsLoaded = false;
		scene->DestroyScene(awakenedSystemCount);
		scene->ClearEntities();

		if (m_ActiveScene == scene.get()) {
			m_ActiveScene = nullptr;
		}
		RefreshActiveScene();

		try {
			ScenePostStopEvent e(sceneName);
			Application* app = Application::GetInstance();
			if (app) app->DispatchEvent(e);
		}
		catch (const std::exception& e) {
			BT_CORE_ERROR_TAG("SceneManager", "Rollback post-stop event failed for '{}': {}", sceneName, e.what());
		}
		catch (...) {
			BT_CORE_ERROR_TAG("SceneManager", "Rollback post-stop event failed for '{}'", sceneName);
		}
	}

	void SceneManager::UnloadScene(const std::string& name) {
		auto it = FindLoadedSceneIterator(name);
		if (it == m_LoadedScenes.end()) {
			BT_CORE_WARN_TAG("SceneManager", "Scene '{}' is not loaded", name);
			return;
		}

		Scene& scene = *(*it);
		if (scene.m_Persistent) {
			return;
		}

		ReleaseScene(it);
	}

	void SceneManager::ReleaseScene(LoadedSceneList::iterator it) {
		if (it == m_LoadedScenes.end() || !(*it)) {
			return;
		}

		std::shared_ptr<Scene> scenePointer = *it;
		Scene& scene = *scenePointer;
		const std::string sceneName = scene.GetName();

		scene.m_IsLoaded = false;
		if (m_ActiveScene == &scene) {
			m_ActiveScene = nullptr;
		}
		if (ScriptEngine::GetScene() == &scene) {
			ScriptEngine::SetScene(nullptr);
		}
		m_LoadedScenes.erase(it);
		RefreshActiveScene();

		try {
			ScenePreStopEvent e(sceneName);
			ScriptEngine::RaiseBeforeSceneUnloaded(sceneName);
			Application* app = Application::GetInstance();
			if (app) app->DispatchEvent(e);
		}
		catch (const std::exception& e) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene pre-stop event failed for '{}': {}", sceneName, e.what());
		}
		catch (...) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene pre-stop event failed for '{}'", sceneName);
		}

		if (scene.m_Definition) {
			for (const auto& callback : scene.m_Definition->m_UnloadCallbacks) {
				try {
					callback(scene);
				}
				catch (const std::exception& e) {
					BT_CORE_ERROR_TAG("SceneManager", "Scene unload callback failed for '{}': {}", sceneName, e.what());
				}
				catch (...) {
					BT_CORE_ERROR_TAG("SceneManager", "Scene unload callback failed for '{}'", sceneName);
				}
			}
		}
		try {
			scene.DestroyScene();
		}
		catch (const std::exception& e) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene destroy failed for '{}': {}", sceneName, e.what());
		}
		catch (...) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene destroy failed for '{}'", sceneName);
		}
		try {
			scene.ClearEntities();
		}
		catch (const std::exception& e) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene entity cleanup failed for '{}': {}", sceneName, e.what());
		}
		catch (...) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene entity cleanup failed for '{}'", sceneName);
		}
		try {
			ScenePostStopEvent e(sceneName);
			ScriptEngine::RaiseSceneUnloaded(sceneName);
			Application* app = Application::GetInstance();
			if (app) app->DispatchEvent(e);
		}
		catch (const std::exception& e) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene post-stop event failed for '{}': {}", sceneName, e.what());
		}
		catch (...) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene post-stop event failed for '{}'", sceneName);
		}
	}

	void SceneManager::UnloadAllScenes(bool includePersistent) {
		for (auto it = m_LoadedScenes.begin(); it != m_LoadedScenes.end();) {
			if (!includePersistent && (*it)->IsPersistent()) {
				++it;
				continue;
			}

			ReleaseScene(it);
			it = m_LoadedScenes.begin();
		}
	}

	std::vector<std::weak_ptr<Scene>> SceneManager::GetLoadedScenes() {
		std::vector<std::weak_ptr<Scene>> loadedScenes;
		loadedScenes.reserve(m_LoadedScenes.size());
		for (const std::shared_ptr<Scene>& scene : m_LoadedScenes) {
			loadedScenes.emplace_back(scene);
		}
		return loadedScenes;
	}

	std::weak_ptr<Scene> SceneManager::GetLoadedScene(const std::string& name) {
		auto it = FindLoadedSceneIterator(name);
		if (it == m_LoadedScenes.end()) {
			BT_CORE_WARN_TAG("SceneManager", "Scene '{}' is not loaded", name);
			return {};
		}
		return std::weak_ptr(*it);
	}

	Scene* SceneManager::GetActiveScene() {
		if (!m_ActiveScene) {
			BT_CORE_WARN_TAG("SceneManager", "There is no active scene");
		}
		return m_ActiveScene;
	}

	const Scene* SceneManager::GetActiveScene() const {
		if (!m_ActiveScene) {
			BT_CORE_WARN_TAG("SceneManager", "There is no active scene");
		}
		return m_ActiveScene;
	}

	bool SceneManager::SetActiveScene(const std::string& name) {
		auto it = FindLoadedSceneIterator(name);
		if (it == m_LoadedScenes.end()) {
			BT_CORE_WARN_TAG("SceneManager", "Scene '{}' is not loaded, load it first", name);
			return false;
		}
		m_ActiveScene = it->get();
		return true;
	}

	bool SceneManager::HasSceneDefinition(const std::string& name) const {
		return m_SceneDefinitions.find(name) != m_SceneDefinitions.end();
	}

	bool SceneManager::IsSceneLoaded(const std::string& name) const {
		return FindLoadedSceneIterator(name) != m_LoadedScenes.end();
	}

	std::vector<std::string> SceneManager::GetRegisteredSceneNames() const {
		return m_SceneDefinitionOrder;
	}
	std::vector<std::string> SceneManager::GetLoadedSceneNames() const {
		std::vector<std::string> names;
		names.reserve(m_LoadedScenes.size());
		for (const auto& scene : m_LoadedScenes) {
			names.push_back(scene->GetName());
		}
		return names;
	}

	void SceneManager::UpdateScenes() {
		LoadedSceneList scenes = m_LoadedScenes;
		for (auto& scene : scenes) if (scene && scene->IsLoaded()) scene->UpdateSystems();
	}

	void SceneManager::OnGuiScenes() {
		LoadedSceneList scenes = m_LoadedScenes;
		for (auto& scene : scenes) if (scene && scene->IsLoaded()) scene->OnGuiSystems();
	}

	void SceneManager::FixedUpdateScenes() {
		LoadedSceneList scenes = m_LoadedScenes;
		for (auto& scene : scenes) if (scene && scene->IsLoaded()) scene->FixedUpdateSystems();
	}

	void SceneManager::InitializeStartupScenes() {
		for (const std::string& name : m_SceneDefinitionOrder) {
			auto definitionIt = m_SceneDefinitions.find(name);
			if (definitionIt == m_SceneDefinitions.end()) {
				continue;
			}

			SceneDefinition* definition = definitionIt->second.get();
			if (!definition->IsStartupScene()) {
				continue;
			}
			try {
				if (m_ActiveScene == nullptr) {
					LoadScene(name);
				}
				else {
					LoadSceneAdditive(name);
				}
			}
			catch (const std::exception& e) {
				BT_CORE_ERROR_TAG("SceneManager", "Failed to load startup scene '{}': {}", name, e.what());
			}
			catch (...) {
				BT_CORE_ERROR_TAG("SceneManager", "Failed to load startup scene '{}'", name);
			}
		}
	}

	SceneDefinition* SceneManager::GetSceneDefinition(const std::string& name) {
		auto it = m_SceneDefinitions.find(name);
		if (it == m_SceneDefinitions.end()) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene definition '{}' not found, call RegisterScene() first", name);
			return nullptr;
		}
		return it->second.get();
	}

	const SceneDefinition* SceneManager::GetSceneDefinition(const std::string& name) const {
		auto it = m_SceneDefinitions.find(name);
		if (it == m_SceneDefinitions.end()) {
			BT_CORE_ERROR_TAG("SceneManager", "Scene definition '{}' not found, call RegisterScene() first", name);
			return nullptr;
		}
		return it->second.get();
	}

	SceneManager::LoadedSceneList::iterator SceneManager::FindLoadedSceneIterator(const std::string& name) {
		return std::find_if(m_LoadedScenes.begin(), m_LoadedScenes.end(), [&name](const std::shared_ptr<Scene>& scene) {
			return scene && scene->GetName() == name;
			});
	}

	SceneManager::LoadedSceneList::const_iterator SceneManager::FindLoadedSceneIterator(const std::string& name) const {
		return std::find_if(m_LoadedScenes.begin(), m_LoadedScenes.end(), [&name](const std::shared_ptr<Scene>& scene) {
			return scene && scene->GetName() == name;
			});
	}

	void SceneManager::RefreshActiveScene() {
		if (m_ActiveScene != nullptr) {
			return;
		}

		for (const auto& loadedScene : m_LoadedScenes) {
			if (loadedScene && loadedScene->IsLoaded()) {
				m_ActiveScene = loadedScene.get();
				break;
			}
		}
	}
}
