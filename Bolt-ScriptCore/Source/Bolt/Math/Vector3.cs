using System;
using System.Runtime.InteropServices;

namespace Bolt;
/// <summary>
/// 3D float vector. Used for compatibility and future 3D features.
/// Layout matches glm::vec3 for direct interop.
/// </summary>
[StructLayout(LayoutKind.Explicit, Size = 12)]
public struct Vector3 : IEquatable<Vector3>
{
    [FieldOffset(0)] public float X;
    [FieldOffset(4)] public float Y;
    [FieldOffset(8)] public float Z;

    public static readonly Vector3 Zero = new(0.0f, 0.0f, 0.0f);
    public static readonly Vector3 One = new(1.0f, 1.0f, 1.0f);
    public static readonly Vector3 Right = new(1.0f, 0.0f, 0.0f);
    public static readonly Vector3 Left = new(-1.0f, 0.0f, 0.0f);
    public static readonly Vector3 Up = new(0.0f, 1.0f, 0.0f);
    public static readonly Vector3 Down = new(0.0f, -1.0f, 0.0f);
    public static readonly Vector3 Forward = new(0.0f, 0.0f, -1.0f);
    public static readonly Vector3 Back = new(0.0f, 0.0f, 1.0f);

    public Vector3(float scalar) { X = scalar; Y = scalar; Z = scalar; }
    public Vector3(float x, float y, float z) { X = x; Y = y; Z = z; }
    public Vector3(Vector2 v, float z = 0.0f) { X = v.X; Y = v.Y; Z = z; }

    public float Length() => Mathf.Sqrt(X * X + Y * Y + Z * Z);
    public float LengthSquared() => X * X + Y * Y + Z * Z;

    public Vector3 Normalized()
    {
        float len = Length();
        return len > Mathf.Epsilon ? new Vector3(X / len, Y / len, Z / len) : Zero;
    }

    public void Normalize()
    {
        float len = Length();
        if (len > Mathf.Epsilon) { X /= len; Y /= len; Z /= len; }
    }

    public static float Dot(Vector3 a, Vector3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

    public static Vector3 Cross(Vector3 a, Vector3 b) => new(
        a.Y * b.Z - a.Z * b.Y,
        a.Z * b.X - a.X * b.Z,
        a.X * b.Y - a.Y * b.X
    );

    public static float Distance(Vector3 a, Vector3 b) => (a - b).Length();

    public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Vector3(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t);
    }

    public Vector2 XY => new(X, Y);

    // Operators
    public static Vector3 operator +(Vector3 a, Vector3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vector3 operator -(Vector3 a, Vector3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vector3 operator *(Vector3 a, Vector3 b) => new(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
    public static Vector3 operator *(Vector3 v, float s) => new(v.X * s, v.Y * s, v.Z * s);
    public static Vector3 operator *(float s, Vector3 v) => new(s * v.X, s * v.Y, s * v.Z);
    public static Vector3 operator /(Vector3 v, float s) => new(v.X / s, v.Y / s, v.Z / s);
    public static Vector3 operator -(Vector3 v) => new(-v.X, -v.Y, -v.Z);

    public static bool operator ==(Vector3 a, Vector3 b) => a.Equals(b);
    public static bool operator !=(Vector3 a, Vector3 b) => !a.Equals(b);

    public bool Equals(Vector3 other) => X == other.X && Y == other.Y && Z == other.Z;
    public override bool Equals(object? obj) => obj is Vector3 other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z);
    public override string ToString() => $"Vector3({X}, {Y}, {Z})";

    // ── Conversions ─────────────────────────────────────────────
    public static explicit operator Vector2(Vector3 v) => new(v.X, v.Y);
    public static implicit operator Vector4(Vector3 v) => new(v.X, v.Y, v.Z, 0f);
    public static explicit operator Vector3(Vector4 v) => new(v.X, v.Y, v.Z);
}
