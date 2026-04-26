#include <Scripting/NativeScript.hpp>

#if defined(_WIN32)
#define BOLT_NATIVE_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__)
#define BOLT_NATIVE_SCRIPT_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define BOLT_NATIVE_SCRIPT_EXPORT extern "C"
#endif

BOLT_NATIVE_SCRIPT_EXPORT void BoltInitialize(void* engineAPI) {
	Bolt::g_EngineAPI = static_cast<Bolt::NativeEngineAPI*>(engineAPI);
}

BOLT_NATIVE_SCRIPT_EXPORT Bolt::NativeScript* BoltCreateScript(const char* className) {
	return Bolt::NativeScriptRegistry::Create(className);
}

BOLT_NATIVE_SCRIPT_EXPORT int BoltHasScript(const char* className) {
	return Bolt::NativeScriptRegistry::Has(className) ? 1 : 0;
}

BOLT_NATIVE_SCRIPT_EXPORT void BoltDestroyScript(Bolt::NativeScript* script) {
	delete script;
}
