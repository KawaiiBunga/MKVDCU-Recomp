using System.IO;
using System.IO.Compression;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace MKVDCU.Launcher.Core;

// A mod is a folder with mod.json and a files/ folder that mirrors the game
// folder. The game reads a mod's file instead of its own; earlier mods in the
// load order win. See docs/MODDING.md.
internal sealed class ModManifest
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Version { get; set; } = "";
    public string Author { get; set; } = "";
    public string Description { get; set; } = "";
    public string Homepage { get; set; } = "";
}

internal sealed record InstalledMod(ModManifest Manifest, string Folder)
{
    public string Files => Path.Combine(Folder, "files");
}

internal sealed class ModIndexEntry
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Version { get; set; } = "";
    public string Author { get; set; } = "";
    public string Description { get; set; } = "";
    public string Homepage { get; set; } = "";
    public string Url { get; set; } = "";
    public string Sha256 { get; set; } = "";
    public long Size { get; set; }
}

internal static partial class ModStore
{
    public const string IndexUrl = "https://raw.githubusercontent.com/" + Paths.Repository + "/main/mods/index.json";
    private static readonly JsonSerializerOptions Json = new() { PropertyNameCaseInsensitive = true, WriteIndented = true };

    public static List<InstalledMod> Installed()
    {
        if (!Directory.Exists(Paths.Mods)) return [];
        var mods = new List<InstalledMod>();
        foreach (var dir in Directory.EnumerateDirectories(Paths.Mods))
        {
            if (Path.GetFileName(dir).StartsWith('.')) continue;
            var manifest = ReadManifest(dir);
            if (manifest != null && Directory.Exists(Path.Combine(dir, "files"))) mods.Add(new(manifest, dir));
        }
        return mods;
    }

    // Enabled mods' content folders, highest priority first, for the game.
    public static List<string> EnabledFolders(LauncherSettings settings)
    {
        var installed = Installed().ToDictionary(m => m.Manifest.Id, StringComparer.OrdinalIgnoreCase);
        return settings.EnabledMods.Where(installed.ContainsKey).Select(id => installed[id].Files).ToList();
    }

    public static async Task<List<ModIndexEntry>> FetchIndexAsync(CancellationToken token)
    {
        var text = await Http.Client.GetStringAsync(IndexUrl, token);
        using var doc = JsonDocument.Parse(text);
        var mods = doc.RootElement.GetProperty("mods").Deserialize<List<ModIndexEntry>>(Json) ?? [];
        return mods.Where(m => ValidId(m.Id) && m.Url.StartsWith("https://", StringComparison.OrdinalIgnoreCase) &&
                               m.Sha256.Length == 64).ToList();
    }

    public static async Task<InstalledMod> InstallFromIndexAsync(ModIndexEntry entry, IProgress<DownloadProgress> progress,
        CancellationToken token)
    {
        var archive = await Downloader.FetchAsync(entry.Url, $"mod-{entry.Id}-{entry.Version}.zip", entry.Sha256,
            entry.Size, progress, token);
        try
        {
            return InstallFromZip(archive);
        }
        finally
        {
            File.Delete(archive);
        }
    }

    // Accepts a zip with mod.json at its root or inside a single top folder.
    public static InstalledMod InstallFromZip(string zip)
    {
        var staging = NewStaging();
        try
        {
            ZipFile.ExtractToDirectory(zip, staging);
            return InstallFromStaging(staging);
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
        }
    }

    public static InstalledMod InstallFromFolder(string folder)
    {
        var staging = NewStaging();
        try
        {
            CopyDirectory(folder, staging);
            return InstallFromStaging(staging);
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
        }
    }

    public static void Remove(InstalledMod mod, LauncherSettings settings)
    {
        settings.EnabledMods.RemoveAll(id => id.Equals(mod.Manifest.Id, StringComparison.OrdinalIgnoreCase));
        if (Paths.IsWithin(Paths.Mods, mod.Folder)) Directory.Delete(mod.Folder, true);
    }

    private static InstalledMod InstallFromStaging(string staging)
    {
        var root = staging;
        if (!File.Exists(Path.Combine(root, "mod.json")))
        {
            var dirs = Directory.GetDirectories(root);
            if (dirs.Length == 1 && File.Exists(Path.Combine(dirs[0], "mod.json"))) root = dirs[0];
        }
        var manifest = ReadManifest(root) ?? throw new InvalidDataException(
            "This is not a mod for MKVDCU-Recomp: mod.json is missing or has no valid id.");
        if (!Directory.Exists(Path.Combine(root, "files")))
            throw new InvalidDataException("The mod has no files folder.");
        if (File.Exists(Path.Combine(root, "files", "default.xex")))
            throw new InvalidDataException("Mods cannot replace default.xex.");
        var target = Path.Combine(Paths.Mods, manifest.Id);
        if (Directory.Exists(target)) Directory.Delete(target, true);
        Directory.Move(root, target);
        return new InstalledMod(manifest, target);
    }

    private static ModManifest? ReadManifest(string dir)
    {
        try
        {
            var manifest = JsonSerializer.Deserialize<ModManifest>(File.ReadAllText(Path.Combine(dir, "mod.json")), Json);
            if (manifest == null || !ValidId(manifest.Id)) return null;
            if (manifest.Name.Length == 0) manifest.Name = manifest.Id;
            return manifest;
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static bool ValidId(string id) => IdPattern().IsMatch(id);

    [GeneratedRegex("^[a-z0-9][a-z0-9._-]{0,63}$")]
    private static partial Regex IdPattern();

    private static string NewStaging()
    {
        var staging = Path.Combine(Paths.Mods, ".staging-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(staging);
        return staging;
    }

    private static void CopyDirectory(string from, string to)
    {
        foreach (var dir in Directory.EnumerateDirectories(from, "*", SearchOption.AllDirectories))
            Directory.CreateDirectory(Path.Combine(to, Path.GetRelativePath(from, dir)));
        foreach (var file in Directory.EnumerateFiles(from, "*", SearchOption.AllDirectories))
            File.Copy(file, Path.Combine(to, Path.GetRelativePath(from, file)), true);
    }
}
