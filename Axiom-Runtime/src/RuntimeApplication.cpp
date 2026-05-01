#include <Axiom.hpp>
#include "Core/Application.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"

#include "Systems/ImGuiDebugSystem.hpp"
#include <Scene/EntityHelper.hpp>
#include <Serialization/SceneSerializer.hpp>
#include <Serialization/Path.hpp>
#include <Serialization/File.hpp>
#include <Project/ProjectManager.hpp>
#include <Project/AxiomProject.hpp>

#include <Core/Version.hpp>
#include <Core/Window.hpp>
#include <filesystem>

using namespace Axiom;


class RuntimeApplication : public Axiom::Application {
public:
	ApplicationConfig GetConfiguration() const override {
		ApplicationConfig config;
		AxiomProject* project = ProjectManager::GetCurrentProject();
		std::string title = project
			? project->Name + " - Axiom Runtime"
			: "Axiom Runtime Application " + std::string(AIM_VERSION);

		if (project) {
			config.WindowSpecification = WindowSpecification(
				project->BuildWidth, project->BuildHeight, title,
				project->BuildResizable, true, project->BuildFullscreen);
		} else {
			config.WindowSpecification = WindowSpecification(800, 800, title, true, true, false);
		}
		config.EnableAudio = true;
		config.EnablePhysics2D = true;
		return config;
	}

	void ConfigureScenes() override {
		// Runtime is always in "playing" state — set before scenes load
		// so that systems like AudioUpdateSystem know to trigger PlayOnAwake.
		Application::SetIsPlaying(true);

		AxiomProject* project = ProjectManager::GetCurrentProject();
		std::string startupScene = "SampleScene";
		if (project) {
			if (!project->StartupScene.empty()) startupScene = project->StartupScene;
			else if (!project->LastOpenedScene.empty()) startupScene = project->LastOpenedScene;
		}

		// Helper: registers a scene definition with standard systems + OnLoad deserializer
		auto registerScene = [&](const std::string& sceneName) -> SceneDefinition& {
			auto& def = GetSceneManager()->RegisterScene(sceneName);

			// Load scene file in OnLoad callback — runs BEFORE Awake/Start,
			// so entities exist when systems initialize (e.g. PlayOnAwake).
			if (project) {
				std::string scenePath = project->GetSceneFilePath(sceneName);
				def.OnLoad([scenePath](Scene& scene) {
					if (File::Exists(scenePath))
						SceneSerializer::LoadFromFile(scene, scenePath);
				});
			}
			return def;
		};

		if (project && !project->BuildSceneList.empty()) {
			for (const auto& sceneName : project->BuildSceneList) {
				auto& def = registerScene(sceneName);
				if (sceneName == startupScene) def.SetAsStartupScene();
			}
			if (!GetSceneManager()->HasSceneDefinition(startupScene)) {
				registerScene(startupScene).SetAsStartupScene();
			}
		} else {
			registerScene(startupScene).SetAsStartupScene();
		}
	}

	~RuntimeApplication() override = default;

	void Start() override {
		if (!ProjectManager::GetCurrentProject()) {
			EntityHelper::CreateCamera2DEntity();
		}
	}

	void Update() override {}
	void FixedUpdate() override {}
	void OnPaused() override {}
	void OnQuit() override {}
};


Axiom::Application* Axiom::CreateApplication() {
	// Auto-detect project: look for axiom-project.json next to the executable
	std::string exeDir = Path::ExecutableDir();
	if (AxiomProject::Validate(exeDir)) {
		auto project = std::make_unique<AxiomProject>(AxiomProject::Load(exeDir));
		AIM_CORE_INFO_TAG("Runtime", "Loaded project: {} ({})", project->Name, project->RootDirectory);
		ProjectManager::SetCurrentProject(std::move(project));
	}

	return new RuntimeApplication();
}

#include <EntryPoint.hpp>
