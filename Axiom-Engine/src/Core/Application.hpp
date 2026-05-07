#pragma once
#include "Core/ApplicationConfig.hpp"
#include "Core/Export.hpp"
#include "Core/Layer.hpp"
#include "Core/LayerStack.hpp"
#include "Events/AxiomEvent.hpp"
#include "Events/EventBus.hpp"

#include "Input.hpp"
#include "Time.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if !AXIOM_WITH_APPLICATION
#error "Axiom::Application is part of the full runtime surface. Enable the application module profile or include Axiom/Core.hpp for standalone core usage."
#endif

namespace Axiom {
	class GuiRenderer;
	class GizmoRenderer2D;
	class PhysicsSystem2D;
	class Renderer2D;
	class SceneManager;
	class Window;

	class AXIOM_API Application {
		friend class Window;
		friend class SceneManager;
		friend class ApplicationEditorAccess;
		friend class ScriptBindings;

		using Clock = std::chrono::steady_clock;
		using DurationChrono = Clock::duration;

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

		Application();
		virtual ~Application();


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
		static bool IsShuttingDown() { return s_Instance ? s_Instance->m_IsShuttingDown : false; }

		/// True for editor host, false for standalone runtime / launcher.
		static bool IsEditor() { return s_Instance ? s_Instance->m_IsEditorHost : false; }
		void SetEditorHost(bool isEditor) { m_IsEditorHost = isEditor; }

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
		// Snapshot the current layer order into a local vector. The previous index-based
		// iteration with re-`.Size()` checks was robust against iterator invalidation on
		// container reallocation, but NOT against position shifts: PushLayer inserts at
		// m_InsertIndex, which moves indices >= insertIndex by one, so the next iteration
		// would land on the wrong slot (skip a sibling on insert, double-visit on pop).
		// Snapshotting forward and reverse orders into local pointer vectors gives stable
		// per-call iteration: layers added during the dispatch don't appear until next
		// frame, layers popped during dispatch are still visited (their `OnX` runs), but
		// no sibling is ever skipped or duplicated.
		std::vector<Layer*> SnapshotLayerOrder() const {
			std::vector<Layer*> out;
			out.reserve(m_LayerStack.Size());
			for (size_t i = 0; i < m_LayerStack.Size(); ++i) {
				if (Layer* layer = const_cast<LayerStack&>(m_LayerStack).At(i)) {
					out.push_back(layer);
				}
			}
			return out;
		}

		std::vector<Layer*> SnapshotLayerOrderReversed() const {
			std::vector<Layer*> out;
			out.reserve(m_LayerStack.Size());
			for (size_t i = m_LayerStack.Size(); i > 0; --i) {
				if (Layer* layer = const_cast<LayerStack&>(m_LayerStack).At(i - 1)) {
					out.push_back(layer);
				}
			}
			return out;
		}

	private:
		std::unique_ptr<Window> m_Window;
		std::unique_ptr<Renderer2D> m_Renderer2D;
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
		bool m_IsEditorHost = false;
		bool m_IsPaused = false;
		// Atomic — written from GLFW window-focus / iconify callbacks (which today
		// fire from the polling thread, but the data type makes the contract
		// explicit and survives any future move to a real event thread).
		std::atomic<bool> m_IsBackgroundPaused{ false };
		std::atomic<bool> m_IsMinimized{ false };
		std::atomic<bool> m_WindowHasFocus{ true };
		bool m_IsPlaying = true;
		bool m_IsGameplayPaused = false;
		bool m_IsScriptInputEnabled = true;
		bool m_WasEnginePaused = false;
		bool m_IsRenderingFrame = false;
		bool m_IsRenderingRefresh = false;
		bool m_IsShuttingDown = false;
		bool m_QuitRequested = false;
		int m_QuitRequestFrame = -1;

		float m_TargetFramerate = 144.0f;
		static const float k_PausedTargetFrameRate;

		static Application* s_Instance;
		static CommandLineArgs s_CommandLineArgs;


		std::vector<std::string> m_PendingFileDrops;
		double m_FixedUpdateAccumulator;
		Clock::time_point m_LastFrameTime = Clock::now();
		// Stamped at end of frame; next frame reads it to compute the VSync bucket.
		Clock::time_point m_LastFrameEndTime{};
		LayerStack m_LayerStack;
		EventBus m_EventBus;

		void Initialize();
		void Shutdown(bool invokeOnQuit = true);
		void DispatchEvent(AxiomEvent& event);
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

		// Iconify (minimize) state. Routed from Window's GLFW iconify callback because the
		// framebuffer-resize-to-(0,0) path that previously fed m_IsMinimized doesn't fire on
		// every platform / monitor configuration (multi-monitor, DPI-virtualized layouts, X11
		// with certain WMs). Without this, the engine kept rendering at full target framerate
		// while invisible — battery drain on laptops + CPU/GPU usage on a window the user
		// can't see.
		static void SetWindowMinimized(bool minimized) {
			if (s_Instance) {
				s_Instance->m_IsMinimized = minimized;
			}
		}
	};

	// To be defined in client
	Application* CreateApplication();
}
