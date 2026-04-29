using System;
using System.Runtime.InteropServices;

namespace Bolt.Interop;
/// <summary>
/// Low-level interop utilities for marshalling data between managed C#
/// and native C++ engine code.
///
/// These helpers reduce boilerplate when working with pinned memory,
/// unmanaged arrays, and native string conversions.
/// </summary>
public static class NativeInterop
{
    /// <summary>
    /// Pin a struct and pass its pointer to a native action.
    /// Useful for passing blittable types (Vector2, Vector4, etc.) by pointer.
    /// </summary>
    public static unsafe void WithPinned<T>(ref T value, Action<IntPtr> action) where T : unmanaged
    {
        fixed (T* ptr = &value)
        {
            action(new IntPtr(ptr));
        }
    }

    /// <summary>
    /// Allocate unmanaged memory for an array of T, execute an action with the pointer,
    /// then free the memory. Useful for passing buffers to native code.
    /// </summary>
    public static unsafe T[] FromNativeArray<T>(IntPtr ptr, int count) where T : unmanaged
    {
        if (ptr == IntPtr.Zero || count <= 0)
            return Array.Empty<T>();

        T[] result = new T[count];
        int stride = sizeof(T);

        for (int i = 0; i < count; i++)
        {
            result[i] = Marshal.PtrToStructure<T>(ptr + i * stride);
        }

        return result;
    }

    /// <summary>
    /// Copy a managed string to an unmanaged UTF-8 buffer.
    /// Caller is responsible for freeing the returned pointer with Marshal.FreeHGlobal.
    /// </summary>
    public static IntPtr StringToNativeUtf8(string str)
    {
        if (str == null)
            return IntPtr.Zero;

        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(str);
        IntPtr ptr = Marshal.AllocHGlobal(bytes.Length + 1);
        Marshal.Copy(bytes, 0, ptr, bytes.Length);
        Marshal.WriteByte(ptr, bytes.Length, 0); // null terminator
        return ptr;
    }

    /// <summary>
    /// Read a null-terminated UTF-8 string from an unmanaged pointer.
    /// </summary>
    public static string? NativeUtf8ToString(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero)
            return null;

        return Marshal.PtrToStringUTF8(ptr);
    }
}
