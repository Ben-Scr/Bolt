using System;
using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeHashset<T> : IDisposable where T : unmanaged
    {
        private const byte StateEmpty = 0;
        private const byte StateUsed = 1;
        private const byte StateDeleted = 2;

        private T* keys;
        private byte* states;
        private readonly IEqualityComparer<T> comparer;
        private int capacity;
        private int count;
        private int occupied;
        private int version;
        private bool disposed;
        private const float LoadFactor = 0.7f;

        public int Count { get { ThrowIfDisposed(); return count; } }
        public int Capacity { get { ThrowIfDisposed(); return capacity; } }
        public bool IsCreated => !disposed && keys != null;

        ~NativeHashset()
        {
            Dispose(false);
        }

        public NativeHashset(int initialCapacity, IEqualityComparer<T>? comparer = null)
        {
            if (initialCapacity < 0)
                throw new ArgumentOutOfRangeException(nameof(initialCapacity));

            if (initialCapacity == 0)
            {
                keys = null;
                states = null;
                this.comparer = comparer ?? EqualityComparer<T>.Default;
                capacity = 0;
                count = 0;
                occupied = 0;
                return;
            }

            this.comparer = comparer ?? EqualityComparer<T>.Default;
            AllocateStorage(NativeCollectionUtility.NextPowerOfTwo(initialCapacity));
        }

        public bool Add(in T item)
        {
            ThrowIfDisposed();
            if (capacity == 0)
                Resize(4);

            if (occupied >= capacity * LoadFactor)
                Resize(NativeCollectionUtility.GrowPowerOfTwoCapacity(capacity, capacity + 1));

            bool added = AddInternal(in item);
            if (added)
                version++;
            return added;
        }

        public bool Contains(in T item)
        {
            ThrowIfDisposed();
            if (capacity == 0 || count == 0)
                return false;

            int cap = capacity;
            int hash = GetHash(in item);
            int idx = hash & (cap - 1);

            for (int i = 0; i < cap; i++)
            {
                int slot = (idx + i) & (cap - 1);
                byte state = states[slot];

                if (state == StateEmpty)
                    return false;

                if (state == StateUsed && comparer.Equals(keys[slot], item))
                    return true;
            }

            return false;
        }

        public bool Remove(in T item)
        {
            ThrowIfDisposed();
            if (capacity == 0 || count == 0)
                return false;

            int cap = capacity;
            int hash = GetHash(in item);
            int idx = hash & (cap - 1);

            for (int i = 0; i < cap; i++)
            {
                int slot = (idx + i) & (cap - 1);
                byte state = states[slot];

                if (state == StateEmpty)
                    return false;

                if (state == StateUsed && comparer.Equals(keys[slot], item))
                {
                    states[slot] = StateDeleted;
                    count--;
                    version++;
                    return true;
                }
            }

            return false;
        }

        public void Clear()
        {
            ThrowIfDisposed();
            if (capacity == 0)
                return;

            for (int i = 0; i < capacity; i++)
                states[i] = StateEmpty;

            count = 0;
            occupied = 0;
            version++;
        }

        public void EnsureCapacity(int min)
        {
            ThrowIfDisposed();
            if (min < 0)
                throw new ArgumentOutOfRangeException(nameof(min));

            if (capacity >= min)
                return;

            Resize(min);
        }

        public Enumerator GetEnumerator()
        {
            ThrowIfDisposed();
            return new Enumerator(this);
        }

        public struct Enumerator
        {
            private readonly NativeHashset<T> _owner;
            private readonly int _capturedVersion;
            private int _index;
            private T _current;

            internal Enumerator(NativeHashset<T> set)
            {
                _owner = set;
                _capturedVersion = set.version;
                _index = -1;
                _current = default;
            }

            public T Current
            {
                get
                {
                    ValidateVersion();
                    return _current;
                }
            }

            public bool MoveNext()
            {
                ValidateVersion();
                if (_owner.states == null || _owner.keys == null || _owner.capacity <= 0)
                    return false;

                while (true)
                {
                    _index++;
                    if (_index >= _owner.capacity)
                        return false;

                    if (_owner.states[_index] == StateUsed)
                    {
                        _current = _owner.keys[_index];
                        return true;
                    }
                }
            }

            private void ValidateVersion()
            {
                _owner.ThrowIfDisposed();
                if (_capturedVersion != _owner.version)
                    throw new InvalidOperationException("Collection was modified during enumeration.");
            }
        }

        public void Reset(bool clearMemory = false)
        {
            ThrowIfDisposed();
            if (capacity == 0)
                return;

            for (int i = 0; i < capacity; i++)
                states[i] = StateEmpty;

            if (clearMemory && keys != null)
            {
                for (int i = 0; i < capacity; i++)
                    keys[i] = default;
            }

            count = 0;
            occupied = 0;
            version++;
        }

        private bool AddInternal(in T item)
        {
            int cap = capacity;
            int hash = GetHash(in item);
            int idx = hash & (cap - 1);

            int firstDeleted = -1;

            for (int i = 0; i < cap; i++)
            {
                int slot = (idx + i) & (cap - 1);
                byte state = states[slot];

                if (state == StateEmpty)
                {
                    if (firstDeleted >= 0)
                        slot = firstDeleted;
                    else
                        occupied++;

                    keys[slot] = item;
                    states[slot] = StateUsed;
                    count++;
                    return true;
                }

                if (state == StateUsed)
                {
                    if (comparer.Equals(keys[slot], item))
                        return false;
                }
                else if (state == StateDeleted && firstDeleted < 0)
                {
                    firstDeleted = slot;
                }
            }

            return false;
        }

        private void Resize(int newCapacity)
        {
            int newCap = NativeCollectionUtility.NextPowerOfTwo(newCapacity);

            T* oldKeys = keys;
            byte* oldStates = states;
            int oldCap = capacity;

            AllocateStorage(newCap);

            if (oldKeys != null)
            {
                for (int i = 0; i < oldCap; i++)
                {
                    if (oldStates[i] == StateUsed)
                    {
                        AddInternal(in oldKeys[i]);
                    }
                }

                Marshal.FreeHGlobal((IntPtr)oldKeys);
                Marshal.FreeHGlobal((IntPtr)oldStates);
            }

            version++;
        }

        private int GetHash(in T item)
        {
            return comparer.GetHashCode(item) & 0x7fffffff;
        }

        private void AllocateStorage(int newCapacity)
        {
            T* newKeys = null;
            byte* newStates = null;

            try
            {
                newKeys = (T*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(T), newCapacity));
                newStates = (byte*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(byte), newCapacity));

                for (int i = 0; i < newCapacity; i++)
                    newStates[i] = StateEmpty;
            }
            catch
            {
                if (newKeys != null)
                    Marshal.FreeHGlobal((IntPtr)newKeys);
                if (newStates != null)
                    Marshal.FreeHGlobal((IntPtr)newStates);
                throw;
            }

            keys = newKeys;
            states = newStates;
            capacity = newCapacity;
            count = 0;
            occupied = 0;
            version++;
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

            if (keys != null)
            {
                Marshal.FreeHGlobal((IntPtr)keys);
                keys = null;
            }
            if (states != null)
            {
                Marshal.FreeHGlobal((IntPtr)states);
                states = null;
            }

            capacity = 0;
            count = 0;
            occupied = 0;
            version++;
            disposed = true;
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(NativeHashset<T>));
        }
    }
}
