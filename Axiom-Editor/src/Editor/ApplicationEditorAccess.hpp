#pragma once

#include "Core/Application.hpp"

namespace Axiom {
	class ApplicationEditorAccess {
	public:
		static void SetPlaymodePaused(bool paused)
		{
			if (Application::s_Instance) {
				Application::s_Instance->m_IsGameplayPaused = paused;
			}
		}

		static bool IsPlaymodePaused()
		{
			return Application::s_Instance ? Application::s_Instance->m_IsGameplayPaused : false;
		}

		static void SetGameInputEnabled(bool enabled)
		{
			if (Application::s_Instance) {
				Application::s_Instance->m_IsScriptInputEnabled = enabled;
			}
		}

		static bool IsGameInputEnabled()
		{
			return Application::s_Instance ? Application::s_Instance->m_IsScriptInputEnabled : true;
		}

	private:
		ApplicationEditorAccess() = delete;
	};
}
