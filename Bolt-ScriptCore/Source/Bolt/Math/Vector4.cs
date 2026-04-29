using System;
using System.Runtime.InteropServices;

namespace Bolt;
/// <summary>
/// 4D float vector. Used for colors (RGBA), shader uniforms,
/// and other four-component data. Layout matches glm::vec4.
/// </summary>
[StructLayout(LayoutKind.Explicit, Size = 16)]
public struct Vector4 : IEquatable<Vector4>
{
    [FieldOffset(0)]  public float X;
    [FieldOffset(4)]  public float Y;
    [FieldOffset(8)]  public float Z;
    [FieldOffset(12)] public float W;

    public static readonly Vector4 Zero = new(0.0f, 0.0f, 0.0f, 0.0f);
    public static readonly Vector4 One = new(1.0f, 1.0f, 1.0f, 1.0f);

    public Vector4(float scalar) { X = scalar; Y = scalar; Z = scalar; W = scalar; }
    public Vector4(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }
    public Vector4(Vector3 v, float w) { X = v.X; Y = v.Y; Z = v.Z; W = w; }
    public Vector4(Vector2 v, float z, float w) { X = v.X; Y = v.Y; Z = z; W = w; }

    public Vector2 XY => new(X, Y);
    public Vector3 XYZ => new(X, Y, Z);

    public float Length() => Mathf.Sqrt(X * X + Y * Y + Z * Z + W * W);

    public static float Dot(Vector4 a, Vector4 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;

    public static Vector4 Lerp(Vector4 a, Vector4 b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Vector4(
            a.X + (b.X - a.X) * t,
            a.Y + (b.Y - a.Y) * t,
            a.Z + (b.Z - a.Z) * t,
            a.W + (b.W - a.W) * t
        );
    }

    // Operators
    public static Vector4 operator +(Vector4 a, Vector4 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
    public static Vector4 operator -(Vector4 a, Vector4 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
    public static Vector4 operator *(Vector4 v, float s) => new(v.X * s, v.Y * s, v.Z * s, v.W * s);
    public static Vector4 operator *(float s, Vector4 v) => new(s * v.X, s * v.Y, s * v.Z, s * v.W);
    public static Vector4 operator /(Vector4 v, float s) => new(v.X / s, v.Y / s, v.Z / s, v.W / s);
    public static Vector4 operator -(Vector4 v) => new(-v.X, -v.Y, -v.Z, -v.W);

    public static bool operator ==(Vector4 a, Vector4 b) => a.Equals(b);
    public static bool operator !=(Vector4 a, Vector4 b) => !a.Equals(b);

    public bool Equals(Vector4 other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;
    public override bool Equals(object? obj) => obj is Vector4 other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override string ToString() => $"Vector4({X}, {Y}, {Z}, {W})";

    // ── Conversions ─────────────────────────────────────────────
    public static explicit operator Vector2(Vector4 v) => new(v.X, v.Y);
    public static explicit operator Vector3(Vector4 v) => new(v.X, v.Y, v.Z);

    // Vector4 ↔ Color
    public static implicit operator Color(Vector4 v) => new(v.X, v.Y, v.Z, v.W);
    public static implicit operator Vector4(Color c) => new(c.R, c.G, c.B, c.A);
}
