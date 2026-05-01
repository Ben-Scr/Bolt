using System;
using System.Runtime.InteropServices;

namespace Axiom;
[StructLayout(LayoutKind.Explicit, Size = 8)]
public struct Vector2 : IEquatable<Vector2>
{
    [FieldOffset(0)] public float X;
    [FieldOffset(4)] public float Y;

    public static readonly Vector2 Zero = new(0.0f, 0.0f);
    public static readonly Vector2 One = new(1.0f, 1.0f);
    public static readonly Vector2 Right = new(1.0f, 0.0f);
    public static readonly Vector2 Left = new(-1.0f, 0.0f);
    public static readonly Vector2 Up = new(0.0f, 1.0f);
    public static readonly Vector2 Down = new(0.0f, -1.0f);

    public Vector2(float scalar) { X = scalar; Y = scalar; }
    public Vector2(float x, float y) { X = x; Y = y; }
    public Vector2(Vector3 v) { X = v.X; Y = v.Y; }

    public float Length() => Mathf.Sqrt(X * X + Y * Y);
    public float LengthSquared() => X * X + Y * Y;

    public Vector2 Normalized()
    {
        float len = Length();
        return len > Mathf.Epsilon ? new Vector2(X / len, Y / len) : Zero;
    }

    public void Normalize()
    {
        float len = Length();
        if (len > Mathf.Epsilon) { X /= len; Y /= len; }
    }

    public static float Dot(Vector2 a, Vector2 b) => a.X * b.X + a.Y * b.Y;

    public static float Distance(Vector2 a, Vector2 b) => (a - b).Length();

    public static Vector2 Lerp(Vector2 a, Vector2 b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Vector2(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t);
    }

    public static Vector2 MoveTowards(Vector2 current, Vector2 target, float maxDelta)
    {
        Vector2 diff = target - current;
        float dist = diff.Length();
        if (dist <= maxDelta || dist < Mathf.Epsilon) return target;
        return current + diff / dist * maxDelta;
    }

    public static Vector2 Reflect(Vector2 direction, Vector2 normal)
    {
        float dot = Dot(direction, normal);
        return direction - 2.0f * dot * normal;
    }

    public static Vector2 Perpendicular(Vector2 v) => new(-v.Y, v.X);

    public static float Angle(Vector2 from, Vector2 to)
    {
        float denom = Mathf.Sqrt(from.LengthSquared() * to.LengthSquared());
        if (denom < Mathf.Epsilon) return 0.0f;
        float dot = Mathf.Clamp(Dot(from, to) / denom, -1.0f, 1.0f);
        return Mathf.Acos(dot) * Mathf.Rad2Deg;
    }

    public void Clamp(Vector2 min, Vector2 max)
    {
        X = Mathf.Clamp(X, min.X, max.X);
        Y = Mathf.Clamp(Y, min.Y, max.Y);
    }

    // Operators
    public static Vector2 operator +(Vector2 a, Vector2 b) => new(a.X + b.X, a.Y + b.Y);
    public static Vector2 operator -(Vector2 a, Vector2 b) => new(a.X - b.X, a.Y - b.Y);
    public static Vector2 operator *(Vector2 a, Vector2 b) => new(a.X * b.X, a.Y * b.Y);
    public static Vector2 operator /(Vector2 a, Vector2 b) => new(a.X / b.X, a.Y / b.Y);
    public static Vector2 operator *(Vector2 v, float s) => new(v.X * s, v.Y * s);
    public static Vector2 operator *(float s, Vector2 v) => new(s * v.X, s * v.Y);
    public static Vector2 operator /(Vector2 v, float s) => new(v.X / s, v.Y / s);
    public static Vector2 operator -(Vector2 v) => new(-v.X, -v.Y);

    public static bool operator ==(Vector2 a, Vector2 b) => a.Equals(b);
    public static bool operator !=(Vector2 a, Vector2 b) => !a.Equals(b);

    public bool Equals(Vector2 other) => X == other.X && Y == other.Y;
    public override bool Equals(object? obj) => obj is Vector2 other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(X, Y);
    public override string ToString() => $"Vector2({X}, {Y})";

    // ── Conversions ─────────────────────────────────────────────
    public static implicit operator Vector3(Vector2 v) => new(v.X, v.Y, 0f);
    public static implicit operator Vector4(Vector2 v) => new(v.X, v.Y, 0f, 0f);
    public static explicit operator Vector2(Vector3 v) => new(v.X, v.Y);
    public static explicit operator Vector2(Vector4 v) => new(v.X, v.Y);
}
