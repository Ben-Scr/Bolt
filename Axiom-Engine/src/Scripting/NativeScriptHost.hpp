#pragma once
#include "Scene/EntityHandle.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace Axiom {

	class NativeScript;
	class Scene;

	class NativeScriptHost {
	public:
		bool LoadDLL(const std::string& dllPath);
		void UnloadDLL();
		bool IsLoaded() const { return m_DllHandle != nullptr; }
		bool Reload();

		NativeScript* CreateInstance(const std::string& className, EntityHandle entity, Scene* scene);
		void DestroyInstance(NativeScript* script);
		void DestroyAllInstances();

		bool HasClass(const std::string& className);
		const std::string& GetDLLPath() const { return m_DllPath; }

	private:
		using CreateFn  = NativeScript* (*)(const char*);
		using DestroyFn = void (*)(NativeScript*);
		using HasFn     = int (*)(const char*);
		using InitFn    = void (*)(void* engineAPI);

		void* m_DllHandle = nullptr;
		CreateFn  m_CreateFn = nullptr;
		DestroyFn m_DestroyFn = nullptr;
		HasFn     m_HasFn = nullptr;
		std::string m_DllPath;
		std::filesystem::path m_LoadedDllPath;

		std::vector<NativeScript*> m_LiveInstances;
	};

} // namespace Axiom
