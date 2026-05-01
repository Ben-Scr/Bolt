#include <Scripting/NativeScript.hpp>

#if defined(_WIN32)
#define AXIOM_NATIVE_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__)
#define AXIOM_NATIVE_SCRIPT_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define AXIOM_NATIVE_SCRIPT_EXPORT extern "C"
#endif

AXIOM_NATIVE_SCRIPT_EXPORT void AxiomInitialize(void* engineAPI) {
	Axiom::g_EngineAPI = static_cast<Axiom::NativeEngineAPI*>(engineAPI);
}

AXIOM_NATIVE_SCRIPT_EXPORT Axiom::NativeScript* AxiomCreateScript(const char* className) {
	return Axiom::NativeScriptRegistry::Create(className);
}

AXIOM_NATIVE_SCRIPT_EXPORT int AxiomHasScript(const char* className) {
	return Axiom::NativeScriptRegistry::Has(className) ? 1 : 0;
}

AXIOM_NATIVE_SCRIPT_EXPORT void AxiomDestroyScript(Axiom::NativeScript* script) {
	delete script;
}
