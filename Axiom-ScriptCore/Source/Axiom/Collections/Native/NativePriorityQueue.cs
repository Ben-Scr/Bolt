using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativePriorityQueue<T> : IDisposable where T : unmanaged
    {
        private T* heap;
        private int count;
        private int capacity;
        private readonly bool pooled;
        private readonly IComparer<T> comparer;
        private bool disposed;

        public int Count { get { ThrowIfDisposed(); return count; } }
        public int Capacity { get { ThrowIfDisposed(); return capacity; } }
        public bool IsCreated => !disposed && heap != null;

        ~NativePriorityQueue()
        {
            Dispose(false);
        }

        public NativePriorityQueue(int initialCapacity, IComparer<T>? comparer = null, bool usePool = false)
        {
            if (initialCapacity < 0)
                throw new ArgumentOutOfRangeException(nameof(initialCapacity));

            if (initialCapacity == 0)
            {
                heap = null;
                count = 0;
                capacity = 0;
                pooled = usePool;
                this.comparer = comparer ?? Comparer<T>.Default;
                return;
            }

            heap = usePool ? NativeMemoryPool.Rent<T>(initialCapacity) : (T*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(T), initialCapacity));
            count = 0;
            capacity = initialCapacity;
            pooled = usePool;
            this.comparer = comparer ?? Comparer<T>.Default;
        }

        public void Enqueue(in T item)
        {
            ThrowIfDisposed();
            if (count == capacity)
            {
                int newCapacity = NativeCollectionUtility.GrowCapacity(capacity, count + 1);
                Reserve(newCapacity);
            }

            heap[count] = item;
            SiftUp(count);
            count++;
        }

        public T Dequeue()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Priority queue is empty.");

            T root = heap[0];
            count--;
            if (count > 0)
            {
                heap[0] = heap[count];
                SiftDown(0);
            }

            return root;
        }

        public bool TryDequeue(out T value)
        {
            ThrowIfDisposed();
            if (count == 0)
            {
                value = default;
                return false;
            }

            value = Dequeue();
            return true;
        }

        public T Peek()
        {
            ThrowIfDisposed();
            if (count == 0)
                throw new InvalidOperationException("Priority queue is empty.");

            return heap[0];
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
            T* newHeap = pooled ? NativeMemoryPool.Rent<T>(minCapacity) : (T*)Marshal.AllocHGlobal(totalSize);
            if (heap != null)
            {
                if (count > 0)
                    Buffer.MemoryCopy(heap, newHeap, (long)totalSize, (long)sizeof(T) * count);

                ReleaseBuffer(heap, capacity, returnToPool: true);
            }

            heap = newHeap;
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

            ReleaseBuffer(heap, capacity, returnToPool: disposing);
            heap = null;
            count = 0;
            capacity = 0;
            disposed = true;
        }

        private void SiftUp(int index)
        {
            while (index > 0)
            {
                int parent = (index - 1) >> 1;
                if (comparer.Compare(heap[index], heap[parent]) >= 0)
                    break;

                Swap(index, parent);
                index = parent;
            }
        }

        private void SiftDown(int index)
        {
            while (true)
            {
                int left = (index << 1) + 1;
                int right = left + 1;
                int smallest = index;

                if (left < count && comparer.Compare(heap[left], heap[smallest]) < 0)
                    smallest = left;
                if (right < count && comparer.Compare(heap[right], heap[smallest]) < 0)
                    smallest = right;

                if (smallest == index)
                    break;

                Swap(index, smallest);
                index = smallest;
            }
        }

        private void Swap(int a, int b)
        {
            T temp = heap[a];
            heap[a] = heap[b];
            heap[b] = temp;
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
                throw new ObjectDisposedException(nameof(NativePriorityQueue<T>));
        }
    }
}
