using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace MKVDCU.Launcher.Core;

// One downloadable file of a release: a GitHub asset, or a local file when
// MKVDCU_RELEASE_DIR points the launcher at an unpublished release to test.
internal sealed class ReleaseAsset
{
    public required string File { get; init; }
    public required string Sha256 { get; init; }
    public required long Size { get; init; }
    public string Url { get; set; } = "";
    public string LocalPath { get; set; } = "";

    public async Task<string> FetchAsync(IProgress<DownloadProgress>? progress, CancellationToken token)
    {
        if (LocalPath.Length > 0)
        {
            if (!await Downloader.HashMatchesAsync(LocalPath, Sha256, token))
                throw new InvalidDataException(File + " does not match release.json.");
            Directory.CreateDirectory(Paths.Downloads);
            var copy = Path.Combine(Paths.Downloads, Path.GetFileName(File));
            System.IO.File.Copy(LocalPath, copy, true);
            return copy;
        }
        return await Downloader.FetchAsync(Url, File, Sha256, Size, progress, token);
    }
}

internal sealed class ToolchainAsset
{
    public required string Id { get; init; }
    public required string File { get; init; }
    public required string Sha256 { get; init; }
    public required long Size { get; init; }
    // Toolchains rarely change, so a release may point at an older release's asset.
    public string Url { get; init; } = "";

    public ReleaseAsset ToAsset(ReleaseManifest owner) =>
        owner.Resolve(new ReleaseAsset { File = File, Sha256 = Sha256, Size = Size, Url = Url });
}

internal sealed class ReleaseManifest
{
    public int Schema { get; init; }
    public required string Version { get; init; }
    public string Notes { get; init; } = "";
    public bool Prerelease { get; set; }
    public required ReleaseAsset Launcher { get; init; }
    public required ReleaseAsset Port { get; init; }
    public required ToolchainAsset Toolchain { get; init; }
    public string XexSha256 { get; init; } = "";

    [JsonIgnore] public Dictionary<string, string> AssetUrls { get; set; } = new(StringComparer.OrdinalIgnoreCase);
    [JsonIgnore] public string LocalDirectory { get; set; } = "";
    [JsonIgnore] public string HtmlUrl { get; set; } = Paths.RepositoryUrl + "/releases";

    public ReleaseAsset Resolve(ReleaseAsset asset)
    {
        if (LocalDirectory.Length > 0)
            asset.LocalPath = Path.Combine(LocalDirectory, Path.GetFileName(asset.File));
        else if (asset.Url.Length == 0 && AssetUrls.TryGetValue(asset.File, out var url))
            asset.Url = url;
        if (asset.LocalPath.Length == 0 && !IsTrustedDownload(asset.Url))
            throw new InvalidDataException("Release asset has no usable download: " + asset.File);
        return asset;
    }

    private static bool IsTrustedDownload(string url) =>
        Uri.TryCreate(url, UriKind.Absolute, out var uri) && uri.Scheme == Uri.UriSchemeHttps &&
        uri.Host.Equals("github.com", StringComparison.OrdinalIgnoreCase) &&
        uri.AbsolutePath.StartsWith("/" + Paths.Repository + "/releases/download/", StringComparison.OrdinalIgnoreCase);
}

internal static class Releases
{
    public static readonly string CurrentVersion = (Assembly.GetExecutingAssembly()
        .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "0.0.0").Split('+')[0];
    public static bool IsDevBuild => CurrentVersion is "0.0.0" or "0.0.0-dev";

    private static readonly JsonSerializerOptions Json = new() { PropertyNameCaseInsensitive = true };
    private static string? LocalReleaseDirectory => Environment.GetEnvironmentVariable("MKVDCU_RELEASE_DIR");

    // The newest release on the channel, or null when there is none.
    public static async Task<ReleaseManifest?> LatestAsync(string channel, CancellationToken token)
    {
        if (LocalReleaseDirectory is { Length: > 0 } local) return await ReadLocalAsync(local, token);
        var releases = await GetJsonAsync($"https://api.github.com/repos/{Paths.Repository}/releases?per_page=15", token);
        if (releases == null) return null;
        ReleaseManifest? best = null;
        foreach (var release in releases.RootElement.EnumerateArray())
        {
            if (release.GetProperty("draft").GetBoolean()) continue;
            var prerelease = release.GetProperty("prerelease").GetBoolean();
            if (prerelease && channel != "preview") continue;
            var manifest = await ReadReleaseAsync(release, token);
            if (manifest == null) continue;
            if (best == null || Compare(manifest.Version, best.Version) > 0) best = manifest;
        }
        return best;
    }

