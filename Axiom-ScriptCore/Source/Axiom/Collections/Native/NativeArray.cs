using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeArray<T> : IDisposable where T : unmanaged
    {
        private T* buffer;
        private int length;
        private int version;
        private int disposeState;

        public int Length
        {
            get
            {
                ThrowIfDisposed();
                return length;
            }
        }

        public bool IsCreated => Volatile.Read(ref disposeState) == 0 && buffer != null;

        ~NativeArray()
        {
            Dispose(false);
        }

        public NativeArray(int length)
        {
            if (length < 0)
                throw new ArgumentOutOfRangeException(nameof(length));

            if (length == 0)
            {
                buffer = null;
                this.length = 0;
                return;
            }

            IntPtr totalSize = NativeCollectionUtility.CheckedByteSize(sizeof(T), length);
            buffer = (T*)Marshal.AllocHGlobal(totalSize);
            this.length = length;

            for (int i = 0; i < length; i++)
            {
                buffer[i] = default;
            }
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

        public T* GetUnsafePtr()
        {
            ThrowIfDisposed();
            return buffer;
        }

        public void CopyFrom(T[] source)
        {
            ThrowIfDisposed();
            if (source == null)
                throw new ArgumentNullException(nameof(source));
            if (source.Length != length)
                throw new ArgumentException("Source length must match NativeArray length.", nameof(source));
            if (length == 0)
                return;

            fixed (T* srcPtr = source)
            {
                long bytes = (long)sizeof(T) * length;
                Buffer.MemoryCopy(srcPtr, buffer, bytes, bytes);
            }
        }
        public void CopyTo(T[] destination)
        {
            ThrowIfDisposed();
            if (destination == null)
                throw new ArgumentNullException(nameof(destination));
            if (destination.Length != length)
                throw new ArgumentException("Destination length must match NativeArray length.", nameof(destination));
            if (length == 0)
                return;

            fixed (T* dstPtr = destination)
            {
                long bytes = (long)sizeof(T) * length;
                Buffer.MemoryCopy(buffer, dstPtr, bytes, bytes);
            }
        }

        public void Fill(in T value)
        {
            ThrowIfDisposed();
            if (length == 0)
                return;
            if (buffer == null)
                throw new InvalidOperationException("NativeArray is not created.");

            buffer[0] = value;

            int filled = 1;
            int sizeOfT = sizeof(T);

            while (filled < length)
            {
                int toCopy = Math.Min(filled, length - filled);
                long bytesToCopy = (long)sizeOfT * toCopy;
                long destSize = (long)sizeOfT * (length - filled);

                Buffer.MemoryCopy(
                    buffer,
                    buffer + filled,
                    destSize,
                    bytesToCopy);

                filled += toCopy;
            }
        }

        public Enumerator GetEnumerator()
        {
            ThrowIfDisposed();
            return new Enumerator(this);
        }

        public struct Enumerator
        {
            private readonly NativeArray<T> _owner;
            private readonly int _capturedVersion;
            private int _index;

            internal Enumerator(NativeArray<T> array)
            {
                _owner = array;
                _capturedVersion = array.version;
                _index = -1;
            }

            public T Current
            {
                get
                {
                    ValidateVersion();
                    if ((uint)_index >= (uint)_owner.length)
                        throw new InvalidOperationException("Enumeration has not started or has already finished.");
                    return _owner.buffer[_index];
                }
            }

            public bool MoveNext()
            {
                ValidateVersion();
                _index++;
                return _index < _owner.length;
            }

            private void ValidateVersion()
            {
                _owner.ThrowIfDisposed();
                if (_capturedVersion != _owner.version)
                    throw new InvalidOperationException("Collection was modified during enumeration.");
            }
        }
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        private void Dispose(bool disposing)
        {
            if (Interlocked.Exchange(ref disposeState, 1) != 0)
                return;

            if (buffer != null)
            {
                Marshal.FreeHGlobal((IntPtr)buffer);
                buffer = null;
            }

            length = 0;
            version++;
        }

        private void ThrowIfDisposed()
        {
            if (Volatile.Read(ref disposeState) != 0)
                throw new ObjectDisposedException(nameof(NativeArray<T>));
        }
    }
}
