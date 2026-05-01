#pragma once
#include <cmath>
#include <algorithm>

namespace Axiom {
    template<typename T>
    inline T Abs(T a) {
        return std::abs(a);
    }

    template<typename T>
    inline T Ceil(T a) {
        return std::ceil(a);
    }

    template<typename T>
    inline T Floor(T a) {
        return std::floor(a);
    }

    template<typename T>
    inline T Round(T a) {
        return std::round(a);
    }

    template<typename T>
    inline T Pow(T base, T exp) {
        return std::pow(base, exp);
    }

    template<typename T>
    inline T Sqrt(T value) {
        return std::sqrt(value);
    }

    template<typename T>
    inline T Min(T a, T b) {
        return (a < b) ? a : b;
    }

    template<typename T>
    inline T Max(T a, T b) {
        return (a > b) ? a : b;
    }

    template<typename T>
    inline T Clamp(T value, T minVal, T maxVal) {
        return Max(minVal, Min(value, maxVal));
    }

    template<typename T>
    inline T Lerp(T a, T b, T t) {
        return a + t * (b - a);
    }

    inline float Distance(float a, float b) {
        return std::abs(b - a);
    }
}