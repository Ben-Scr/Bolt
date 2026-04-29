using System;
using Bolt.Interop;

namespace Bolt;
public sealed class Texture : IEquatable<Texture>
{
    public ulong UUID { get; }

    internal Texture(ulong assetId)
    {
        UUID = assetId;
    }

    public bool IsValid => UUID != 0 && InternalCalls.Texture_LoadAsset(UUID);

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

    public string Path => UUID != 0 ? InternalCalls.Asset_GetPath(UUID) : "";
    public int Width => UUID != 0 ? InternalCalls.Texture_GetWidth(UUID) : 0;
    public int Height => UUID != 0 ? InternalCalls.Texture_GetHeight(UUID) : 0;

    internal static Texture? FromAssetUUID(ulong assetId)
    {
        if (assetId == 0 || !InternalCalls.Asset_IsValid(assetId))
            return null;

        return InternalCalls.Texture_LoadAsset(assetId) ? new Texture(assetId) : null;
    }

    public bool Equals(Texture? other) => other is not null && UUID == other.UUID;
    public override bool Equals(object? obj) => obj is Texture other && Equals(other);
    public override int GetHashCode() => UUID.GetHashCode();
    public override string ToString() => Name;
}
