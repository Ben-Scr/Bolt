#pragma once
#include "Core/Export.hpp"
#include "Scene/ISystem.hpp"

namespace Axiom {
	// Composes each entity's world-space Transform2DComponent::Position/Scale
	// /Rotation from its authored Local* values and its parent's world transform.
	// Runs first in Update so that scripts, rendering, gizmos, and physics
	// teleport queries see the current world snapshot. Also runs once in Awake
	// so that scripts that read transforms from their own Awake hook get the
	// post-deserialization world values.
	//
	// Entities with a physics body (Rigidbody2DComponent / FastBody2DComponent)
	// keep the world value the engine writes them — we don't overwrite their
	// transforms from Local — but we do recurse INTO their children so a body's
	// visual children follow physics-driven motion.
	class TransformHierarchySystem : public ISystem {
	public:
		void Awake(Scene& scene) override;
		void Update(Scene& scene) override;
		// Also runs every frame in OnPreRender so the editor (which doesn't
		// tick Update outside of play mode) sees children follow when the
		// user drags a parent's transform via the inspector.
		void OnPreRender(Scene& scene) override;

		// Imperative one-shot. Recomputes World from Local for every entity
		// in `scene` exactly once. The editor calls this between processing
		// inspector input and drawing the viewport FBO so a slider edit on
		// a child shows up the same frame instead of one frame behind.
		// Exported (AXIOM_API) so the Editor DLL can invoke it directly —
		// the rest of the class is internal and called only via virtual
		// dispatch through ISystem* in the engine.
		static AXIOM_API void Propagate(Scene& scene);
	};
}
