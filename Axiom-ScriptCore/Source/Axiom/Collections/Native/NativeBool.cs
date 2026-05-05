
using System;

namespace Axiom.Collections.Native
{
    // Note: A Collection that uses all 8 bits of the lowest datatype (byte) to store 8 bools in one bool
    public struct NativeBool
    {
        private byte data;

        public NativeBool()
        {

        }

        // Broadcast: sets all 8 bits to value. Use Set(index, value) for per-bit control.
        public NativeBool(bool value)
        {
            data = value ? (byte)0xFF : (byte)0x00;
        }

        public bool Get(int index)
        {
            if (index < 0 || index > 7)
                throw new ArgumentOutOfRangeException(nameof(index));

            return ((data >> index) & 1) == 0 ? false : true;
        }

        public void Set(int index, bool value)
        {
            if (index < 0 || index > 7)
                throw new ArgumentOutOfRangeException(nameof(index));

            if (value == true)
                data |= (byte)(1 << index);
            else
                data &= (byte)~(1 << index);
        }

        public byte ToByte() => data;

        public void FromByte(byte value) => data = value;

        public static explicit operator bool(NativeBool nb) => nb.Get(0);
        public static implicit operator NativeBool(bool b) => new NativeBool(b);

        public override string ToString()
        {
            return $"[{Get(0)}, {Get(1)}, {Get(2)}, {Get(3)}, {Get(4)}, {Get(5)}, {Get(6)}, {Get(7)}]";
        }
    }

}
