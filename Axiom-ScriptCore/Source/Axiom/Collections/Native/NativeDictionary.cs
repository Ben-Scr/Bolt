
using System.Runtime.InteropServices;

namespace Axiom.Collections.Native
{
    public unsafe sealed class NativeDictionary<TKey, TValue> : IDisposable
        where TKey : unmanaged, IEquatable<TKey>
        where TValue : unmanaged
    {
        private const byte StateEmpty = 0;
        private const byte StateUsed = 1;
        private const byte StateDeleted = 2;

        private TKey* keys;
        private TValue* values;
        private byte* states;

        private int capacity;
        private int count;
        private int occupied;
        private int version;
        private bool disposed;

        private const float LoadFactor = 0.7f;

        public int Count { get { ThrowIfDisposed(); return count; } }
        public int Capacity { get { ThrowIfDisposed(); return capacity; } }
        public bool IsCreated => !disposed && keys != null;

        ~NativeDictionary()
        {
            Dispose(false);
        }

        public NativeDictionary(int initialCapacity)
        {
            if (initialCapacity < 0)
                throw new ArgumentOutOfRangeException(nameof(initialCapacity));

            if (initialCapacity == 0)
            {
                keys = null;
                values = null;
                states = null;
                capacity = 0;
                count = 0;
                occupied = 0;
                return;
            }

            AllocateStorage(NativeCollectionUtility.NextPowerOfTwo(initialCapacity));
        }

        public void Add(TKey key, TValue value)
        {
            ThrowIfDisposed();
            EnsureInitialized(4);

            if (occupied >= capacity * LoadFactor)
                Resize(NativeCollectionUtility.GrowPowerOfTwoCapacity(capacity, capacity + 1));

            if (!AddInternal(key, value, overwriteExisting: false))
                throw new ArgumentException("An element with the same key already exists.", nameof(key));

            version++;
        }

        public bool TryAdd(TKey key, TValue value)
        {
            ThrowIfDisposed();
            EnsureInitialized(4);

            if (occupied >= capacity * LoadFactor)
                Resize(NativeCollectionUtility.GrowPowerOfTwoCapacity(capacity, capacity + 1));

            bool added = AddInternal(key, value, overwriteExisting: false);
            if (added)
                version++;
            return added;
        }

        public bool TryGetValue(TKey key, out TValue value)
        {
            ThrowIfDisposed();
            if (capacity == 0 || count == 0)
            {
                value = default;
                return false;
            }

            int cap = capacity;
            int hash = GetHash(key);
            int idx = hash & (cap - 1);

            for (int i = 0; i < cap; i++)
            {
                int slot = (idx + i) & (cap - 1);
                byte state = states[slot];

                if (state == StateEmpty)
                {
                    value = default;
                    return false;
                }

                if (state == StateUsed && keys[slot].Equals(key))
                {
                    value = values[slot];
                    return true;
                }
            }

            value = default;
            return false;
        }

        public bool ContainsKey(TKey key)
        {
            return TryGetValue(key, out _);
        }

        public bool Remove(TKey key)
        {
            ThrowIfDisposed();
            if (capacity == 0 || count == 0)
                return false;

            int cap = capacity;
            int hash = GetHash(key);
            int idx = hash & (cap - 1);

            for (int i = 0; i < cap; i++)
            {
                int slot = (idx + i) & (cap - 1);
                byte state = states[slot];

                if (state == StateEmpty)
                    return false;

                if (state == StateUsed && keys[slot].Equals(key))
                {
                    states[slot] = StateDeleted;
                    count--;
                    version++;
                    return true;
                }
            }

            return false;
        }

        public TValue this[TKey key]
        {
            get
            {
                ThrowIfDisposed();
                if (!TryGetValue(key, out TValue value))
                    throw new KeyNotFoundException();
                return value;
            }
            set
            {
                ThrowIfDisposed();
                EnsureInitialized(4);

                if (occupied >= capacity * LoadFactor)
                    Resize(NativeCollectionUtility.GrowPowerOfTwoCapacity(capacity, capacity + 1));

                if (AddInternal(key, value, overwriteExisting: true))
                    version++;
            }
        }

        public void Clear()
        {
            ThrowIfDisposed();
            if (capacity == 0)
                return;

            for (int i = 0; i < capacity; i++)
            {
                states[i] = StateEmpty;
            }

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
            private readonly NativeDictionary<TKey, TValue> _owner;
            private readonly int _capturedVersion;
            private int _index;
            private KeyValuePair<TKey, TValue> _current;

            internal Enumerator(NativeDictionary<TKey, TValue> dict)
            {
                _owner = dict;
                _capturedVersion = dict.version;
                _index = -1;
                _current = default;
            }

            public KeyValuePair<TKey, TValue> Current
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
                if (_owner.keys == null || _owner.values == null || _owner.states == null || _owner.capacity <= 0)
                    return false;

                while (true)
                {
                    _index++;
                    if (_index >= _owner.capacity)
                        return false;

                    if (_owner.states[_index] == StateUsed)
                    {
                        _current = new KeyValuePair<TKey, TValue>(_owner.keys[_index], _owner.values[_index]);
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
        private void EnsureInitialized(int minCapacity)
        {
            if (capacity == 0)
            {
                Resize(minCapacity);
            }
        }

        private bool AddInternal(TKey key, TValue value, bool overwriteExisting)
        {
            int cap = capacity;
            int hash = GetHash(key);
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

                    keys[slot] = key;
                    values[slot] = value;
                    states[slot] = StateUsed;
                    count++;
                    return true;
                }

                if (state == StateUsed && keys[slot].Equals(key))
                {
                    if (overwriteExisting)
                    {
                        values[slot] = value;
                        return true;
                    }
                    return false;
                }

                if (state == StateDeleted && firstDeleted < 0)
                {
                    firstDeleted = slot;
                }
            }

            return false;
        }

        private void Resize(int newCapacity)
        {
            int newCap = NativeCollectionUtility.NextPowerOfTwo(newCapacity);

            TKey* oldKeys = keys;
            TValue* oldValues = values;
            byte* oldStates = states;
            int oldCap = capacity;

            AllocateStorage(newCap);

            if (oldKeys != null)
            {
                for (int i = 0; i < oldCap; i++)
                {
                    if (oldStates[i] == StateUsed)
                    {
                        AddInternal(oldKeys[i], oldValues[i], overwriteExisting: false);
                    }
                }

                Marshal.FreeHGlobal((IntPtr)oldKeys);
                Marshal.FreeHGlobal((IntPtr)oldValues);
                Marshal.FreeHGlobal((IntPtr)oldStates);
            }

            version++;
        }

        private static int GetHash(TKey key)
        {
            return key.GetHashCode() & 0x7fffffff;
        }

        private void AllocateStorage(int newCapacity)
        {
            TKey* newKeys = null;
            TValue* newValues = null;
            byte* newStates = null;

            try
            {
                newKeys = (TKey*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(TKey), newCapacity));
                newValues = (TValue*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(TValue), newCapacity));
                newStates = (byte*)Marshal.AllocHGlobal(NativeCollectionUtility.CheckedByteSize(sizeof(byte), newCapacity));

                for (int i = 0; i < newCapacity; i++)
                    newStates[i] = StateEmpty;
            }
            catch
            {
                if (newKeys != null)
                    Marshal.FreeHGlobal((IntPtr)newKeys);
                if (newValues != null)
                    Marshal.FreeHGlobal((IntPtr)newValues);
                if (newStates != null)
                    Marshal.FreeHGlobal((IntPtr)newStates);
                throw;
            }

            keys = newKeys;
            values = newValues;
            states = newStates;
            capacity = newCapacity;
            count = 0;
            occupied = 0;
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
            if (values != null)
            {
                Marshal.FreeHGlobal((IntPtr)values);
                values = null;
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
                throw new ObjectDisposedException(nameof(NativeDictionary<TKey, TValue>));
        }
    }
}
