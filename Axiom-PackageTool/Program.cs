using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;

// Axiom-PackageTool: Lightweight CLI for package operations.
// Called by the engine via _popen(). All output is JSON to stdout, errors to stderr.
// Stateless — every invocation is independent.

var handler = new HttpClientHandler { AllowAutoRedirect = true };
using var http = new HttpClient(handler);
http.DefaultRequestHeaders.UserAgent.ParseAdd("Axiom-PackageTool/1.0");
http.Timeout = TimeSpan.FromSeconds(15);

var jsonOpts = new JsonSerializerOptions
{
    PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    WriteIndented = false
};

if (args.Length == 0)
{
    Console.Error.WriteLine("Usage: Axiom-PackageTool <command> [args]");
    Console.Error.WriteLine("Commands: nuget-search, nuget-versions, github-index, github-download");
    return 1;
}

try
{
    return args[0] switch
    {
        "nuget-search" => await NuGetSearch(args),
        "nuget-versions" => await NuGetVersions(args),
        "github-index" => await GitHubIndex(args),
        "github-download" => await GitHubDownload(args),
        _ => Error($"Unknown command: {args[0]}")
    };
}
catch (HttpRequestException ex)
{
    Console.Error.WriteLine($"Network error: {ex.Message}");
    Console.WriteLine("[]");
    return 1;
}
catch (TaskCanceledException)
{
    Console.Error.WriteLine("Request timed out");
    Console.WriteLine("[]");
    return 1;
}
catch (Exception ex)
{
    Console.Error.WriteLine($"Error: {ex.Message}");
    return 1;
}


// ── NuGet v3 Search ──────────────────────────────────────────────

async Task<int> NuGetSearch(string[] args)
{
    if (args.Length < 2) return Error("Usage: nuget-search <query> [--take N]");

    string query = args[1];
    int take = 20;

    for (int i = 2; i < args.Length - 1; i++)
    {
        if (args[i] == "--take" && int.TryParse(args[i + 1], out int t))
            take = t;
    }

    string url = $"https://azuresearch-usnc.nuget.org/query?q={Uri.EscapeDataString(query)}&take={take}&prerelease=false&semVerLevel=2.0.0";
    var response = await http.GetFromJsonAsync<NuGetSearchResponse>(url);

    if (response?.Data == null)
    {
        Console.WriteLine("[]");
        return 0;
    }

    var results = response.Data.Select(p => new
    {
        id = p.Id,
        version = p.Version,
        description = Truncate(p.Description, 200),
        authors = string.Join(", ", p.Authors ?? []),
        downloads = p.TotalDownloads,
        verified = p.Verified
    });

    Console.WriteLine(JsonSerializer.Serialize(results, jsonOpts));
    return 0;
}


// ── NuGet v3 Versions ────────────────────────────────────────────

async Task<int> NuGetVersions(string[] args)
{
    if (args.Length < 2) return Error("Usage: nuget-versions <packageId>");

    string packageId = args[1].ToLowerInvariant();
    string url = $"https://api.nuget.org/v3-flatcontainer/{packageId}/index.json";
    var response = await http.GetFromJsonAsync<NuGetVersionsResponse>(url);

    if (response?.Versions == null)
    {
        Console.WriteLine("[]");
        return 0;
    }

    // Return newest first
    var versions = response.Versions.AsEnumerable().Reverse().Take(30);
    Console.WriteLine(JsonSerializer.Serialize(versions, jsonOpts));
    return 0;
}


// ── GitHub Index Fetch ───────────────────────────────────────────

async Task<int> GitHubIndex(string[] args)
{
    if (args.Length < 2) return Error("Usage: github-index <url>");

    string url = args[1];
    string json = await http.GetStringAsync(url);
    Console.WriteLine(json);
    return 0;
}


// ── GitHub File Download ─────────────────────────────────────────

async Task<int> GitHubDownload(string[] args)
{
    if (args.Length < 3) return Error("Usage: github-download <url> <outputPath>");

    string url = args[1];
    string outputPath = args[2];

    string? dir = Path.GetDirectoryName(outputPath);
    if (!string.IsNullOrEmpty(dir))
        Directory.CreateDirectory(dir);

    using var response = await http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
    response.EnsureSuccessStatusCode();

    await using var fileStream = File.Create(outputPath);
    await response.Content.CopyToAsync(fileStream);
    long size = fileStream.Position;

    Console.WriteLine(JsonSerializer.Serialize(new { success = true, path = outputPath, size }, jsonOpts));
    return 0;
}


// ── Helpers ──────────────────────────────────────────────────────

int Error(string msg)
{
    Console.Error.WriteLine(msg);
    return 1;
}

string Truncate(string? s, int max) =>
    s == null ? "" : s.Length <= max ? s : s[..max] + "...";


// ── NuGet API response models ────────────────────────────────────

class NuGetSearchResponse
{
    [JsonPropertyName("data")]
    public List<NuGetPackageResult>? Data { get; set; }
}

class NuGetPackageResult
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = "";

    [JsonPropertyName("version")]
    public string Version { get; set; } = "";

    [JsonPropertyName("description")]
    public string? Description { get; set; }

    [JsonPropertyName("authors")]
    public List<string>? Authors { get; set; }

    [JsonPropertyName("totalDownloads")]
    public long TotalDownloads { get; set; }

    [JsonPropertyName("verified")]
    public bool Verified { get; set; }
}

class NuGetVersionsResponse
{
    [JsonPropertyName("versions")]
    public List<string>? Versions { get; set; }
}
