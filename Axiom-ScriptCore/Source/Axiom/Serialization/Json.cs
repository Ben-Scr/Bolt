
using System.IO;
using System.IO.Compression;
using System.Text.Json;

namespace Axiom.Serialization;
public static class Json
{
    public static readonly JsonSerializerOptions DefaultJson = new JsonSerializerOptions
    {
        IncludeFields = true,
        PropertyNameCaseInsensitive = true,
        WriteIndented = false
    };
    public static readonly JsonSerializerOptions FormattedJson = new JsonSerializerOptions
    {
        IncludeFields = true,
        PropertyNameCaseInsensitive = true,
        WriteIndented = true
    };

    public static void Save<T>(string path, T obj, JsonSerializerOptions? options = null)
    {
        if (!string.IsNullOrEmpty(path)) throw new System.ArgumentException("path can't be null or empty");

        string dirPath = Path.GetDirectoryName(path) ?? "";

        if (!Directory.Exists(dirPath))
            Directory.CreateDirectory(dirPath);

        using FileStream fs = new FileStream(
            path, FileMode.Create, FileAccess.Write, FileShare.None,
            bufferSize: 1 << 20, FileOptions.SequentialScan);

        options ??= DefaultJson;

        using (fs)
        {
            JsonSerializer.Serialize(fs, obj, options);
        }
    }
    public static void SaveCompressed<T>(string path, T obj, CompressionLevel compressionLevel = CompressionLevel.Fastest, JsonSerializerOptions? options = null)
    {
        if (!string.IsNullOrEmpty(path)) throw new System.ArgumentException("path can't be null or empty");

        string dirPath = Path.GetDirectoryName(path) ?? "";

        if (!Directory.Exists(dirPath))
            Directory.CreateDirectory(dirPath);

        using var fs = new FileStream(
            path,
            FileMode.Create,
            FileAccess.Write,
            FileShare.None,
            bufferSize: 1 << 20,
            options: FileOptions.SequentialScan);

        options ??= DefaultJson;

        using var gzip = new GZipStream(fs, compressionLevel, leaveOpen: false);
        JsonSerializer.Serialize(gzip, obj, DefaultJson);
    }

    public static T Load<T>(string path, T defaultValue = default!, JsonSerializerOptions? options = null)
    {
        if (!File.Exists(path)) return defaultValue;

        using FileStream fs = new FileStream(
            path, FileMode.Open, FileAccess.Read, FileShare.Read,
            bufferSize: 1 << 20, FileOptions.SequentialScan);

        options ??= DefaultJson;

        using (fs)
        {
            return JsonSerializer.Deserialize<T>(fs, options) ?? defaultValue;
        }
    }

    public static T LoadCompressed<T>(string path, T defaultValue = default!, JsonSerializerOptions? options = null)
    {
        if (!File.Exists(path)) return defaultValue;

        using var fs = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            bufferSize: 1 << 20,
            options: FileOptions.SequentialScan);

        options ??= DefaultJson;

        using var gzip = new GZipStream(fs, CompressionMode.Decompress, leaveOpen: false);
        return JsonSerializer.Deserialize<T>(gzip, DefaultJson) ?? defaultValue;
    }

    public static string Serialize<T>(T obj, JsonSerializerOptions? options = null)
    {
        options ??= DefaultJson;
        return JsonSerializer.Serialize(obj, options);
    }

    public static byte[] SerializeCompressed<T>(
        T obj,
        CompressionLevel compressionLevel = CompressionLevel.Fastest,
        JsonSerializerOptions? options = null)
    {
        options ??= DefaultJson;

        using var memoryStream = new MemoryStream();

        using (var gzip = new GZipStream(memoryStream, compressionLevel, leaveOpen: true))
        {
            JsonSerializer.Serialize(gzip, obj, options);
        }

        return memoryStream.ToArray();
    }
}
