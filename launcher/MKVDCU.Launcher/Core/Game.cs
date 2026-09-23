using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;

namespace MKVDCU.Launcher.Core;

internal sealed record ValidationResult(bool Valid, string Heading, string Detail, string? Hash = null);

internal static class GameValidator
{
    private static readonly string[] Required = ["Asset", "Config", "Localization", "Movies"];

    public static async Task<ValidationResult> ValidateAsync(string? folder, CancellationToken token = default)
    {
        if (string.IsNullOrWhiteSpace(folder) || !Directory.Exists(folder))
            return new(false, "Choose your game folder", "The extracted game folder with default.xex in it.");
        var xex = Path.Combine(folder, "default.xex");
        if (!File.Exists(xex))
            return new(false, "default.xex not found", "Choose the folder that has default.xex directly inside it.");
        try
        {
            await using var stream = File.OpenRead(xex);
            var magic = new byte[4];
            if (await stream.ReadAsync(magic, token) != 4 || magic[0] != 'X' || magic[1] != 'E' || magic[2] != 'X' || magic[3] != '2')
                return new(false, "Not an Xbox 360 executable", "default.xex is damaged or from another platform.");
            stream.Position = 0;
            var hash = Convert.ToHexString(await SHA256.HashDataAsync(stream, token));
            if (!hash.Equals(Paths.ExpectedXexHash, StringComparison.OrdinalIgnoreCase))
                return new(false, "Unsupported game version",
                    "This port needs the original retail release without title updates. SHA-256: " + hash, hash);
            var missing = Required.Where(name => !Directory.Exists(Path.Combine(folder, name))).ToArray();
            if (missing.Length > 0)
                return new(false, "Game folder is incomplete", "Missing: " + string.Join(", ", missing), hash);
            return new(true, "Game files verified", "Retail Mortal Kombat vs. DC Universe", hash);
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return new(false, "Cannot read the game folder", ex.Message);
        }
    }
}

internal static class GameLauncher
{
    public static bool IsInstalled(LauncherSettings settings) =>
        File.Exists(Paths.GameExe(settings.InstallRoot)) &&
        settings.BuiltXexHash.Equals(Paths.ExpectedXexHash, StringComparison.OrdinalIgnoreCase);

    public static Process Launch(LauncherSettings settings, string portVersion, IReadOnlyList<string> modFolders)
    {
        var exe = Paths.GameExe(settings.InstallRoot);
        if (!IsInstalled(settings)) throw new FileNotFoundException("The game is not built yet.");
        Directory.CreateDirectory(settings.UserRoot);
        Directory.CreateDirectory(settings.CacheRoot);
        var logs = Path.Combine(settings.UserRoot, "logs");
        Directory.CreateDirectory(logs);
        PruneLogs(logs);
        var psi = new ProcessStartInfo(exe) { WorkingDirectory = Path.GetDirectoryName(exe)!, UseShellExecute = false };
        foreach (var arg in new[]
                 {
                     "--gpu_plugin", "xenos", "--game_data_root", settings.GameFolder,
                     "--user_data_root", settings.UserRoot, "--cache_root", settings.CacheRoot,
                     "--log_file", Path.Combine(logs, "play-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".log"),
                     "--port_version=" + portVersion,
                 })
            psi.ArgumentList.Add(arg);
        if (modFolders.Count > 0) psi.ArgumentList.Add("--port_mods=" + string.Join('|', modFolders));
        return Process.Start(psi) ?? throw new InvalidOperationException("The game did not start.");
    }

    private static void PruneLogs(string logs)
    {
        foreach (var file in new DirectoryInfo(logs).GetFiles("play-*.log").OrderByDescending(f => f.LastWriteTimeUtc).Skip(20))
            try { file.Delete(); } catch (IOException) { }
    }
}
