using System;

namespace Axiom;
/// <summary>
/// Static math utility functions and constants.
/// Uses System.MathF for native float-precision math on CoreCLR.
/// </summary>
public static class Mathf
{
    public const float PI = MathF.PI;
    public const float TwoPI = PI * 2.0f;
    public const float HalfPI = PI * 0.5f;
    public const float Deg2Rad = PI / 180.0f;
    public const float Rad2Deg = 180.0f / PI;
    public const float Epsilon = 1e-6f;
    public const float Infinity = float.PositiveInfinity;

    public static float Sin(float x) => MathF.Sin(x);
    public static float Cos(float x) => MathF.Cos(x);
    public static float Tan(float x) => MathF.Tan(x);
    public static float Asin(float x) => MathF.Asin(x);
    public static float Acos(float x) => MathF.Acos(x);
    public static float Atan(float x) => MathF.Atan(x);
    public static float Atan2(float y, float x) => MathF.Atan2(y, x);

    public static float Sqrt(float x) => MathF.Sqrt(x);
    public static float Abs(float x) => MathF.Abs(x);
    public static float Pow(float x, float y) => MathF.Pow(x, y);
    public static float Log(float x) => MathF.Log(x);
    public static float Log10(float x) => MathF.Log10(x);
    public static float Exp(float x) => MathF.Exp(x);

    public static float Floor(float x) => MathF.Floor(x);
    public static float Ceil(float x) => MathF.Ceiling(x);
    public static float Round(float x) => MathF.Round(x);

    public static float Sign(float x) => x < 0.0f ? -1.0f : (x > 0.0f ? 1.0f : 0.0f);

    public static float Min(float a, float b) => a < b ? a : b;
    public static float Max(float a, float b) => a > b ? a : b;
    public static int Min(int a, int b) => a < b ? a : b;
    public static int Max(int a, int b) => a > b ? a : b;

    public static float Clamp(float value, float min, float max)
    {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    public static int Clamp(int value, int min, int max)
    {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    public static float Clamp01(float value) => Clamp(value, 0.0f, 1.0f);

    public static float Lerp(float a, float b, float t) => a + (b - a) * Clamp01(t);

    public static float LerpUnclamped(float a, float b, float t) => a + (b - a) * t;

    public static float InverseLerp(float a, float b, float value)
    {
        if (Abs(b - a) < Epsilon) return 0.0f;
        return Clamp01((value - a) / (b - a));
    }

    public static float MoveTowards(float current, float target, float maxDelta)
    {
        if (Abs(target - current) <= maxDelta) return target;
        return current + Sign(target - current) * maxDelta;
    }

    public static float SmoothStep(float a, float b, float t)
    {
        t = Clamp01(t);
        t = t * t * (3.0f - 2.0f * t);
        return a + (b - a) * t;
    }

    public static bool Approximately(float a, float b) => Abs(a - b) < Epsilon;
}
