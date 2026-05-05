using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeBitArray : IDisposable
    {
        private byte* buffer;
        private int bitLength;
        private int byteLength;
        private bool disposed;

        public int Length
        {
            get
            {
                ThrowIfDisposed();
                return bitLength;
            }
        }

        public bool IsCreated => !disposed && buffer != null;

        ~NativeBitArray()
        {
            Dispose(false);
        }


        public NativeBitArray(int bitLength)
        {
            if (bitLength < 0)
                throw new ArgumentOutOfRangeException(nameof(bitLength));

            if (bitLength == 0)
            {
                buffer = null;
                this.bitLength = 0;
                byteLength = 0;
                return;
            }

            this.bitLength = bitLength;
            byteLength = (bitLength + 7) / 8;

            buffer = (byte*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(byte), byteLength));

            for (int i = 0; i < byteLength; i++)
                buffer[i] = 0;
        }

        public bool this[int index]
        {
            get => Get(index);
            set => Set(index, value);
        }

        public bool Get(int index)
        {
            ThrowIfDisposed();
            if ((uint)index >= (uint)bitLength)
                throw new ArgumentOutOfRangeException(nameof(index));

            int byteIndex = index >> 3;
            int bitOffset = index & 7;
            byte mask = (byte)(1 << bitOffset);

            return (buffer[byteIndex] & mask) != 0;
        }

        public void Set(int index, bool value)
        {
            ThrowIfDisposed();
            if ((uint)index >= (uint)bitLength)
                throw new ArgumentOutOfRangeException(nameof(index));

            int byteIndex = index >> 3;
            int bitOffset = index & 7;
            byte mask = (byte)(1 << bitOffset);

            if (value)
            {
                buffer[byteIndex] |= mask;
            }
            else
            {
                buffer[byteIndex] &= (byte)~mask;
            }
        }

        public void SetBit(int index) => Set(index, true);
        public void ClearBit(int index) => Set(index, false);

        public void Clear()
        {
            ThrowIfDisposed();
            if (buffer == null)
                return;

            for (int i = 0; i < byteLength; i++)
                buffer[i] = 0;
        }

        public void SetAll(bool value)
        {
            ThrowIfDisposed();
            if (buffer == null || byteLength == 0)
                return;

            byte fill = value ? (byte)0xFF : (byte)0x00;

            for (int i = 0; i < byteLength; i++)
                buffer[i] = fill;

            if (value)
            {
                int validBitsInLastByte = bitLength & 7;
                if (validBitsInLastByte != 0)
                {
                    int lastByteIndex = byteLength - 1;
                    byte mask = (byte)((1 << validBitsInLastByte) - 1);
                    buffer[lastByteIndex] &= mask;
                }
            }
        }

        public byte* GetUnsafePtr()
        {
            ThrowIfDisposed();
            return buffer;
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

            bitLength = 0;
            byteLength = 0;
            disposed = true;
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(NativeBitArray));
        }
    }
}
