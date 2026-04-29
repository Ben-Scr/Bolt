#pragma once
#include "Scene/Entity.hpp"

namespace Bolt {

	void DrawNameComponentInspector(Entity entity);
	void DrawTransform2DInspector(Entity entity);
	void DrawRigidbody2DInspector(Entity entity);
	void DrawSpriteRendererInspector(Entity entity);
	void DrawCamera2DInspector(Entity entity);
	void DrawParticleSystem2DInspector(Entity entity);
	void DrawBoxCollider2DInspector(Entity entity);
	void DrawAudioSourceInspector(Entity entity);

	// Bolt-Physics inspectors
	void DrawFastBody2DInspector(Entity entity);
	void DrawFastBoxCollider2DInspector(Entity entity);
	void DrawFastCircleCollider2DInspector(Entity entity);

} // namespace Bolt
