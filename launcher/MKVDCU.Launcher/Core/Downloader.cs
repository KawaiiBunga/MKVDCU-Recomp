using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Reflection;
using System.Security.Cryptography;

namespace MKVDCU.Launcher.Core;

internal sealed record DownloadProgress(long Received, long Total, double BytesPerSecond);

internal static class Http
{
    public static readonly string Version =
        Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "0.0.0";

    public static readonly HttpClient Client = Create();

    private static HttpClient Create()
    {
        var client = new HttpClient(new SocketsHttpHandler
        {
            AutomaticDecompression = DecompressionMethods.All,
            PooledConnectionLifetime = TimeSpan.FromMinutes(10),
            ConnectTimeout = TimeSpan.FromSeconds(20),
        })
        { Timeout = Timeout.InfiniteTimeSpan };
        client.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("MKVDCU-Recomp-Launcher", Version));
        return client;
    }
}

internal static class Downloader
{
    // Downloads into the launcher's download cache, resuming a partial file,
    // and returns the path once the size and SHA-256 match. A file already in
    // the cache with the right hash is reused without touching the network.
    public static async Task<string> FetchAsync(string url, string fileName, string? sha256, long size,
        IProgress<DownloadProgress>? progress, CancellationToken token)
    {
        if (!Uri.TryCreate(url, UriKind.Absolute, out var uri) || uri.Scheme != Uri.UriSchemeHttps)
            throw new InvalidOperationException("Refusing a non-HTTPS download: " + url);
        Directory.CreateDirectory(Paths.Downloads);
        var target = Path.Combine(Paths.Downloads, Path.GetFileName(fileName));
        if (File.Exists(target) && (size <= 0 || new FileInfo(target).Length == size) &&
            (sha256 == null || await HashMatchesAsync(target, sha256, token)))
            return target;

        var part = target + ".part";
        for (var attempt = 1; ; attempt++)
        {
            try
            {
                await DownloadAsync(uri, part, size, progress, token);
                break;
            }
            catch (Exception ex) when (attempt < 5 && ex is IOException or HttpRequestException &&
                                       !token.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromSeconds(2 * attempt), token);
            }
        }
        if (size > 0 && new FileInfo(part).Length != size)
            throw new InvalidDataException($"{fileName} downloaded with the wrong size.");
        if (sha256 != null && !await HashMatchesAsync(part, sha256, token))
        {
            File.Delete(part);
            throw new InvalidDataException($"{fileName} failed its SHA-256 check and was deleted. Try again.");
        }
        File.Move(part, target, true);
        return target;
    }

    private static async Task DownloadAsync(Uri uri, string part, long size, IProgress<DownloadProgress>? progress,
        CancellationToken token)
    {
        var existing = File.Exists(part) ? new FileInfo(part).Length : 0;
        if (size > 0 && existing >= size) existing = 0;
        using var request = new HttpRequestMessage(HttpMethod.Get, uri);
        if (existing > 0) request.Headers.Range = new RangeHeaderValue(existing, null);
        using var response = await Http.Client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, token);
        response.EnsureSuccessStatusCode();
        var resumed = existing > 0 && response.StatusCode == HttpStatusCode.PartialContent;
        if (!resumed) existing = 0;
        var total = size > 0 ? size : (response.Content.Headers.ContentLength ?? 0) + existing;

        await using var source = await response.Content.ReadAsStreamAsync(token);
        await using var output = new FileStream(part, resumed ? FileMode.Append : FileMode.Create, FileAccess.Write,
            FileShare.None, 1 << 20, useAsync: true);
        var buffer = new byte[1 << 20];
        var received = existing;
        var clock = System.Diagnostics.Stopwatch.StartNew();
        long windowBytes = 0;
        var lastReport = TimeSpan.Zero;
        int count;
        while ((count = await source.ReadAsync(buffer, token)) > 0)
        {
            await output.WriteAsync(buffer.AsMemory(0, count), token);
            received += count;
            windowBytes += count;
            if (size > 0 && received > size) throw new InvalidDataException("Download is larger than expected.");
            if (clock.Elapsed - lastReport > TimeSpan.FromMilliseconds(250))
            {
                var seconds = (clock.Elapsed - lastReport).TotalSeconds;
                progress?.Report(new DownloadProgress(received, total, windowBytes / seconds));
                lastReport = clock.Elapsed;
                windowBytes = 0;
            }
        }
        progress?.Report(new DownloadProgress(received, total, 0));
    }

    public static async Task<bool> HashMatchesAsync(string file, string sha256, CancellationToken token)
    {
        await using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20, true);
        var hash = Convert.ToHexString(await SHA256.HashDataAsync(stream, token));
        return hash.Equals(sha256.Replace("sha256:", "", StringComparison.OrdinalIgnoreCase),
            StringComparison.OrdinalIgnoreCase);
    }

    public static string Size(long bytes) => bytes switch
    {
        >= 1L << 30 => $"{bytes / (double)(1L << 30):0.0} GB",
        >= 1L << 20 => $"{bytes / (double)(1L << 20):0} MB",
        _ => $"{Math.Max(1, bytes / 1024)} KB",
    };
}
