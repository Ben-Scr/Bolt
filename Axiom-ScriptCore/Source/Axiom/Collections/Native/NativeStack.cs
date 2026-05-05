using System;
using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeStack<T> : IDisposable where T : unmanaged
    {
        private T* buffer;
        private int count;
        private int capacity;
        private readonly bool pooled;
        private bool disposed;

        public int Count { get { ThrowIfDisposed(); return count; } }
        public int Capacity { get { ThrowIfDisposed(); return capacity; } }
        public bool IsCreated => !disposed && buffer != null;

        ~NativeStack()
        {
            Dispose(false);
        }

        public NativeStack(int initialCapacity, bool usePool = false)
        {
            if (initialCapacity < 0)
                throw new ArgumentOutOfRangeException(nameof(initialCapacity));

            if (initialCapacity == 0)
            {
                buffer = null;
                count = 0;
                capacity = 0;
                pooled = usePool;
                return;
            }

            buffer = usePool ? NativeMemoryPool.Rent<T>(initialCapacity) : (T*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(T), initialCapacity));
            capacity = initialCapacity;
            count = 0;
            pooled = usePool;
        }

        public void Push(in T value)
        {
            ThrowIfDisposed();
            if (count == capacity)
            {
                int newCapacity = NativeCollectionUtility.GrowCapacity(capacity, count + 1);
                Reserve(newCapacity);
            }

            buffer[count++] = value;
        }

        public T Pop()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Stack is empty.");

            return buffer[--count];
        }

        public bool TryPop(out T value)
        {
            ThrowIfDisposed();
            if (count == 0)
            {
                value = default;
                return false;
            }

            count--;
            value = buffer[count];
            return true;
        }

        public T Peek()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Stack is empty.");

            return buffer[count - 1];
        }

        public void Clear()
        {
            ThrowIfDisposed();
            count = 0;
        }

        public void Reserve(int minCapacity)
        {
            ThrowIfDisposed();
            if (minCapacity < 0)
                throw new ArgumentOutOfRangeException(nameof(minCapacity));
            if (minCapacity <= capacity)
                return;

            IntPtr totalSize = NativeCollectionUtility.CheckedByteSize(sizeof(T), minCapacity);
            T* newBuffer = pooled ? NativeMemoryPool.Rent<T>(minCapacity) : (T*)Marshal.AllocHGlobal(totalSize);

            if (buffer != null)
            {
                if (count > 0)
                    Buffer.MemoryCopy(buffer, newBuffer, (long)totalSize, (long)sizeof(T) * count);

                ReleaseBuffer(buffer, capacity, returnToPool: true);
            }

            buffer = newBuffer;
            capacity = minCapacity;
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        private void Dispose(bool disposing)
        {
            if (disposed)
                return;

            ReleaseBuffer(buffer, capacity, returnToPool: disposing);
            buffer = null;
            count = 0;
            capacity = 0;
            disposed = true;
        }

        private void ReleaseBuffer(T* ptr, int capacity, bool returnToPool)
        {
            if (ptr == null)
                return;

            if (pooled)
                NativeMemoryPool.Release(ptr, capacity, returnToPool, clear: false);
            else
                Marshal.FreeHGlobal((IntPtr)ptr);
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(NativeStack<T>));
        }
    }
}
