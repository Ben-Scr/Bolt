using System.Diagnostics;
using System;

namespace Bolt;
public sealed class Random
{
    private ulong state;

    public Random(ulong seed)
    {
        SetSeed(seed);
    }

    public Random()
    {
        RemoveSeed();
    }

    private ulong NextState()
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 2685821657736338717UL;
    }

    public void SetSeed(ulong seed)
    {
        state = seed;
    }
    public void RemoveSeed()
    {
        ulong t = (ulong)DateTime.Now.Ticks;
        ulong g = (ulong)Guid.NewGuid().GetHashCode();
        ulong s = (ulong)Stopwatch.GetTimestamp();
        state = t ^ g ^ s;
    }

    public ulong GetSeed() => state;


    public bool NextBool() => NextInt(0, 2) == 0;

    public byte NextByte()
    {
        return (byte)(NextState() & 0xFF);
    }
    public byte NextByte(byte max)
    {
        if (max <= 0) throw new ArgumentOutOfRangeException(nameof(max));
        return (byte)(NextByte() % max);
    }
    public byte NextByte(byte min, byte max)
    {
        if (min >= max) throw new ArgumentOutOfRangeException($"Next({min},{max}) is wrong, min can't be more or equal to max.");
        return (byte)(min + (NextByte() % (max - min)));
    }

    public int NextInt()
    {
        return (int)(NextState() & 0x7FFFFFFF);
    }
    public int NextInt(int max)
    {
        if (max <= 0) throw new ArgumentOutOfRangeException(nameof(max));
        return NextInt() % max;
    }
    public int NextInt(int min, int max)
    {
        if (min >= max) throw new ArgumentOutOfRangeException($"Next({min},{max}) is wrong, min can't be more or equal to max.");
        return min + (NextInt() % (max - min));
    }

    public double NextDouble()
    {
        return (double)NextState() / ulong.MaxValue;
    }
    public double NextDouble(double max)
    {
        if (max <= 0) throw new ArgumentOutOfRangeException(nameof(max));
        return NextDouble() % max;
    }
    public double NextDouble(double min, double max)
    {
        return min + (NextDouble() * (max - min));
    }

    public float NextFloat()
    {
        return (float)NextDouble();
    }
    public float NextFloat(float max)
    {
        if (max <= 0) throw new ArgumentOutOfRangeException(nameof(max));
        return NextFloat() % max;
    }
    public float NextFloat(float min, float max)
    {
        return min + (NextFloat() * (max - min));
    }

    public string NextString(int length = 10, string? charset = null)
    {
        charset ??= TextUtils.Letters;
        int charsetLength = charset.Length;

        string code = string.Empty;

        for (int i = 0; i < length; i++)
            code += charset[NextInt(charsetLength)];

        return code;
    }

    public T Next<T>() where T : IComparable<T>
    {
        if (typeof(T) == typeof(int))
        {
            int result = NextInt();
            return (T)(object)result;
        }

        if (typeof(T) == typeof(float))
        {
            float result = NextFloat();
            return (T)(object)result;
        }

        if (typeof(T) == typeof(double))
        {
            double result = NextDouble();
            return (T)(object)result;
        }

        if (typeof(T) == typeof(byte))
        {
            byte result = NextByte();
            return (T)(object)result;
        }

        if (typeof(T) == typeof(bool))
        {
            bool result = NextBool();
            return (T)(object)result;
        }

        if (typeof(T) == typeof(string))
        {
            string result = NextString();
            return (T)(object)result;
        }

        throw new NotSupportedException($"Type '{typeof(T)}' is not supported.");
    }

    public T Next<T>(T min, T max) where T : IComparable<T>
    {
        if (typeof(T) == typeof(int))
        {
            int result = NextInt(Convert.ToInt32(min), Convert.ToInt32(max));
            return (T)(object)result;
        }

        if (typeof(T) == typeof(float))
        {
            float result = NextFloat(Convert.ToSingle(min), Convert.ToSingle(max));
            return (T)(object)result;
        }

        if (typeof(T) == typeof(double))
        {
            double result = NextDouble(Convert.ToDouble(min), Convert.ToDouble(max));
            return (T)(object)result;
        }

        if (typeof(T) == typeof(byte))
        {
            byte result = NextByte(Convert.ToByte(min), Convert.ToByte(max));
            return (T)(object)result;
        }

        throw new NotSupportedException($"Type '{typeof(T)}' is not supported.");
    }
}
