#pragma once
#include "Core/Export.hpp"
#include <random>
#include <cstdint>
#include <mutex>

namespace Axiom {
    struct Color;

    class AXIOM_API Random {
    public:
        Random() = delete;

        static bool NextBool();
        static Color NextColor();

        static std::uint8_t NextByte(std::uint8_t min, std::uint8_t max);
        static std::uint8_t NextByte(std::uint8_t max);

        static float NextFloat();
        static float NextFloat(float max);
        static float NextFloat(float min, float max);

        static double NextDouble();
        static double NextDouble(double max);
        static double NextDouble(double min, double max);

        static int NextInt();
        static  int NextInt(int max);
        static int NextInt(int min, int max);

        static void SetSeed(uint32_t seed) { std::scoped_lock lock(s_Mutex); s_Gen.seed(seed); }

    private:
        inline static std::mt19937 s_Gen{ std::random_device{}() };
        inline static std::mutex s_Mutex;
    };

}