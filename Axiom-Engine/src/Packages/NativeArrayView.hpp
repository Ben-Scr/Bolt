#pragma once

// Lightweight C++ view over data passed from a C# `Axiom.Collections.Native`
// container. Use this in package P/Invoke functions to consume buffers the
// managed side allocated, without copying.
//
// Calling convention (matches Axiom-ScriptCore's NativeArray<T>, NativeList<T>,
// NativeBuffer, etc.):
//
//   The C# caller passes `pointer, length` — TWO arguments — into the native
//   function. On the C# side that comes from `array.GetUnsafePtr()` and
//   `array.Length`. On the C++ side you build a `NativeArrayView<T>` from the
//   pair so the function body reads like a regular range.
//
//   NativeArray<T>  → pass GetUnsafePtr() + Length     → NativeArrayView<T>
//   NativeList<T>   → pass GetUnsafePtr() + Length     → NativeArrayView<T> (current count)
//   NativeBuffer    → pass GetUnsafePtr() + Length     → NativeArrayView<std::byte>
//
// Lifetime is the C# side's responsibility — DO NOT free the pointer here.
// The view is non-owning. If the C# code calls Dispose() while a C++ function
// is still using the view, you have a use-after-free; design your APIs so
// the C# side keeps the container alive across the call.
//
// Example: noise generation called from C#.
//
//   // C#:
//   using var noise = new NativeArray<float>(width * height);
//   unsafe { Bindings.GenerateNoise(noise.GetUnsafePtr(), noise.Length, seed); }
//
//   // C++ (in a package):
//   extern "C" AXIOM_PACKAGE_API
//   void GenerateNoise(float* data, int length, int seed) {
//       Axiom::NativeArrayView<float> out{data, length};
//       for (int i = 0; i < out.Length; ++i) {
//           out[i] = noise_at(i, seed);
//       }
//   }

#include <cstddef>

namespace Axiom {

    template <typename T>
    struct NativeArrayView {
        T*  Data   = nullptr;
        int Length = 0;

        constexpr NativeArrayView() = default;
        constexpr NativeArrayView(T* data, int length) noexcept : Data(data), Length(length) {}

        constexpr bool   IsEmpty()        const noexcept { return Length == 0 || Data == nullptr; }
        constexpr T&     operator[](int i) const noexcept { return Data[i]; }
        constexpr T*     begin()          const noexcept { return Data; }
        constexpr T*     end()            const noexcept { return Data + Length; }

        // Bytes occupied by the view (handy for memcpy / memset paths).
        constexpr std::size_t SizeInBytes() const noexcept {
            return static_cast<std::size_t>(Length) * sizeof(T);
        }
    };

} // namespace Axiom
