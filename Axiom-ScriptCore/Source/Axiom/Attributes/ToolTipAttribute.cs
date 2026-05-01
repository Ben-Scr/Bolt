using System;

namespace Axiom;
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public class ToolTipAttribute : Attribute
{
    public string Text { get; }

    public ToolTipAttribute(string text)
    {
        Text = text;
    }
}
