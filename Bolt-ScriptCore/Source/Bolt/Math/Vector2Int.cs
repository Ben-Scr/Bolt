using System;
using System.Runtime.InteropServices;

namespace Bolt;
[StructLayout(LayoutKind.Explicit, Size = 8)]
public struct Vector2Int : IEquatable<Vector2Int>
{
    [FieldOffset(0)] public int X;
    [FieldOffset(4)] public int Y;

    public static readonly Vector2Int Zero = new(0, 0);
    public static readonly Vector2Int One = new(1, 1);
    public static readonly Vector2Int Right = new(1, 0);
    public static readonly Vector2Int Left = new(-1, 0);
    public static readonly Vector2Int Up = new(0, 1);
    public static readonly Vector2Int Down = new(0, -1);

    public Vector2Int(int scalar) { X = scalar; Y = scalar; }
    public Vector2Int(int x, int y) { X = x; Y = y; }
    public Vector2Int(Vector3Int v) { X = v.X; Y = v.Y; }

    public float Length() => Mathf.Sqrt(LengthSquared());
    public int LengthSquared() => X * X + Y * Y;

    public void Clamp(Vector2Int min, Vector2Int max)
    {
        X = Math.Clamp(X, min.X, max.X);
        Y = Math.Clamp(Y, min.Y, max.Y);
    }

    public static int Dot(Vector2Int a, Vector2Int b) => a.X * b.X + a.Y * b.Y;
    public static float Distance(Vector2Int a, Vector2Int b) => (a - b).Length();

    public static Vector2Int Min(Vector2Int a, Vector2Int b) => new(Math.Min(a.X, b.X), Math.Min(a.Y, b.Y));
    public static Vector2Int Max(Vector2Int a, Vector2Int b) => new(Math.Max(a.X, b.X), Math.Max(a.Y, b.Y));

    public static Vector2Int operator +(Vector2Int a, Vector2Int b) => new(a.X + b.X, a.Y + b.Y);
    public static Vector2Int operator -(Vector2Int a, Vector2Int b) => new(a.X - b.X, a.Y - b.Y);
    public static Vector2Int operator *(Vector2Int a, Vector2Int b) => new(a.X * b.X, a.Y * b.Y);
    public static Vector2Int operator /(Vector2Int a, Vector2Int b) => new(a.X / b.X, a.Y / b.Y);
    public static Vector2Int operator *(Vector2Int v, int s) => new(v.X * s, v.Y * s);
    public static Vector2Int operator *(int s, Vector2Int v) => new(s * v.X, s * v.Y);
    public static Vector2Int operator /(Vector2Int v, int s) => new(v.X / s, v.Y / s);
    public static Vector2Int operator -(Vector2Int v) => new(-v.X, -v.Y);

    public static bool operator ==(Vector2Int a, Vector2Int b) => a.Equals(b);
    public static bool operator !=(Vector2Int a, Vector2Int b) => !a.Equals(b);

    public bool Equals(Vector2Int other) => X == other.X && Y == other.Y;
    public override bool Equals(object? obj) => obj is Vector2Int other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(X, Y);
    public override string ToString() => $"Vector2Int({X}, {Y})";

    public static implicit operator Vector2(Vector2Int v) => new(v.X, v.Y);
    public static implicit operator Vector3(Vector2Int v) => new(v.X, v.Y, 0.0f);
    public static implicit operator Vector4(Vector2Int v) => new(v.X, v.Y, 0.0f, 0.0f);
    public static explicit operator Vector2Int(Vector2 v) => new((int)v.X, (int)v.Y);
    public static explicit operator Vector2Int(Vector3 v) => new((int)v.X, (int)v.Y);
    public static explicit operator Vector2Int(Vector4 v) => new((int)v.X, (int)v.Y);
}
