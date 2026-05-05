using System;
using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeDeque<T> : IDisposable where T : unmanaged
    {
        private T* buffer;
        private int capacity;
        private int count;
        private int head;
        private int tail;
        private readonly bool pooled;
        private bool disposed;

        public int Count { get { ThrowIfDisposed(); return count; } }
        public int Capacity { get { ThrowIfDisposed(); return capacity; } }
        public bool IsCreated => !disposed && buffer != null;

        ~NativeDeque()
        {
            Dispose(false);
        }

        public NativeDeque(int initialCapacity, bool usePool = false)
        {
            if (initialCapacity < 0)
                throw new ArgumentOutOfRangeException(nameof(initialCapacity));

            if (initialCapacity == 0)
            {
                buffer = null;
                capacity = 0;
                count = 0;
                head = 0;
                tail = 0;
                pooled = usePool;
                return;
            }

            int cap = NativeCollectionUtility.NextPowerOfTwo(initialCapacity);

            buffer = usePool ? NativeMemoryPool.Rent<T>(cap) : (T*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(T), cap));
            capacity = cap;
            count = 0;
            head = 0;
            tail = 0;
            pooled = usePool;
        }

        public void AddFirst(in T value)
        {
            ThrowIfDisposed();
            EnsureCapacity(count + 1);
            head = (head - 1) & (capacity - 1);
            buffer[head] = value;
            count++;
        }

        public void AddLast(in T value)
        {
            ThrowIfDisposed();
            EnsureCapacity(count + 1);
            buffer[tail] = value;
            tail = (tail + 1) & (capacity - 1);
            count++;
        }

        public T RemoveFirst()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Deque is empty.");

            T value = buffer[head];
            head = (head + 1) & (capacity - 1);
            count--;
            return value;
        }

        public T RemoveLast()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Deque is empty.");

            tail = (tail - 1) & (capacity - 1);
            T value = buffer[tail];
            count--;
            return value;
        }

        public bool TryRemoveFirst(out T value)
        {
            ThrowIfDisposed();
            if (count == 0)
            {
                value = default;
                return false;
            }

            value = RemoveFirst();
            return true;
        }

        public bool TryRemoveLast(out T value)
        {
            ThrowIfDisposed();
            if (count == 0)
            {
                value = default;
                return false;
            }

            value = RemoveLast();
            return true;
        }

        public T PeekFirst()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Deque is empty.");

            return buffer[head];
        }

        public T PeekLast()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Deque is empty.");

            int lastIndex = (tail - 1) & (capacity - 1);
            return buffer[lastIndex];
        }

        public void Clear()
        {
            ThrowIfDisposed();
            count = 0;
            head = 0;
            tail = 0;
        }

        public void EnsureCapacity(int minCapacity)
        {
            ThrowIfDisposed();
            if (minCapacity < 0)
                throw new ArgumentOutOfRangeException(nameof(minCapacity));
            if (minCapacity <= capacity)
                return;

            int newCapacity = NativeCollectionUtility.GrowPowerOfTwoCapacity(capacity, minCapacity);

            T* newBuffer = pooled ? NativeMemoryPool.Rent<T>(newCapacity) : (T*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(T), newCapacity));
            for (int i = 0; i < count; i++)
            {
                newBuffer[i] = buffer[(head + i) & (capacity - 1)];
            }

            ReleaseBuffer(buffer, capacity, returnToPool: true);
            buffer = newBuffer;
            capacity = newCapacity;
            head = 0;
            tail = count;
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
            capacity = 0;
            count = 0;
            head = 0;
            tail = 0;
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
                throw new ObjectDisposedException(nameof(NativeDeque<T>));
        }
    }
}
