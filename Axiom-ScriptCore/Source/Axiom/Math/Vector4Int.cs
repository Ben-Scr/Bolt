using System;
using System.Runtime.InteropServices;

namespace Axiom;
[StructLayout(LayoutKind.Explicit, Size = 16)]
public struct Vector4Int : IEquatable<Vector4Int>
{
    [FieldOffset(0)] public int X;
    [FieldOffset(4)] public int Y;
    [FieldOffset(8)] public int Z;
    [FieldOffset(12)] public int W;

    public static readonly Vector4Int Zero = new(0, 0, 0, 0);
    public static readonly Vector4Int One = new(1, 1, 1, 1);

    public Vector4Int(int scalar) { X = scalar; Y = scalar; Z = scalar; W = scalar; }
    public Vector4Int(int x, int y, int z, int w) { X = x; Y = y; Z = z; W = w; }
    public Vector4Int(Vector3Int v, int w) { X = v.X; Y = v.Y; Z = v.Z; W = w; }
    public Vector4Int(Vector2Int v, int z, int w) { X = v.X; Y = v.Y; Z = z; W = w; }

    public Vector2Int XY => new(X, Y);
    public Vector3Int XYZ => new(X, Y, Z);

    public float Length() => Mathf.Sqrt(LengthSquared());
    public int LengthSquared() => X * X + Y * Y + Z * Z + W * W;

    public void Clamp(Vector4Int min, Vector4Int max)
    {
        X = Math.Clamp(X, min.X, max.X);
        Y = Math.Clamp(Y, min.Y, max.Y);
        Z = Math.Clamp(Z, min.Z, max.Z);
        W = Math.Clamp(W, min.W, max.W);
    }

    public static int Dot(Vector4Int a, Vector4Int b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;

    public static Vector4Int Min(Vector4Int a, Vector4Int b) => new(
        Math.Min(a.X, b.X),
        Math.Min(a.Y, b.Y),
        Math.Min(a.Z, b.Z),
        Math.Min(a.W, b.W));

    public static Vector4Int Max(Vector4Int a, Vector4Int b) => new(
        Math.Max(a.X, b.X),
        Math.Max(a.Y, b.Y),
        Math.Max(a.Z, b.Z),
        Math.Max(a.W, b.W));

    public static Vector4Int operator +(Vector4Int a, Vector4Int b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
    public static Vector4Int operator -(Vector4Int a, Vector4Int b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
    public static Vector4Int operator *(Vector4Int a, Vector4Int b) => new(a.X * b.X, a.Y * b.Y, a.Z * b.Z, a.W * b.W);
    public static Vector4Int operator /(Vector4Int a, Vector4Int b) => new(a.X / b.X, a.Y / b.Y, a.Z / b.Z, a.W / b.W);
    public static Vector4Int operator *(Vector4Int v, int s) => new(v.X * s, v.Y * s, v.Z * s, v.W * s);
    public static Vector4Int operator *(int s, Vector4Int v) => new(s * v.X, s * v.Y, s * v.Z, s * v.W);
    public static Vector4Int operator /(Vector4Int v, int s) => new(v.X / s, v.Y / s, v.Z / s, v.W / s);
    public static Vector4Int operator -(Vector4Int v) => new(-v.X, -v.Y, -v.Z, -v.W);

    public static bool operator ==(Vector4Int a, Vector4Int b) => a.Equals(b);
    public static bool operator !=(Vector4Int a, Vector4Int b) => !a.Equals(b);

    public bool Equals(Vector4Int other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;
    public override bool Equals(object? obj) => obj is Vector4Int other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override string ToString() => $"Vector4Int({X}, {Y}, {Z}, {W})";

    public static implicit operator Vector4(Vector4Int v) => new(v.X, v.Y, v.Z, v.W);
    public static explicit operator Vector4Int(Vector2 v) => new((int)v.X, (int)v.Y, 0, 0);
    public static explicit operator Vector4Int(Vector2Int v) => new(v.X, v.Y, 0, 0);
    public static explicit operator Vector4Int(Vector3 v) => new((int)v.X, (int)v.Y, (int)v.Z, 0);
    public static explicit operator Vector4Int(Vector3Int v) => new(v.X, v.Y, v.Z, 0);
    public static explicit operator Vector4Int(Vector4 v) => new((int)v.X, (int)v.Y, (int)v.Z, (int)v.W);
}
