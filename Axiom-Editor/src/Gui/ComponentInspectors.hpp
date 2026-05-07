#pragma once
#include "Scene/Entity.hpp"

#include <span>

namespace Axiom {

	// Hybrid inspectors: the bulk of each component's fields are declared as
	// PropertyDescriptors in `Axiom-Engine/src/Scene/BuiltInComponentRegistration.cpp`,
	// and the editor's auto-drawer fallback handles the standard cases. The
	// functions below remain only because their components have widgets that
	// don't fit the property model — texture preview, filter/wrap, runtime
	// physics read-outs. Each one calls PropertyDrawer::DrawAll(...) first
	// and then appends those extras.
	void DrawSpriteRendererInspector(std::span<const Entity> entities);
	void DrawCamera2DInspector(std::span<const Entity> entities);
	void DrawFastBody2DInspector(std::span<const Entity> entities);

	// Custom-only inspector — variant types (CircleParams/SquareParams) and
	// per-shape conditional UI don't map to declarative properties.
	void DrawParticleSystem2DInspector(std::span<const Entity> entities);

	// Custom-only inspector — Unity-style RectTransform layout: stacked
	// column headers ("Pos X / Pos Y", "Width / Height") for the position
	// + size pair, then a collapsible Anchors group, with the remaining
	// rows (Pivot, Rotation, Scale) using inline axis labels. Doesn't fit
	// the declarative property model because it wants per-row sub-labels
	// and a different layout than the standard "label : Vec2 drag" row.
	void DrawRectTransform2DInspector(std::span<const Entity> entities);

} // namespace Axiom