    // The release this launcher belongs to; it supplies the matching port package.
    public static async Task<ReleaseManifest?> ForVersionAsync(string version, CancellationToken token)
    {
        if (LocalReleaseDirectory is { Length: > 0 } local) return await ReadLocalAsync(local, token);
        using var release = await GetJsonAsync(
            $"https://api.github.com/repos/{Paths.Repository}/releases/tags/v{version}", token);
        return release == null ? null : await ReadReleaseAsync(release.RootElement, token);
    }

    private static async Task<ReleaseManifest?> ReadLocalAsync(string dir, CancellationToken token)
    {
        var file = Path.Combine(dir, "release.json");
        if (!File.Exists(file)) return null;
        var manifest = JsonSerializer.Deserialize<ReleaseManifest>(await File.ReadAllTextAsync(file, token), Json);
        if (manifest != null) manifest.LocalDirectory = dir;
        return manifest;
    }

    private static async Task<ReleaseManifest?> ReadReleaseAsync(JsonElement release, CancellationToken token)
    {
        var urls = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var asset in release.GetProperty("assets").EnumerateArray())
            urls[asset.GetProperty("name").GetString() ?? ""] = asset.GetProperty("browser_download_url").GetString() ?? "";
        if (!urls.TryGetValue("release.json", out var manifestUrl)) return null;
        var text = await Http.Client.GetStringAsync(manifestUrl, token);
        var manifest = JsonSerializer.Deserialize<ReleaseManifest>(text, Json);
        if (manifest == null || manifest.Schema != 1) return null;
        manifest.AssetUrls = urls;
        manifest.Prerelease = release.GetProperty("prerelease").GetBoolean();
        manifest.HtmlUrl = release.GetProperty("html_url").GetString() ?? manifest.HtmlUrl;
        return manifest;
    }

    private static async Task<JsonDocument?> GetJsonAsync(string url, CancellationToken token)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, url);
        request.Headers.Accept.ParseAdd("application/vnd.github+json");
        using var response = await Http.Client.SendAsync(request, token);
        if (response.StatusCode == HttpStatusCode.NotFound) return null;
        response.EnsureSuccessStatusCode();
        return await JsonDocument.ParseAsync(await response.Content.ReadAsStreamAsync(token), cancellationToken: token);
    }

    // Semantic-version order; a pre-release sorts before its release.
    public static int Compare(string a, string b)
    {
        static (Version, string) Parse(string v)
        {
            v = v.TrimStart('v', 'V');
            var dash = v.IndexOf('-');
            var core = dash < 0 ? v : v[..dash];
            return (Version.TryParse(core, out var parsed) ? parsed : new Version(0, 0), dash < 0 ? "" : v[(dash + 1)..]);
        }
        var (va, pa) = Parse(a);
        var (vb, pb) = Parse(b);
        var result = va.CompareTo(vb);
        if (result != 0) return result;
        if (pa.Length == 0 && pb.Length == 0) return 0;
        if (pa.Length == 0) return 1;
        if (pb.Length == 0) return -1;
        return string.CompareOrdinal(pa, pb);
    }

    // Replaces the running launcher with the release's one. Windows lets a
    // running executable be renamed, so the new file takes its place and the
    // old one is deleted on the next start. Returns false when the launcher
    // is already this release's build.
    public static async Task<bool> ReplaceLauncherAsync(ReleaseManifest release, IProgress<DownloadProgress> progress,
        CancellationToken token)
    {
        var exe = Environment.ProcessPath ?? throw new InvalidOperationException("Launcher path unknown.");
        await using (var current = File.OpenRead(exe))
        {
            var hash = Convert.ToHexString(await SHA256.HashDataAsync(current, token));
            if (hash.Equals(release.Launcher.Sha256, StringComparison.OrdinalIgnoreCase)) return false;
        }
        var downloaded = await release.Resolve(release.Launcher).FetchAsync(progress, token);
        var old = exe + ".old";
        if (File.Exists(old)) File.Delete(old);
        File.Move(exe, old);
        try
        {
            File.Copy(downloaded, exe);
        }
        catch
        {
            File.Move(old, exe);
            throw;
        }
        File.Delete(downloaded);
        return true;
    }

    public static void RestartLauncher()
    {
        var exe = Environment.ProcessPath!;
        Process.Start(new ProcessStartInfo(exe) { UseShellExecute = false, ArgumentList = { "--updated" } });
    }

    public static void CleanUpAfterUpdate()
    {
        var old = Environment.ProcessPath + ".old";
        for (var attempt = 0; attempt < 10 && File.Exists(old); attempt++)
        {
            try { File.Delete(old); }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { Thread.Sleep(300); }
        }
    }
}
