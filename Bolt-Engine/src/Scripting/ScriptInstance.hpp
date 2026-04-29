#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scripting/ScriptType.hpp"

#include <cstdint>
#include <string>

namespace Bolt {

	class NativeScript;

	class BOLT_API ScriptInstance {
	public:
		ScriptInstance() = default;
		explicit ScriptInstance(const std::string& className, ScriptType type = ScriptType::Managed)
			: m_ClassName(className), m_Type(type) {}

		const std::string& GetClassName() const { return m_ClassName; }
		void SetClassName(const std::string& name) { m_ClassName = name; }

		void Bind(EntityHandle entity) { m_Entity = entity; m_IsBound = true; }
		void Unbind() { m_Entity = entt::null; m_IsBound = false; m_HasStarted = false; m_HasEnabled = false; m_GCHandle = 0; m_NativePtr = nullptr; }
		bool IsBound() const { return m_IsBound; }
		EntityHandle GetEntity() const { return m_Entity; }

		bool HasStarted() const { return m_HasStarted; }
		void MarkStarted() { m_HasStarted = true; }
		bool HasEnabled() const { return m_HasEnabled; }
		void MarkEnabled() { m_HasEnabled = true; }
		void MarkDisabled() { m_HasEnabled = false; }

		// Managed (C#) instance
		uint32_t GetGCHandle() const { return m_GCHandle; }
		void SetGCHandle(uint32_t handle) { m_GCHandle = handle; m_Type = ScriptType::Managed; }
		bool HasManagedInstance() const { return m_GCHandle != 0; }

		// Native (C++) instance
		NativeScript* GetNativePtr() const { return m_NativePtr; }
		void SetNativePtr(NativeScript* ptr) { m_NativePtr = ptr; m_Type = ScriptType::Native; }
		bool HasNativeInstance() const { return m_NativePtr != nullptr; }

		bool HasAnyInstance() const { return HasManagedInstance() || HasNativeInstance(); }
		ScriptType GetType() const { return m_Type; }
		void SetType(ScriptType type) { m_Type = type; }

	private:
		std::string m_ClassName;
		EntityHandle m_Entity = entt::null;
		bool m_IsBound = false;
		bool m_HasStarted = false;
		bool m_HasEnabled = false;
		ScriptType m_Type = ScriptType::Unknown;
		uint32_t m_GCHandle = 0;
		NativeScript* m_NativePtr = nullptr;
	};

} // namespace Bolt
