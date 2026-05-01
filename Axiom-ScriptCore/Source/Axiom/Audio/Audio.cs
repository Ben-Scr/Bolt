using System;
using Axiom.Interop;

namespace Axiom;
public sealed class Audio : IEquatable<Audio>
{
    public ulong UUID { get; }

    internal Audio(ulong assetId)
    {
        UUID = assetId;
    }

    public bool IsValid => UUID != 0 && InternalCalls.Audio_LoadAsset(UUID);

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

    public void PlayOneShot(float volume = 1.0f)
    {
        if (UUID == 0)
            return;

        InternalCalls.Audio_PlayOneShotAsset(UUID, volume);
    }

    internal static Audio? FromAssetUUID(ulong assetId)
    {
        if (assetId == 0 || !InternalCalls.Asset_IsValid(assetId))
            return null;

        return InternalCalls.Audio_LoadAsset(assetId) ? new Audio(assetId) : null;
    }

    public bool Equals(Audio? other) => other is not null && UUID == other.UUID;
    public override bool Equals(object? obj) => obj is Audio other && Equals(other);
    public override int GetHashCode() => UUID.GetHashCode();
    public override string ToString() => Name;
}
