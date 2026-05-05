namespace Axiom.Collections.Native
{
    internal static class NativeCollectionUtility
    {
        public const int MaxPowerOfTwoCapacity = 1 << 30;

        public static int GrowCapacity(int currentCapacity, int minRequired)
        {
            if (minRequired < 0)
                throw new ArgumentOutOfRangeException(nameof(minRequired));

            int newCapacity = currentCapacity <= 0 ? 4 : currentCapacity;
            while (newCapacity < minRequired)
            {
                if (newCapacity > int.MaxValue / 2)
                {
                    newCapacity = int.MaxValue;
                    break;
                }

                newCapacity *= 2;
            }

            if (newCapacity < minRequired)
                throw new OutOfMemoryException("Collection has reached maximum capacity.");

            return newCapacity;
        }

        public static int GrowPowerOfTwoCapacity(int currentCapacity, int minRequired)
        {
            if (minRequired < 0)
                throw new ArgumentOutOfRangeException(nameof(minRequired));
            if (minRequired > MaxPowerOfTwoCapacity)
                throw new OutOfMemoryException("Collection has reached maximum capacity.");

            int newCapacity = currentCapacity <= 0 ? 4 : currentCapacity;
            while (newCapacity < minRequired)
            {
                if (newCapacity > MaxPowerOfTwoCapacity / 2)
                    return MaxPowerOfTwoCapacity;

                newCapacity <<= 1;
            }

            return newCapacity;
        }

        public static int NextPowerOfTwo(int value)
        {
            if (value < 1)
                return 1;
            if (value > MaxPowerOfTwoCapacity)
                throw new OutOfMemoryException("Requested capacity is too large.");

            value--;
            value |= value >> 1;
            value |= value >> 2;
            value |= value >> 4;
            value |= value >> 8;
            value |= value >> 16;
            value++;
            return value;
        }

        public static IntPtr CheckedByteSize(int elementSize, int count)
        {
            if (elementSize < 0)
                throw new ArgumentOutOfRangeException(nameof(elementSize));
            if (count < 0)
                throw new ArgumentOutOfRangeException(nameof(count));

            long totalSize = (long)elementSize * count;
            if (totalSize > int.MaxValue)
                throw new OutOfMemoryException();

            return (IntPtr)totalSize;
        }
    }
}
