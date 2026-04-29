using System;

namespace Bolt;
/// <summary>
/// Marks a field to be visible and editable in the Bolt Editor inspector.
/// Optionally specify a display name and whether the field is read-only.
/// </summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public class ShowInEditorAttribute : Attribute
{
    public string DisplayName { get; }
    public bool ReadOnly { get; }

    public ShowInEditorAttribute(string displayName = "", bool readOnly = false)
    {
        DisplayName = displayName;
        ReadOnly = readOnly;
    }
}
