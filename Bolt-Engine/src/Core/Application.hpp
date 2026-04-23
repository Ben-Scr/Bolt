#pragma once
#include "Graphics/Renderer2D.hpp"
#include "Graphics/GizmoRenderer.hpp"

#include "Physics/PhysicsSystem2D.hpp"

#include "Scene/SceneManager.hpp"
#include "Gui/ImGuiRenderer.hpp"
#include "Gui/GuiRenderer.hpp"
#include "Core/ApplicationConfig.hpp"
#include "Core/Layer.hpp"
#include "Core/LayerStack.hpp"
#include "Events/BoltEvent.hpp"
#include "Events/EventBus.hpp"

#include "Input.hpp"
#include "Time.hpp"
#include "Window.hpp"
#include "Export.hpp"

#include <chrono>
#include <type_traits>
#include <utility>

namespace Bolt {
	class BOLT_API Application {
		friend class Window;
		friend class SceneManager;

		using DurationChrono = std::chrono::high_resolution_clock::duration;
		using Clock = std::chrono::high_resolution_clock;

	public:
		struct CommandLineArgs {
			int Count = 0;
			char** Values = nullptr;

			const char* operator[](int index) const {
				if (!Values || index < 0 || index >= Count) {
					return nullptr;
				}

				return Values[index];
			}
		};

		Application()
			: m_SceneManager(std::make_unique<SceneManager>())
			, m_FixedUpdateAccumulator{ 0 } {
			Application::s_Instance = this;
		};

		virtual ~Application() {
			if (s_Instance == this) {
				s_Instance = nullptr;
			}
		}


		void Run();

		virtual ApplicationConfig GetConfiguration() const { return {}; }
		virtual void ConfigureScenes() {}
		virtual void ConfigureLayers() {}
		virtual void Start() = 0;
		virtual void Update() = 0;
		virtual void FixedUpdate() = 0;
		virtual void OnPaused() = 0;
		virtual void OnQuit() = 0;

		static void SetIsPlaying(bool enabled) { if (s_Instance) s_Instance->m_IsPlaying = enabled; }
		static bool GetIsPlaying() { if (s_Instance) return s_Instance->m_IsPlaying; return false; }

		void SetName(const std::string& s) { m_Name = s; }
		static void SetTargetFramerate(float framerate) { if (s_Instance) s_Instance->m_TargetFramerate = framerate; }
		static void SetForceSingleInstance(bool value) { if (s_Instance) s_Instance->m_ForceSingleInstance = value; }
		static void SetRunInBackground(bool value) {
			if (s_Instance) {
				s_Instance->m_RunInBackground = value;
				s_Instance->RefreshBackgroundPauseState();
			}
		}

		const std::string& GetName() { return m_Name; }
		static float GetTargetFramerate() { return s_Instance ? (s_Instance->IsEnginePaused() ? k_PausedTargetFrameRate : s_Instance->m_TargetFramerate) : 0.0f; }
		static bool GetForceSingleInstance() { return s_Instance ? s_Instance->m_ForceSingleInstance : false; }
		static bool GetRunInBackground() { return s_Instance ? s_Instance->m_RunInBackground : false; }
		static Window* GetWindow() { return s_Instance ? s_Instance->m_Window.get() : nullptr; }
		static CommandLineArgs GetCommandLineArgs() { return s_CommandLineArgs; }
		static void SetCommandLineArgs(int argc, char** argv) { s_CommandLineArgs = { argc, argv }; }


		Renderer2D* GetRenderer2D() { return m_Renderer2D.get(); }
		Input& GetInput() { return m_Input; }
		Time& GetTime() { return m_Time; }
		const Time& GetTime() const { return m_Time; }

		static Application* GetInstance() { return s_Instance; }

		SceneManager* GetSceneManager() { return m_SceneManager.get(); }
		const SceneManager* GetSceneManager() const { return m_SceneManager.get(); }

		static void Quit();
		static void Pause(bool paused) { if (s_Instance) s_Instance->m_IsPaused = paused; }
		static void Reload() { if (s_Instance) { s_Instance->m_ShouldQuit = true; s_Instance->m_CanReload = true; } };
		static bool IsPaused() { return s_Instance ? s_Instance->IsEnginePaused() : false; }

