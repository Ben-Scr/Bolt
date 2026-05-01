#pragma once
#include "Core/UUID.hpp"
#include "Graphics/TextureHandle.hpp"
#include "Collections/Color.hpp"

namespace Axiom {
	struct ImageComponent {
		TextureHandle TextureHandle;
		UUID TextureAssetId{ 0 };
		Color Color;
	};
}
