#pragma once
#include "Core/Export.hpp"
#include "Core/UUID.hpp"
#include "Graphics/TextureHandle.hpp"
#include "Collections/Color.hpp"

namespace Axiom {
	struct AXIOM_API SpriteRendererComponent {
		SpriteRendererComponent() = default;

		short SortingOrder{0};
		uint8_t SortingLayer{0};
		TextureHandle TextureHandle;
		UUID TextureAssetId{ 0 };
		Color Color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};
}
