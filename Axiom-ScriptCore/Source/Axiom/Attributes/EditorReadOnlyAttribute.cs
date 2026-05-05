using System;

namespace Axiom;

/// <summary>
/// Marks a field or property as read-only in the Axiom Editor inspector.
/// The value is still displayed but cannot be edited from the inspector UI.
///
/// Stackable with <see cref="ShowInEditorAttribute"/>; equivalent to passing
/// <c>readOnly: true</c> to that attribute, but separate so it can also be
/// applied to public fields that are auto-shown without an explicit
/// <c>[ShowInEditor]</c>.
/// </summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class EditorReadOnlyAttribute : Attribute
{
}