		/// Pauses only gameplay (scene updates, physics, audio) while keeping the editor responsive.
		static void SetPlaymodePaused(bool paused) { if (s_Instance) s_Instance->m_IsPlaymodePaused = paused; }
		static bool IsPlaymodePaused() { return s_Instance ? s_Instance->m_IsPlaymodePaused : false; }
		static void SetGameInputEnabled(bool enabled) { if (s_Instance) s_Instance->m_IsGameInputEnabled = enabled; }
		static bool IsGameInputEnabled() { return s_Instance ? s_Instance->m_IsGameInputEnabled : true; }

		/// Signals a quit request that can be intercepted (e.g. to show a save dialog).
		static void RequestQuit();
		static bool IsQuitRequested();
		static void CancelQuit();
		static void ConfirmQuit();
		void RenderOnceForRefresh();

		template<typename TLayer, typename... Args>
		TLayer& PushLayer(Args&&... args) {
			static_assert(std::is_base_of_v<Layer, TLayer>, "TLayer must derive from Layer");

			auto layer = std::make_unique<TLayer>(std::forward<Args>(args)...);
			TLayer& layerRef = *layer;
			m_LayerStack.PushLayer(std::move(layer));
			layerRef.OnAttach(*this);
			return layerRef;
		}

		template<typename TLayer, typename... Args>
		TLayer& PushOverlay(Args&&... args) {
			static_assert(std::is_base_of_v<Layer, TLayer>, "TLayer must derive from Layer");

			auto layer = std::make_unique<TLayer>(std::forward<Args>(args)...);
			TLayer& layerRef = *layer;
			m_LayerStack.PushOverlay(std::move(layer));
			layerRef.OnAttach(*this);
			return layerRef;
		}

		template<typename TEvent, typename F>
		EventId SubscribeEvent(F&& callback) {
			return m_EventBus.Subscribe<TEvent>(std::forward<F>(callback));
		}

		bool UnsubscribeEvent(EventId id) { return m_EventBus.Unsubscribe(id); }

		std::vector<std::string> TakePendingFileDrops() {
			std::vector<std::string> paths = std::move(m_PendingFileDrops);
			m_PendingFileDrops.clear();
			return paths;
		}

	private:
		std::unique_ptr<Window> m_Window;
		std::unique_ptr<Renderer2D> m_Renderer2D;
		std::unique_ptr<ImGuiRenderer> m_ImGuiRenderer;
		std::unique_ptr<GuiRenderer> m_GuiRenderer;
		std::unique_ptr<GizmoRenderer2D> m_GizmoRenderer2D;
		std::unique_ptr<PhysicsSystem2D> m_PhysicsSystem2D;
		std::unique_ptr<SceneManager> m_SceneManager;
		Input m_Input;
		Time m_Time;
		ApplicationConfig m_Configuration;

	    std::string m_Name;

		bool m_ForceSingleInstance = false;
		bool m_RunInBackground = false;
		bool m_ShouldQuit = false;
		bool m_CanReload = false;
		bool m_IsPaused = false;
		bool m_IsBackgroundPaused = false;
		bool m_IsMinimized = false;
		bool m_WindowHasFocus = true;
		bool m_IsPlaying = true;
		bool m_IsPlaymodePaused = false;
		bool m_IsGameInputEnabled = true;
		bool m_QuitRequested = false;
		int m_QuitRequestFrame = -1;

		float m_TargetFramerate = 144.0f;
		static const float k_PausedTargetFrameRate;

		static Application* s_Instance;
		static CommandLineArgs s_CommandLineArgs;


		std::vector<std::string> m_PendingFileDrops;
		double m_FixedUpdateAccumulator;
		std::chrono::steady_clock::time_point m_LastFrameTime = Clock::now();
		LayerStack m_LayerStack;
		EventBus m_EventBus;

		void Initialize();
		void Shutdown(bool invokeOnQuit = true);
		void DispatchEvent(BoltEvent& event);
		void RefreshBackgroundPauseState();
		bool IsEnginePaused() const { return m_IsPaused || m_IsBackgroundPaused || m_IsMinimized; }
		void CoreInput();
		void ResetTimePoints();
		void TryCompleteQuitRequest();
		void BeginFrame();
		void BeginFixedFrame();
		void EndFixedFrame();
		void EndFrame();
		void RenderPipelineOnly();
		static void SetWindowFocused(bool focused) {
			if (s_Instance) {
				s_Instance->m_WindowHasFocus = focused;
				s_Instance->RefreshBackgroundPauseState();
			}
		}
	};

	// To be defined in client
	Application* CreateApplication();
}
