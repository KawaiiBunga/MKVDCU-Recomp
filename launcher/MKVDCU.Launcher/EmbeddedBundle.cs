using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;

namespace MKVDCU.Launcher;

internal static class EmbeddedBundle
{
    private const string BuilderResource = "MKVDCU.BuilderBundle.zip";
    private const string UpdaterResource = "MKVDCU.Updater.exe";
    private const string ManifestResource = "MKVDCU.ReleaseManifest.json";

    public static bool HasBuilder => Assembly.GetExecutingAssembly().GetManifestResourceInfo(BuilderResource) != null;

    public static string Version
    {
        get
        {
            using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(ManifestResource);
            if (stream == null) return "local";
            using var manifest = JsonDocument.Parse(stream);
            return manifest.RootElement.GetProperty("version").GetString() ?? "local";
        }
    }

    public static string EnsureBuilder(string installRoot)
    {
        using var input = Assembly.GetExecutingAssembly().GetManifestResourceStream(BuilderResource)
            ?? throw new InvalidOperationException("This launcher has no embedded builder bundle.");
        var builderBase = Path.Combine(installRoot, "builder");
        Directory.CreateDirectory(builderBase);
        var bundleTemp = Path.Combine(builderBase, "bundle-" + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            using (var output = File.Create(bundleTemp)) input.CopyTo(output);
            using var stream = File.OpenRead(bundleTemp);
            var digest = Convert.ToHexString(SHA256.HashData(stream));
            var target = Path.Combine(builderBase, digest[..16]);
            var marker = Path.Combine(target, ".bundle-sha256");
            if (File.Exists(marker) && File.ReadAllText(marker).Trim() == digest &&
                File.Exists(Path.Combine(target, "scripts", "build-pc.ps1"))) return target;

            var staging = Path.Combine(builderBase, "staging-" + Guid.NewGuid().ToString("N"));
            try
            {
                ExtractSourceArchive(bundleTemp, staging);
                if (!File.Exists(Path.Combine(staging, "scripts", "build-pc.ps1")) ||
                    !File.Exists(Path.Combine(staging, "targets", "mkvsdcu", "private", "rexglue-host", "CMakeLists.txt")))
                    throw new InvalidDataException("Embedded builder is incomplete.");
                File.WriteAllText(Path.Combine(staging, ".bundle-sha256"), digest);
                if (Directory.Exists(target)) Directory.Delete(target, true);
                Directory.Move(staging, target);
                return target;
            }
            finally
            {
                if (Directory.Exists(staging)) Directory.Delete(staging, true);
            }
        }
        finally
        {
            if (File.Exists(bundleTemp)) File.Delete(bundleTemp);
        }
    }

    public static string ExtractUpdater(string destination)
    {
        using var input = Assembly.GetExecutingAssembly().GetManifestResourceStream(UpdaterResource)
            ?? throw new InvalidOperationException("This launcher has no embedded updater helper.");
        Directory.CreateDirectory(destination);
        var path = Path.Combine(destination, "MKVDCU.Updater.exe");
        using var output = File.Create(path);
        input.CopyTo(output);
        return path;
    }

    private static void ExtractSourceArchive(string archive, string staging)
    {
        Directory.CreateDirectory(staging);
        using var zip = ZipFile.OpenRead(archive);
        long total = 0;
        foreach (var entry in zip.Entries)
        {
            var name = entry.FullName.Replace('/', Path.DirectorySeparatorChar);
            if (Path.IsPathRooted(name) || name.Split(Path.DirectorySeparatorChar).Any(p => p == "..") ||
                ((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000)
                throw new InvalidDataException("Embedded builder contains an unsafe path.");
            total += entry.Length;
            if (total > 512L * 1024 * 1024) throw new InvalidDataException("Embedded builder is too large.");
            var destination = Path.GetFullPath(Path.Combine(staging, name));
            if (!Paths.IsWithin(staging, destination)) throw new InvalidDataException("Embedded builder escaped its staging directory.");
            if (name.EndsWith(Path.DirectorySeparatorChar))
            {
                Directory.CreateDirectory(destination);
                continue;
            }
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            entry.ExtractToFile(destination, overwrite: false);
        }
    }
}
