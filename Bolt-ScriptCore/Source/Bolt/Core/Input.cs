using System;

namespace Bolt
{
    public enum MouseButton
    {
        Left = 0,
        Right = 1,
        Middle = 2
    }

    /// <summary>
    /// Provides static access to keyboard and mouse input state.
    /// Maps to the native Bolt InputManager.
    /// </summary>
    public static class Input
    {
        public static Action<KeyCode>? OnKeyDown;
        public static Action<KeyCode>? OnKeyUp;

        public static Action<MouseButton>? OnMouseDown;
        public static Action<MouseButton>? OnMouseUp;
        public static Action<MouseButton>? OnMouseScroll;
        public static Action<Vector2>? OnMouseMove;

        public static int KeyCount => 0;
        public static int MouseCount => 3;

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
        /// Returns the current mouse position in screen coordinates.
        /// </summary>
        public static Vector2 GetMousePosition()
        {
            InternalCalls.Input_GetMousePosition(out float x, out float y);
            return new Vector2(x, y);
        }
    }
}
