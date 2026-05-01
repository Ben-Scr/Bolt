
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;

namespace Axiom.Serialization;

public static class ZipUtility
{
    public static void ZipFolder(
        string sourceFolder,
        string zipPath,
        bool includeBaseDirectory = false,
        bool overwrite = true)
    {
        if (!Directory.Exists(sourceFolder))
            throw new DirectoryNotFoundException(sourceFolder);

        if (File.Exists(zipPath))
        {
            if (!overwrite)
                throw new IOException($"ZIP already exists: {zipPath}");

            File.Delete(zipPath);
        }

        Directory.CreateDirectory(Path.GetDirectoryName(zipPath)!);

        ZipFile.CreateFromDirectory(
            sourceFolder,
            zipPath,
            CompressionLevel.Optimal,
            includeBaseDirectory);
    }

    public static void UnzipToFolder(
        string zipPath,
        string destinationFolder,
        bool overwriteFiles = true)
    {
        if (!File.Exists(zipPath))
            throw new FileNotFoundException("ZIP not found.", zipPath);

        Directory.CreateDirectory(destinationFolder);

        ZipFile.ExtractToDirectory(
            zipPath,
            destinationFolder,
            overwriteFiles);
    }

    public static bool IsZipFile(string path)
    {
        return File.Exists(path)
            && string.Equals(Path.GetExtension(path), ".zip", StringComparison.OrdinalIgnoreCase);
    }

    public static IEnumerable<string> ListEntries(string zipPath)
    {
        using var archive = ZipFile.OpenRead(zipPath);

        foreach (var entry in archive.Entries)
            yield return entry.FullName;
    }

    public static void AddFileToZip(
        string zipPath,
        string filePath,
        string? entryName = null,
        CompressionLevel compressionLevel = CompressionLevel.Optimal)
    {
        if (!File.Exists(filePath))
            throw new FileNotFoundException("File not found.", filePath);

        using var archive = ZipFile.Open(zipPath, ZipArchiveMode.Update);

        archive.CreateEntryFromFile(
            filePath,
            entryName ?? Path.GetFileName(filePath),
            compressionLevel);
    }

    public static void AddFolderToZip(
        string zipPath,
        string folderPath,
        string entryRoot = "",
        CompressionLevel compressionLevel = CompressionLevel.Optimal)
    {
        if (!Directory.Exists(folderPath))
            throw new DirectoryNotFoundException(folderPath);

        using var archive = ZipFile.Open(zipPath, ZipArchiveMode.Update);

        foreach (var file in Directory.EnumerateFiles(folderPath, "*", SearchOption.AllDirectories))
        {
            var relativePath = Path.GetRelativePath(folderPath, file);
            var entryName = Path.Combine(entryRoot, relativePath).Replace('\\', '/');

            archive.CreateEntryFromFile(file, entryName, compressionLevel);
        }
    }

    public static void DeleteEntryFromZip(string zipPath, string entryName)
    {
        using var archive = ZipFile.Open(zipPath, ZipArchiveMode.Update);

        var entry = archive.GetEntry(entryName);
        entry?.Delete();
    }

    public static bool ContainsEntry(string zipPath, string entryName)
    {
        using var archive = ZipFile.OpenRead(zipPath);

        return archive.GetEntry(entryName) is not null;
    }

    public static long GetUncompressedSize(string zipPath)
    {
        using var archive = ZipFile.OpenRead(zipPath);

        return archive.Entries.Sum(entry => entry.Length);
    }

    public static long GetCompressedSize(string zipPath)
    {
        using var archive = ZipFile.OpenRead(zipPath);

        return archive.Entries.Sum(entry => entry.CompressedLength);
    }
}
