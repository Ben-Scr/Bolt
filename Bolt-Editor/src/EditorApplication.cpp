#include <Bolt.hpp>

#include "Core/Application.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include <Systems/ImGuiEditorLayer.hpp>
#include <Systems/ImGuiDebugSystem.hpp>
#include <Systems/GizmosDebugSystem.hpp>
#include <Project/ProjectManager.hpp>
#include <Project/BoltProject.hpp>
#include <Serialization/SceneSerializer.hpp>
#include <Serialization/File.hpp>
#include <Core/Version.hpp>
#include "Editor/EditorComponentRegistration.hpp"

using namespace Bolt;

static std::string s_ProjectPath;

class EditorApplication : public Application {
public:
	ApplicationConfig GetConfiguration() const override {
		ApplicationConfig config;
		std::string title = "Bolt Editor " + std::string(BT_VERSION);
		if (ProjectManager::HasProject())
			title += " - " + ProjectManager::GetCurrentProject()->Name;
		config.WindowSpecification = WindowSpecification(0, 0, title, true, true, true);
		config.EnableAudio = true;
		config.EnableGizmoRenderer = true;
		config.EnableGuiRenderer = false;
		config.EnablePhysics2D = true;
		config.SetWindowIcon = true;
		SetRunInBackground(true);
		return config;
	}

	void ConfigureScenes() override {
		Application::SetIsPlaying(false);

		SceneDefinition& editorScene = GetSceneManager()->RegisterScene("SampleScene");
		editorScene.OnLoad([](Scene& scene) {
			BoltProject* project = ProjectManager::GetCurrentProject();
			if (project) {
				const std::string scenePath = project->GetSceneFilePath(project->LastOpenedScene);
				if (File::Exists(scenePath)) {
					SceneSerializer::LoadFromFile(scene, scenePath);
					return;
				}
			}

			EntityHelper::CreateCamera2DEntity(scene);
		});
		editorScene.SetAsStartupScene();
	}

	void ConfigureLayers() override {
		PushLayer<ImGuiEditorLayer>();
		PushOverlay<ImGuiDebugSystem>();
		PushOverlay<GizmosDebugSystem>();
	}

	void Start() override {
		RegisterEditorComponentInspectors(*GetSceneManager());
	}
	void Update() override {}
	void FixedUpdate() override {}
	void OnPaused() override {}

	void OnQuit() override {
		BoltProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			return;
		}

		auto* sceneManager = GetSceneManager();
		if (!sceneManager) {
			return;
		}

		Scene* active = sceneManager->GetActiveScene();
		if (active) {
			project->LastOpenedScene = active->GetName();
			project->Save();
		}
	}
};

Bolt::Application* Bolt::CreateApplication() {
	// Parse --project="path" from command line
	const Application::CommandLineArgs args = Application::GetCommandLineArgs();

	for (int i = 1; i < args.Count; i++) {
		const char* rawArg = args[i];
		if (!rawArg) {
			continue;
		}

		std::string arg(rawArg);
		if (arg.rfind("--project=", 0) == 0) {
			s_ProjectPath = arg.substr(10);
			// Remove surrounding quotes if present
			if (s_ProjectPath.size() >= 2 && s_ProjectPath.front() == '"' && s_ProjectPath.back() == '"')
				s_ProjectPath = s_ProjectPath.substr(1, s_ProjectPath.size() - 2);
		}
	}

	if (!s_ProjectPath.empty() && BoltProject::Validate(s_ProjectPath)) {
		auto project = std::make_unique<BoltProject>(BoltProject::Load(s_ProjectPath));
		BT_CORE_INFO_TAG("Editor", "Opening project: {} ({})", project->Name, project->RootDirectory);
		ProjectManager::SetCurrentProject(std::move(project));
	}

	if (!ProjectManager::HasProject()) {
		BT_CORE_WARN_TAG("Editor", "No project specified. Use: Bolt-Editor.exe --project=\"path/to/project\"");
		BT_CORE_WARN_TAG("Editor", "Starting with default fallback paths.");
	}

	return new EditorApplication();
}

#include <EntryPoint.hpp>
