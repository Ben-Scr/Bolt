using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeString : IDisposable
    {
        private char* pointer;
        private nuint length;
        private bool disposed;

        ~NativeString()
        {
            Dispose(false);
        }

        public NativeString(ReadOnlySpan<char> value)
        {
            length = (nuint)value.Length;
            pointer = (char*)NativeMemory.Alloc(length + 1, (nuint)sizeof(char));
            value.CopyTo(new Span<char>(pointer, value.Length));
            pointer[length] = '\0';
        }

        public static NativeString? FromString(string? s)
            => s is null ? null : new NativeString(s.AsSpan());

        public bool IsNull
        {
            get
            {
                ThrowIfDisposed();
                return pointer is null;
            }
        }

        public int Length
        {
            get
            {
                ThrowIfDisposed();
                return checked((int)length);
            }
        }

        public char* Ptr
        {
            get
            {
                ThrowIfDisposed();
                return pointer;
            }
        }

        public ReadOnlySpan<char> AsSpan()
        {
            ThrowIfDisposed();
            return pointer == null ? ReadOnlySpan<char>.Empty : new ReadOnlySpan<char>(pointer, Length);
        }

        public override string ToString() => new string(AsSpan());

        public static implicit operator string?(NativeString? nativeString) => nativeString?.ToString();
        public static implicit operator NativeString?(string? s) => FromString(s);

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        private void Dispose(bool disposing)
        {
            if (disposed)
                return;

            if (pointer != null)
            {
                NativeMemory.Free(pointer);
                pointer = null;
            }

            length = 0;
            disposed = true;
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(NativeString));
        }
    }
}
