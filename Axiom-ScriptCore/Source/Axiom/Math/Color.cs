namespace Axiom;
public struct Color : System.IEquatable<Color>
{
    public float R, G, B, A;

    public Color(float r, float g, float b)
    {
        R = r; G = g; B = b; A = 1.0f;
    }
    public Color(float r, float g, float b, float a)
    {
        R = r; G = g; B = b; A = a;
    }

    public float X { get => R; set => R = value; }
    public float Y { get => G; set => G = value; }
    public float Z { get => B; set => B = value; }
    public float W { get => A; set => A = value; }

    public static readonly Color White = new(1.0f, 1.0f, 1.0f);
    public static readonly Color Black = new(0.0f, 0.0f, 0.0f);
    public static readonly Color Red = new(1.0f, 0.0f, 0.0f);
    public static readonly Color Green = new(0.0f, 1.0f, 0.0f);
    public static readonly Color Blue = new(0.0f, 0.0f, 1.0f);
    public static readonly Color Yellow = new(1.0f, 1.0f, 0.0f);
    public static readonly Color Cyan = new(0.0f, 1.0f, 1.0f);
    public static readonly Color Magenta = new(1.0f, 0.0f, 1.0f);
    public static readonly Color Gray = new(0.5f, 0.5f, 0.5f);
    public static readonly Color Clear = new(0.0f, 0.0f, 0.0f, 0.0f);

    // ── Operators ───────────────────────────────────────────────
    public static Color operator +(Color a, Color b) => new(a.R + b.R, a.G + b.G, a.B + b.B, a.A + b.A);
    public static Color operator -(Color a, Color b) => new(a.R - b.R, a.G - b.G, a.B - b.B, a.A - b.A);
    public static Color operator *(Color c, float s) => new(c.R * s, c.G * s, c.B * s, c.A * s);
    public static Color operator *(float s, Color c) => new(s * c.R, s * c.G, s * c.B, s * c.A);

    public static bool operator ==(Color a, Color b) => a.Equals(b);
    public static bool operator !=(Color a, Color b) => !a.Equals(b);

    // ── Conversions ─────────────────────────────────────────────
    // Color ↔ Vector4 conversion is defined in Vector4.cs to avoid ambiguity.
    // Use: Vector4 v = (Vector4)color;  or  Color c = (Color)vec4;
    public static explicit operator Vector3(Color c) => new(c.R, c.G, c.B);
    public static explicit operator Vector2(Color c) => new(c.R, c.G);

    // ── Equality ────────────────────────────────────────────────
    public bool Equals(Color other) => R == other.R && G == other.G && B == other.B && A == other.A;
    public override bool Equals(object? obj) => obj is Color other && Equals(other);
    public override int GetHashCode() => System.HashCode.Combine(R, G, B, A);
    public override string ToString() => $"Color({R}, {G}, {B}, {A})";

    /// <summary>Linear interpolation between two colors.</summary>
    public static Color Lerp(Color a, Color b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Color(a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t,
                         a.B + (b.B - a.B) * t, a.A + (b.A - a.A) * t);
    }
}
