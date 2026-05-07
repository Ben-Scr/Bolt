#pragma once
#include "Collections/Vec2.hpp"

namespace Axiom {

	// 2D rectangle transform used by the UI system. Conceptually mirrors
	// Unity's RectTransform but trimmed to the bits this engine actually
	// needs:
	//
	//   AnchorMin / AnchorMax — fractional anchors inside the parent's
	//     rect, in [0, 1]. AnchorMin == AnchorMax means a point anchor
	//     (the rect floats with that fraction of the parent). When they
	//     differ on an axis the rect stretches, so size on that axis
	//     becomes parent_size_on_axis * (max - min) + SizeDelta.
	//   Pivot — fractional point inside the rect that AnchoredPosition
	//     refers to. (0.5, 0.5) keeps the rect centred on the anchor;
	//     (0, 0) puts the bottom-left at the anchor. Also the centre of
	//     rotation and scale.
	//   AnchoredPosition — offset from the anchor (or anchor mid-point
	//     when anchors differ) to the pivot, in world units.
	//   SizeDelta — additional size on top of the stretched anchor span.
	//     For point anchors (min == max) this *is* the size.
	//
	// World-space placement (no parent or parent has no RectTransform2D):
	//     bottomLeft = AnchoredPosition - Pivot * SizeDelta
	//     topRight   = bottomLeft + SizeDelta
	//
	// The legacy fields Position, Width, Height, Rotation, Scale are
	// kept so existing scenes/inspectors keep working — internally they
	// shadow AnchoredPosition / SizeDelta. Newer code should prefer the
	// anchor-aware API.
	struct RectTransform2DComponent {
		// Anchored layout (Unity-style). Defaults match a centred,
		// 100x100 rect with a centred pivot — the common "drop in a
		// button" case.
		Vec2 AnchorMin{ 0.5f, 0.5f };
		Vec2 AnchorMax{ 0.5f, 0.5f };
		Vec2 Pivot    { 0.5f, 0.5f };
		Vec2 AnchoredPosition{ 0.0f, 0.0f };
		Vec2 SizeDelta{ 100.0f, 100.0f };

		// Local-space rotation (radians) + scale, applied around Pivot.
		float Rotation = 0.0f;
		Vec2 Scale{ 1.0f, 1.0f };

		// ── Convenience accessors (point-anchor case) ─────────────────
		// These keep working for the "no parent / point anchors" path
		// that 90% of UI ends up using; for parent-stretched rects use
		// UI helper functions in the renderer / event system that have
		// access to the parent's resolved rect.
		Vec2 GetSize() const {
			return Vec2{ SizeDelta.x * Scale.x, SizeDelta.y * Scale.y };
		}

		Vec2 GetBottomLeft() const {
			const Vec2 size = GetSize();
			return Vec2{
				AnchoredPosition.x - size.x * Pivot.x,
				AnchoredPosition.y - size.y * Pivot.y
			};
		}

		Vec2 GetTopRight() const {
			const Vec2 bl = GetBottomLeft();
			const Vec2 size = GetSize();
			return Vec2{ bl.x + size.x, bl.y + size.y };
		}

		Vec2 GetCenter() const {
			const Vec2 bl = GetBottomLeft();
			const Vec2 size = GetSize();
			return Vec2{ bl.x + size.x * 0.5f, bl.y + size.y * 0.5f };
		}

		bool ContainsPoint(const Vec2& worldPoint) const {
			const Vec2 bl = GetBottomLeft();
			const Vec2 tr = GetTopRight();
			return worldPoint.x >= bl.x && worldPoint.x <= tr.x
				&& worldPoint.y >= bl.y && worldPoint.y <= tr.y;
		}
	};

}
