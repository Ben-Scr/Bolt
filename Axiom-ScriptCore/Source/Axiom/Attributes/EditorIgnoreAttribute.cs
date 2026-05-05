using System;

namespace Axiom;

/// <summary>
/// Hides a field, property, struct, or class from the Axiom editor inspector.
/// On a field/property: that one member is skipped.
/// On a struct/class: every field of that type is treated as unsupported,
/// so any composite that nests it will simply leave it out of its sub-fields.
/// Composites tolerate unsupported leaves -- only when EVERY non-ignored
/// sub-field is unsupported does the composite itself become unsupported.
/// </summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property | AttributeTargets.Struct | AttributeTargets.Class)]
public class EditorIgnoreAttribute : Attribute
{
}
