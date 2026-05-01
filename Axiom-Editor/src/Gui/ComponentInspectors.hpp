#pragma once
#include "Scene/Entity.hpp"

namespace Axiom {

	void DrawNameComponentInspector(Entity entity);
	void DrawTransform2DInspector(Entity entity);
	void DrawRigidbody2DInspector(Entity entity);
	void DrawSpriteRendererInspector(Entity entity);
	void DrawCamera2DInspector(Entity entity);
	void DrawParticleSystem2DInspector(Entity entity);
	void DrawBoxCollider2DInspector(Entity entity);
	void DrawAudioSourceInspector(Entity entity);

	// Axiom-Physics inspectors
	void DrawFastBody2DInspector(Entity entity);
	void DrawFastBoxCollider2DInspector(Entity entity);
	void DrawFastCircleCollider2DInspector(Entity entity);

} // namespace Axiom
