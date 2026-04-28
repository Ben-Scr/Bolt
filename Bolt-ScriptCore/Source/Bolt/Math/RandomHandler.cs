using Bolt.Cryptography;
using System;

namespace Bolt
{
    public static class RandomHandler
    {
        private static Random random = new Random();
        private static RandomSecure randomSecure = new RandomSecure();

        public static void SetSeed(ulong seed) => random.SetSeed(seed);
        public static void RemoveSeed() => random.RemoveSeed();
        public static bool NextBool() => random.NextBool();
        public static byte NextByte() => random.NextByte();
        public static int NextInt(int min, int max) => random.NextInt(min, max);
        public static double NextDouble(double min, double max) => random.NextDouble(min, max);
        public static string NextString(int length = 10, string? charset = null) => random.NextString(length, charset);

        public static T Next<T>(T min, T max) where T : IComparable<T> => random.Next(min, max);
        public static T Next<T>() where T : IComparable<T> => random.Next<T>();

        public static class Secure
        {
            public static bool NextBool() => randomSecure.NextBool();
            public static byte NextByte() => randomSecure.NextByte();
            public static int NextInt(int min, int max) => randomSecure.NextInt(min, max);
            public static double NextDouble(double min, double max) => randomSecure.NextDouble(min, max);
            public static string NextString() => randomSecure.NextString();
            public static void GenerateBytes(byte[] bytes) => randomSecure.GenerateBytes(bytes);
            public static T Next<T>(T min, T max) where T : IComparable<T> => randomSecure.Next(min, max);
            public static T Next<T>() where T : IComparable<T> => randomSecure.Next<T>();
        }
    }
}
