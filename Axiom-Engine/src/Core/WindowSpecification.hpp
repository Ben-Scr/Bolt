#pragma once

#include "Collections/Color.hpp"

#include <string>

namespace Axiom {

	struct WindowSpecification {
		int Width{ 800 }, Height{ 800 };
		std::string Title{ "GLFW Window" };
		bool Resizeable{ true };
		bool Decorated{ true };
		bool Fullscreen{ false };
		Color Clearcolor;

		WindowSpecification() = default;

		WindowSpecification(int width, int height, const std::string& title)
			: Width{ width }, Height{ height }, Title{ title } {
		}

		WindowSpecification(int width, int height, const std::string& title, bool resizeable, bool decorated, bool fullscreen)
			: Width{ width }, Height{ height }, Title{ title }, Resizeable{ resizeable }, Decorated{ decorated }, Fullscreen{ fullscreen } {
		}
	};

} // namespace Axiom
