#include "pch.hpp"
#include "Application.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/Window.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/GizmoRenderer.hpp"
#include "Graphics/TextureManager.hpp"
#include "Core/PackageHost.hpp"
#include "Graphics/OpenGL.hpp"
#include "Gui/GuiRenderer.hpp"
#include "Math/Math.hpp"
#include "Core/SingleInstance.hpp"
#include "Audio/AudioManager.hpp"
#include "Events/EventDispatcher.hpp"
#include "Events/WindowEvents.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/TextureManager.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Serialization/Path.hpp"

#ifdef AIM_PLATFORM_WINDOWS
// winmm: timeBeginPeriod for 1ms timer resolution (frame-cap accuracy).
#  include <windows.h>
#  include <timeapi.h>
#  pragma comment(lib, "winmm.lib")
#  ifdef AXIOM_PROFILER_ENABLED
#    include <psapi.h>
#    pragma comment(lib, "psapi.lib")
#  endif
#endif
#include <Utils/Timer.hpp>
#include <Utils/StringHelper.hpp>

#include "Input.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/AxiomProject.hpp"
#include <GLFW/glfw3.h>

namespace Axiom {
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
			AIM_ASSERT(!instance.IsAlreadyRunning(), AxiomErrorCode::Undefined, "An Instance of this app is already running!");
		}

		for (;;) {
			AIM_INFO_TAG("Application", "Initializing...");
			Timer timer = Timer();
			try {
				Initialize();
			}
			catch (const std::exception& e) {
				AIM_ERROR_TAG("Application", std::string("Initialization failed: ") + e.what());
				Shutdown(false);
				return;
			}
			catch (...) {
				AIM_ERROR_TAG("Application", "Initialization failed with unknown exception");
				Shutdown(false);
				return;
			}
			AIM_INFO_TAG("Application", "Full Initialization took " + StringHelper::ToString(timer));

			try {
				Start();
			}
			catch (const std::exception& e) {
				AIM_ERROR_TAG("Application", std::string("Start failed: ") + e.what());
				Shutdown();
				return;
			}
			catch (...) {
				AIM_ERROR_TAG("Application", "Start failed with unknown exception");
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

				// VSync bucket: gap between last frame's render end and now (covers SwapBuffers + frame-cap idle).
				const auto vsyncStart = m_LastFrameEndTime != Clock::time_point{}
					? m_LastFrameEndTime
					: now;

				// CPU idle for runtime fps cap. Relies on 1ms timer res from timeBeginPeriod in Initialize().
				if (m_Configuration.UseTargetFrameRateForMainLoop && targetFps > 0.0f && (!m_Window->IsVsync() || IsEnginePaused()))
				{
					auto const nextFrameTime = m_LastFrameTime + targetFrameTime;

					if (now + std::chrono::milliseconds(1) < nextFrameTime) {
						std::this_thread::sleep_until(nextFrameTime - std::chrono::milliseconds(1));
					}
				}


				auto frameStart = Clock::now();
				const float vsyncMs = std::chrono::duration<float, std::milli>(frameStart - vsyncStart).count();
				float deltaTime = std::chrono::duration<float>(frameStart - m_LastFrameTime).count();

				if (deltaTime >= 0.25f) {
					ResetTimePoints();
					deltaTime = 0.0f;
				}

				try {
					AXIOM_PROFILE_SCOPE("Frame");

					Profiler::PushFrameDelta(deltaTime);

					m_Time.Update(deltaTime);

					// Snapshot prev-state BEFORE polling so GetKeyDown/Up reflect events from this frame.
					m_Input.Update();
					glfwPollEvents();
					TryCompleteQuitRequest();

					// Scale at the accumulator input so TimeScale controls step *frequency*.
					// The step quantum stays unscaled — Box2D's solver needs a constant dt
					// for stability, and per-call dt staying fixed prevents script integration
					// (vel * Time.FixedDeltaTime) from compounding with the changed call rate.
					m_FixedUpdateAccumulator += m_Time.GetDeltaTime();
					const double fixedDt = m_Time.GetUnscaledFixedDeltaTime();
					if (fixedDt <= 0.0) {
						// Misconfigured fixed-step: a zero/negative step would make the
						// accumulator loop infinite. Drop accumulated time and skip the
						// FixedUpdate dispatch this frame.
						m_FixedUpdateAccumulator = 0.0;
					} else {
						while (m_FixedUpdateAccumulator >= fixedDt) {
							if (!IsEnginePaused() && !m_IsGameplayPaused) {
								BeginFixedFrame();
								// Index-based with re-check: a layer's OnFixedUpdate
								// may PushLayer/PopLayer, which would invalidate
								// a range-for iterator.
								for (size_t i = 0; i < m_LayerStack.Size(); ++i) {
									Layer* layer = m_LayerStack.At(i);
									if (!layer) break;
									layer->OnFixedUpdate(*this, m_Time.GetUnscaledFixedDeltaTime());
								}
								EndFixedFrame();
							}

							m_FixedUpdateAccumulator -= fixedDt;
						}
					}

					BeginFrame();
					EndFrame();
					TryCompleteQuitRequest();

					m_LastFrameTime = frameStart;
					m_Time.AdvanceFrameCount();

#ifdef AXIOM_PROFILER_ENABLED
					AXIOM_PROFILE_VALUE("VSync", vsyncMs);

#  ifdef AIM_PLATFORM_WINDOWS
					{
						PROCESS_MEMORY_COUNTERS pmc{};
						if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
							AXIOM_PROFILE_VALUE("Total Memory", float(pmc.WorkingSetSize / (1024 * 1024)));
						}
					}
#  endif
					{
						const std::size_t textureBytes = TextureManager::GetTotalTextureMemoryBytes();
						AXIOM_PROFILE_VALUE("Texture Memory", float(textureBytes / (1024 * 1024)));
					}

					{
						std::size_t totalEntities = 0;
						if (m_SceneManager) {
							m_SceneManager->ForeachLoadedScene([&totalEntities](Scene& scene) {
								auto& entityStorage = scene.GetRegistry().storage<entt::entity>();
								totalEntities += entityStorage.free_list();
							});
						}
						AXIOM_PROFILE_VALUE("Entity Count", float(totalEntities));
					}

					AXIOM_PROFILE_VALUE("Playing Sources", float(AudioManager::GetActiveSoundCount()));

					// "Others" residual: Frame Time minus the four named CPU buckets.
					{
						const float frameMs    = Profiler::GetCurrentValue("Frame Time");
						const float renderMs   = Profiler::GetCurrentValue("Rendering");
						const float scriptsMs  = Profiler::GetCurrentValue("Scripts");
						const float physicsMs  = Profiler::GetCurrentValue("Physics");
						const float others = frameMs - renderMs - scriptsMs - physicsMs - vsyncMs;
						AXIOM_PROFILE_VALUE("Others", std::max(0.0f, others));
					}
#endif

					AXIOM_PROFILE_FRAME("Frame");

					// MUST be last in try-block — anything after eats into next frame's VSync bucket.
					m_LastFrameEndTime = Clock::now();
				}
				catch (const std::exception& e) {
					m_IsRenderingFrame = false;
					AIM_ERROR_TAG("Application", "Unhandled frame exception: {}", e.what());
					m_ShouldQuit = true;
				}
				catch (...) {
					m_IsRenderingFrame = false;
					AIM_ERROR_TAG("Application", "Unhandled frame exception: unknown exception");
					m_ShouldQuit = true;
				}
			}

			Shutdown();

			if (!m_CanReload) break;
			m_CanReload = false;

			// Rebuild SceneManager so reload doesn't reuse a shut-down instance.
			m_SceneManager.reset();
			m_SceneManager = std::make_unique<SceneManager>();
		}
	}

	void Application::Initialize() {
#ifdef AIM_PLATFORM_WINDOWS
		// 1ms timer res for accurate sleep_until in the frame-cap path. Paired with timeEndPeriod in Shutdown.
		timeBeginPeriod(1);
#endif
		m_Configuration = GetConfiguration();
		SetName(m_Configuration.WindowSpecification.Title);

		// Profiler first — no GL/window deps, lets later subsystems emit profile marks during init.
#ifdef AXIOM_PROFILER_ENABLED
		Profiler::Initialize();
#endif

		Timer timer = Timer();
		Window::Initialize();
		m_Window = std::make_unique<Window>(m_Configuration.WindowSpecification);
		m_Window->SetVsync(m_Configuration.Vsync);
		m_Window->SetEventCallback([this](AxiomEvent& e) { DispatchEvent(e); });
		AIM_INFO_TAG("Window", "Initialization took " + StringHelper::ToString(timer));

		timer.Reset();
		OpenGL::Initialize(GLInitSpecifications(Color::Background(), GLCullingMode::GLBack));
		AIM_INFO_TAG("OpenGL", "Initialization took " + StringHelper::ToString(timer));

		if (m_Configuration.EnableRenderer2D) {
			timer.Reset();
			m_Renderer2D = std::make_unique<Renderer2D>();
			m_Renderer2D->Initialize();
			m_Renderer2D->SetSceneProvider([this](const std::function<void(Scene&)>& fn) {
				if (m_SceneManager) {
					m_SceneManager->ForeachLoadedScene(fn);
				}
				});
			AIM_INFO_TAG("Renderer2D", "Initialization took " + StringHelper::ToString(timer));
		}

		if (m_Configuration.EnableGizmoRenderer) {
			timer.Reset();
			m_GizmoRenderer2D = std::make_unique<GizmoRenderer2D>();
			m_GizmoRenderer2D->Initialize();
			AIM_INFO_TAG("GizmoRenderer", "Initialization took " + StringHelper::ToString(timer));
		}

		if (m_Configuration.EnableGuiRenderer) {
			timer.Reset();
			m_GuiRenderer = std::make_unique<GuiRenderer>();
			m_GuiRenderer->Initialize();
			AIM_INFO_TAG("GuiRenderer", "Initialization took " + StringHelper::ToString(timer));
		}

		if (m_Configuration.EnablePhysics2D) {
			timer.Reset();
			m_PhysicsSystem2D = std::make_unique<PhysicsSystem2D>();
			m_PhysicsSystem2D->Initialize();
			AIM_INFO_TAG("PhysicsSystem", "Initialization took " + StringHelper::ToString(timer));
		}

		if (m_Configuration.EnableTextureManager) {
			timer.Reset();
			TextureManager::Initialize();
			AIM_INFO_TAG("TextureManager", "Initialization took " + StringHelper::ToString(timer));
		}

		// FontManager piggy-backs on Renderer2D's GL context but lives on
		// the same enable flag as TextureManager (both are GPU asset
		// caches). No extra config knob — text without a renderer is
		// pointless anyway.
		if (m_Configuration.EnableTextureManager && m_Configuration.EnableRenderer2D) {
			timer.Reset();
			FontManager::Initialize();
			AIM_INFO_TAG("FontManager", "Initialization took " + StringHelper::ToString(timer));
		}

		// Register the rest of AxiomAssets/ as built-in assets so the
		// inspector's reference picker can offer engine-shipped icons,
		// audio, etc. via its eye toggle. Runs AFTER FontManager so the
		// default font's hand-picked GUID wins over the auto-scan.
		{
			timer.Reset();
			const std::string axiomAssetsRoot = Path::ResolveAxiomAssets("");
			AIM_INFO_TAG("AssetRegistry", "AxiomAssets root resolved to: '{}'", axiomAssetsRoot);
			if (!axiomAssetsRoot.empty()) {
				const size_t before = AssetRegistry::GetBuiltInCount();
				AssetRegistry::RegisterBuiltInDirectory(axiomAssetsRoot);
				const size_t after = AssetRegistry::GetBuiltInCount();
				AIM_INFO_TAG("AssetRegistry", "Built-in scan registered {} files in {}", after - before, StringHelper::ToString(timer));
			} else {
				AIM_WARN_TAG("AssetRegistry", "AxiomAssets root not found - built-in assets won't appear in picker");
			}
		}

		if (m_Configuration.EnableAudio) {
			timer.Reset();
			if (AudioManager::Initialize()) {
				AIM_INFO_TAG("AudioManager", "Initialization took " + StringHelper::ToString(timer));
			}
			else {
				AIM_ERROR_TAG("AudioManager", "Initialization failed. Continuing without audio.");
				m_Configuration.EnableAudio = false;
			}
		}

		timer.Reset();

		ConfigureScenes();
		m_SceneManager->Initialize();
		AIM_INFO_TAG("SceneManager", "Initialization took " + StringHelper::ToString(timer));
		ConfigureLayers();

		if (m_Configuration.SetWindowIcon) {
			m_Window->SetWindowIconFromResource();

			AxiomProject* project = ProjectManager::GetCurrentProject();
			if (project && !project->AppIconPath.empty()) {
				TextureHandle h = TextureManager::LoadTexture(project->AppIconPath);
				Texture2D* tex = TextureManager::GetTexture(h);
				if (tex && tex->IsValid()) {
					m_Window->SetWindowIcon(tex);
				}
			}
		}

		if (m_Configuration.EnablePackageHost) {
			// MUST run before InitializeStartupScenes so package-registered components exist for deserialization.
			PackageHost::LoadAll();
		}

		m_SceneManager->InitializeStartupScenes();

		ScriptEngine::RaiseApplicationStart();
	}

	void Application::BeginFrame() {
		AXIOM_PROFILE_SCOPE("Application::BeginFrame");
		m_IsRenderingFrame = true;

		CoreInput();

		const bool enginePaused = IsEnginePaused();
		if (enginePaused && !m_WasEnginePaused) {
			ScriptEngine::RaiseApplicationPaused();
		}
		m_WasEnginePaused = enginePaused;

		if (!enginePaused) {
			bool gameplayActive = m_IsPlaying && !m_IsGameplayPaused;

			if (gameplayActive && m_Configuration.EnableAudio) 
				AudioManager::Update();

			if (gameplayActive)
				ScriptEngine::UpdateGlobalSystems();

			Update();

			// Index-based: a layer's OnUpdate may PushLayer/PopLayer.
			for (size_t i = 0; i < m_LayerStack.Size(); ++i) {
				Layer* layer = m_LayerStack.At(i);
				if (!layer) break;
				layer->OnUpdate(*this, m_Time.GetDeltaTime());
			}

			if (gameplayActive && m_SceneManager) m_SceneManager->UpdateScenes();

			// Sync Transform2D into physics every frame (editor + play).
			if (m_PhysicsSystem2D) m_PhysicsSystem2D->Update();

			if (m_SceneManager) m_SceneManager->OnPreRenderScenes();
			for (size_t i = 0; i < m_LayerStack.Size(); ++i) {
				Layer* layer = m_LayerStack.At(i);
				if (!layer) break;
				AXIOM_TRY_CATCH_LOG(layer->OnPreRender(*this));
			}

			if (m_Renderer2D)
				AXIOM_TRY_CATCH_LOG(m_Renderer2D->BeginFrame());

			if (m_GuiRenderer)
				AXIOM_TRY_CATCH_LOG(m_GuiRenderer->BeginFrame(*m_SceneManager));

			if (m_GizmoRenderer2D)
				AXIOM_TRY_CATCH_LOG(m_GizmoRenderer2D->BeginFrame());
		}
		else {
			OnPaused();
		}
	}

	void Application::EndFrame() {
		AXIOM_PROFILE_SCOPE("Application::EndFrame");
		if (!IsEnginePaused()) {
			RenderPipelineOnly();
		}

		m_IsRenderingFrame = false;
	}

	void Application::DispatchEvent(AxiomEvent& event) {
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

		// Reverse index-based: a layer's OnEvent may PushLayer/PopLayer.
		for (size_t i = m_LayerStack.Size(); i > 0; --i) {
			if (event.Handled) {
				break;
			}
			Layer* layer = m_LayerStack.At(i - 1);
			if (!layer) continue;
			layer->OnEvent(*this, event);
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
		// Step quantum is unscaled and constant. TimeScale already affected how many
		// times this fires per real second via the accumulator above.
		if (m_PhysicsSystem2D) m_PhysicsSystem2D->FixedUpdate(m_Time.GetUnscaledFixedDeltaTime());
		// Runs after physics so global systems observe transforms synced from the physics step.
		ScriptEngine::FixedUpdateGlobalSystems();
	}

	void Application::EndFixedFrame() { }

	void Application::RenderPipelineOnly() {
		if (m_Renderer2D)
			AXIOM_TRY_CATCH_LOG(m_Renderer2D->EndFrame());

		for (size_t i = 0; i < m_LayerStack.Size(); ++i) {
			Layer* layer = m_LayerStack.At(i);
			if (!layer) break;
			AXIOM_TRY_CATCH_LOG(layer->OnPostRender(*this));
		}

		if (m_GuiRenderer)
			AXIOM_TRY_CATCH_LOG(m_GuiRenderer->EndFrame());
		if (m_GizmoRenderer2D)
			AXIOM_TRY_CATCH_LOG(m_GizmoRenderer2D->EndFrame());

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

		if (m_SceneManager) AXIOM_TRY_CATCH_LOG(m_SceneManager->OnPreRenderScenes());
		for (size_t i = 0; i < m_LayerStack.Size(); ++i) {
			Layer* layer = m_LayerStack.At(i);
			if (!layer) break;
			AXIOM_TRY_CATCH_LOG(layer->OnPreRender(*this));
		}

		if (m_Renderer2D) {
			AXIOM_TRY_CATCH_LOG(m_Renderer2D->BeginFrame());
			AXIOM_TRY_CATCH_LOG(m_Renderer2D->EndFrame());
		}

		for (size_t i = 0; i < m_LayerStack.Size(); ++i) {
			Layer* layer = m_LayerStack.At(i);
			if (!layer) break;
			AXIOM_TRY_CATCH_LOG(layer->OnPostRender(*this));
		}

		if (m_GuiRenderer && m_SceneManager) {
			AXIOM_TRY_CATCH_LOG(m_GuiRenderer->BeginFrame(*m_SceneManager));
			AXIOM_TRY_CATCH_LOG(m_GuiRenderer->EndFrame());
		}

		if (m_GizmoRenderer2D) {
			AXIOM_TRY_CATCH_LOG(m_GizmoRenderer2D->BeginFrame());
			AXIOM_TRY_CATCH_LOG(m_GizmoRenderer2D->EndFrame());
		}

		if (m_Window) m_Window->SwapBuffers();
	}


	void Application::CoreInput() {
		if (m_Input.GetKey(KeyCode::LeftControl) && m_Input.GetKeyDown(KeyCode::M)) {
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
				ScriptEngine::RaiseApplicationQuit();
				OnQuit();
			}
			catch (const std::exception& e) {
				AIM_ERROR_TAG("Application", std::string("OnQuit failed: ") + e.what());
			}
			catch (...) {
				AIM_ERROR_TAG("Application", "OnQuit failed with unknown exception");
			}
		}

		// Snapshot raw pointers — layer OnDetach may pop other layers
		// (legitimate during shutdown), and we need to detach each one
		// exactly once regardless of mutations.
		std::vector<Layer*> detachOrder;
		detachOrder.reserve(m_LayerStack.Size());
		for (size_t i = m_LayerStack.Size(); i > 0; --i) {
			Layer* layer = m_LayerStack.At(i - 1);
			if (layer) detachOrder.push_back(layer);
		}
		for (Layer* layer : detachOrder) {
			layer->OnDetach(*this);
		}
		m_EventBus.Clear();
		m_LayerStack.Clear();

		// Order matters: subsystems that hold package callbacks tear down BEFORE PackageHost::UnloadAll.
		if (m_SceneManager) m_SceneManager->Shutdown();
		if (ScriptEngine::IsInitialized()) ScriptEngine::Shutdown();
		if (FontManager::IsInitialized()) FontManager::Shutdown();
		if (m_Configuration.EnableTextureManager) TextureManager::Shutdown();

		if (m_PhysicsSystem2D) m_PhysicsSystem2D->Shutdown();
		if (m_GuiRenderer) m_GuiRenderer->Shutdown();
		if (m_GizmoRenderer2D) m_GizmoRenderer2D->Shutdown();
		if (m_Renderer2D) m_Renderer2D->Shutdown();

		if (AudioManager::IsInitialized())
			AudioManager::Shutdown();

		if (m_Configuration.EnablePackageHost) PackageHost::UnloadAll();

		if (m_Window) {
			m_Window->SetEventCallback({});
			m_Window->Destroy();
		}
		if (Window::IsInitialized()) {
			Window::Shutdown();
		}

		m_GuiRenderer.reset();
		m_GizmoRenderer2D.reset();
		m_Renderer2D.reset();
		m_PhysicsSystem2D.reset();
		m_Window.reset();

#ifdef AXIOM_PROFILER_ENABLED
		// Profiler last so subsystems above can still emit profile marks during teardown.
		Profiler::Shutdown();
#endif

#ifdef AIM_PLATFORM_WINDOWS
		timeEndPeriod(1);
#endif

		m_ShouldQuit = false;
		m_QuitRequested = false;
		m_QuitRequestFrame = -1;
		m_IsPaused = false;
		m_IsBackgroundPaused = false;
		m_IsMinimized = false;
		m_WindowHasFocus = true;
		m_IsGameplayPaused = false;
		m_IsScriptInputEnabled = true;
		m_IsShuttingDown = false;
		m_WasEnginePaused = false;
		m_FixedUpdateAccumulator = 0.0;
		m_PendingFileDrops.clear();
	}
}
