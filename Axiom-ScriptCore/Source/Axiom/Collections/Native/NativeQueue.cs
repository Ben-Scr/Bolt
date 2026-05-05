using System;
using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeQueue<T> : IDisposable where T : unmanaged
    {
        private T* buffer;
        private int capacity;
        private int head;
        private int tail;
        private int count;
        private bool disposed;

        public int Count { get { ThrowIfDisposed(); return count; } }
        public int Capacity { get { ThrowIfDisposed(); return capacity; } }
        public bool IsCreated => !disposed && buffer != null;
        public bool IsEmpty { get { ThrowIfDisposed(); return count == 0; } }

        ~NativeQueue()
        {
            Dispose(false);
        }

        public NativeQueue(int initialCapacity)
        {
            if (initialCapacity < 0)
                throw new ArgumentOutOfRangeException(nameof(initialCapacity));

            if (initialCapacity == 0)
            {
                buffer = null;
                capacity = 0;
                head = 0;
                tail = 0;
                count = 0;
                return;
            }

            buffer = (T*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(T), initialCapacity));
            capacity = initialCapacity;
            head = 0;
            tail = 0;
            count = 0;
        }

        public void Enqueue(T value)
        {
            ThrowIfDisposed();
            if (count == capacity)
            {
                int newCap = NativeCollectionUtility.GrowCapacity(capacity, count + 1);
                EnsureCapacity(newCap);
            }

            buffer[tail] = value;
            tail = (tail + 1) % capacity;
            count++;
        }

        public T Dequeue()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Queue is empty.");

            T value = buffer[head];
            head = (head + 1) % capacity;
            count--;
            return value;
        }

        public bool TryDequeue(out T value)
        {
            ThrowIfDisposed();
            if (count == 0)
            {
                value = default;
                return false;
            }

            value = buffer[head];
            head = (head + 1) % capacity;
            count--;
            return true;
        }

        public T Peek()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Queue is empty.");

            return buffer[head];
        }

        public bool TryPeek(out T value)
        {
            ThrowIfDisposed();
            if (count == 0)
            {
                value = default;
                return false;
            }

            value = buffer[head];
            return true;
        }

        public void EnsureCapacity(int min)
        {
            ThrowIfDisposed();
            if (min <= capacity)
                return;

            T* newBuffer = (T*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(T), min));

            if (buffer != null)
            {
                if (count > 0)
                {
                    for (int i = 0; i < count; i++)
                    {
                        int index = (head + i) % capacity;
                        newBuffer[i] = buffer[index];
                    }
                }

                Marshal.FreeHGlobal((IntPtr)buffer);
            }

            buffer = newBuffer;
            capacity = min;
            head = 0;
            tail = count;
        }

        public void Clear()
        {
            ThrowIfDisposed();
            head = 0;
            tail = 0;
            count = 0;
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

            if (buffer != null)
            {
                Marshal.FreeHGlobal((IntPtr)buffer);
                buffer = null;
            }

            capacity = 0;
            head = 0;
            tail = 0;
            count = 0;
            disposed = true;
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(NativeQueue<T>));
        }
    }
}
