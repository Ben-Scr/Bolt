using System;

namespace Bolt;


/// <summary>
/// Provides static access to keyboard and mouse input state.
/// Maps to the native Bolt InputManager.
/// </summary>
public static class Input
{
    public static event Action<KeyCode>? KeyDown;
    public static event Action<KeyCode>? KeyUp;

    public static event Action<MouseButton>? MouseDown;
    public static event Action<MouseButton>? MouseUp;
    public static event Action<float>? MouseScroll;
    public static event Action<Vector2>? MouseMove;

    public static int KeyCount => 349;
    public static int MouseCount => 8;

    /// <summary>
    /// Returns the current mouse position in screen coordinates.
    /// </summary>
    public static Vector2 MousePosition
    {
        get
        {
            InternalCalls.Input_GetMousePosition(out float x, out float y);
            return new Vector2(x, y);
        }
    }

    internal static void RaiseKeyDown(KeyCode key) => KeyDown?.Invoke(key);
    internal static void RaiseKeyUp(KeyCode key) => KeyUp?.Invoke(key);
    internal static void RaiseMouseDown(MouseButton button) => MouseDown?.Invoke(button);
    internal static void RaiseMouseUp(MouseButton button) => MouseUp?.Invoke(button);
    internal static void RaiseMouseScroll(float delta) => MouseScroll?.Invoke(delta);
    internal static void RaiseMouseMove(Vector2 position) => MouseMove?.Invoke(position);

    /// <summary>
    /// Returns a smoothed axis vector based on WASD/Arrow key input.
    /// X = horizontal (-1 to 1), Y = vertical (-1 to 1).
    /// </summary>
    public static Vector2 GetAxis()
    {
        InternalCalls.Input_GetAxis(out float x, out float y);
        return new Vector2(x, y);
    }

    /// <summary>
    /// Returns a raw (unsmoothed) axis vector based on key input.
    /// Values are -1, 0, or 1 per axis.
    /// </summary>
    public static Vector2 GetAxisRaw()
    {
        float x = 0f, y = 0f;
        if (GetKey(KeyCode.D) || GetKey(KeyCode.Right)) x += 1f;
        if (GetKey(KeyCode.A) || GetKey(KeyCode.Left))  x -= 1f;
        if (GetKey(KeyCode.W) || GetKey(KeyCode.Up))    y += 1f;
        if (GetKey(KeyCode.S) || GetKey(KeyCode.Down))  y -= 1f;
        return new Vector2(x, y);
    }

    /// <summary>
    /// Returns the mouse movement delta since the last frame.
    /// </summary>
    public static Vector2 GetMouseDelta()
    {
        InternalCalls.Input_GetMouseDelta(out float x, out float y);
        return new Vector2(x, y);
    }

    /// <summary>
    /// Returns the scroll wheel delta since the last frame.
    /// </summary>
    public static float GetScrollWheelDelta()
    {
        return InternalCalls.Input_GetScrollWheelDelta();
    }

    /// <summary>
    /// Returns true every frame the key is held down.
    /// </summary>
    public static bool GetKey(KeyCode key) => InternalCalls.Input_GetKey((int)key);

    /// <summary>
    /// Returns true only on the frame the key was first pressed.
    /// </summary>
    public static bool GetKeyDown(KeyCode key) => InternalCalls.Input_GetKeyDown((int)key);

    /// <summary>
    /// Returns true only on the frame the key was released.
    /// </summary>
    public static bool GetKeyUp(KeyCode key) => InternalCalls.Input_GetKeyUp((int)key);

    /// <summary>
    /// Returns true every frame the mouse button is held down.
    /// </summary>
    public static bool GetMouseButton(MouseButton button) => InternalCalls.Input_GetMouseButton((int)button);

    /// <summary>
    /// Returns true only on the frame the mouse button was first pressed.
    /// </summary>
    public static bool GetMouseButtonDown(MouseButton button) => InternalCalls.Input_GetMouseButtonDown((int)button);

    /// <summary>
    /// Returns true only on the frame the mouse button was released.
    /// </summary>
    public static bool GetMouseButtonUp(MouseButton button) => InternalCalls.Input_GetMouseButtonUp((int)button);
}
