using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;

namespace MKVDCU.Launcher.Core;

// The port's build inputs: host sources and configuration plus the pinned
// ReXGlue SDK. Each release ships them as one zip; a developer checkout
// provides them directly.
internal sealed record PortSource(string Version, string Root)
{
    public const string HostPath = "targets/mkvsdcu/private/rexglue-host";
    public const string SdkPath = "references/rexglue-sdk/out/install/win-amd64";
}

internal static class PortPackage
{
    private const string Marker = "port.json";

    public static PortSource? FindInstalled(string version)
    {
        if (Paths.FindRepository() is { } repo) return new PortSource("dev", repo);
        var dir = Path.Combine(Paths.Ports, version);
        return File.Exists(Path.Combine(dir, Marker)) ? new PortSource(version, dir) : null;
    }

    public static async Task<PortSource> InstallAsync(ReleaseManifest release, IProgress<DownloadProgress> progress,
        CancellationToken token)
    {
        var archive = await release.Resolve(release.Port).FetchAsync(progress, token);
        var target = Path.Combine(Paths.Ports, release.Version);
        var staging = Path.Combine(Paths.Ports, ".staging-" + Guid.NewGuid().ToString("N"));
        try
        {
            await Task.Run(() =>
            {
                ZipFile.ExtractToDirectory(archive, staging);
                if (!File.Exists(Path.Combine(staging, Marker)) ||
                    !File.Exists(Path.Combine(staging, PortSource.HostPath, "CMakeLists.txt")))
                    throw new InvalidDataException("The port package is incomplete.");
                if (Directory.Exists(target)) Directory.Delete(target, true);
                Directory.Move(staging, target);
            }, token);
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
        }
        // Keep only the package in use; older ones are rebuilt from nothing.
        foreach (var dir in Directory.EnumerateDirectories(Paths.Ports))
            if (!string.Equals(dir, target, StringComparison.OrdinalIgnoreCase))
                try { Directory.Delete(dir, true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        File.Delete(archive);
        return new PortSource(release.Version, target);
    }

    // Copies the build inputs into the workspace, touching only files whose
    // content changed, so an update recompiles only what it changed and
    // codegen reruns only when its inputs did.
    public static void SyncToWorkspace(PortSource source, string workspace)
    {
        var listPath = Path.Combine(workspace, ".port-files.json");
        var previous = File.Exists(listPath)
            ? JsonSerializer.Deserialize<List<string>>(File.ReadAllText(listPath)) ?? []
            : [];
        var current = new List<string>();
        foreach (var relative in EnumerateInputs(source.Root))
        {
            current.Add(relative);
            var from = Path.Combine(source.Root, relative);
            var to = Path.Combine(workspace, relative);
            if (File.Exists(to) && SameContent(from, to)) continue;
            Directory.CreateDirectory(Path.GetDirectoryName(to)!);
            File.Copy(from, to, true);
        }
        foreach (var stale in previous.Except(current, StringComparer.OrdinalIgnoreCase))
        {
            var path = Path.Combine(workspace, stale);
            if (Paths.IsWithin(workspace, path) && File.Exists(path)) File.Delete(path);
        }
        Directory.CreateDirectory(workspace);
        File.WriteAllText(listPath, JsonSerializer.Serialize(current));
    }

    private static IEnumerable<string> EnumerateInputs(string root)
    {
        var host = Path.Combine(root, PortSource.HostPath);
        foreach (var file in Directory.EnumerateFiles(host, "*", SearchOption.AllDirectories))
        {
            var relative = Path.GetRelativePath(root, file);
            var inHost = Path.GetRelativePath(host, file).Replace('\\', '/');
            // Build output, generated code and machine-local files stay local.
            if (inHost.StartsWith("out/") || inHost.StartsWith("generated/") || inHost.StartsWith(".") ||
                inHost.EndsWith(".local.toml") || inHost.Contains("/.")) continue;
            yield return relative;
        }
        var sdk = Path.Combine(root, PortSource.SdkPath);
        foreach (var file in Directory.EnumerateFiles(sdk, "*", SearchOption.AllDirectories))
            yield return Path.GetRelativePath(root, file);
        var license = Path.Combine(root, "references", "rexglue-sdk", "LICENSE");
        if (File.Exists(license)) yield return Path.GetRelativePath(root, license);
    }

    private static bool SameContent(string a, string b)
    {
        var infoA = new FileInfo(a);
        var infoB = new FileInfo(b);
        if (infoA.Length != infoB.Length) return false;
        using var streamA = infoA.OpenRead();
        using var streamB = infoB.OpenRead();
        return SHA256.HashData(streamA).AsSpan().SequenceEqual(SHA256.HashData(streamB));
    }
}
