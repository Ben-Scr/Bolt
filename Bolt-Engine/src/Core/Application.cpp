#include "pch.hpp"
#include "Application.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/Window.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/GizmoRenderer.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/OpenGL.hpp"
#include "Gui/ImGuiRenderer.hpp"
#include "Gui/GuiRenderer.hpp"
#include "Math/Math.hpp"
#include "Core/SingleInstance.hpp"
#include "Audio/AudioManager.hpp"
#include "Events/EventDispatcher.hpp"
#include "Events/WindowEvents.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Scripting/ScriptEngine.hpp"
#include <Utils/Timer.hpp>
#include <Utils/StringHelper.hpp>

#include "Input.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/BoltProject.hpp"
#include <GLFW/glfw3.h>

namespace Bolt {
	const float Application::k_PausedTargetFrameRate = 10;

	Application* Application::s_Instance = nullptr;
	Application::CommandLineArgs Application::s_CommandLineArgs{};

	Application::Application()
		: m_SceneManager(std::make_unique<SceneManager>())
		, m_FixedUpdateAccumulator{ 0 }
	{
		Application::s_Instance = this;
	}

	Application::~Application()
	{
		if (s_Instance == this) {
			s_Instance = nullptr;
		}
	}

	void Application::Run()
	{
		if (m_ForceSingleInstance) {
			static SingleInstance instance(m_Name);
			BT_ASSERT(!instance.IsAlreadyRunning(), BoltErrorCode::Undefined, "An Instance of this app is already running!");
		}

		for (;;) {
			BT_INFO_TAG("Application", "Initializing...");
			Timer timer = Timer();
			try {
				Initialize();
			}
			catch (const std::exception& e) {
				BT_ERROR_TAG("Application", std::string("Initialization failed: ") + e.what());
				Shutdown(false);
				return;
			}
			catch (...) {
				BT_ERROR_TAG("Application", "Initialization failed with unknown exception");
				Shutdown(false);
				return;
			}
			BT_INFO_TAG("Application", "Full Initialization took " + StringHelper::ToString(timer));

			try {
				Start();
			}
			catch (const std::exception& e) {
				BT_ERROR_TAG("Application", std::string("Start failed: ") + e.what());
				Shutdown();
				return;
			}
			catch (...) {
				BT_ERROR_TAG("Application", "Start failed with unknown exception");
				Shutdown();
				return;
			}
			m_LastFrameTime = Clock::now();

			while (m_Window && !m_Window->ShouldClose() && !m_ShouldQuit) {
				const float targetFps = Max(GetTargetFramerate(), 0.0f);
				DurationChrono targetFrameTime{};
				if (targetFps > 0.0f) {
					targetFrameTime = std::chrono::duration_cast<DurationChrono>(std::chrono::duration<double>(1.0 / targetFps));
				}
				auto now = Clock::now();

				// CPU idling for runtime fps caps. The editor renders Game View pacing separately.
				if (m_Configuration.UseTargetFrameRateForMainLoop && targetFps > 0.0f && (!m_Window->IsVsync() || IsEnginePaused()))
				{
					auto const nextFrameTime = m_LastFrameTime + targetFrameTime;
					auto const idleStart = Clock::now();

					if (now + std::chrono::milliseconds(10) < nextFrameTime)
						std::this_thread::sleep_until(nextFrameTime - std::chrono::milliseconds(10));

					while (Clock::now() < nextFrameTime) {
						std::this_thread::yield();
					}
				}


				auto frameStart = Clock::now();
				float deltaTime = std::chrono::duration<float>(frameStart - m_LastFrameTime).count();

				if (deltaTime >= 0.25f) {
					ResetTimePoints();
					deltaTime = 0.0f;
				}

				try {
					m_Time.Update(deltaTime);

					m_FixedUpdateAccumulator += m_Time.GetDeltaTime();
					while (m_FixedUpdateAccumulator >= m_Time.GetUnscaledFixedDeltaTime()) {
						if (!IsEnginePaused() && !m_IsPlaymodePaused) {
							BeginFixedFrame();
							for (const auto& layer : m_LayerStack) {
								layer->OnFixedUpdate(*this, m_Time.GetFixedDeltaTime());
							}
							EndFixedFrame();
						}

						m_FixedUpdateAccumulator -= m_Time.GetUnscaledFixedDeltaTime();
					}

					BeginFrame();
					EndFrame();
					TryCompleteQuitRequest();

					glfwPollEvents();
					TryCompleteQuitRequest();

					m_LastFrameTime = frameStart;
					m_Time.AdvanceFrameCount();
				}
				catch (const std::exception& e) {
					m_IsRenderingFrame = false;
					BT_ERROR_TAG("Application", "Unhandled frame exception: {}", e.what());
					m_ShouldQuit = true;
				}
				catch (...) {
					m_IsRenderingFrame = false;
					BT_ERROR_TAG("Application", "Unhandled frame exception: unknown exception");
					m_ShouldQuit = true;
				}
			}

			Shutdown();

			if (!m_CanReload) break;
			m_CanReload = false;
		}
	}

