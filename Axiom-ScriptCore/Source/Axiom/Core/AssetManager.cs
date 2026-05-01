using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.Json;
using Axiom.Interop;

namespace Axiom;

public static class AssetManager
{
    private enum AssetKind
    {
        Unknown = 0,
        Texture = 1,
        Audio = 2,
        Scene = 3,
        Prefab = 4,
        Script = 5,
        Other = 6
    }

    public static T? Load<T>(string path) where T : class
    {
        ulong guid = GetGUID(path);
        return LoadByGUID<T>(guid);
    }

    public static T? LoadByGUID<T>(string guid) where T : class
        => LoadByGUID<T>(ParseGUID(guid));

    public static T? LoadByGUID<T>(ulong guid) where T : class
    {
        if (guid == 0 || !InternalCalls.Asset_IsValid(guid))
            return null;

        Type type = typeof(T);
        AssetKind kind = (AssetKind)InternalCalls.Asset_GetKind(guid);

        if (type == typeof(Texture) && kind == AssetKind.Texture)
            return Texture.FromAssetUUID(guid) as T;

        if (type == typeof(Audio) && kind == AssetKind.Audio)
            return Audio.FromAssetUUID(guid) as T;

        if (type == typeof(Entity) && kind == AssetKind.Prefab)
            return Entity.FromPrefabGUID(guid) as T;

        return null;
    }

    public static IReadOnlyList<T> FindAll<T>(string pathPrefix = "") where T : class
    {
        AssetKind kind = KindForType(typeof(T));
        if (kind == AssetKind.Unknown)
            return Array.Empty<T>();

        string json = InternalCalls.Asset_FindAll(pathPrefix ?? "", (int)kind);
        List<T> assets = new();

        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            if (document.RootElement.ValueKind != JsonValueKind.Array)
                return assets;

            foreach (JsonElement element in document.RootElement.EnumerateArray())
            {
                ulong guid = element.ValueKind == JsonValueKind.String
                    ? ParseGUID(element.GetString() ?? "")
                    : element.GetUInt64();

                T? asset = LoadByGUID<T>(guid);
                if (asset != null)
                    assets.Add(asset);
            }
        }
        catch
        {
        }

        return assets;
    }

    public static ulong GetGUID(string path)
        => InternalCalls.Asset_GetOrCreateUUIDFromPath(path);

    private static AssetKind KindForType(Type type)
    {
        if (type == typeof(Texture)) return AssetKind.Texture;
        if (type == typeof(Audio)) return AssetKind.Audio;
        if (type == typeof(Entity)) return AssetKind.Prefab;
        return AssetKind.Unknown;
    }

    private static ulong ParseGUID(string guid)
    {
        if (string.IsNullOrWhiteSpace(guid))
            return 0;

        string compact = guid.Trim().Replace("-", "");
        if (ulong.TryParse(compact, NumberStyles.None, CultureInfo.InvariantCulture, out ulong numeric))
            return numeric;

        return compact.Length <= 16
            && ulong.TryParse(compact, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out ulong hex)
            ? hex
            : 0;
    }
}
