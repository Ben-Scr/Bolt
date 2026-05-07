#include <pch.hpp>
#include "Editor/EditorComponentRegistration.hpp"

#include "Gui/ComponentInspectors.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptComponentInspector.hpp"

#include "Components/Components.hpp"

#include <span>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace Axiom {
	namespace {
		using InspectorFn = void (*)(std::span<const Entity>);

		struct InspectorBinding {
			std::type_index type;
			InspectorFn inspector;
		};

		template<typename T>
		InspectorBinding Bind(InspectorFn inspector) {
			return InspectorBinding{ std::type_index(typeid(T)), inspector };
		}

		// Editor-only: attach a draw-inspector callback to an already-registered
		// component. The component itself (display name, category, serializedName,
		// has/add/remove/copyTo) MUST be registered by the engine in
		// BuiltInComponentRegistration.cpp first; this file only paints in the UI
		// behavior on top.
		void AttachInspector(SceneManager& sceneManager, std::type_index type, InspectorFn inspector) {
			bool attached = false;
			sceneManager.GetComponentRegistry().ForEachComponentInfo([&](const std::type_index& id, ComponentInfo& info) {
				if (id == type) {
					info.drawInspector = inspector;
					attached = true;
				}
			});
			AIM_CORE_ASSERT(attached, AxiomErrorCode::InvalidArgument,
				"AttachInspector: component type not registered. Register it in BuiltInComponentRegistration.cpp before attaching an inspector.");
		}
	}

	// Single dispatch entry-point used by every inspector loop. If the
	// component carries a custom drawInspector lambda we honour it; otherwise
	// we fall through to the unified PropertyDrawer driven by the component's
	// declared PropertyDescriptors. Components with neither path are skipped.
	void DispatchComponentInspector(const ComponentInfo& info, std::span<const Entity> entities) {
		if (info.drawInspector) {
			info.drawInspector(entities);
			return;
		}
		if (!info.properties.empty()) {
			PropertyDrawer::DrawAll(entities, info.properties, info.displayName);
		}
	}

	void RegisterEditorComponentInspectors(SceneManager& sceneManager) {
		// Inspector-only attachments. The component metadata itself lives in
		// Axiom-Engine/src/Scene/BuiltInComponentRegistration.cpp — do not
		// re-declare display names, categories, or serialized names here.
		//
		// Most built-ins now declare PropertyDescriptors in their engine-side
		// registration (see BuiltInComponentRegistration.cpp). Those flow
		// through DispatchComponentInspector's auto-drawer fallback and don't
		// need an entry here. The bindings below are for components that
		// either:
		//   * have no PropertyDescriptors at all (ParticleSystem2D — variant
		//     fields don't fit the declarative model), OR
		//   * need a SLIM inspector that draws PropertyDrawer::DrawAll(...)
		//     first and then appends a few extra widgets the property model
		//     can't express (texture preview, runtime read-outs, scripts UI).
		const InspectorBinding bindings[] = {
			// Hybrid: properties + extras (texture preview, runtime read-outs).
			Bind<SpriteRendererComponent>(DrawSpriteRendererInspector),
			Bind<Camera2DComponent>(DrawCamera2DInspector),
			Bind<FastBody2DComponent>(DrawFastBody2DInspector),

			// Custom-only: variant types + per-shape branches don't map cleanly.
			Bind<ParticleSystem2DComponent>(DrawParticleSystem2DInspector),

			// Custom-only: Unity-style RectTransform layout — column-header
			// rows for Position / Size and inline axis-labeled rows for the
			// rest don't fit the declarative property model.
			Bind<RectTransform2DComponent>(DrawRectTransform2DInspector),

			// Custom-only: per-script field rendering goes through its own
			// PropertyDrawer-driven path (see ScriptComponentInspector.cpp).
			Bind<ScriptComponent>(DrawScriptComponentInspector),
		};

		for (const auto& b : bindings)
			AttachInspector(sceneManager, b.type, b.inspector);
	}
}