	void Application::Initialize() {
		m_Configuration = GetConfiguration();
		SetName(m_Configuration.WindowSpecification.Title);

		Timer timer = Timer();
		Window::Initialize();
		m_Window = std::make_unique<Window>(m_Configuration.WindowSpecification);
		m_Window->SetVsync(m_Configuration.Vsync);
		m_Window->SetEventCallback([this](BoltEvent& e) { DispatchEvent(e); });
		BT_INFO_TAG("Window", "Initialization took " + StringHelper::ToString(timer));

		timer.Reset();
		OpenGL::Initialize(GLInitSpecifications(Color::Background(), GLCullingMode::GLBack));
		BT_INFO_TAG("OpenGL", "Initialization took " + StringHelper::ToString(timer));

		timer.Reset();
		m_Renderer2D = std::make_unique<Renderer2D>();
		m_Renderer2D->Initialize();
		m_Renderer2D->SetSceneProvider([this](const std::function<void(const Scene&)>& fn) {
			if (m_SceneManager) {
				m_SceneManager->ForeachLoadedScene(fn);
			}
			});
		BT_INFO_TAG("Renderer2D", "Initialization took " + StringHelper::ToString(timer));

		if (m_Configuration.EnableGizmoRenderer) {
			timer.Reset();
			m_GizmoRenderer2D = std::make_unique<GizmoRenderer2D>();
			m_GizmoRenderer2D->Initialize();
			BT_INFO_TAG("GizmoRenderer", "Initialization took " + StringHelper::ToString(timer));
		}

		if (m_Configuration.EnableImGui) {
			timer.Reset();
			m_ImGuiRenderer = std::make_unique<ImGuiRenderer>();
			m_ImGuiRenderer->Initialize(m_Window->GetGLFWWindow());
			BT_INFO_TAG("ImGuiRenderer", "Initialization took " + StringHelper::ToString(timer));
		}

		if (m_Configuration.EnableGuiRenderer) {
			timer.Reset();
			m_GuiRenderer = std::make_unique<GuiRenderer>();
			m_GuiRenderer->Initialize();
			BT_INFO_TAG("GuiRenderer", "Initialization took " + StringHelper::ToString(timer));
		}

		if (m_Configuration.EnablePhysics2D) {
			timer.Reset();
			m_PhysicsSystem2D = std::make_unique<PhysicsSystem2D>();
			m_PhysicsSystem2D->Initialize();
			BT_INFO_TAG("PhysicsSystem", "Initialization took " + StringHelper::ToString(timer));
		}

		timer.Reset();
		TextureManager::Initialize();
		BT_INFO_TAG("TextureManager", "Initialization took " + StringHelper::ToString(timer));

		if (m_Configuration.EnableAudio) {
			timer.Reset();
			if (AudioManager::Initialize()) {
				BT_INFO_TAG("AudioManager", "Initialization took " + StringHelper::ToString(timer));
			}
			else {
				BT_ERROR_TAG("AudioManager", "Initialization failed. Continuing without audio.");
				m_Configuration.EnableAudio = false;
			}
		}

		timer.Reset();

		ConfigureScenes();
		m_SceneManager->Initialize();
		BT_INFO_TAG("SceneManager", "Initialization took " + StringHelper::ToString(timer));
		ConfigureLayers();

		if (m_Configuration.SetWindowIcon) {
			m_Window->SetWindowIconFromResource();

			BoltProject* project = ProjectManager::GetCurrentProject();
			if (project && !project->AppIconPath.empty()) {
				TextureHandle h = TextureManager::LoadTexture(project->AppIconPath);
				Texture2D* tex = TextureManager::GetTexture(h);
				if (tex && tex->IsValid()) {
					m_Window->SetWindowIcon(tex);
				}
			}
		}

		ScriptEngine::RaiseApplicationStart();
	}

