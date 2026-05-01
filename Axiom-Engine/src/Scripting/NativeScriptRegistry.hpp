#pragma once

#include <cstring>

namespace Axiom {

	class NativeScript;

	class NativeScriptRegistry {
	public:
		using Factory = NativeScript* (*)();

		struct Entry {
			const char* name;
			Factory factory;
			Entry* next;
		};

		inline static Entry* s_Head = nullptr;

		static void Register(const char* name, Factory factory) {
			if (!name || !factory) {
				return;
			}

			for (Entry* entry = s_Head; entry; entry = entry->next) {
				if (std::strcmp(name, entry->name) == 0) {
					entry->factory = factory;
					return;
				}
			}

			static Entry entries[256];
			static int count = 0;
			if (count < 256) {
				entries[count] = { name, factory, s_Head };
				s_Head = &entries[count++];
			}
		}

		static NativeScript* Create(const char* name) {
			if (!name) {
				return nullptr;
			}

			for (auto* e = s_Head; e; e = e->next) {
				if (std::strcmp(name, e->name) == 0) {
					return e->factory();
				}
			}

			return nullptr;
		}

		static bool Has(const char* name) {
			if (!name) {
				return false;
			}

			for (auto* e = s_Head; e; e = e->next) {
				if (std::strcmp(name, e->name) == 0) {
					return true;
				}
			}

			return false;
		}
	};

} // namespace Axiom
