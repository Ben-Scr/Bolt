#pragma once
#include "Scene/Entity.hpp"
#include "Scene/ISystem.hpp"
#include "Scripting/NativeScriptHost.hpp"
#include "Serialization/FileWatcher.hpp"
#include "Core/Export.hpp"
#include "Utils/Process.hpp"
#include <string>
#include <memory>
#include <cstddef>
#include <chrono>

namespace Bolt {
	struct ScriptSystemProcessTaskState;

	class BOLT_API ScriptSystem : public ISystem {
	public:
		void Awake(Scene& scene) override;
		void Update(Scene& scene) override;
		void OnGui(Scene& scene) override;
		void OnDestroy(Scene& scene) override;
		static bool RemoveScript(Entity entity, size_t index);
		static void RemoveAllScripts(Entity entity);

		void SetCoreAssemblyPath(const std::string& path) { m_CoreAssemblyPath = path; }
		void SetUserAssemblyPath(const std::string& path) { m_UserAssemblyPath = path; }

		/// Suppress file watcher polling (e.g. while a script is being created/renamed)
		void SetRecompileSuppressed(bool suppressed) { m_SuppressRecompile = suppressed; }
		bool IsRecompileSuppressed() const { return m_SuppressRecompile; }

		bool RequestRebuildAndReloadAll();
		bool IsRebuilding() const;
		bool DidLastRebuildSucceed() const { return !m_LastRebuildFailed; }

	private:
		void RebuildAndReloadScripts();
		void RebuildAndReloadNativeScripts();
		void TeardownManagedScripts(Scene& scene);
		void TeardownNativeScripts(Scene& scene);

		static inline bool m_SuppressRecompile = false;

		static inline std::string m_CoreAssemblyPath;
		static inline std::string m_UserAssemblyPath;
		static inline std::string m_SandboxProjectPath;
		static inline std::string m_NativeProjectDirectory;
		static inline Scene* m_LastScene = nullptr;
		static inline ScriptSystem* m_PollingOwner = nullptr;
		static inline std::size_t m_ActiveSystemCount = 0;

		// C# hot-reload
		static inline FileWatcher m_ScriptWatcher;
		static inline std::shared_ptr<ScriptSystemProcessTaskState> m_RebuildTask;
		static inline bool m_LastRebuildFailed = false;

		// C++ native scripts
		static inline NativeScriptHost m_NativeHost;
		static inline FileWatcher m_NativeWatcher;
		static inline std::string m_NativeDLLPath;
		static inline std::string m_NativeTargetName;
		static inline std::shared_ptr<ScriptSystemProcessTaskState> m_NativeRebuildTask;
	};

} // namespace Bolt
