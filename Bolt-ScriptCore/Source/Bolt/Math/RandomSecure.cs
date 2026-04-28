using System;
using System.Collections.Generic;
using System.Security.Cryptography;

namespace Bolt
{
    public sealed class RandomSecure
    {
        private static readonly RandomNumberGenerator randomNumberGenerator = RandomNumberGenerator.Create();

        private static void ValidateRange<T>(T min, T max) where T : IComparable<T>
        {
            if (Comparer<T>.Default.Compare(min, max) >= 0)
                throw new ArgumentOutOfRangeException(nameof(min), $"Next({min},{max}) is wrong, min can't be more or equal to max.");
        }

        public bool NextBool() => NextInt(0, 2) == 0;
        public byte NextByte()
        {
            return (byte)(NextInt() & 0xFF);
        }
        public byte NextByte(byte max)
        {
            if (max <= 0) throw new ArgumentOutOfRangeException(nameof(max));
            return (byte)(NextByte() % max);
        }
        public byte NextByte(byte min, byte max)
        {
            ValidateRange(min, max);
            return (byte)(min + (NextByte() % (max - min)));
        }

        public int NextInt()
        {
            byte[] fourBytes = new byte[4];
            randomNumberGenerator.GetBytes(fourBytes);
            int value = BitConverter.ToInt32(fourBytes, 0) & int.MaxValue;
            return value;
        }
        public int NextInt(int min, int max)
        {
            ValidateRange(min, max);
            return min + (NextInt() % (max - min));
        }
        public int NextInt(int max)
        {
            if (max <= 0) throw new ArgumentOutOfRangeException(nameof(max));
            return NextInt() % max;
        }

        public double NextDouble()
        {
            byte[] eightBytes = new byte[8];
            randomNumberGenerator.GetBytes(eightBytes);
            double value = BitConverter.ToUInt64(eightBytes, 0) / (ulong.MaxValue + 1.0);
            return value;
        }
        public double NextDouble(double max)
        {
            if (max <= 0) throw new ArgumentOutOfRangeException(nameof(max));
            return NextDouble() % max;
        }
        public double NextDouble(double min, double max)
        {
            ValidateRange(min, max);
            return min + (NextDouble() * (max - min));
        }

        public float NextFloat()
        {
            byte[] fourBytes = new byte[4];
            randomNumberGenerator.GetBytes(fourBytes);
            float value = BitConverter.ToUInt32(fourBytes, 0) / (uint.MaxValue + 1.0f);
            return value;
        }
        public float NextFloat(float max)
        {
            if (max <= 0) throw new ArgumentOutOfRangeException(nameof(max));
            return NextFloat() % max;
        }
        public float NextFloat(float min, float max)
        {
            ValidateRange(min, max);
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

        public void GenerateBytes(byte[] bytes) => randomNumberGenerator.GetBytes(bytes);
    }
}
