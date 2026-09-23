using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace MKVDCU.Launcher;

internal static class Paths
{
    public const string ExpectedXexHash = "2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7";
    public static readonly string AppData = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MKVDCU-Recomp");

    public static string? FindRepository()
    {
        foreach (var start in new[] { AppContext.BaseDirectory, Environment.CurrentDirectory })
        {
            var dir = new DirectoryInfo(start);
            while (dir != null)
            {
                if (File.Exists(Path.Combine(dir.FullName, "scripts", "build-pc.ps1")) &&
                    File.Exists(Path.Combine(dir.FullName, "targets", "mkvsdcu", "private", "rexglue-host", "CMakeLists.txt")))
                    return dir.FullName;
                dir = dir.Parent;
            }
        }
        return null;
    }

    public static string GameInstall(string installRoot) => Path.Combine(installRoot, "game", "current");
    public static string GameExe(string installRoot) => Path.Combine(GameInstall(installRoot), "mkvsdcu.exe");

    public static bool IsWithin(string parent, string child)
    {
        var p = Path.GetFullPath(parent).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var c = Path.GetFullPath(child);
        return c.StartsWith(p, StringComparison.OrdinalIgnoreCase);
    }
}

internal sealed class LauncherSettings
{
    public int SchemaVersion { get; set; } = 1;
    public string GameFolder { get; set; } = "";
    public string InstallRoot { get; set; } = Path.Combine(Paths.AppData, "install");
    public string UserRoot { get; set; } = Path.Combine(Paths.AppData, "user");
    public string CacheRoot { get; set; } = Path.Combine(Paths.AppData, "cache");
    public string SdkRoot { get; set; } = "";
    public string UpdateChannel { get; set; } = "stable";
    public string BuiltXexHash { get; set; } = "";
    public string BuiltVersion { get; set; } = "";
    public string InstalledVersion { get; set; } = EmbeddedBundle.Version;
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
                if (loaded?.SchemaVersion == 1)
                {
                    var marker = Path.Combine(loaded.InstallRoot, "launcher", "current", "installed-version.txt");
                    loaded.InstalledVersion = EmbeddedBundle.Version != "local"
                        ? EmbeddedBundle.Version
                        : File.Exists(marker) ? File.ReadAllText(marker).Trim() : "local";
                    return loaded;
                }
            }
        }
        catch (JsonException) { }
        catch (IOException) { }
        var settings = new LauncherSettings();
        if (Paths.FindRepository() is { } repo)
        {
            settings.SdkRoot = Path.Combine(repo, "references", "rexglue-sdk");
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

internal sealed record ValidationResult(bool Valid, string Heading, string Detail, string? Hash = null);

internal static class GameValidator
{
    private static readonly string[] Required = ["Asset", "Config", "Localization", "Movies"];

    public static async Task<ValidationResult> ValidateAsync(string? folder, CancellationToken token = default)
    {
        if (string.IsNullOrWhiteSpace(folder) || !Directory.Exists(folder))
            return new(false, "SELECT GAME FOLDER", "Choose the extracted Xbox 360 game directory containing default.xex.");
        var xex = Path.Combine(folder, "default.xex");
        if (!File.Exists(xex)) return new(false, "XEX NOT FOUND", "The selected folder needs default.xex at its root.");
        try
        {
            await using var stream = File.OpenRead(xex);
            var magic = new byte[4];
            if (await stream.ReadAsync(magic, token) != 4 || magic[0] != 'X' || magic[1] != 'E' || magic[2] != 'X' || magic[3] != '2')
                return new(false, "INVALID XEX", "default.xex does not have an XEX2 header.");
            stream.Position = 0;
            var hash = Convert.ToHexString(await SHA256.HashDataAsync(stream, token));
            if (!hash.Equals(Paths.ExpectedXexHash, StringComparison.OrdinalIgnoreCase))
                return new(false, "UNSUPPORTED REVISION", $"This executable differs from the verified retail base build. SHA-256: {hash}", hash);
            var missing = Required.Where(name => !Directory.Exists(Path.Combine(folder, name))).ToArray();
            if (missing.Length > 0)
                return new(false, "INCOMPLETE GAME FOLDER", "Missing: " + string.Join(", ", missing), hash);
            return new(true, "GAME VERIFIED", "Matching retail base XEX · required content folders present", hash);
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return new(false, "CANNOT READ GAME", ex.Message);
        }
    }
}

internal sealed class BuildService
{
    public async Task BuildAsync(LauncherSettings settings, Action<string> log, CancellationToken token)
    {
        var validated = await GameValidator.ValidateAsync(settings.GameFolder, token);
        if (!validated.Valid) throw new InvalidOperationException(validated.Heading + ": " + validated.Detail);
        ValidateDestinations(settings);
        log("PREPARING EMBEDDED BUILDER");
        var repo = EmbeddedBundle.HasBuilder ? await Task.Run(() => EmbeddedBundle.EnsureBuilder(settings.InstallRoot), token) :
            Paths.FindRepository() ?? throw new InvalidOperationException("Build sources are unavailable.");
        var sdkRoot = string.IsNullOrWhiteSpace(settings.SdkRoot)
            ? Path.Combine(repo, "references", "rexglue-sdk", "out", "install", "win-amd64")
            : settings.SdkRoot;
        var sdkInstall = Path.Combine(sdkRoot, "out", "install", "win-amd64");
        if (!File.Exists(Path.Combine(sdkInstall, "bin", "rexglue.exe"))) sdkInstall = sdkRoot;
        if (!File.Exists(Path.Combine(sdkInstall, "bin", "rexglue.exe")))
            throw new InvalidOperationException("ReXGlue SDK is missing from the embedded builder. Choose an SDK install folder in Setup.");
        var script = Path.Combine(repo, "scripts", "build-pc.ps1");
        var psi = new ProcessStartInfo("powershell.exe")
        {
            WorkingDirectory = repo,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        foreach (var arg in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
                     "-GameDataRoot", settings.GameFolder, "-SdkRoot", sdkRoot })
            psi.ArgumentList.Add(arg);
        var tools = new[] { @"C:\Program Files\LLVM\bin", @"C:\Program Files\CMake\bin",
            Path.Combine(sdkInstall, "bin") }
            .Where(Directory.Exists);
        psi.Environment["PATH"] = string.Join(Path.PathSeparator, tools) + Path.PathSeparator + psi.Environment["PATH"];
        log("VERIFY  →  CODEGEN  →  CMAKE  →  COMPILE  →  INSTALL");
        using var process = new Process { StartInfo = psi };
        process.OutputDataReceived += (_, e) => { if (e.Data != null) log(e.Data); };
        process.ErrorDataReceived += (_, e) => { if (e.Data != null) log(e.Data); };
        if (!process.Start()) throw new InvalidOperationException("Could not start build process.");
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        try { await process.WaitForExitAsync(token); }
        catch (OperationCanceledException)
        {
            if (!process.HasExited) process.Kill(true);
            throw;
        }
        if (process.ExitCode != 0) throw new InvalidOperationException($"Build failed with exit code {process.ExitCode}. See the build log.");

        var output = Path.Combine(repo, "targets", "mkvsdcu", "private", "rexglue-host", "out", "build", "win-amd64-release");
        var required = new[] { "mkvsdcu.exe", "rexruntime.dll", "rexgpu-xenos.dll" };
        foreach (var name in required)
            if (!File.Exists(Path.Combine(output, name))) throw new FileNotFoundException("Build output missing " + name);
        // The SDK build decides which runtime DLLs sit next to the game (for
        // example the FidelityFX library), so ship every one it copied.
        var names = required.Concat(Directory.EnumerateFiles(output, "*.dll").Select(Path.GetFileName)
            .OfType<string>()).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        var gameBase = Path.Combine(settings.InstallRoot, "game");
        Directory.CreateDirectory(gameBase);
        var staging = Path.Combine(gameBase, "staging-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(staging);
        try
        {
            foreach (var name in names) File.Copy(Path.Combine(output, name), Path.Combine(staging, name));
            var current = Paths.GameInstall(settings.InstallRoot);
            var previous = Path.Combine(gameBase, "previous");
            if (!Paths.IsWithin(gameBase, staging) || !Paths.IsWithin(gameBase, current) || !Paths.IsWithin(gameBase, previous))
                throw new InvalidOperationException("Unsafe install path.");
            if (Directory.Exists(previous)) Directory.Delete(previous, true);
            if (Directory.Exists(current)) Directory.Move(current, previous);
            try { Directory.Move(staging, current); }
            catch
            {
                if (Directory.Exists(previous) && !Directory.Exists(current)) Directory.Move(previous, current);
                throw;
            }
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
        }
        settings.BuiltXexHash = validated.Hash!;
        settings.BuiltVersion = settings.InstalledVersion;
        SettingsStore.Save(settings);
        log("READY  →  Installed at " + Paths.GameInstall(settings.InstallRoot));
    }

    public static void ValidateDestinations(LauncherSettings settings)
    {
        var source = Path.GetFullPath(settings.GameFolder);
        var paths = new[] { settings.InstallRoot, settings.UserRoot, settings.CacheRoot }
            .Select(Path.GetFullPath).ToArray();
        if (paths.Any(p => string.Equals(p, source, StringComparison.OrdinalIgnoreCase) ||
                           Paths.IsWithin(p, source) || Paths.IsWithin(source, p)))
            throw new InvalidOperationException("Install, user, and cache folders must be separate from the original game folder.");
        for (var i = 0; i < paths.Length; i++)
            for (var j = i + 1; j < paths.Length; j++)
                if (string.Equals(paths[i], paths[j], StringComparison.OrdinalIgnoreCase) || Paths.IsWithin(paths[i], paths[j]) || Paths.IsWithin(paths[j], paths[i]))
                    throw new InvalidOperationException("Install, user, and cache locations must be separate.");
    }

    public static Process Launch(LauncherSettings settings)
    {
        var exe = Paths.GameExe(settings.InstallRoot);
        if (!File.Exists(exe) || !File.Exists(Path.Combine(Path.GetDirectoryName(exe)!, "rexgpu-xenos.dll")))
            throw new FileNotFoundException("Build is missing. Use BUILD before PLAY.");
        if (!settings.BuiltXexHash.Equals(Paths.ExpectedXexHash, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The installed game build has no matching XEX validation record. Rebuild it.");
        if (settings.BuiltVersion != settings.InstalledVersion)
            throw new InvalidOperationException("The port was updated. Rebuild the game before playing this release.");
        Directory.CreateDirectory(settings.UserRoot);
        Directory.CreateDirectory(settings.CacheRoot);
        var logs = Path.Combine(settings.UserRoot, "logs");
        Directory.CreateDirectory(logs);
        var psi = new ProcessStartInfo(exe)
        {
            WorkingDirectory = Path.GetDirectoryName(exe)!,
            UseShellExecute = false
        };
        foreach (var arg in new[] { "--no-audio_mute", "--gpu_plugin", "xenos", "--game_data_root", settings.GameFolder,
                     "--user_data_root", settings.UserRoot, "--cache_root", settings.CacheRoot,
                     "--log_level", "info", "--log_file", Path.Combine(logs, "play-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".log") })
            psi.ArgumentList.Add(arg);
        return Process.Start(psi) ?? throw new InvalidOperationException("Could not launch the game.");
    }
}

internal sealed record ReleaseInfo(string Tag, string Body, string AssetUrl, string Digest, long Size);

internal sealed class UpdateService
{
    private static readonly HttpClient Http = new();
    static UpdateService()
    {
        Http.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("MKVDCU-Recomp-Launcher", "0.1"));
        Http.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
        Http.Timeout = TimeSpan.FromSeconds(30);
    }

    public async Task<ReleaseInfo?> CheckAsync(string channel, CancellationToken token)
    {
        var uri = channel == "preview"
            ? "https://api.github.com/repos/KawaiiBunga/MKVDCU-Recomp/releases?per_page=20"
            : "https://api.github.com/repos/KawaiiBunga/MKVDCU-Recomp/releases/latest";
        using var response = await Http.GetAsync(uri, token);
        if (response.StatusCode == System.Net.HttpStatusCode.NotFound) return null;
        response.EnsureSuccessStatusCode();
        using var doc = JsonDocument.Parse(await response.Content.ReadAsStreamAsync(token));
        var releases = doc.RootElement.ValueKind == JsonValueKind.Array
            ? doc.RootElement.EnumerateArray().ToArray() : new[] { doc.RootElement };
        foreach (var release in releases)
        {
            if (release.GetProperty("draft").GetBoolean()) continue;
            if (channel == "stable" && release.GetProperty("prerelease").GetBoolean()) continue;
            foreach (var asset in release.GetProperty("assets").EnumerateArray())
            {
                if (asset.GetProperty("name").GetString() != "MKVDCU-Recomp-win-x64.zip") continue;
                var url = asset.GetProperty("browser_download_url").GetString() ?? "";
                var digest = asset.TryGetProperty("digest", out var value) ? value.GetString() ?? "" : "";
                if (!Uri.TryCreate(url, UriKind.Absolute, out var parsed) || parsed.Scheme != "https" || parsed.Host != "github.com") continue;
                return new(release.GetProperty("tag_name").GetString() ?? "", release.GetProperty("body").GetString() ?? "",
                    url, digest, asset.GetProperty("size").GetInt64());
            }
        }
        return null;
    }

    public async Task<string> DownloadAsync(ReleaseInfo release, string installRoot, IProgress<double> progress, CancellationToken token)
    {
        if (!release.Digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase) || release.Size <= 0 || release.Size > 2L * 1024 * 1024 * 1024)
            throw new InvalidOperationException("Release asset lacks a valid SHA-256 digest or size.");
        var updates = Path.Combine(installRoot, "updates");
        Directory.CreateDirectory(updates);
        var file = Path.Combine(updates, "download-" + Guid.NewGuid().ToString("N") + ".zip");
        try
        {
            using var response = await Http.GetAsync(release.AssetUrl, HttpCompletionOption.ResponseHeadersRead, token);
            response.EnsureSuccessStatusCode();
            await using (var source = await response.Content.ReadAsStreamAsync(token))
            await using (var target = File.Create(file))
            {
                var buffer = new byte[128 * 1024];
                long total = 0;
                int count;
                while ((count = await source.ReadAsync(buffer, token)) > 0)
                {
                    total += count;
                    if (total > release.Size) throw new InvalidDataException("Download exceeded declared size.");
                    await target.WriteAsync(buffer.AsMemory(0, count), token);
                    progress.Report((double)total / release.Size);
                }
                if (total != release.Size) throw new InvalidDataException("Download size did not match release metadata.");
            }
            await using var stream = File.OpenRead(file);
            var digest = Convert.ToHexString(await SHA256.HashDataAsync(stream, token));
            if (!digest.Equals(release.Digest[7..], StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Release SHA-256 mismatch.");
            return file;
        }
        catch
        {
            if (File.Exists(file)) File.Delete(file);
            throw;
        }
    }

    public static void StartInstall(string verifiedZip, string installRoot, string tag)
    {
        var tempDir = Path.Combine(Paths.AppData, "update-helper", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        string helper;
        if (System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceInfo("MKVDCU.Updater.exe") != null)
            helper = EmbeddedBundle.ExtractUpdater(tempDir);
        else
        {
            var source = AppContext.BaseDirectory;
            helper = Path.Combine(tempDir, "MKVDCU.Updater.exe");
            if (!File.Exists(Path.Combine(source, "MKVDCU.Updater.exe")))
                throw new FileNotFoundException("Updater helper is missing from this launcher build.");
            foreach (var extension in new[] { ".exe", ".dll", ".deps.json", ".runtimeconfig.json" })
            {
                var name = "MKVDCU.Updater" + extension;
                var input = Path.Combine(source, name);
                if (File.Exists(input)) File.Copy(input, Path.Combine(tempDir, name));
            }
        }
        var psi = new ProcessStartInfo(helper)
        {
            WorkingDirectory = tempDir,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        foreach (var arg in new[] { Environment.ProcessId.ToString(), verifiedZip, installRoot, tag })
            psi.ArgumentList.Add(arg);
        Process.Start(psi)?.Dispose();
    }
}
