#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <array>
#include <ostream>
#include "Core/Export.hpp"

namespace Axiom {
	struct AXIOM_API Color {
		float r, g, b, a;
		Color() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}

		Color(float red, float green, float blue)
			: r(red), g(green), b(blue), a(1.0f) {
		}
		Color(float red, float green, float blue, float alpha)
			: r(red), g(green), b(blue), a(alpha) {
		}

		std::array<float,4> ToArray() const {
			return std::array<float, 4>{ r, g, b, a };
		}
		static Color FromArray(std::array<float, 4> col) {
			return { col[0], col[1], col[2], col[3] };
		}

		Color& SetAlpha(float alpha) { this->a = alpha; return *this; }
		Color WithAlpha(float alpha) const { Color c = *this; c.a = alpha; return c; }

		static const Color Red() { return Color(1.0f, 0.0f, 0.0f); }
		static const Color Green() { return Color(0.0f, 1.0f, 0.0f); }
		static const Color Blue() { return Color(0.0f, 0.0f, 1.0f); }
		static const Color Yellow() { return Color(1.0f, 1.0f, 0.0f); }
		static const Color Magenta() { return Color(1.0f, 0.0f, 1.0f); }
		static const Color Cyan() { return Color(0.0f, 1.0f, 1.0f); }
		static const Color White() { return Color(1.0f, 1.0f, 1.0f); }
		static const Color Black() { return Color(0.0f, 0.0f, 0.0f); }
		static const Color Gray() { return Color(0.5f, 0.5f, 0.5f); }
		static const Color Background() { return Color(0.35f, 0.35f, 0.35f); }
		static const Color Transparent() { return Color(0.0f, 0.0f, 0.0f, 0.0f); }

		inline uint32_t RGBA32() const {
			const uint32_t R = ToU8(r), G = ToU8(g), B = ToU8(b), A = ToU8(a);
			return (R << 24) | (G << 16) | (B << 8) | A;
		}

		inline uint32_t ABGR32() const {
			const uint32_t R = ToU8(r), G = ToU8(g), B = ToU8(b), A = ToU8(a);
			return (A << 24) | (B << 16) | (G << 8) | R;
		}

		inline uint32_t ARGB32() const {
			const uint32_t R = ToU8(r), G = ToU8(g), B = ToU8(b), A = ToU8(a);
			return (A << 24) | (R << 16) | (G << 8) | B;
		}

		static Color FromRGBA32(uint32_t rgba) {
			return Color(
				((rgba >> 24) & 0xFF) / 255.0f,
				((rgba >> 16) & 0xFF) / 255.0f,
				((rgba >> 8) & 0xFF) / 255.0f,
				(rgba & 0xFF) / 255.0f
			);
		}

		static Color FromABGR32(uint32_t abgr) {
			return Color(
				(abgr & 0xFF) / 255.0f,
				((abgr >> 8) & 0xFF) / 255.0f,
				((abgr >> 16) & 0xFF) / 255.0f,
				((abgr >> 24) & 0xFF) / 255.0f
			);
		}


		static Color Lerp(const Color& a, const Color& b, float t) {
			t = std::clamp(t, 0.0f, 1.0f);
			return Color(
				a.r + t * (b.r - a.r),
				a.g + t * (b.g - a.g),
				a.b + t * (b.b - a.b),
				a.a + t * (b.a - a.a)
			);
		}

		Color Clamped() const {
			return Color(
				std::clamp(r, 0.0f, 1.0f),
				std::clamp(g, 0.0f, 1.0f),
				std::clamp(b, 0.0f, 1.0f),
				std::clamp(a, 0.0f, 1.0f)
			);
		}

		Color operator+(const Color& other) const {
			return Color(r + other.r, g + other.g, b + other.b, a + other.a);
		}

		Color operator-(const Color& other) const {
			return Color(r - other.r, g - other.g, b - other.b, a - other.a);
		}

		Color operator*(float scalar) const {
			return Color(r * scalar, g * scalar, b * scalar, a * scalar);
		}

		Color operator*(const Color& other) const {
			return Color(r * other.r, g * other.g, b * other.b, a * other.a);
		}

		Color& operator+=(const Color& other) {
			r += other.r; g += other.g; b += other.b; a += other.a;
			return *this;
		}

		Color& operator*=(float scalar) {
			r *= scalar; g *= scalar; b *= scalar; a *= scalar;
			return *this;
		}

		bool operator==(const Color& other) const {
			return r == other.r && g == other.g && b == other.b && a == other.a;
		}

		bool operator!=(const Color& other) const {
			return !(*this == other);
		}

	private:
		inline uint8_t ToU8(float x) const {
			return static_cast<uint8_t>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f));
		}
	};

	inline std::ostream& operator<<(std::ostream& os, const Color& c) {
		return os << "RGBA(" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")";
	}
}