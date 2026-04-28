using System;

namespace Bolt
{
    //INFO(Ben-Scr): Used for writing text above a field within the editor inspector ui
    public class HeaderAttribute : Attribute
    {
        public string Content = string.Empty;
    }
}