	void Application::BeginFrame() {
		m_IsRenderingFrame = true;

		CoreInput();

		const bool enginePaused = IsEnginePaused();
		if (enginePaused && !m_WasEnginePaused) {
			ScriptEngine::RaiseApplicationPaused();
		}
		m_WasEnginePaused = enginePaused;

		if (!enginePaused) {
			bool gameplayActive = m_IsPlaying && !m_IsPlaymodePaused;

			if (gameplayActive && m_Configuration.EnableAudio) 
				AudioManager::Update();

			if (gameplayActive)
				ScriptEngine::UpdateGlobalSystems();

			Update();

			for (const auto& layer : m_LayerStack) {
				layer->OnUpdate(*this, m_Time.GetDeltaTime());
			}

			if (gameplayActive && m_SceneManager) m_SceneManager->UpdateScenes();

			if (m_ImGuiRenderer) {
				BOLT_TRY_CATCH_LOG(m_ImGuiRenderer->BeginFrame());
				if (m_SceneManager) m_SceneManager->OnGuiScenes();
				for (const auto& layer : m_LayerStack) {
					layer->OnImGuiRender(*this);
				}
			}

			if (m_Renderer2D)
				BOLT_TRY_CATCH_LOG(m_Renderer2D->BeginFrame());

			if (m_GuiRenderer)
				BOLT_TRY_CATCH_LOG(m_GuiRenderer->BeginFrame(*m_SceneManager));

			if (m_GizmoRenderer2D)
				BOLT_TRY_CATCH_LOG(m_GizmoRenderer2D->BeginFrame());
		}
		else {
			OnPaused();
		}
	}

	void Application::EndFrame() {
		if (!IsEnginePaused()) {
			RenderPipelineOnly();
		}

		m_Input.Update();
		m_IsRenderingFrame = false;
	}

