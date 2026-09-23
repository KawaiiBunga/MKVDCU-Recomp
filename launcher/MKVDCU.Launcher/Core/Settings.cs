using System.IO;
using System.Text.Json;

namespace MKVDCU.Launcher.Core;

internal static class Paths
{
    public const string ExpectedXexHash = "2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7";
    public const string Repository = "KawaiiBunga/MKVDCU-Recomp";
    public const string RepositoryUrl = "https://github.com/" + Repository;

    // MKVDCU_DATA_DIR keeps a test run (or a portable copy) apart from the
    // player's normal data.
    public static readonly string AppData = Environment.GetEnvironmentVariable("MKVDCU_DATA_DIR") is { Length: > 0 } dir
        ? Path.GetFullPath(dir)
        : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MKVDCU-Recomp");
    public static string Tools => Path.Combine(AppData, "tools");
    public static string Downloads => Path.Combine(AppData, "downloads");
    public static string Mods => Path.Combine(AppData, "mods");
    public static string Ports => Path.Combine(AppData, "port");

    // A checkout of the repository, when a development build of the launcher
    // runs from one. Release builds always use their release's files.
    public static string? FindRepository()
    {
        if (!Releases.IsDevBuild) return null;
        foreach (var start in new[] { AppContext.BaseDirectory, Environment.CurrentDirectory })
        {
            var dir = new DirectoryInfo(start);
            while (dir != null)
            {
                if (File.Exists(Path.Combine(dir.FullName, "targets", "mkvsdcu", "private", "rexglue-host", "CMakeLists.txt")) &&
                    Directory.Exists(Path.Combine(dir.FullName, "references", "rexglue-sdk", "out", "install", "win-amd64")))
                    return dir.FullName;
                dir = dir.Parent;
            }
        }
        return null;
    }

    public static string GameInstall(string installRoot) => Path.Combine(installRoot, "game", "current");
    public static string GameExe(string installRoot) => Path.Combine(GameInstall(installRoot), "mkvsdcu.exe");
    public static string Workspace(string installRoot) => Path.Combine(installRoot, "workspace");

    public static bool IsWithin(string parent, string child)
    {
        var p = Path.GetFullPath(parent).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        return Path.GetFullPath(child).StartsWith(p, StringComparison.OrdinalIgnoreCase);
    }
}

internal sealed class LauncherSettings
{
    public int SchemaVersion { get; set; } = 2;
    public string GameFolder { get; set; } = "";
    public string InstallRoot { get; set; } = Path.Combine(Paths.AppData, "install");
    public string UserRoot { get; set; } = Path.Combine(Paths.AppData, "user");
    public string CacheRoot { get; set; } = Path.Combine(Paths.AppData, "cache");
    public string UpdateChannel { get; set; } = "stable";
    public bool CheckForUpdates { get; set; } = true;
    // What the installed game was built from.
    public string BuiltXexHash { get; set; } = "";
    public string BuiltPortVersion { get; set; } = "";
    // Enabled mods, highest priority first.
    public List<string> EnabledMods { get; set; } = [];
}

internal static class SettingsStore
{
    private static readonly JsonSerializerOptions Options = new() { WriteIndented = true };
    public static string SettingsPath => Path.Combine(Paths.AppData, "launcher.json");

    public static LauncherSettings Load()
    {
        try
        {
            if (File.Exists(SettingsPath))
            {
                var loaded = JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllText(SettingsPath));
                if (loaded != null)
                {
                    // Version 1 tracked builds differently; a rebuild picks it up.
                    if (loaded.SchemaVersion < 2) loaded.BuiltPortVersion = "";
                    loaded.SchemaVersion = 2;
                    loaded.EnabledMods ??= [];
                    return loaded;
                }
            }
        }
        catch (JsonException) { }
        catch (IOException) { }
        var settings = new LauncherSettings();
        if (Paths.FindRepository() is { } repo)
        {
            var staged = Path.Combine(repo, "user-game-files", "work", "mkvsdcu");
            if (File.Exists(Path.Combine(staged, "default.xex"))) settings.GameFolder = staged;
        }
        return settings;
    }

    public static void Save(LauncherSettings settings)
    {
        Directory.CreateDirectory(Paths.AppData);
        var temp = SettingsPath + ".tmp";
        File.WriteAllText(temp, JsonSerializer.Serialize(settings, Options));
        File.Move(temp, SettingsPath, true);
    }
}
