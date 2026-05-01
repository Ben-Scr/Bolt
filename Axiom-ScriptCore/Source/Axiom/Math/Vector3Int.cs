using System;
using System.Runtime.InteropServices;

namespace Axiom;
[StructLayout(LayoutKind.Explicit, Size = 12)]
public struct Vector3Int : IEquatable<Vector3Int>
{
    [FieldOffset(0)] public int X;
    [FieldOffset(4)] public int Y;
    [FieldOffset(8)] public int Z;

    public static readonly Vector3Int Zero = new(0, 0, 0);
    public static readonly Vector3Int One = new(1, 1, 1);
    public static readonly Vector3Int Right = new(1, 0, 0);
    public static readonly Vector3Int Left = new(-1, 0, 0);
    public static readonly Vector3Int Up = new(0, 1, 0);
    public static readonly Vector3Int Down = new(0, -1, 0);
    public static readonly Vector3Int Forward = new(0, 0, -1);
    public static readonly Vector3Int Back = new(0, 0, 1);

    public Vector3Int(int scalar) { X = scalar; Y = scalar; Z = scalar; }
    public Vector3Int(int x, int y, int z) { X = x; Y = y; Z = z; }
    public Vector3Int(Vector2Int v, int z = 0) { X = v.X; Y = v.Y; Z = z; }

    public Vector2Int XY => new(X, Y);

    public float Length() => Mathf.Sqrt(LengthSquared());
    public int LengthSquared() => X * X + Y * Y + Z * Z;

    public void Clamp(Vector3Int min, Vector3Int max)
    {
        X = Math.Clamp(X, min.X, max.X);
        Y = Math.Clamp(Y, min.Y, max.Y);
        Z = Math.Clamp(Z, min.Z, max.Z);
    }

    public static int Dot(Vector3Int a, Vector3Int b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

    public static Vector3Int Cross(Vector3Int a, Vector3Int b) => new(
        a.Y * b.Z - a.Z * b.Y,
        a.Z * b.X - a.X * b.Z,
        a.X * b.Y - a.Y * b.X
    );

    public static float Distance(Vector3Int a, Vector3Int b) => (a - b).Length();

    public static Vector3Int Min(Vector3Int a, Vector3Int b) => new(
        Math.Min(a.X, b.X),
        Math.Min(a.Y, b.Y),
        Math.Min(a.Z, b.Z));

    public static Vector3Int Max(Vector3Int a, Vector3Int b) => new(
        Math.Max(a.X, b.X),
        Math.Max(a.Y, b.Y),
        Math.Max(a.Z, b.Z));

    public static Vector3Int operator +(Vector3Int a, Vector3Int b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vector3Int operator -(Vector3Int a, Vector3Int b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vector3Int operator *(Vector3Int a, Vector3Int b) => new(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
    public static Vector3Int operator /(Vector3Int a, Vector3Int b) => new(a.X / b.X, a.Y / b.Y, a.Z / b.Z);
    public static Vector3Int operator *(Vector3Int v, int s) => new(v.X * s, v.Y * s, v.Z * s);
    public static Vector3Int operator *(int s, Vector3Int v) => new(s * v.X, s * v.Y, s * v.Z);
    public static Vector3Int operator /(Vector3Int v, int s) => new(v.X / s, v.Y / s, v.Z / s);
    public static Vector3Int operator -(Vector3Int v) => new(-v.X, -v.Y, -v.Z);

    public static bool operator ==(Vector3Int a, Vector3Int b) => a.Equals(b);
    public static bool operator !=(Vector3Int a, Vector3Int b) => !a.Equals(b);

    public bool Equals(Vector3Int other) => X == other.X && Y == other.Y && Z == other.Z;
    public override bool Equals(object? obj) => obj is Vector3Int other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z);
    public override string ToString() => $"Vector3Int({X}, {Y}, {Z})";

    public static implicit operator Vector3(Vector3Int v) => new(v.X, v.Y, v.Z);
    public static implicit operator Vector4(Vector3Int v) => new(v.X, v.Y, v.Z, 0.0f);
    public static explicit operator Vector3Int(Vector2 v) => new((int)v.X, (int)v.Y, 0);
    public static explicit operator Vector3Int(Vector2Int v) => new(v.X, v.Y, 0);
    public static explicit operator Vector3Int(Vector3 v) => new((int)v.X, (int)v.Y, (int)v.Z);
    public static explicit operator Vector3Int(Vector4 v) => new((int)v.X, (int)v.Y, (int)v.Z);
}
