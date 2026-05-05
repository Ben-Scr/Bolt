#pragma once
#include "Scripting/ScriptGlue.hpp"

namespace Axiom {
	class ScriptBindings {
	public:
		static void PopulateNativeBindings(NativeBindings& bindings);
		static bool IsScriptInputEnabled();
	};

}
