#pragma once
#include "Core/Layer.hpp"
#include "Project/LauncherRegistry.hpp"
#include "Project/AxiomProject.hpp"
#include "Core/Export.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#ifdef AIM_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Axiom {

	class LauncherLayer : public Layer {
	public:
		using Layer::Layer;

		void OnAttach(Application& app) override;
		void OnPreRender(Application& app) override;
		void OnDetach(Application& app) override;

	private:
		struct CreateProjectTaskState {
			std::mutex Mutex;
			std::thread Worker;
			std::optional<AxiomProject> CreatedProject;
			std::string Error;
			std::string Stage = "Idle";
			float Progress = 0.0f;
			int BuildExitCode = 0;
			bool Running = false;
			bool Finished = false;
			bool Success = false;
			bool InitialBuildSucceeded = true;
		};

		struct ProjectSizeTaskState {
			std::mutex Mutex;
			bool Finished = false;
			bool Failed = false;
			std::uintmax_t Bytes = 0;
			std::string Error;
		};

		// Async pipeline run when the user clicks "Open" on a project: regen → build →
		// spawn the editor. Stage/progress are read by the overlay each frame so the UI
		// doesn't freeze during the (potentially seconds-long) MSBuild step.
		struct OpenProjectTaskState {
			std::mutex Mutex;
			std::thread Worker;
			LauncherProjectEntry Entry;
			std::string Stage = "Idle";
			float Progress = 0.0f;
			std::string Error;
			bool Running = false;
			bool Finished = false;
			bool Success = false;
#ifdef AIM_PLATFORM_WINDOWS
			DWORD SpawnedProcessId = 0;
#endif
		};

		void RenderLauncherPanel();
		void RenderProjectList();
		void RenderCreateProjectPopup();
		void RenderDeleteProjectPopups();
		void OpenProject(const LauncherProjectEntry& entry);
		void OpenProjectInExplorer(const LauncherProjectEntry& entry);
		void BrowseForExistingProject();
		void RequestProjectDelete(const LauncherProjectEntry& entry);
		bool DeleteProjectFromDisk(const LauncherProjectEntry& entry);
		void StartCreateProjectAsync(const std::string& name, const std::string& location);
		void PollCreateProjectTask();
		void ResetCreateProjectTask(bool clearWorker = true);
		void OpenProjectWorkerBody(const LauncherProjectEntry& entry);
		void PollOpenProjectTask();
		std::shared_ptr<ProjectSizeTaskState> GetOrStartProjectSizeTask(const LauncherProjectEntry& entry);
		std::string GetProjectSizeDisplayText(const LauncherProjectEntry& entry);

		LauncherRegistry m_Registry;
		std::unordered_map<std::string, std::shared_ptr<ProjectSizeTaskState>> m_ProjectSizeTasks;

		char m_NewProjectName[256]{};
		char m_NewProjectLocation[512]{};
		std::string m_CreateError;
		std::string m_BrowseError;
		std::string m_ProjectActionError;

		bool m_IsCreating = false;
		bool m_OpenCreatePopup = false;
		bool m_CloseCreatePopup = false;
		std::string m_DeferredUpdatePath;
		CreateProjectTaskState m_CreateTask;
		std::optional<LauncherProjectEntry> m_PendingCreatedProjectOpen;

		bool m_IsOpening = false;
		std::chrono::steady_clock::time_point m_OpenStartTime;
		std::string m_OpeningProjectName;
		OpenProjectTaskState m_OpenTask;

		std::optional<LauncherProjectEntry> m_PendingDeleteProject;
		bool m_OpenDeleteConfirmPopup = false;
		bool m_OpenDeleteFinalConfirmPopup = false;
		std::string m_DeleteError;

#ifdef AIM_PLATFORM_WINDOWS
		std::unordered_map<std::string, DWORD> m_RunningProjects;
#endif
		std::string m_OpenError;
	};

}
