
namespace Axiom;

public static class CameraUtility
{
    public static Vector2 GetMousePosition2D()
    {
        Camera2D? mainCamera = Camera2D.Main;

        if (mainCamera == null) throw new NullReferenceException("Main Camera doesn't exist");

        return mainCamera.ScreenToWorld(Input.MousePosition);
    }
    public static Vector2 GetMousePosition(Camera2D camera)
    {
        if (camera == null) throw new NullReferenceException("Main Camera doesn't exist");

        return camera.ScreenToWorld(Input.MousePosition);
    }
}
