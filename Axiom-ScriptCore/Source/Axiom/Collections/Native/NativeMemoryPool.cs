using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public static class NativeMemoryPool
    {
        private static readonly Dictionary<int, Stack<IntPtr>> Pools = new();
        private static readonly HashSet<IntPtr> PooledPointers = new();
        private static readonly object Sync = new();

        public static unsafe T* Rent<T>(int count, bool zeroFill = false) where T : unmanaged
        {
            if (count < 0)
                throw new ArgumentOutOfRangeException(nameof(count));

            if (count == 0)
                return null;

            int elementSize = sizeof(T);
            long totalBytes = (long)elementSize * count;
            if (totalBytes > int.MaxValue)
                throw new OutOfMemoryException();

            IntPtr ptr = IntPtr.Zero;
            int key = (int)totalBytes;

            lock (Sync)
            {
                if (Pools.TryGetValue(key, out var stack) && stack.Count > 0)
                {
                    ptr = stack.Pop();
                    PooledPointers.Remove(ptr);
                }
            }

            if (ptr == IntPtr.Zero)
            {
                ptr = Marshal.AllocHGlobal((IntPtr)totalBytes);
            }

            if (zeroFill)
            {
                byte* bytes = (byte*)ptr;
                for (long i = 0; i < totalBytes; i++)
                    bytes[i] = 0;
            }

            return (T*)ptr;
        }

        public static unsafe void Return<T>(T* pointer, int count, bool clear = false) where T : unmanaged
        {
            if (pointer == null)
                return;
            if (count < 0)
                throw new ArgumentOutOfRangeException(nameof(count));
            if (count == 0)
                throw new ArgumentOutOfRangeException(nameof(count), "Cannot return a non-null pointer with count == 0.");

            int elementSize = sizeof(T);
            long totalBytes = (long)elementSize * count;
            if (totalBytes > int.MaxValue)
            {
                Marshal.FreeHGlobal((IntPtr)pointer);
                return;
            }

            if (clear)
            {
                byte* bytes = (byte*)pointer;
                for (long i = 0; i < totalBytes; i++)
                    bytes[i] = 0;
            }

            int key = (int)totalBytes;
            IntPtr nativePointer = (IntPtr)pointer;
            lock (Sync)
            {
                if (!PooledPointers.Add(nativePointer))
                    throw new InvalidOperationException("Pointer has already been returned to the native memory pool.");

                if (!Pools.TryGetValue(key, out var stack))
                {
                    stack = new Stack<IntPtr>();
                    Pools[key] = stack;
                }
                stack.Push(nativePointer);
            }
        }

        public static unsafe void Release<T>(T* pointer, int count, bool returnToPool, bool clear = false) where T : unmanaged
        {
            if (pointer == null)
                return;

            if (returnToPool)
            {
                Return(pointer, count, clear);
                return;
            }

            if (count < 0)
                throw new ArgumentOutOfRangeException(nameof(count));

            if (clear && count > 0)
            {
                long totalBytes = (long)sizeof(T) * count;
                byte* bytes = (byte*)pointer;
                for (long i = 0; i < totalBytes; i++)
                    bytes[i] = 0;
            }

            Marshal.FreeHGlobal((IntPtr)pointer);
        }

        public static void Clear()
        {
            lock (Sync)
            {
                foreach (var stack in Pools.Values)
                {
                    while (stack.Count > 0)
                    {
                        Marshal.FreeHGlobal(stack.Pop());
                    }
                }

                Pools.Clear();
                PooledPointers.Clear();
            }
        }
    }
}
