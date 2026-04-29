using System;

namespace Bolt
{
    //INFO(Ben-Scr): Used for writing text above a field within the editor inspector ui
    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
    public class HeaderAttribute : Attribute
    {
        public string Content { get; }
        public int Size { get; }

        public HeaderAttribute(string content = "", int size = 0)
        {
            Content = content;
            Size = size;
        }
    }

    //INFO(Ben-Scr): Used for adding vertical spacing before a field within the editor inspector ui
    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
    public class SpaceAttribute : Attribute
    {
        public float Height { get; }

        public SpaceAttribute(float height = 8.0f)
        {
            Height = height;
        }
    }
}
