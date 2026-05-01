using System;

namespace Axiom;
/// <summary>
/// Specifies min/max clamp bounds for a numeric field in the editor inspector.
/// </summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public class ClampValueAttribute : Attribute
{
    public float Min { get; }
    public float Max { get; }

    public ClampValueAttribute(float min, float max)
    {
        Min = min;
        Max = max;
    }
}
