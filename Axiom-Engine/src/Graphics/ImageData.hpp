#pragma once

#include "Core/Export.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace Axiom {

	struct AXIOM_API ImageData {
		ImageData(int width, int height, unsigned char* pixels)
			: Width(width), Height(height), Pixels(pixels) {
		}
		ImageData(int width, int height, std::vector<unsigned char>&& pixels)
			: Width(width), Height(height), OwnedPixels(std::move(pixels)) {
			Pixels = OwnedPixels.data();
		}

		int Width;
		int Height;
		unsigned char* Pixels;
		std::vector<unsigned char> OwnedPixels;

		void FlipVerticalRGBA(int bytesPerPixel = 4) {
			if (!Pixels || Width <= 0 || Height <= 0 || bytesPerPixel <= 0) return;

			const size_t rowBytes = static_cast<size_t>(Width) * static_cast<size_t>(bytesPerPixel);
			std::vector<unsigned char> temp(rowBytes);

			for (int y = 0; y < Height / 2; ++y) {
				unsigned char* rowTop = Pixels + static_cast<size_t>(y) * rowBytes;
				unsigned char* rowBottom = Pixels + static_cast<size_t>(Height - 1 - y) * rowBytes;

				std::memcpy(temp.data(), rowTop, rowBytes);
				std::memcpy(rowTop, rowBottom, rowBytes);
				std::memcpy(rowBottom, temp.data(), rowBytes);
			}
		}

		static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

		static inline void SampleRGBA8(
			const std::vector<unsigned char>& src, int w, int h,
			int x, int y,
			unsigned char bgR, unsigned char bgG, unsigned char bgB, unsigned char bgA,
			float& r, float& g, float& b, float& a) {
			if (x < 0 || y < 0 || x >= w || y >= h) {
				r = static_cast<float>(bgR);
				g = static_cast<float>(bgG);
				b = static_cast<float>(bgB);
				a = static_cast<float>(bgA);
				return;
			}

			const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
			r = static_cast<float>(src[i + 0]);
			g = static_cast<float>(src[i + 1]);
			b = static_cast<float>(src[i + 2]);
			a = static_cast<float>(src[i + 3]);
		}

		static inline void BilinearRGBA8(
			const std::vector<unsigned char>& src, int w, int h,
			float x, float y,
			unsigned char bgR, unsigned char bgG, unsigned char bgB, unsigned char bgA,
			unsigned char& outR, unsigned char& outG, unsigned char& outB, unsigned char& outA) {
			const int x0 = static_cast<int>(std::floor(x));
			const int y0 = static_cast<int>(std::floor(y));
			const int x1 = x0 + 1;
			const int y1 = y0 + 1;

			const float tx = x - static_cast<float>(x0);
			const float ty = y - static_cast<float>(y0);

			float r00, g00, b00, a00, r10, g10, b10, a10, r01, g01, b01, a01, r11, g11, b11, a11;
			SampleRGBA8(src, w, h, x0, y0, bgR, bgG, bgB, bgA, r00, g00, b00, a00);
			SampleRGBA8(src, w, h, x1, y0, bgR, bgG, bgB, bgA, r10, g10, b10, a10);
			SampleRGBA8(src, w, h, x0, y1, bgR, bgG, bgB, bgA, r01, g01, b01, a01);
			SampleRGBA8(src, w, h, x1, y1, bgR, bgG, bgB, bgA, r11, g11, b11, a11);

			const float r0 = Lerp(r00, r10, tx);
			const float g0 = Lerp(g00, g10, tx);
			const float b0 = Lerp(b00, b10, tx);
			const float a0 = Lerp(a00, a10, tx);

			const float r1 = Lerp(r01, r11, tx);
			const float g1 = Lerp(g01, g11, tx);
			const float b1 = Lerp(b01, b11, tx);
			const float a1 = Lerp(a01, a11, tx);

			const float rf = Lerp(r0, r1, ty);
			const float gf = Lerp(g0, g1, ty);
			const float bf = Lerp(b0, b1, ty);
			const float af = Lerp(a0, a1, ty);

			auto clampU8 = [](float v) -> unsigned char {
				v = std::clamp(v, 0.0f, 255.0f);
				return static_cast<unsigned char>(std::lround(v));
			};

			outR = clampU8(rf);
			outG = clampU8(gf);
			outB = clampU8(bf);
			outA = clampU8(af);
		}

		void SetRotationRGBAAnyAngle(
			std::vector<unsigned char>& pixels, int& width, int& height, double degrees,
			bool expandCanvas = false,
			unsigned char bgR = 0, unsigned char bgG = 0, unsigned char bgB = 0, unsigned char bgA = 0) {
			if (width <= 0 || height <= 0) return;
			if (pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4u) return;

			const double rad = degrees * (3.14159265358979323846 / 180.0);
			const double c = std::cos(rad);
			const double s = std::sin(rad);

			const int srcW = width;
			const int srcH = height;
			const std::vector<unsigned char> src = pixels;

			int dstW = srcW;
			int dstH = srcH;

			if (expandCanvas) {
				const double cx = static_cast<double>(srcW) * 0.5;
				const double cy = static_cast<double>(srcH) * 0.5;

				const double corners[4][2] = {
					{0.0 - cx, 0.0 - cy},
					{static_cast<double>(srcW) - cx, 0.0 - cy},
					{0.0 - cx, static_cast<double>(srcH) - cy},
					{static_cast<double>(srcW) - cx, static_cast<double>(srcH) - cy}
				};

				double minX = 1e30, minY = 1e30;
				double maxX = -1e30, maxY = -1e30;

				for (int i = 0; i < 4; ++i) {
					const double x = corners[i][0];
					const double y = corners[i][1];
					const double rx = x * c - y * s;
					const double ry = x * s + y * c;

					minX = std::min(minX, rx);
					minY = std::min(minY, ry);
					maxX = std::max(maxX, rx);
					maxY = std::max(maxY, ry);
				}

				dstW = static_cast<int>(std::ceil(maxX - minX));
				dstH = static_cast<int>(std::ceil(maxY - minY));

				dstW = std::max(dstW, 1);
				dstH = std::max(dstH, 1);
			}

			std::vector<unsigned char> out(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4u, 0);

			const double srcCx = static_cast<double>(srcW) * 0.5;
			const double srcCy = static_cast<double>(srcH) * 0.5;
			const double dstCx = static_cast<double>(dstW) * 0.5;
			const double dstCy = static_cast<double>(dstH) * 0.5;

			const double ic = c;
			const double is = -s;

			for (int y = 0; y < dstH; ++y) {
				for (int x = 0; x < dstW; ++x) {
					const double dx = (static_cast<double>(x) + 0.5) - dstCx;
					const double dy = (static_cast<double>(y) + 0.5) - dstCy;

					const double sx = dx * ic + dy * (-is);
					const double sy = dx * is + dy * ic;

					const float srcX = static_cast<float>(sx + srcCx - 0.5);
					const float srcY = static_cast<float>(sy + srcCy - 0.5);

					unsigned char r, g, b, a;
					BilinearRGBA8(src, srcW, srcH, srcX, srcY, bgR, bgG, bgB, bgA, r, g, b, a);

					const size_t di = (static_cast<size_t>(y) * static_cast<size_t>(dstW) + static_cast<size_t>(x)) * 4u;
					out[di + 0] = r;
					out[di + 1] = g;
					out[di + 2] = b;
					out[di + 3] = a;
				}
			}

			pixels.swap(out);
			width = dstW;
			height = dstH;
		}
	};

} // namespace Axiom
