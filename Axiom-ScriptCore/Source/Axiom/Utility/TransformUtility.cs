
namespace Axiom;

public static class TransformUtility
{
    /// <summary>
    /// Calculates the rotation angle (in degrees) for <paramref name="origin"/> to face <paramref name="target"/> in 2D space.
    /// Use <paramref name="lerp"/> to smoothly interpolate towards the target rotation.
    /// </summary>
    /// <param name="origin">The position of the entity that should rotate.</param>
    /// <param name="target">The position to rotate towards.</param>
    /// <param name="lerp">Interpolation factor between 0 (no rotation) and 1 (instant rotation).</param>
    /// <returns>The interpolated rotation angle in degrees.</returns>
    public static float LookAt2D(Vector2 origin, Vector2 target, float lerp)
    {
        throw new System.NotImplementedException();
    }
}