	void Application::DispatchEvent(BoltEvent& event) {
		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent&) {
			RequestQuit();
			glfwSetWindowShouldClose(m_Window->GetGLFWWindow(), GLFW_FALSE);
			return false;
			});

		dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& e) {
			m_IsMinimized = e.GetWidth() == 0 || e.GetHeight() == 0;
			return false;
			});

		dispatcher.Dispatch<FileDropEvent>([this](FileDropEvent& e) {
			m_PendingFileDrops = e.GetPaths();
			return false;
			});

		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it) {
			if (event.Handled) {
				break;
			}
			(*it)->OnEvent(*this, event);
		}

		if (!event.Handled) {
			m_EventBus.Publish(event);
		}
	}

	void Application::Quit() {
		if (!s_Instance) {
			return;
		}

		s_Instance->m_QuitRequested = false;
		s_Instance->m_QuitRequestFrame = -1;
		s_Instance->m_ShouldQuit = true;
	}
	void Application::RequestQuit() {
		if (!s_Instance) {
			return;
		}

		s_Instance->m_QuitRequested = true;
		s_Instance->m_QuitRequestFrame = s_Instance->m_Time.GetFrameCount();
	}
	bool Application::IsQuitRequested() {
		return s_Instance ? s_Instance->m_QuitRequested : false;
	}
	void Application::CancelQuit() {
		if (!s_Instance) {
			return;
		}

		s_Instance->m_QuitRequested = false;
		s_Instance->m_QuitRequestFrame = -1;
	}
	void Application::ConfirmQuit() {
		Quit();
	}
	void Application::TryCompleteQuitRequest() {
		if (!m_QuitRequested || m_ShouldQuit) {
			return;
		}

		if (m_Time.GetFrameCount() <= m_QuitRequestFrame) {
			return;
		}

		Quit();
	}

	void Application::BeginFixedFrame() {
		FixedUpdate();

		if (!m_IsPlaying) return;

		if (m_SceneManager) m_SceneManager->FixedUpdateScenes();
		if (m_PhysicsSystem2D) m_PhysicsSystem2D->FixedUpdate(m_Time.GetFixedDeltaTime());
	}

	void Application::EndFixedFrame() { }

	void Application::RenderPipelineOnly() {
		if (m_Renderer2D)
			BOLT_TRY_CATCH_LOG(m_Renderer2D->EndFrame());

		if (m_ImGuiRenderer)
			BOLT_TRY_CATCH_LOG(m_ImGuiRenderer->EndFrame());

		if (m_GuiRenderer)
			BOLT_TRY_CATCH_LOG(m_GuiRenderer->EndFrame());
		if (m_GizmoRenderer2D)
			BOLT_TRY_CATCH_LOG(m_GizmoRenderer2D->EndFrame());

		if (m_Window) m_Window->SwapBuffers();
	}

	void Application::RenderOnceForRefresh() {
		if (m_IsRenderingFrame || m_IsRenderingRefresh || !m_Window || m_ShouldQuit) {
			return;
		}

		struct RefreshRenderGuard {
			explicit RefreshRenderGuard(Application& application)
				: App(application)
			{
				App.m_IsRenderingRefresh = true;
			}

			~RefreshRenderGuard()
			{
				App.m_IsRenderingRefresh = false;
			}

			Application& App;
		};

		RefreshRenderGuard guard(*this);

		if (m_ImGuiRenderer) {
			BOLT_TRY_CATCH_LOG(m_ImGuiRenderer->BeginFrame());
			if (m_SceneManager) BOLT_TRY_CATCH_LOG(m_SceneManager->OnGuiScenes());
			for (const auto& layer : m_LayerStack) {
				layer->OnImGuiRender(*this);
			}
		}

		if (m_Renderer2D) {
			BOLT_TRY_CATCH_LOG(m_Renderer2D->BeginFrame());
			BOLT_TRY_CATCH_LOG(m_Renderer2D->EndFrame());
		}

		if (m_ImGuiRenderer) {
			BOLT_TRY_CATCH_LOG(m_ImGuiRenderer->EndFrame());
		}

		if (m_GuiRenderer && m_SceneManager) {
			BOLT_TRY_CATCH_LOG(m_GuiRenderer->BeginFrame(*m_SceneManager));
			BOLT_TRY_CATCH_LOG(m_GuiRenderer->EndFrame());
		}

		if (m_GizmoRenderer2D) {
			BOLT_TRY_CATCH_LOG(m_GizmoRenderer2D->BeginFrame());
			BOLT_TRY_CATCH_LOG(m_GizmoRenderer2D->EndFrame());
		}

		if (m_Window) m_Window->SwapBuffers();
	}


	void Application::CoreInput() {
		if (m_Input.GetKey(KeyCode::LeftControl) && m_Input.GetKeyDown(KeyCode::I))
		{
			BT_INFO_TAG("Debug", "From: " + m_Name);
			BT_INFO_TAG("Debug", "Current FPS: " + StringHelper::ToString(m_Time.GetFrameRate()));
			BT_INFO_TAG("Debug", "Time Elapsed Since Start: " + StringHelper::ToString(m_Time.GetElapsedTime(), " s"));
		}

		if (m_Input.GetKeyDown(KeyCode::Esc)) {
			if (m_Window) m_Window->MinimizeWindow();
		}
		if (m_Input.GetKeyDown(KeyCode::F11)) {
			if (m_Window) m_Window->SetFullScreen(!m_Window->IsFullScreen());
		}
	}

	void Application::ResetTimePoints() {
		m_LastFrameTime = Clock::now();
		m_FixedUpdateAccumulator = 0;
	}

	void Application::RefreshBackgroundPauseState() {
		m_IsBackgroundPaused = !m_RunInBackground && !m_WindowHasFocus;
	}

	void Application::Shutdown(bool invokeOnQuit) {
		m_IsShuttingDown = true;

		if (invokeOnQuit) {
			try {
				OnQuit();
			}
			catch (const std::exception& e) {
				BT_ERROR_TAG("Application", std::string("OnQuit failed: ") + e.what());
			}
			catch (...) {
				BT_ERROR_TAG("Application", "OnQuit failed with unknown exception");
			}
		}

		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it) {
			(*it)->OnDetach(*this);
		}
		m_EventBus.Clear();
		m_LayerStack.Clear();

		if (m_SceneManager) m_SceneManager->Shutdown();
		if (ScriptEngine::IsInitialized()) ScriptEngine::Shutdown();
		TextureManager::Shutdown();

		if (m_PhysicsSystem2D) m_PhysicsSystem2D->Shutdown();
		if (m_GuiRenderer) m_GuiRenderer->Shutdown();
		if (m_GizmoRenderer2D) m_GizmoRenderer2D->Shutdown();
		if (m_Renderer2D) m_Renderer2D->Shutdown();
		if (m_ImGuiRenderer) m_ImGuiRenderer->Shutdown();

		if (AudioManager::IsInitialized())
			AudioManager::Shutdown();

		if (m_Window) {
			m_Window->SetEventCallback({});
			m_Window->Destroy();
		}
		if (Window::IsInitialized()) {
			Window::Shutdown();
		}

		m_ImGuiRenderer.reset();
		m_GuiRenderer.reset();
		m_GizmoRenderer2D.reset();
		m_Renderer2D.reset();
		m_PhysicsSystem2D.reset();
		m_Window.reset();

		m_ShouldQuit = false;
		m_QuitRequested = false;
		m_QuitRequestFrame = -1;
		m_IsPaused = false;
		m_IsBackgroundPaused = false;
		m_IsMinimized = false;
		m_WindowHasFocus = true;
		m_IsPlaymodePaused = false;
		m_IsGameInputEnabled = true;
		m_IsShuttingDown = false;
		m_WasEnginePaused = false;
		m_FixedUpdateAccumulator = 0.0;
		m_PendingFileDrops.clear();
	}
}
