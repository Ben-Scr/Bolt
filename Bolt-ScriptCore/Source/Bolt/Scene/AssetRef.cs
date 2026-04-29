using Bolt.Interop;


namespace Bolt;

internal struct TextureRef
{
    public ulong UUID;

    public bool IsValid => UUID != 0 && InternalCalls.Texture_LoadAsset(UUID);

    public string Path
    {
        get => UUID != 0 ? InternalCalls.Asset_GetPath(UUID) : "";
        set => UUID = string.IsNullOrEmpty(value) ? 0UL : InternalCalls.Asset_GetOrCreateUUIDFromPath(value);
    }

    public string Name
    {
        get
        {
            if (UUID == 0)
                return "(None)";

            string name = InternalCalls.Asset_GetDisplayName(UUID);
            return string.IsNullOrEmpty(name) ? "(Missing Asset)" : name;
        }
    }

    public Texture? Resource => Texture.FromAssetUUID(UUID);
    public Texture? Texture => Resource;

    public TextureRef(ulong uuid)
    {
        UUID = uuid;
    }

    public TextureRef(string path)
    {
        UUID = string.IsNullOrEmpty(path) ? 0UL : InternalCalls.Asset_GetOrCreateUUIDFromPath(path);
    }

    public override string ToString()
    {
        if (UUID == 0)
            return "(None)";

        string name = InternalCalls.Asset_GetDisplayName(UUID);
        if (!string.IsNullOrEmpty(name))
            return name;

        string path = InternalCalls.Asset_GetPath(UUID);
        return string.IsNullOrEmpty(path) ? "(Missing Asset)" : path;
    }
}

internal struct AudioRef
{
    public ulong UUID;

    public bool IsValid => UUID != 0 && InternalCalls.Audio_LoadAsset(UUID);

    public string Path
    {
        get => UUID != 0 ? InternalCalls.Asset_GetPath(UUID) : "";
        set => UUID = string.IsNullOrEmpty(value) ? 0UL : InternalCalls.Asset_GetOrCreateUUIDFromPath(value);
    }

    public string Name
    {
        get
        {
            if (UUID == 0)
                return "(None)";

            string name = InternalCalls.Asset_GetDisplayName(UUID);
            return string.IsNullOrEmpty(name) ? "(Missing Asset)" : name;
        }
    }

    public Audio? Resource => Audio.FromAssetUUID(UUID);
    public Audio? Audio => Resource;

    public AudioRef(ulong uuid)
    {
        UUID = uuid;
    }

    public AudioRef(string path)
    {
        UUID = string.IsNullOrEmpty(path) ? 0UL : InternalCalls.Asset_GetOrCreateUUIDFromPath(path);
    }

    public override string ToString()
    {
        if (UUID == 0)
            return "(None)";

        string name = InternalCalls.Asset_GetDisplayName(UUID);
        if (!string.IsNullOrEmpty(name))
            return name;

        string path = InternalCalls.Asset_GetPath(UUID);
        return string.IsNullOrEmpty(path) ? "(Missing Asset)" : path;
    }
}
