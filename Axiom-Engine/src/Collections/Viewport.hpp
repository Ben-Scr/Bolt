#pragma once
#include "Vec2.hpp"
#include "Core/Export.hpp"
#include "Math/Common.hpp"

namespace Axiom {
	class AXIOM_API Viewport {
	public:
		Viewport() = default;
		Viewport(int width, int height) : Width{ width }, Height{ height } {}

		void SetSize(int width, int height) { Width = width, Height = height; }
		Vec2Int GetSize() const noexcept { return Vec2Int{ Width, Height }; }
		float GetAspect() const noexcept { return Height > 0 ? static_cast<float>(Width) / static_cast<float>(Height) : 1.0f; }
		Vec2  GetHalfSize() const noexcept { return Vec2(0.5f * Width, 0.5f * Height); }
		Vec2  GetCenter() const noexcept { return Vec2(0.5f * Width, 0.5f * Height); }

		void SetWidth(int width) { Width = Max(1, width); }
		void SetHeight(int height) { Height = Max(1, height); }

		int GetWidth() const { return Width; }
		int GetHeight() const { return Height; }

	private:
		int   Width = 1920;
		int   Height = 1080;
	};

	inline std::ostream& operator<<(std::ostream& os, const Viewport& vp) {
		return os << vp.GetWidth() << " x " << vp.GetHeight();
	}
}