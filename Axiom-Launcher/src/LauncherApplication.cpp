#include <Axiom.hpp>

#include "Core/Application.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include <Systems/LauncherLayer.hpp>
#include <Core/Version.hpp>

using namespace Axiom;

class LauncherApplication : public Application {
public:
	ApplicationConfig GetConfiguration() const override {
		ApplicationConfig config;
		config.WindowSpecification = WindowSpecification(900, 600, "Axiom Launcher " + std::string(AIM_VERSION), true, true, false);
		config.EnableAudio = false;
		config.EnableGuiRenderer = false;
		config.EnableGizmoRenderer = false;
		config.EnablePhysics2D = false;
		config.SetWindowIcon = true;
		config.Vsync = false;

		SetTargetFramerate(144.f);

		return config;
	}

	void ConfigureScenes() override {
		SceneDefinition& launcherScene = GetSceneManager()->RegisterScene("Launcher");
		launcherScene.SetAsStartupScene();
	}

	void ConfigureLayers() override {
		PushLayer<LauncherLayer>();
	}

	void Start() override {}
	void Update() override {}
	void FixedUpdate() override {}
	void OnPaused() override {}
	void OnQuit() override {}
};

Axiom::Application* Axiom::CreateApplication() {
	return new LauncherApplication();
}

#include <EntryPoint.hpp>
