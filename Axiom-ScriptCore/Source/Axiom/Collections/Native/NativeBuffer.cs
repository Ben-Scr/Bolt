using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeBuffer : IDisposable
    {
        private byte* buffer;
        private int length;
        private bool disposed;

        public int Length
        {
            get
            {
                ThrowIfDisposed();
                return length;
            }
        }

        public bool IsCreated => !disposed && (buffer != null || length == 0);

        ~NativeBuffer()
        {
            Dispose(false);
        }

        public NativeBuffer(int length)
        {
            if (length < 0)
                throw new ArgumentOutOfRangeException(nameof(length));

            if (length == 0)
            {
                buffer = null;
                this.length = 0;
                return;
            }

            buffer = (byte*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(byte), length));
            this.length = length;

            Fill(0);
        }
        public ref byte this[int index]
        {
            get
            {
                ThrowIfDisposed();
                if ((uint)index >= (uint)length)
                    throw new ArgumentOutOfRangeException(nameof(index));
                return ref buffer[index];
            }
        }

        public byte* GetUnsafePtr()
        {
            ThrowIfDisposed();
            return buffer;
        }

        public void Fill(byte value)
        {
            ThrowIfDisposed();
            if (length == 0)
                return;
            if (buffer == null)
                throw new InvalidOperationException("NativeBuffer is not created.");


            int i = 0;

            int alignBytes = ((int)((long)buffer & 7) == 0) ? 0 : (8 - (int)((long)buffer & 7));
            alignBytes = Math.Min(alignBytes, length);

            for (; i < alignBytes; i++)
            {
                buffer[i] = value;
            }

            int remaining = length - i;
            if (remaining >= 8)
            {
                ulong pattern = 0;
                for (int b = 0; b < 8; b++)
                {
                    pattern |= (ulong)value << (b * 8);
                }

                ulong* p64 = (ulong*)(buffer + i);
                int count64 = remaining / 8;

                for (int j = 0; j < count64; j++)
                {
                    p64[j] = pattern;
                }

                i += count64 * 8;
            }

            for (; i < length; i++)
            {
                buffer[i] = value;
            }
        }


        public void CopyFrom(byte[] source)
        {
            ThrowIfDisposed();
            if (source == null)
                throw new ArgumentNullException(nameof(source));
            if (source.Length != length)
                throw new ArgumentException("Source length must match buffer length.", nameof(source));
            if (length == 0)
                return;

            fixed (byte* srcPtr = source)
            {
                Buffer.MemoryCopy(srcPtr, buffer, length, length);
            }
        }

        public void CopyTo(byte[] destination)
        {
            ThrowIfDisposed();
            if (destination == null)
                throw new ArgumentNullException(nameof(destination));
            if (destination.Length != length)
                throw new ArgumentException("Destination length must match buffer length.", nameof(destination));
            if (length == 0)
                return;

            fixed (byte* dstPtr = destination)
            {
                Buffer.MemoryCopy(buffer, dstPtr, length, length);
            }
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
            disposed = true;
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(NativeBuffer));
        }
    }
}
