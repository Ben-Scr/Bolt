using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeList<T> : IDisposable where T : unmanaged
    {
        private T* buffer;
        private int length;
        private int capacity;
        private int version;
        private bool disposed;

        public int Length { get { ThrowIfDisposed(); return length; } }
        public int Capacity { get { ThrowIfDisposed(); return capacity; } }
        public bool IsCreated => !disposed && buffer != null;

        ~NativeList()
        {
            Dispose(false);
        }

        public NativeList(int initialCapacity)
        {
            if (initialCapacity < 0)
                throw new ArgumentOutOfRangeException(nameof(initialCapacity));

            if (initialCapacity == 0)
            {
                buffer = null;
                length = 0;
                capacity = 0;
                return;
            }

            buffer = (T*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(T), initialCapacity));
            length = 0;
            capacity = initialCapacity;
        }

        public struct Enumerator
        {
            private readonly NativeList<T> owner;
            private readonly int capturedVersion;
            private int index;

            internal Enumerator(NativeList<T> owner)
            {
                this.owner = owner;
                capturedVersion = owner.version;
                index = -1;
            }

            public T Current
            {
                get
                {
                    ValidateVersion();
                    if ((uint)index >= (uint)owner.length)
                        throw new InvalidOperationException("Enumeration has not started or has already finished.");
                    return owner.buffer[index];
                }
            }

            public bool MoveNext()
            {
                ValidateVersion();
                index++;
                return index < owner.length;
            }

            private void ValidateVersion()
            {
                owner.ThrowIfDisposed();
                if (capturedVersion != owner.version)
                    throw new InvalidOperationException("Collection was modified during enumeration.");
            }
        }

        public Enumerator GetEnumerator()
        {
            ThrowIfDisposed();
            return new Enumerator(this);
        }



        public ref T this[int index]
        {
            get
            {
                ThrowIfDisposed();
                if ((uint)index >= (uint)length)
                    throw new ArgumentOutOfRangeException(nameof(index));
                return ref buffer[index];
            }
        }

        public void Add(in T value)
        {
            ThrowIfDisposed();
            if (length == capacity)
            {
                int newCapacity = NativeCollectionUtility.GrowCapacity(capacity, length + 1);
                Reserve(newCapacity);
            }

            buffer[length++] = value;
            version++;
        }

        public bool Remove(in T value)
        {
            ThrowIfDisposed();
            int index = IndexOf(value);
            if (index < 0)
                return false;

            RemoveAt(index);
            return true;
        }

        public void RemoveAt(int index)
        {
            ThrowIfDisposed();
            if ((uint)index >= (uint)length)
                throw new ArgumentOutOfRangeException(nameof(index));

            for (int i = index; i < length - 1; i++)
            {
                buffer[i] = buffer[i + 1];
            }

            length--;
            version++;
        }

        public void Reserve(int count)
        {
            ThrowIfDisposed();
            if (count < 0)
                throw new ArgumentOutOfRangeException(nameof(count));
            if (count <= capacity)
                return;

            int sizeOfT = sizeof(T);
            IntPtr totalSize = NativeCollectionUtility.CheckedByteSize(sizeOfT, count);
            T* newBuffer = (T*)Marshal.AllocHGlobal(totalSize);

            if (buffer != null)
            {
                if (length > 0)
                {
                    long bytesToCopy = (long)sizeOfT * length;
                    Buffer.MemoryCopy(buffer, newBuffer, (long)totalSize, bytesToCopy);
                }

                Marshal.FreeHGlobal((IntPtr)buffer);
            }

            buffer = newBuffer;
            capacity = count;
            version++;
        }

        public int IndexOf(in T value)
        {
            ThrowIfDisposed();
            for (int i = 0; i < length; i++)
            {
                if (EqualityComparer<T>.Default.Equals(buffer[i], value))
                    return i;
            }
            return -1;
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

            length = 0;
            capacity = 0;
            version++;
            disposed = true;
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(NativeList<T>));
        }
    }
}
