
namespace Axiom;

public static class CameraUtility
{
    public static Vector2 GetMousePosition2D()
    {
        Camera2DComponent mainCamera = Camera2DComponent.Main;

        if (mainCamera == null) throw new System.NullReferenceException("Main Camera doesn't exist");

        return mainCamera.ScreenToWorld(Input.MousePosition);
    }
    public static Vector2 GetMousePosition(Camera2DComponent camera)
    {
        if (camera == null) throw new System.NullReferenceException("Main Camera doesn't exist");

        return camera.ScreenToWorld(Input.MousePosition);
    }
}
